#include "WatchlistTreeModel.h"

#include <QColor>
#include <QLocale>

namespace {
QString currency(double value) {
  return QLocale().toCurrencyString(value, "$", 2);
}

QString percentage(double value) {
  return QLocale().toString(value, 'f', 2) + "%";
}
}  // namespace

WatchlistTreeModel::WatchlistTreeModel(QObject* parent)
    : QAbstractItemModel(parent) {}

QModelIndex WatchlistTreeModel::index(int row, int column,
                                      const QModelIndex& parentIndex) const {
  if (row < 0 || column < 0 || column >= ColumnCount) return {};
  if (!parentIndex.isValid()) {
    return row <= entries_.size() ? createIndex(row, column, quintptr(0))
                                  : QModelIndex{};
  }
  if (parentIndex.column() != 0 || parentIndex.internalId() != 0 ||
      parentIndex.row() >= entries_.size())
    return {};
  const auto& entry = entries_.at(parentIndex.row());
  const int childCount = entry.optionsLoaded ? entry.options.size() : 1;
  if (row >= childCount) return {};
  return createIndex(row, column, static_cast<quintptr>(parentIndex.row() + 1));
}

QModelIndex WatchlistTreeModel::parent(const QModelIndex& child) const {
  if (!child.isValid() || child.internalId() == 0) return {};
  const int row = static_cast<int>(child.internalId()) - 1;
  return row < entries_.size() ? createIndex(row, 0, quintptr(0))
                               : QModelIndex{};
}

int WatchlistTreeModel::rowCount(const QModelIndex& parentIndex) const {
  if (!parentIndex.isValid()) return entries_.size() + 1;
  if (parentIndex.column() != 0 || parentIndex.internalId() != 0 ||
      parentIndex.row() >= entries_.size())
    return 0;
  const auto& entry = entries_.at(parentIndex.row());
  return entry.optionsLoaded ? entry.options.size() : 1;
}

int WatchlistTreeModel::columnCount(const QModelIndex&) const {
  return ColumnCount;
}

QVariant WatchlistTreeModel::data(const QModelIndex& modelIndex,
                                  int role) const {
  if (!modelIndex.isValid()) return {};
  if (role == Qt::TextAlignmentRole && modelIndex.column() != Contract &&
      modelIndex.column() != Remove)
    return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);

  if (!isOption(modelIndex)) {
    if (isAddRow(modelIndex)) {
      if (modelIndex.column() == Contract && role == Qt::DisplayRole)
        return QString("Add underlying...");
      return {};
    }
    const auto& entry = entries_.at(modelIndex.row()).entry;
    const auto& state = entries_.at(modelIndex.row());
    if (modelIndex.column() == Remove && role == Qt::ForegroundRole)
      return QColor("#b42318");
    if (role == Qt::ForegroundRole && (modelIndex.column() == TodayChange ||
                                       modelIndex.column() == FiveDayChange)) {
      const double change = modelIndex.column() == TodayChange
                                ? entry.todayChangePercent
                                : entry.fiveDayChangePercent;
      return change < 0.0 ? QColor("#b42318") : QColor("#067647");
    }
    if (role == Qt::EditRole) {
      if (modelIndex.column() == ExpectedChange)
        return entry.expectedChangePercent;
      if (modelIndex.column() == Sides)
        return entry.showBothSides ? QString("Both sides")
                                   : QString("Trend side");
      if (modelIndex.column() == Expiry) return entry.expiryDate;
      if (modelIndex.column() == Rule)
        return entry.followsGlobalRules ? QString("Global")
                                        : QString("Customized");
    }
    if (role != Qt::DisplayRole) return {};
    switch (modelIndex.column()) {
      case Remove:
        return QString("-");
      case Contract:
        return entry.symbol;
      case CurrentOrTarget:
        return entry.marketPrice > 0.0 ? currency(entry.marketPrice)
                                       : QString("--");
      case TodayChange:
        return entry.marketPrice > 0.0 && state.previousClose > 0.0
                   ? percentage(entry.todayChangePercent)
                   : QString("--");
      case FiveDayChange:
        return percentage(entry.fiveDayChangePercent);
      case ExpectedChange:
        return percentage(entry.expectedChangePercent);
      case Sides:
        return entry.showBothSides ? QString("Both sides")
                                   : QString("Trend side");
      case Expiry:
        return entry.expiryDate.toString("yyyy-MM-dd");
      case Rule:
        return entry.followsGlobalRules ? QString("Global")
                                        : QString("Customized");
      default:
        return {};
    }
  }

  const auto& state = entries_.at(parentRow(modelIndex));
  if (!state.optionsLoaded) {
    if (modelIndex.column() == Contract && role == Qt::DisplayRole)
      return QString("Expand to load options");
    return {};
  }
  if (modelIndex.row() >= state.options.size()) return {};
  const auto& option = state.options.at(modelIndex.row());
  if (role == Qt::EditRole && modelIndex.column() == Quantity)
    return option.expectedQuantity;
  if (role != Qt::DisplayRole) return {};
  switch (modelIndex.column()) {
    case Contract:
      return QString("%1 %2 %3")
          .arg(option.expiry.toString("yyyy-MM-dd"),
               QString::number(option.strike, 'f', 2),
               option.right == "C" ? "Call" : "Put");
    case CurrentOrTarget:
      return option.lastPrice > 0.0 ? currency(option.lastPrice)
                                    : QString("--");
    case Bid:
      return option.bid > 0.0 ? currency(option.bid) : QString("--");
    case Ask:
      return option.ask > 0.0 ? currency(option.ask) : QString("--");
    case Quantity:
      return QLocale().toString(option.expectedQuantity, 'f', 0);
    case MaintenanceMargin:
      return option.maintenanceMargin.isEmpty() ? option.quoteStatus
                                                : option.maintenanceMargin;
    default:
      return {};
  }
}

