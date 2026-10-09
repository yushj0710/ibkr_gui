#include "PortfolioStore.h"

#include <algorithm>

#include "domain/LocalStore.h"

PortfolioStore::PortfolioStore(QObject* parent)
    : QObject(parent), localStore_(std::make_unique<LocalStore>()) {}

PortfolioStore::~PortfolioStore() = default;

void PortfolioStore::clear() {
  equities_.clear();
  options_.clear();
  publishEquities();
  publishOptions();
}

void PortfolioStore::updateEquity(EquityPosition position) {
  position.earningsDate = localStore_->earningsDate(position.conId);
  if (equities_.contains(position.conId)) {
    position.prevClose = equities_.value(position.conId).prevClose;
    position.todayChange = position.prevClose > 0
                               ? position.lastPrice / position.prevClose - 1.0
                               : 0.0;
  }
  if (qFuzzyIsNull(position.quantity)) {
    equities_.remove(position.conId);
  } else {
    equities_.insert(position.conId, std::move(position));
  }
  publishEquities();
}

void PortfolioStore::updateOption(OptionPosition position) {
  if (qFuzzyIsNull(position.quantity)) {
    options_.remove(position.conId);
  } else {
    options_.insert(position.conId, std::move(position));
  }
  publishOptions();
}

void PortfolioStore::updateLastPrice(qint64 conId, double lastPrice) {
  if (equities_.contains(conId)) {
    equities_[conId].lastPrice = lastPrice;
    const double prevClose = equities_[conId].prevClose;
    equities_[conId].todayChange =
        prevClose > 0.0 ? lastPrice / prevClose - 1.0 : 0.0;
    publishEquities();
  } else if (options_.contains(conId)) {
    options_[conId].lastPrice = lastPrice;
    publishOptions();
  }
}

void PortfolioStore::updateUnrealizedPnl(qint64 conId, double unrealizedPnl) {
  if (equities_.contains(conId)) {
    equities_[conId].unrealizedPnl = unrealizedPnl;
    publishEquities();
  } else if (options_.contains(conId)) {
    options_[conId].unrealizedPnl = unrealizedPnl;
    publishOptions();
  }
}

void PortfolioStore::updateOptionUnderlyingPrice(qint64 conId,
                                                 double underlyingPrice) {
  if (!options_.contains(conId)) {
    return;
  }
  options_[conId].underlyingPrice = underlyingPrice;
  publishOptions();
}

void PortfolioStore::setEarningsDate(qint64 conId, const QDate& earningsDate) {
  localStore_->saveEarningsDate(conId, earningsDate);
  if (equities_.contains(conId)) {
    equities_[conId].earningsDate = earningsDate;
    publishEquities();
  }
}

void PortfolioStore::saveStrategy(const QString& label,
                                  const QList<OptionPosition>& legs) {
  localStore_->saveStrategy(label, legs);
  publishOptions();
}

void PortfolioStore::removeStrategy(const QString& identifier) {
  localStore_->removeStrategy(identifier);
  publishOptions();
}

QList<WatchlistEntry> PortfolioStore::savedWatchlist() const {
  return localStore_->watchlistEntries();
}
void PortfolioStore::saveWatchlistEntry(const WatchlistEntry& entry) {
  localStore_->saveWatchlistEntry(entry);
}
void PortfolioStore::removeWatchlistEntry(qint64 conId) {
  localStore_->removeWatchlistEntry(conId);
}
void PortfolioStore::recordExecution(TradeRecord record) {
  localStore_->recordExecution(std::move(record));
}
void PortfolioStore::recordCommission(const QString& executionId,
                                      double commission) {
  localStore_->recordCommission(executionId, commission);
}
QList<TradeRecord> PortfolioStore::tradeRecords(
    const QString& assetClass) const {
  return localStore_->records(assetClass);
}
double PortfolioStore::realizedPnl(const QDateTime& start,
                                   const QDateTime& end) const {
  return localStore_->realizedPnl(start, end);
}

void PortfolioStore::publishEquities() {
  auto positions = equities_.values();
  std::sort(positions.begin(), positions.end(),
            [](const EquityPosition& left, const EquityPosition& right) {
              return left.symbol < right.symbol;
            });
  emit equitiesChanged(positions);
}

void PortfolioStore::publishOptions() {
  QList<OptionStrategy> strategies;
  auto positions = options_.values();
  std::sort(positions.begin(), positions.end(),
            [](const OptionPosition& left, const OptionPosition& right) {
              return left.localSymbol < right.localSymbol;
            });
  QSet<qint64> grouped;
  for (auto strategy : localStore_->strategies()) {
    strategy.verified = !strategy.legs.isEmpty();
    for (auto& leg : strategy.legs) {
      if (!options_.contains(leg.conId) ||
          !qFuzzyCompare(options_.value(leg.conId).quantity + 1.0,
                         leg.quantity + 1.0)) {
        strategy.verified = false;
        continue;
      }
      leg = options_.value(leg.conId);
      grouped.insert(leg.conId);
    }
    strategies.append(std::move(strategy));
  }
  for (auto& position : positions) {
    if (grouped.contains(position.conId)) continue;
    OptionStrategy strategy;
    strategy.identifier = QString::number(position.conId);
    strategy.label = position.localSymbol;
    strategy.verified = false;
    strategy.legs.append(std::move(position));
    strategies.append(std::move(strategy));
  }
  emit optionsChanged(strategies);
}

void PortfolioStore::updatePrevClose(qint64 conId, double prevClose) {
  if (!equities_.contains(conId) || prevClose <= 0.0) {
    return;
  }
  auto& position = equities_[conId];
  position.prevClose = prevClose;
  position.todayChange = position.prevClose > 0
                             ? position.lastPrice / position.prevClose - 1.0
                             : 0.0;
  publishEquities();
}