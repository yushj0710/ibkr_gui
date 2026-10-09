#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "domain/ConnectionProfile.h"
#include "domain/PortfolioStore.h"

enum class SessionState {
  Disconnected,
  ConfigurationPending,
  Connecting,
  Ready,
  Error,
};

class IbkrSession final : public QObject {
  Q_OBJECT

 public:
  explicit IbkrSession(QObject* parent = nullptr);
  ~IbkrSession() override;

  [[nodiscard]] SessionState state() const;
  void configureConnection(const ConnectionProfile& profile);
  void selectAccount(const QString& account);
  void requestMarketIndices();
  void subscribeWatchlistSymbol(const QString& symbol);
  void unsubscribeWatchlistContract(qint64 conId);
  void requestWatchlistOptions(const WatchlistEntry& entry);
  void unsubscribeWatchlistOptions(const QString& symbol);
  void requestOptionCandidateMarginPreview(qint64 conId, double quantity,
                                           double price);
  void requestWhatIfPreview(qint64 conId, const QString& action,
                            double quantity, const QString& orderType,
                            double price, const QString& fingerprint);
  void submitOrder(qint64 conId, const QString& action, double quantity,
                   const QString& orderType, double price,
                   const QString& fingerprint);
  [[nodiscard]] QList<WatchlistEntry> savedWatchlist() const;
  void saveWatchlistEntry(const WatchlistEntry& entry);
  void removeWatchlistEntry(qint64 conId);
  void saveEarningsDate(qint64 conId, const QDate& earningsDate);
  void saveStrategy(const QString& label, const QList<OptionPosition>& legs);
  void removeStrategy(const QString& identifier);
  [[nodiscard]] QList<TradeRecord> tradeRecords(
      const QString& assetClass) const;
  [[nodiscard]] double realizedPnl(const QDateTime& start,
                                   const QDateTime& end) const;
  void disconnect();

 signals:
  void stateChanged(SessionState state, QString detail);
  void managedAccountsReceived(QStringList accounts);
  void accountSelected(QString account);
  void accountValueUpdated(QString key, QString value, QString currency);
  void equityPositionsUpdated(QList<EquityPosition> positions);
  void optionPositionsUpdated(QList<OptionStrategy> strategies);
  void whatIfPreviewReceived(qint64 conId, QString fingerprint,
                             QString initialMarginChange,
                             QString maintenanceMarginChange, QString warnings);
  void orderSubmitted(qint64 conId, int orderId);
  void watchlistContractQualified(QString symbol, qint64 conId);
  void watchlistContractRejected(QString symbol, QString reason);
  void watchlistQuoteUpdated(qint64 conId, double marketPrice);
  void watchlistPerformanceUpdated(qint64 conId, double todayChangePercent,
                                   double fiveDayChangePercent);
  void watchlistOptionsReady(QString symbol, QList<OptionCandidate> candidates);
  void optionCandidateUpdated(OptionCandidate candidate);
  void tradeRecordReceived(TradeRecord record);
  void tradeRecordsUpdated(QString assetClass, QList<TradeRecord> records);
  void marketIndexUpdated(QString name, double price, double changePercent);

 private:
  class Impl;
  void subscribePosition(qint64 conId);
  void unsubscribePosition(qint64 conId);
  void subscribeWatchlistContract(qint64 conId);
  void qualifyOptionCandidates(QList<OptionCandidate> candidates);
  void subscribeOptionCandidate(qint64 conId);
  void cancelPositionSubscriptions();
  SessionState state_ = SessionState::Disconnected;
  PortfolioStore portfolio_;
  std::unique_ptr<Impl> impl_;
};