QVariant WatchlistTreeModel::headerData(int section,
                                        Qt::Orientation orientation,
                                        int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return QAbstractItemModel::headerData(section, orientation, role);
  static const QStringList headers = {"",
                                      "Stock / Option",
                                      "Last / target",
                                      "Today change",
                                      "5 sessions",
                                      "Expected rate",
                                      "Sides",
                                      "Expiry",
                                      "Rule",
                                      "Bid",
                                      "Ask",
                                      "Qty",
                                      "Maint. margin"};
  return headers.value(section);
}

Qt::ItemFlags WatchlistTreeModel::flags(const QModelIndex& modelIndex) const {
  if (!modelIndex.isValid()) return Qt::NoItemFlags;
  auto itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  if (isAddRow(modelIndex) && modelIndex.column() == Contract)
    return itemFlags | Qt::ItemIsEditable;
  if (isOption(modelIndex) && modelIndex.column() == Quantity)
    return itemFlags | Qt::ItemIsEditable;
  if (!isOption(modelIndex) && !isAddRow(modelIndex) &&
      (modelIndex.column() == ExpectedChange || modelIndex.column() == Sides ||
        modelIndex.column() == Expiry || modelIndex.column() == Rule))
    itemFlags |= Qt::ItemIsEditable;
  return itemFlags;
}

