#include "LocalStore.h"

#include <QDir>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace {
QSqlDatabase database(const QString& connectionName) {
  return QSqlDatabase::database(connectionName);
}

QString timestamp(const QDateTime& value) {
  return value.toUTC().toString(Qt::ISODateWithMs);
}
}  // namespace

LocalStore::LocalStore()
    : connectionName_("ibkr-local-" +
                      QUuid::createUuid().toString(QUuid::Id128)) {
  auto connection = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
  const QString directory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QDir().mkpath(directory);
  connection.setDatabaseName(directory + "/ibkr-options-desk.sqlite");
  if (connection.open()) initialize();
}

LocalStore::~LocalStore() {
  {
    auto connection = database(connectionName_);
    connection.close();
  }
  QSqlDatabase::removeDatabase(connectionName_);
}

void LocalStore::initialize() {
  QSqlQuery query(database(connectionName_));
  const QStringList schema{
      "CREATE TABLE IF NOT EXISTS watchlist (con_id INTEGER PRIMARY KEY, "
      "symbol TEXT NOT NULL, expected_change REAL NOT NULL, both_sides INTEGER "
      "NOT NULL, follows_global INTEGER NOT NULL DEFAULT 1, "
      "expiry_date TEXT NOT NULL DEFAULT '')",
      "CREATE TABLE IF NOT EXISTS earnings_dates (con_id INTEGER PRIMARY KEY, "
      "earnings_date TEXT NOT NULL)",
      "CREATE TABLE IF NOT EXISTS strategies (id TEXT PRIMARY KEY, label TEXT "
      "NOT NULL)",
      "CREATE TABLE IF NOT EXISTS strategy_legs (strategy_id TEXT NOT NULL, "
      "con_id INTEGER NOT NULL, intended_quantity REAL NOT NULL, PRIMARY "
      "KEY(strategy_id, con_id))",
      "CREATE TABLE IF NOT EXISTS executions (id INTEGER PRIMARY KEY "
      "AUTOINCREMENT, execution_id TEXT UNIQUE, con_id INTEGER NOT NULL, "
      "timestamp TEXT NOT NULL, asset_class TEXT NOT NULL, contract TEXT NOT "
      "NULL, action TEXT NOT NULL, quantity REAL NOT NULL, price REAL NOT "
      "NULL, commission REAL, realized_pnl REAL NOT NULL DEFAULT 0, "
      "provisional INTEGER NOT NULL DEFAULT 0)",
      "CREATE TABLE IF NOT EXISTS snapshots (id INTEGER PRIMARY KEY "
      "AUTOINCREMENT, captured_at TEXT NOT NULL, con_id INTEGER NOT NULL, "
      "asset_class TEXT NOT NULL, quantity REAL NOT NULL, unrealized_pnl REAL "
      "NOT NULL, expiry TEXT)"};
  for (const auto& statement : schema) query.exec(statement);
  bool hasGlobalRule = false;
  bool hasExpiryHorizon = false;
  bool hasExpiryDate = false;
  if (query.exec("PRAGMA table_info(watchlist)")) {
    while (query.next()) {
      hasGlobalRule =
          hasGlobalRule || query.value(1).toString() == "follows_global";
      hasExpiryHorizon =
          hasExpiryHorizon || query.value(1).toString() == "expiry_horizon_days";
      hasExpiryDate = hasExpiryDate || query.value(1).toString() == "expiry_date";
    }
  }
  if (!hasGlobalRule)
    query.exec(
        "ALTER TABLE watchlist ADD COLUMN follows_global INTEGER NOT NULL "
        "DEFAULT 1");
  if (!hasExpiryDate) {
    query.exec("ALTER TABLE watchlist ADD COLUMN expiry_date TEXT NOT NULL " 
               "DEFAULT ''");
    QList<QPair<qint64, int>> legacyRows;
    if (hasExpiryHorizon) {
      QSqlQuery legacy(database(connectionName_));
      if (legacy.exec("SELECT con_id,expiry_horizon_days FROM watchlist")) {
        while (legacy.next())
          legacyRows.append(
              {legacy.value(0).toLongLong(), legacy.value(1).toInt()});
      }
    }
    if (!legacyRows.isEmpty()) {
      QSqlQuery update(database(connectionName_));
      for (const auto& [conId, days] : legacyRows) {
        update.prepare("UPDATE watchlist SET expiry_date=? WHERE con_id=?");
        update.addBindValue(
            QDate::currentDate().addDays(days).toString(Qt::ISODate));
        update.addBindValue(conId);
        update.exec();
      }
    }
  }
}

