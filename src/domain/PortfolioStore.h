#pragma once

#include <QHash>
#include <QObject>
#include <memory>

#include "domain/PortfolioTypes.h"

class PortfolioStore final : public QObject {
  Q_OBJECT

 public:
  explicit PortfolioStore(QObject* parent = nullptr);
  ~PortfolioStore() override;

  void clear();
  void updateEquity(EquityPosition position);
  void updateOption(OptionPosition position);
  void updateLastPrice(qint64 conId, double lastPrice);
  void updatePrevClose(qint64 conId, double prevClose);
  void updateUnrealizedPnl(qint64 conId, double unrealizedPnl);
  void updateOptionUnderlyingPrice(qint64 conId, double underlyingPrice);
  void setEarningsDate(qint64 conId, const QDate& earningsDate);
  void saveStrategy(const QString& label, const QList<OptionPosition>& legs);
  void removeStrategy(const QString& identifier);
  [[nodiscard]] QList<WatchlistEntry> savedWatchlist() const;
  void saveWatchlistEntry(const WatchlistEntry& entry);
  void removeWatchlistEntry(qint64 conId);
  void recordExecution(TradeRecord record);
  void recordCommission(const QString& executionId, double commission);
  [[nodiscard]] QList<TradeRecord> tradeRecords(
      const QString& assetClass) const;
  [[nodiscard]] double realizedPnl(const QDateTime& start,
                                   const QDateTime& end) const;

 signals:
  void equitiesChanged(QList<EquityPosition> positions);
  void optionsChanged(QList<OptionStrategy> strategies);

 private:
  void publishEquities();
  void publishOptions();

  QHash<qint64, EquityPosition> equities_;
  QHash<qint64, OptionPosition> options_;
  std::unique_ptr<class LocalStore> localStore_;
};