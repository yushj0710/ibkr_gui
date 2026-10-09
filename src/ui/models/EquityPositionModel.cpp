#include "EquityPositionModel.h"

#include <QColor>
#include <QFont>
#include <QLocale>

namespace {
QString currency(double value) {
  return QLocale().toCurrencyString(value, "$", 2);
}
}  // namespace

EquityPositionModel::EquityPositionModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int EquityPositionModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : positions_.size() + 1;
}

int EquityPositionModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant EquityPositionModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() > positions_.size()) {
    return {};
  }

  if (index.row() == positions_.size()) {
    if (index.column() == Symbol && role == Qt::DisplayRole)
      return QStringLiteral("Total");
    if (index.column() == UnrealizedPnl) {
      double total = 0.0;
      for (const auto& position : positions_) total += position.unrealizedPnl;
      if (role == Qt::EditRole) return total;
      if (role == Qt::DisplayRole) return currency(total);
      if (role == Qt::ForegroundRole)
        return total < 0.0 ? QColor("#b42318") : QColor("#067647");
    }
    if (role == Qt::FontRole) {
      QFont font;
      font.setBold(true);
      return font;
    }
    return {};
  }

  const auto& position = positions_.at(index.row());
  if (role == Qt::TextAlignmentRole && index.column() >= LastPrice &&
      index.column() <= UnrealizedPnl) {
    return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
  }
  if (role == Qt::ForegroundRole &&
      (index.column() == UnrealizedPnl || index.column() == TodayChange)) {
    const double value = index.column() == UnrealizedPnl
                             ? position.unrealizedPnl
                             : position.todayChange;
    return value < 0.0 ? QColor("#b42318") : QColor("#067647");
  }
  if (role != Qt::DisplayRole && role != Qt::EditRole) {
    return {};
  }

  switch (index.column()) {
    case Symbol:
      return position.symbol;
    case LastPrice:
      return role == Qt::EditRole ? QVariant(position.lastPrice)
                                  : QVariant(currency(position.lastPrice));
    case TodayChange:
      return position.prevClose > 0.0
                 ? QString::number(position.todayChange * 100.0, 'f', 2) + "%"
                 : QStringLiteral("--");
    case AverageCost:
      return role == Qt::EditRole ? QVariant(position.averageCost)
                                  : QVariant(currency(position.averageCost));
    case Quantity:
      return QLocale().toString(position.quantity, 'f', 0);
    case UnrealizedPnl:
      return role == Qt::EditRole ? QVariant(position.unrealizedPnl)
                                  : QVariant(currency(position.unrealizedPnl));
    case EarningsDate:
      return role == Qt::EditRole
                 ? QVariant(position.earningsDate)
                 : QVariant(position.earningsDate.toString("yyyy-MM-dd"));
    default:
      return {};
  }
}

QVariant EquityPositionModel::headerData(int section,
                                         Qt::Orientation orientation,
                                         int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  static const QStringList headers = {
      "Symbol",   "Last",           "Today change", "Open price",
      "Quantity", "Unrealized P&L", "Earnings date"};
  return headers.value(section);
}

Qt::ItemFlags EquityPositionModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }
  auto itemFlags = QAbstractTableModel::flags(index) | Qt::ItemIsSelectable;
  if (index.column() == EarningsDate) {
    itemFlags |= Qt::ItemIsEditable;
  }
  return itemFlags;
}

bool EquityPositionModel::setData(const QModelIndex& index,
                                  const QVariant& value, int role) {
  if (!index.isValid() || index.column() != EarningsDate ||
      role != Qt::EditRole) {
    return false;
  }
  const QDate date = value.toDate();
  if (!date.isValid()) {
    return false;
  }
  positions_[index.row()].earningsDate = date;
  emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
  emit earningsDateChanged(positions_.at(index.row()).conId, date);
  return true;
}

void EquityPositionModel::sort(int column, Qt::SortOrder order) {
  if (column < 0 || column >= ColumnCount) return;
  beginResetModel();
  std::sort(positions_.begin(), positions_.end(),
            [column, order](const EquityPosition& a, const EquityPosition& b) {
              const auto less = [column](const EquityPosition& lhs,
                                         const EquityPosition& rhs) {
                switch (column) {
                  case Symbol:
                    return lhs.symbol < rhs.symbol;
                  case LastPrice:
                    return lhs.lastPrice < rhs.lastPrice;
                  case TodayChange:
                    return lhs.todayChange < rhs.todayChange;
                  case AverageCost:
                    return lhs.averageCost < rhs.averageCost;
                  case Quantity:
                    return lhs.quantity < rhs.quantity;
                  case UnrealizedPnl:
                    return lhs.unrealizedPnl < rhs.unrealizedPnl;
                  case EarningsDate:
                    return lhs.earningsDate < rhs.earningsDate;
                  default:
                    return false;
                }
              };
              return order == Qt::AscendingOrder ? less(a, b) : less(b, a);
            });
  endResetModel();
}

void EquityPositionModel::replacePositions(QList<EquityPosition> positions) {
  beginResetModel();
  positions_ = std::move(positions);
  endResetModel();
}

EquityPosition EquityPositionModel::positionAt(int row) const {
  return positions_.value(row);
}