QList<WatchlistEntry> LocalStore::watchlistEntries() const {
  QList<WatchlistEntry> result;
  QSqlQuery query(database(connectionName_));
  query.exec(
      "SELECT con_id,symbol,expected_change,both_sides,follows_global,"
      "expiry_date FROM "
      "watchlist ORDER BY symbol");
  while (query.next())
    result.append({.conId = query.value(0).toLongLong(),
                   .symbol = query.value(1).toString(),
                   .expectedChangePercent = query.value(2).toDouble(),
                   .showBothSides = query.value(3).toBool(),
                   .expiryDate = QDate::fromString(query.value(5).toString(),
                                                   Qt::ISODate),
                   .followsGlobalRules = query.value(4).toBool()});
  return result;
}

void LocalStore::saveWatchlistEntry(const WatchlistEntry& entry) {
  QSqlQuery query(database(connectionName_));
  query.prepare(
      "INSERT INTO "
      "watchlist(con_id,symbol,expected_change,both_sides,follows_global,"
      "expiry_date) VALUES(?,?,?,?,?,?) ON CONFLICT(con_id) DO UPDATE SET "
      "symbol=excluded.symbol,expected_change=excluded.expected_change,both_"
      "sides=excluded.both_sides,follows_global=excluded.follows_global,"
      "expiry_date=excluded.expiry_date");
  query.addBindValue(entry.conId);
  query.addBindValue(entry.symbol);
  query.addBindValue(entry.expectedChangePercent);
  query.addBindValue(entry.showBothSides);
  query.addBindValue(entry.followsGlobalRules);
  query.addBindValue(entry.expiryDate.toString(Qt::ISODate));
  query.exec();
}

void LocalStore::removeWatchlistEntry(qint64 conId) {
  QSqlQuery query(database(connectionName_));
  query.prepare("DELETE FROM watchlist WHERE con_id=?");
  query.addBindValue(conId);
  query.exec();
}

QDate LocalStore::earningsDate(qint64 conId) const {
  QSqlQuery query(database(connectionName_));
  query.prepare("SELECT earnings_date FROM earnings_dates WHERE con_id=?");
  query.addBindValue(conId);
  return query.exec() && query.next()
             ? QDate::fromString(query.value(0).toString(), Qt::ISODate)
             : QDate{};
}

void LocalStore::saveEarningsDate(qint64 conId, const QDate& date) {
  QSqlQuery query(database(connectionName_));
  query.prepare(
      "INSERT INTO earnings_dates(con_id,earnings_date) VALUES(?,?) ON "
      "CONFLICT(con_id) DO UPDATE SET earnings_date=excluded.earnings_date");
  query.addBindValue(conId);
  query.addBindValue(date.toString(Qt::ISODate));
  query.exec();
}

QList<OptionStrategy> LocalStore::strategies() const {
  QList<OptionStrategy> result;
  QSqlQuery parents(database(connectionName_));
  parents.exec("SELECT id,label FROM strategies ORDER BY label");
  while (parents.next()) {
    OptionStrategy strategy{.identifier = parents.value(0).toString(),
                            .label = parents.value(1).toString()};
    QSqlQuery legs(database(connectionName_));
    legs.prepare(
        "SELECT con_id,intended_quantity FROM strategy_legs WHERE "
        "strategy_id=?");
    legs.addBindValue(strategy.identifier);
    legs.exec();
    while (legs.next())
      strategy.legs.append({.conId = legs.value(0).toLongLong(),
                            .quantity = legs.value(1).toDouble()});
    result.append(std::move(strategy));
  }
  return result;
}

void LocalStore::saveStrategy(const QString& label,
                              const QList<OptionPosition>& legs) {
  if (label.trimmed().isEmpty() || legs.isEmpty()) return;
  const QString id = QUuid::createUuid().toString(QUuid::Id128);
  auto connection = database(connectionName_);
  connection.transaction();
  QSqlQuery parent(connection);
  parent.prepare("INSERT INTO strategies(id,label) VALUES(?,?)");
  parent.addBindValue(id);
  parent.addBindValue(label.trimmed());
  parent.exec();
  QSqlQuery child(connection);
  child.prepare(
      "INSERT INTO strategy_legs(strategy_id,con_id,intended_quantity) "
      "VALUES(?,?,?)");
  for (const auto& leg : legs) {
    child.addBindValue(id);
    child.addBindValue(leg.conId);
    child.addBindValue(leg.quantity);
    child.exec();
    child.finish();
  }
  connection.commit();
}

void LocalStore::removeStrategy(const QString& identifier) {
  auto connection = database(connectionName_);
  connection.transaction();
  QSqlQuery query(connection);
  query.prepare("DELETE FROM strategy_legs WHERE strategy_id=?");
  query.addBindValue(identifier);
  query.exec();
  query.prepare("DELETE FROM strategies WHERE id=?");
  query.addBindValue(identifier);
  query.exec();
  connection.commit();
}

