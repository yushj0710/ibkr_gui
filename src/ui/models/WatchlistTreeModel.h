#pragma once

#include <QAbstractItemModel>

#include "domain/PortfolioTypes.h"

class WatchlistTreeModel final : public QAbstractItemModel {
  Q_OBJECT

 public:
  enum Column {
    Remove,
    Contract,
    CurrentOrTarget,
    TodayChange,
    FiveDayChange,
    ExpectedChange,
    Sides,
    Expiry,
    Rule,
    Bid,
    Ask,
    Quantity,
    MaintenanceMargin,
    ColumnCount,
  };

  explicit WatchlistTreeModel(QObject* parent = nullptr);

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
  bool setData(const QModelIndex& index, const QVariant& value,
               int role) override;

  void replaceEntries(QList<WatchlistEntry> entries);
  void addQualifiedSymbol(QString symbol, qint64 conId);
  void updateQuote(qint64 conId, double marketPrice);
  void updatePerformance(qint64 conId, double todayChangePercent,
                         double fiveDayChangePercent);
  void replaceOptions(const QString& symbol, QList<OptionCandidate> candidates);
  void updateCandidate(const OptionCandidate& candidate);
  [[nodiscard]] WatchlistEntry entryAt(int row) const;
  [[nodiscard]] WatchlistEntry entryFor(const QModelIndex& index) const;
  [[nodiscard]] OptionCandidate candidateAt(const QModelIndex& index) const;
  [[nodiscard]] bool isOption(const QModelIndex& index) const;
  [[nodiscard]] bool isAddRow(const QModelIndex& index) const;
  [[nodiscard]] bool hasOptions(const QModelIndex& index) const;
  [[nodiscard]] WatchlistEntry removeEntryAt(int row);
  void setGlobalRuleValues(double expectedRate, bool bothSides,
                           const QDate& expiryDate);
  [[nodiscard]] QList<WatchlistEntry> applyGlobalRules(double expectedRate,
                                                       bool bothSides,
                                                       const QDate& expiryDate);

 signals:
  void symbolEntryRequested(QString symbol);
  void entryChanged(WatchlistEntry entry);
  void optionsRefreshRequested(WatchlistEntry entry);
  void candidateQuantityChanged(OptionCandidate candidate);

 private:
  struct EntryState {
    WatchlistEntry entry;
    QList<OptionCandidate> options;
    bool optionsLoaded = false;
  };

  [[nodiscard]] int parentRow(const QModelIndex& index) const;
  QList<EntryState> entries_;
  double globalExpectedRate_ = 5.0;
  bool globalBothSides_ = true;
  QDate globalExpiryDate_;
};