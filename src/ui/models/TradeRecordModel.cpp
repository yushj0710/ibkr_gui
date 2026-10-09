#include "TradeRecordModel.h"

#include <QColor>
#include <QLocale>

TradeRecordModel::TradeRecordModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int TradeRecordModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : records_.size();
}

int TradeRecordModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : 8;
}

QVariant TradeRecordModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= records_.size()) return {};
  const auto& record = records_.at(index.row());
  if (role == Qt::ForegroundRole && index.column() == 6)
    return record.realizedPnl < 0.0 ? QColor("#b42318") : QColor("#067647");
  if (role == Qt::TextAlignmentRole && index.column() >= 4 &&
      index.column() <= 6)
    return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
  if (role != Qt::DisplayRole) return {};
  switch (index.column()) {
    case 0:
      return record.timestamp.toString("yyyy-MM-dd HH:mm:ss");
    case 1:
      return record.assetClass;
    case 2:
      return record.contract;
    case 3:
      return record.action;
    case 4:
      return QLocale().toString(record.quantity, 'f', 0);
    case 5:
      return QLocale().toCurrencyString(record.executionPrice, "$", 2);
    case 6:
      return QLocale().toCurrencyString(record.realizedPnl, "$", 2);
    case 7:
      return record.provisional ? "Provisional" : "Execution derived";
    default:
      return {};
  }
}

QVariant TradeRecordModel::headerData(int section, Qt::Orientation orientation,
                                      int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return QAbstractTableModel::headerData(section, orientation, role);
  static const QStringList headers = {"Time",         "Asset",    "Contract",
                                      "Action",       "Quantity", "Price",
                                      "Realized P&L", "Source"};
  return headers.value(section);
}

void TradeRecordModel::appendRecord(TradeRecord record) {
  const int row = records_.size();
  beginInsertRows({}, row, row);
  records_.append(std::move(record));
  endInsertRows();
}

void TradeRecordModel::replaceRecords(QList<TradeRecord> records) {
  beginResetModel();
  records_ = std::move(records);
  endResetModel();
}