void LocalStore::recordExecution(TradeRecord record) {
  if (record.executionId.isEmpty()) return;
  QSqlQuery query(database(connectionName_));
  query.prepare(
      "INSERT OR IGNORE INTO "
      "executions(execution_id,con_id,timestamp,asset_class,contract,action,"
      "quantity,price,provisional) VALUES(?,?,?,?,?,?,?,?,?)");
  query.addBindValue(record.executionId);
  query.addBindValue(record.conId);
  query.addBindValue(timestamp(record.timestamp));
  query.addBindValue(record.assetClass);
  query.addBindValue(record.contract);
  query.addBindValue(record.action);
  query.addBindValue(record.quantity);
  query.addBindValue(record.executionPrice);
  query.addBindValue(record.provisional);
  query.exec();
  recomputeFifo();
}

void LocalStore::recordCommission(const QString& executionId,
                                  double commission) {
  QSqlQuery query(database(connectionName_));
  query.prepare("UPDATE executions SET commission=? WHERE execution_id=?");
  query.addBindValue(commission);
  query.addBindValue(executionId);
  query.exec();
  recomputeFifo();
}

void LocalStore::recomputeFifo() {
  struct Lot {
    double quantity;
    double price;
  };
  QHash<qint64, QList<Lot>> lots;
  QSqlQuery query(database(connectionName_));
  query.exec(
      "SELECT "
      "id,con_id,asset_class,action,quantity,price,COALESCE(commission,0) FROM "
      "executions ORDER BY timestamp,id");
  while (query.next()) {
    const qint64 id = query.value(0).toLongLong(),
                 conId = query.value(1).toLongLong();
    const double multiplier =
        query.value(2).toString() == "Option" ? 100.0 : 1.0;
    const bool buy = query.value(3).toString().toUpper() == "BUY";
    const double originalQuantity = query.value(4).toDouble();
    double remaining = originalQuantity;
    const double price = query.value(5).toDouble(),
                 commission = query.value(6).toDouble();
    double realized = 0.0;
    auto& open = lots[conId];
    while (remaining > 0.000001 && !open.isEmpty() &&
           ((open.first().quantity > 0.0) != buy)) {
      auto& lot = open.first();
      const double matched = qMin(remaining, qAbs(lot.quantity));
      realized +=
          (buy ? lot.price - price : price - lot.price) * matched * multiplier -
          commission * matched / originalQuantity;
      lot.quantity += buy ? matched : -matched;
      remaining -= matched;
      if (qAbs(lot.quantity) < 0.000001) open.removeFirst();
    }
    if (remaining > 0.000001)
      open.append(
          {buy ? remaining : -remaining,
           price + (buy ? commission / originalQuantity / multiplier
                        : -commission / originalQuantity / multiplier)});
    QSqlQuery update(database(connectionName_));
    update.prepare("UPDATE executions SET realized_pnl=? WHERE id=?");
    update.addBindValue(realized);
    update.addBindValue(id);
    update.exec();
  }
}

QList<TradeRecord> LocalStore::records(const QString& assetClass) const {
  QList<TradeRecord> result;
  QSqlQuery query(database(connectionName_));
  query.prepare(
      "SELECT "
      "execution_id,con_id,timestamp,asset_class,contract,action,quantity,"
      "price,realized_pnl,provisional FROM executions WHERE asset_class=? "
      "ORDER BY timestamp DESC,id DESC");
  query.addBindValue(assetClass);
  query.exec();
  while (query.next())
    result.append({.executionId = query.value(0).toString(),
                   .conId = query.value(1).toLongLong(),
                   .timestamp = QDateTime::fromString(query.value(2).toString(),
                                                      Qt::ISODateWithMs),
                   .assetClass = query.value(3).toString(),
                   .contract = query.value(4).toString(),
                   .action = query.value(5).toString(),
                   .quantity = query.value(6).toDouble(),
                   .executionPrice = query.value(7).toDouble(),
                   .realizedPnl = query.value(8).toDouble(),
                   .provisional = query.value(9).toBool()});
  return result;
}

double LocalStore::realizedPnl(const QDateTime& start,
                               const QDateTime& end) const {
  QSqlQuery query(database(connectionName_));
  query.prepare(
      "SELECT COALESCE(SUM(realized_pnl),0) FROM executions WHERE timestamp>=? "
      "AND timestamp<?");
  query.addBindValue(timestamp(start));
  query.addBindValue(timestamp(end));
  return query.exec() && query.next() ? query.value(0).toDouble() : 0.0;
}