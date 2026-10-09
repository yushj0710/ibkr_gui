#pragma once

#include <QAbstractTableModel>

#include "domain/PortfolioTypes.h"

class EquityPositionModel final : public QAbstractTableModel {
  Q_OBJECT

 public:
  enum Column {
    Symbol,
    LastPrice,
    TodayChange,
    AverageCost,
    Quantity,
    UnrealizedPnl,
    EarningsDate,
    ColumnCount,
  };

  explicit EquityPositionModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                    int role) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
  bool setData(const QModelIndex& index, const QVariant& value,
               int role) override;

  void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

  void replacePositions(QList<EquityPosition> positions);
  [[nodiscard]] EquityPosition positionAt(int row) const;

 signals:
  void earningsDateChanged(qint64 conId, QDate earningsDate);

 private:
  QList<EquityPosition> positions_;
};