bool WatchlistTreeModel::setData(const QModelIndex& modelIndex,
                                 const QVariant& value, int role) {
  if (!modelIndex.isValid() || role != Qt::EditRole) return false;
  if (isOption(modelIndex)) {
    if (modelIndex.column() != Quantity) return false;
    bool ok = false;
    const double quantity = value.toDouble(&ok);
    if (!ok || quantity < 1.0 || quantity > 1000000.0) return false;
    auto& option = entries_[parentRow(modelIndex)].options[modelIndex.row()];
    option.expectedQuantity = quantity;
    emit dataChanged(modelIndex, modelIndex, {Qt::DisplayRole, Qt::EditRole});
    emit candidateQuantityChanged(option);
    return true;
  }
  if (isAddRow(modelIndex)) {
    if (modelIndex.column() != Contract) return false;
    const QString symbol = value.toString().trimmed().toUpper();
    if (symbol.isEmpty()) return false;
    emit symbolEntryRequested(symbol);
    return true;
  }
  auto& entry = entries_[modelIndex.row()].entry;
  if (modelIndex.column() == ExpectedChange) {
    bool ok = false;
    const double rate = value.toDouble(&ok);
    if (!ok || rate < 0.0 || rate > 100.0) return false;
    entry.expectedChangePercent = rate;
    entry.followsGlobalRules = false;
  } else if (modelIndex.column() == Sides) {
    entry.showBothSides = value.toString() == "Both sides" || value.toBool();
    entry.followsGlobalRules = false;
  } else if (modelIndex.column() == Expiry) {
    const QDate date = value.toDate();
    if (!date.isValid()) return false;
    entry.expiryDate = date;
    entry.followsGlobalRules = false;
  } else if (modelIndex.column() == Rule) {
    entry.followsGlobalRules = value.toString() == "Global";
    if (entry.followsGlobalRules) {
      entry.expectedChangePercent = globalExpectedRate_;
      entry.showBothSides = globalBothSides_;
      entry.expiryDate = globalExpiryDate_;
    }
  } else {
    return false;
  }
  emit dataChanged(index(modelIndex.row(), ExpectedChange),
                   index(modelIndex.row(), Rule),
                   {Qt::DisplayRole, Qt::EditRole});
  emit entryChanged(entry);
  emit optionsRefreshRequested(entry);
  return true;
}

void WatchlistTreeModel::replaceEntries(QList<WatchlistEntry> entries) {
  beginResetModel();
  entries_.clear();
  for (auto& entry : entries) {
    if (!entry.expiryDate.isValid())
      entry.expiryDate = globalExpiryDate_.isValid()
                             ? globalExpiryDate_
                             : QDate::currentDate().addDays(7);
    entries_.append({.entry = std::move(entry)});
  }
  endResetModel();
}

void WatchlistTreeModel::addQualifiedSymbol(QString symbol, qint64 conId) {
  symbol = symbol.trimmed().toUpper();
  if (symbol.isEmpty() || conId == 0) return;
  for (int row = 0; row < entries_.size(); ++row) {
    auto& entry = entries_[row].entry;
    if (entry.symbol == symbol || entry.conId == conId) {
      entry.symbol = symbol;
      entry.conId = conId;
      emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
      emit entryChanged(entry);
      return;
    }
  }
  const int row = entries_.size();
  beginInsertRows({}, row, row);
  entries_.append({.entry = {
                       .conId = conId,
                       .symbol = std::move(symbol),
                       .expectedChangePercent = globalExpectedRate_,
                       .showBothSides = globalBothSides_,
                       .expiryDate = globalExpiryDate_.isValid()
                                         ? globalExpiryDate_
                                         : QDate::currentDate().addDays(7),
                       .followsGlobalRules = true,
                   }});
  endInsertRows();
  emit entryChanged(entries_.last().entry);
}

void WatchlistTreeModel::updateQuote(qint64 conId, double marketPrice) {
  for (int row = 0; row < entries_.size(); ++row) {
    auto& state = entries_[row];
    if (state.entry.conId == conId) {
      state.entry.marketPrice = marketPrice;
      state.entry.todayChangePercent =
          marketPrice > 0.0 && state.previousClose > 0.0
              ? (marketPrice / state.previousClose - 1.0) * 100.0
              : 0.0;
      emit dataChanged(index(row, CurrentOrTarget), index(row, ExpectedChange));
      return;
    }
  }
}

void WatchlistTreeModel::updateHistory(qint64 conId, double previousClose,
                                       double fiveDayChangePercent) {
  for (int row = 0; row < entries_.size(); ++row) {
    auto& state = entries_[row];
    if (state.entry.conId == conId) {
      state.previousClose = previousClose;
      state.entry.todayChangePercent =
          state.entry.marketPrice > 0.0 && previousClose > 0.0
              ? (state.entry.marketPrice / previousClose - 1.0) * 100.0
              : 0.0;
      state.entry.fiveDayChangePercent = fiveDayChangePercent;
      emit dataChanged(index(row, TodayChange), index(row, FiveDayChange));
      return;
    }
  }
}

