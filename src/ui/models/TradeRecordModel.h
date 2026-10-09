#pragma once

#include <QAbstractTableModel>

#include "domain/PortfolioTypes.h"

class TradeRecordModel final : public QAbstractTableModel {
  Q_OBJECT

 public:
  explicit TradeRecordModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                    int role) const override;

  void appendRecord(TradeRecord record);
  void replaceRecords(QList<TradeRecord> records);

 private:
  QList<TradeRecord> records_;
};