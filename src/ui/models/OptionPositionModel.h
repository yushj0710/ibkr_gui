#pragma once

#include <QAbstractItemModel>

#include "domain/PortfolioTypes.h"

class OptionPositionModel final : public QAbstractItemModel {
  Q_OBJECT

 public:
  enum Column {
    Contract,
    MarketPrice,
    AverageCost,
    Quantity,
    UnrealizedPnl,
    Strike,
    Expiry,
    UnderlyingPrice,
    MaintenanceMargin,
    ColumnCount,
  };

  explicit OptionPositionModel(QObject* parent = nullptr);

  [[nodiscard]] QModelIndex index(
      int row, int column, const QModelIndex& parent = {}) const override;
  [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                    int role) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

  void replaceStrategies(QList<OptionStrategy> strategies);
  [[nodiscard]] bool isLeg(const QModelIndex& index) const;
  [[nodiscard]] OptionPosition legAt(const QModelIndex& index) const;
  [[nodiscard]] OptionStrategy strategyAt(const QModelIndex& index) const;
  [[nodiscard]] QString unrealizedPnlByExpiry(const QDate& start,
                                              const QDate& end,
                                              double* total) const;

 private:
  [[nodiscard]] bool isAtRisk(const OptionPosition& position) const;
  [[nodiscard]] bool strategyIsAtRisk(const OptionStrategy& strategy) const;

  QList<OptionStrategy> strategies_;
};