void WatchlistTreeModel::replaceOptions(const QString& symbol,
                                        QList<OptionCandidate> candidates) {
  for (int row = 0; row < entries_.size(); ++row) {
    if (entries_[row].entry.symbol != symbol) continue;
    beginResetModel();
    entries_[row].options = std::move(candidates);
    entries_[row].optionsLoaded = true;
    endResetModel();
    return;
  }
}

void WatchlistTreeModel::updateCandidate(const OptionCandidate& candidate) {
  for (int row = 0; row < entries_.size(); ++row) {
    if (entries_[row].entry.symbol != candidate.underlying) continue;
    for (int optionRow = 0; optionRow < entries_[row].options.size();
         ++optionRow) {
      auto& option = entries_[row].options[optionRow];
      if (option.expiry != candidate.expiry ||
          option.right != candidate.right ||
          !qFuzzyCompare(option.strike + 1.0, candidate.strike + 1.0))
        continue;
      option = candidate;
      const auto parentIndex = index(row, 0);
      emit dataChanged(index(optionRow, 0, parentIndex),
                       index(optionRow, ColumnCount - 1, parentIndex));
      return;
    }
  }
}

WatchlistEntry WatchlistTreeModel::entryAt(int row) const {
  return row >= 0 && row < entries_.size() ? entries_.at(row).entry
                                           : WatchlistEntry{};
}

WatchlistEntry WatchlistTreeModel::entryFor(
    const QModelIndex& modelIndex) const {
  return modelIndex.isValid() && !isAddRow(modelIndex)
             ? entryAt(parentRow(modelIndex))
             : WatchlistEntry{};
}

OptionCandidate WatchlistTreeModel::candidateAt(
    const QModelIndex& modelIndex) const {
  if (!isOption(modelIndex)) return {};
  const auto& state = entries_.at(parentRow(modelIndex));
  return state.optionsLoaded ? state.options.value(modelIndex.row())
                             : OptionCandidate{};
}

bool WatchlistTreeModel::isOption(const QModelIndex& modelIndex) const {
  return modelIndex.isValid() && modelIndex.internalId() != 0;
}

bool WatchlistTreeModel::isAddRow(const QModelIndex& modelIndex) const {
  return modelIndex.isValid() && !isOption(modelIndex) &&
         modelIndex.row() == entries_.size();
}

bool WatchlistTreeModel::hasOptions(const QModelIndex& modelIndex) const {
  const int row = parentRow(modelIndex);
  return row >= 0 && row < entries_.size() && entries_.at(row).optionsLoaded;
}

WatchlistEntry WatchlistTreeModel::removeEntryAt(int row) {
  if (row < 0 || row >= entries_.size()) return {};
  const auto removed = entries_.at(row).entry;
  beginRemoveRows({}, row, row);
  entries_.removeAt(row);
  endRemoveRows();
  return removed;
}

void WatchlistTreeModel::setGlobalRuleValues(double expectedRate,
                                             bool bothSides,
                                             const QDate& expiryDate) {
  globalExpectedRate_ = qBound(0.0, expectedRate, 100.0);
  globalBothSides_ = bothSides;
  globalExpiryDate_ = expiryDate;
}

QList<WatchlistEntry> WatchlistTreeModel::applyGlobalRules(double expectedRate,
                                                           bool bothSides,
                                                           const QDate& expiryDate) {
  globalExpectedRate_ = expectedRate;
  globalBothSides_ = bothSides;
  globalExpiryDate_ = expiryDate;
  QList<WatchlistEntry> changed;
  for (int row = 0; row < entries_.size(); ++row) {
    auto& entry = entries_[row].entry;
    if (!entry.followsGlobalRules) continue;
    entry.expectedChangePercent = expectedRate;
    entry.showBothSides = bothSides;
    entry.expiryDate = expiryDate;
    changed.append(entry);
    emit dataChanged(index(row, ExpectedChange), index(row, Rule),
                     {Qt::DisplayRole, Qt::EditRole});
  }
  return changed;
}

int WatchlistTreeModel::parentRow(const QModelIndex& modelIndex) const {
  if (!modelIndex.isValid()) return -1;
  return isOption(modelIndex) ? static_cast<int>(modelIndex.internalId()) - 1
                              : modelIndex.row();
}