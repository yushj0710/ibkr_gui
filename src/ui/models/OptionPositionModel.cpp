#include "OptionPositionModel.h"

#include <QColor>
#include <QLocale>

namespace {
QString currency(double value) {
  return QLocale().toCurrencyString(value, "$", 2);
}
}  // namespace

OptionPositionModel::OptionPositionModel(QObject* parent)
    : QAbstractItemModel(parent) {}

QModelIndex OptionPositionModel::index(int row, int column,
                                       const QModelIndex& parent) const {
  if (row < 0 || column < 0 || column >= ColumnCount) {
    return {};
  }
  if (!parent.isValid()) {
    return row < strategies_.size() ? createIndex(row, column, nullptr)
                                    : QModelIndex{};
  }
  if (parent.internalPointer() == nullptr &&
      parent.row() < strategies_.size()) {
    return row < strategies_.at(parent.row()).legs.size()
               ? createIndex(
                     row, column,
                     const_cast<OptionStrategy*>(&strategies_.at(parent.row())))
               : QModelIndex{};
  }
  return {};
}

QModelIndex OptionPositionModel::parent(const QModelIndex& child) const {
  if (!child.isValid() || child.internalPointer() == nullptr) {
    return {};
  }
  const auto* strategy =
      static_cast<const OptionStrategy*>(child.internalPointer());
  for (int row = 0; row < strategies_.size(); ++row) {
    if (&strategies_.at(row) == strategy) {
      return createIndex(row, 0, nullptr);
    }
  }
  return {};
}

int OptionPositionModel::rowCount(const QModelIndex& parent) const {
  if (!parent.isValid()) {
    return strategies_.size();
  }
  if (parent.internalPointer() == nullptr &&
      parent.row() < strategies_.size()) {
    return strategies_.at(parent.row()).legs.size();
  }
  return 0;
}

int OptionPositionModel::columnCount(const QModelIndex& parent) const {
  Q_UNUSED(parent)
  return ColumnCount;
}

QVariant OptionPositionModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) {
    return {};
  }
  const bool leg = isLeg(index);
  const auto* strategy =
      leg ? static_cast<const OptionStrategy*>(index.internalPointer())
          : &strategies_.at(index.row());
  if (role == Qt::BackgroundRole &&
      ((leg && isAtRisk(strategy->legs.at(index.row()))) ||
       (!leg && strategyIsAtRisk(*strategy)))) {
    return QColor("#fef3f2");
  }
  if (role == Qt::ForegroundRole &&
      ((leg && isAtRisk(strategy->legs.at(index.row()))) ||
       (!leg && strategyIsAtRisk(*strategy)))) {
    return QColor("#b42318");
  }
  if (role == Qt::TextAlignmentRole && index.column() >= MarketPrice &&
      index.column() <= MaintenanceMargin) {
    return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
  }
  if (role != Qt::DisplayRole) {
    return {};
  }
  if (!leg) {
    switch (index.column()) {
      case Contract:
        return strategy->label + (strategy->verified ? "" : " (unverified)");
      case Quantity:
        return strategy->legs.size();
      case UnrealizedPnl: {
        double total = 0.0;
        for (const auto& option : strategy->legs) total += option.unrealizedPnl;
        return currency(total);
      }
      default:
        return {};
    }
  }

  const auto& option = strategy->legs.at(index.row());
  switch (index.column()) {
    case Contract:
      return option.localSymbol;
    case MarketPrice:
      return currency(option.lastPrice);
    case AverageCost:
      return currency(option.averageCost);
    case Quantity:
      return QLocale().toString(option.quantity, 'f', 0);
    case UnrealizedPnl:
      return currency(option.unrealizedPnl);
    case Strike:
      return QLocale().toString(option.strike, 'f', 2);
    case Expiry:
      return option.expiry.toString("yyyy-MM-dd");
    case UnderlyingPrice:
      return currency(option.underlyingPrice);
    case MaintenanceMargin:
      return option.maintenanceMargin > 0.0 ? currency(option.maintenanceMargin)
                                            : "--";
    default:
      return {};
  }
}

QVariant OptionPositionModel::headerData(int section,
                                         Qt::Orientation orientation,
                                         int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return QAbstractItemModel::headerData(section, orientation, role);
  }
  static const QStringList headers = {
      "Contract / strategy", "Current", "Open price", "Quantity",
      "Unrealized P&L",      "Strike",  "Expiry",     "Underlying",
      "Maint. margin"};
  return headers.value(section);
}

Qt::ItemFlags OptionPositionModel::flags(const QModelIndex& index) const {
  return index.isValid()
             ? QAbstractItemModel::flags(index) | Qt::ItemIsSelectable
             : Qt::NoItemFlags;
}

void OptionPositionModel::replaceStrategies(QList<OptionStrategy> strategies) {
  beginResetModel();
  strategies_ = std::move(strategies);
  endResetModel();
}

bool OptionPositionModel::isLeg(const QModelIndex& index) const {
  return index.isValid() && index.internalPointer() != nullptr;
}

OptionPosition OptionPositionModel::legAt(const QModelIndex& index) const {
  if (!isLeg(index)) {
    return {};
  }
  const auto* strategy =
      static_cast<const OptionStrategy*>(index.internalPointer());
  return strategy->legs.value(index.row());
}

bool OptionPositionModel::isAtRisk(const OptionPosition& position) const {
  return position.strike > 0.0 && position.underlyingPrice > 0.0 &&
         qAbs(position.underlyingPrice - position.strike) / position.strike <=
             0.05;
}

bool OptionPositionModel::strategyIsAtRisk(
    const OptionStrategy& strategy) const {
  for (const auto& leg : strategy.legs) {
    if (isAtRisk(leg)) return true;
  }
  return false;
}

OptionStrategy OptionPositionModel::strategyAt(const QModelIndex& index) const {
  return !index.isValid() || isLeg(index) ? OptionStrategy{}
                                          : strategies_.value(index.row());
}

QString OptionPositionModel::unrealizedPnlByExpiry(const QDate& start,
                                                   const QDate& end,
                                                   double* total) const {
  QMap<QString, double> grouped;
  for (const auto& strategy : strategies_) {
    for (const auto& leg : strategy.legs) {
      if (leg.expiry >= start && leg.expiry <= end) {
        grouped[leg.expiry.toString("yyyy-MM-dd") + "  " + leg.underlying +
                "  " + strategy.label] += leg.unrealizedPnl;
      }
    }
  }
  double sum = 0.0;
  QStringList lines;
  for (auto it = grouped.cbegin(); it != grouped.cend(); ++it) {
    sum += it.value();
    lines.append(it.key() + ": " + currency(it.value()));
  }
  if (total) *total = sum;
  return lines.isEmpty()
             ? "No currently open option positions expire in this interval."
             : lines.join("\n");
}