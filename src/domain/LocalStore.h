#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>

#include "domain/PortfolioTypes.h"

class LocalStore final {
 public:
  LocalStore();
  ~LocalStore();

  [[nodiscard]] QList<WatchlistEntry> watchlistEntries() const;
  void saveWatchlistEntry(const WatchlistEntry& entry);
  void removeWatchlistEntry(qint64 conId);
  [[nodiscard]] QDate earningsDate(qint64 conId) const;
  void saveEarningsDate(qint64 conId, const QDate& date);
  [[nodiscard]] QList<OptionStrategy> strategies() const;
  void saveStrategy(const QString& label, const QList<OptionPosition>& legs);
  void removeStrategy(const QString& identifier);
  void recordExecution(TradeRecord record);
  void recordCommission(const QString& executionId, double commission);
  [[nodiscard]] QList<TradeRecord> records(const QString& assetClass) const;
  [[nodiscard]] double realizedPnl(const QDateTime& start,
                                   const QDateTime& end) const;

 private:
  void initialize();
  void recomputeFifo();

  QString connectionName_;
};