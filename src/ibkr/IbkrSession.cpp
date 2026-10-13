#include "IbkrSession.h"

#include <QJsonObject>
#include <QMetaObject>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

#include "domain/ActivityLog.h"

#define IBKR_ENABLE_TWS_API

#ifdef IBKR_ENABLE_TWS_API
#include <CommissionAndFeesReport.h>
#include <Contract.h>
#include <DefaultEWrapper.h>
#include <EClientSocket.h>
#include <EReader.h>
#include <EReaderOSSignal.h>
#include <Execution.h>
#include <Order.h>
#include <OrderState.h>
#endif

class IbkrSession::Impl {
 public:
#ifdef IBKR_ENABLE_TWS_API
  class CallbackBridge final : public DefaultEWrapper {
   public:
    explicit CallbackBridge(IbkrSession& session) : session_(session) {}

    void nextValidId(int orderId) override {
      ActivityLog::ibkrResponse("next_valid_order_id", {{"order_id", orderId}});
      QMetaObject::invokeMethod(
          &session_,
          [this, orderId] {
            std::scoped_lock lock(session_.impl_->subscriptionMutex);
            session_.impl_->nextOrderId = orderId;
            session_.state_ = SessionState::Ready;
            emit session_.stateChanged(
                SessionState::Ready,
                "Connected to TWS. Requesting managed accounts.");
          },
          Qt::QueuedConnection);
    }

    void managedAccounts(const std::string& accountsList) override {
      ActivityLog::ibkrResponse(
          "managed_accounts",
          {{"accounts", QString::fromStdString(accountsList)}});
      QStringList accounts =
          QString::fromStdString(accountsList).split(',', Qt::SkipEmptyParts);
      for (QString& account : accounts) account = account.trimmed();
      QMetaObject::invokeMethod(
          &session_,
          [this, accounts = std::move(accounts)] {
            emit session_.managedAccountsReceived(accounts);
          },
          Qt::QueuedConnection);
    }

    void updateAccountValue(const std::string& key, const std::string& value,
                            const std::string& currency,
                            const std::string&) override {
      const QString qtKey = QString::fromStdString(key);
      const QString qtValue = QString::fromStdString(value);
      const QString qtCurrency = QString::fromStdString(currency);
      //   ActivityLog::ibkrResponse(
      //       "account_value",
      //       {{"key", qtKey}, {"value", qtValue}, {"currency", qtCurrency}});
      QMetaObject::invokeMethod(
          &session_,
          [this, qtKey, qtValue, qtCurrency] {
            emit session_.accountValueUpdated(qtKey, qtValue, qtCurrency);
          },
          Qt::QueuedConnection);
    }

    void updatePortfolio(const Contract& contract, Decimal quantity,
                         double marketPrice, double, double averageCost,
                         double unrealizedPnl, double,
                         const std::string&) override {
      const double positionQuantity =
          quantity.isUnset() ? 0.0
                             : DecimalFunctions::decimalToDouble(quantity);
      const QString securityType = QString::fromStdString(contract.secType);
      const QString expiry =
          QString::fromStdString(contract.lastTradeDateOrContractMonth).left(8);
      ActivityLog::ibkrResponse(
          "portfolio_position",
          {{"con_id", contract.conId},
           {"symbol", QString::fromStdString(contract.symbol)},
           {"security_type", securityType},
           {"quantity", positionQuantity},
           {"market_price", marketPrice},
           {"average_cost", averageCost},
           {"unrealized_pnl", unrealizedPnl}});
      auto apiContract = std::make_shared<Contract>(contract);
      QMetaObject::invokeMethod(
          &session_,
          [this, apiContract, conId = static_cast<qint64>(contract.conId),
           symbol = QString::fromStdString(contract.symbol),
           localSymbol = QString::fromStdString(contract.localSymbol),
           securityType, right = QString::fromStdString(contract.right), expiry,
           strike = contract.strike, marketPrice, averageCost, positionQuantity,
           unrealizedPnl] {
            if (securityType == "STK") {
              session_.portfolio_.updateEquity({
                  .conId = conId,
                  .symbol = symbol,
                  .lastPrice = marketPrice,
                  .averageCost = averageCost,
                  .quantity = positionQuantity,
                  .unrealizedPnl = unrealizedPnl,
              });
            } else if (securityType == "OPT") {
              session_.portfolio_.updateOption({
                  .conId = conId,
                  .underlying = symbol,
                  .localSymbol = localSymbol.isEmpty() ? symbol : localSymbol,
                  .right = right,
                  .lastPrice = marketPrice,
                  .averageCost = averageCost,
                  .quantity = positionQuantity,
                  .unrealizedPnl = unrealizedPnl,
                  .strike = strike,
                  .expiry = QDate::fromString(expiry, "yyyyMMdd"),
              });
            }
            if (securityType == "STK" || securityType == "OPT") {
              {
                std::scoped_lock lock(session_.impl_->subscriptionMutex);
                session_.impl_->contracts.insert(conId, apiContract);
                if (qFuzzyIsNull(positionQuantity)) {
                  session_.impl_->positionContracts.remove(conId);
                } else {
                  session_.impl_->positionContracts.insert(conId);
                }
              }
              if (qFuzzyIsNull(positionQuantity))
                session_.unsubscribePosition(conId);
              else
                session_.subscribePosition(conId);
            }
          },
          Qt::QueuedConnection);
    }

    void tickPrice(int requestId, TickType field, double price,
                   const TickAttrib&) override {
      if (price <= 0.0 ||
          (field != BID && field != ASK && field != LAST &&
           field != MARK_PRICE && field != DELAYED_BID &&
           field != DELAYED_ASK && field != DELAYED_LAST && field != OPEN &&
           field != DELAYED_OPEN && field != CLOSE && field != DELAYED_CLOSE)) {
        return;
      }
      qint64 conId = 0;
      OptionCandidate candidate;
      double displayPrice = 0.0;
      bool hasDisplayPrice = false;
      bool isPrevClose = field == CLOSE || field == DELAYED_CLOSE;
      bool isWatchlistSymbol = false;
      {
        std::scoped_lock lock(session_.impl_->subscriptionMutex);
        conId = session_.impl_->marketRequestContracts.value(requestId);
        auto subscription = session_.impl_->marketDataSubscriptions.find(conId);
        if (subscription != session_.impl_->marketDataSubscriptions.end()) {
          if (field == BID || field == DELAYED_BID)
            subscription->bid = price;
          else if (field == ASK || field == DELAYED_ASK)
            subscription->ask = price;
          else if (field == LAST || field == MARK_PRICE ||
                   field == DELAYED_LAST)
            subscription->last = price;
          displayPrice = subscription->last;
          if (displayPrice <= 0.0 && subscription->bid > 0.0 &&
              subscription->ask > 0.0)
            displayPrice = (subscription->bid + subscription->ask) / 2.0;
          hasDisplayPrice = displayPrice > 0.0;
        }
        isWatchlistSymbol = session_.impl_->watchlistSymbols.contains(conId);
        candidate = session_.impl_->optionCandidatesByConId.value(conId);
        if (candidate.conId != 0) {
          if (field == BID || field == DELAYED_BID) candidate.bid = price;
          if (field == ASK || field == DELAYED_ASK) candidate.ask = price;
          candidate.quoteStatus = "Live quote";
          session_.impl_->optionCandidatesByConId.insert(conId, candidate);
        }
      }
      if (conId == 0) return;
      QString indexName;
      {
        std::scoped_lock lock(session_.impl_->subscriptionMutex);
        indexName = session_.impl_->indexSymbolsByConId.value(conId);
      }
      ActivityLog::ibkrResponse("market_data_price",
                                {{"request_id", requestId},
                                 {"con_id", conId},
                                 {"tick_type", static_cast<int>(field)},
                                 {"price", price}});
      QMetaObject::invokeMethod(
          &session_,
          [this, conId, displayPrice, hasDisplayPrice, price, candidate,
           isPrevClose, isWatchlistSymbol] {
            if (hasDisplayPrice)
              session_.portfolio_.updateLastPrice(conId, displayPrice);
            if (isPrevClose) session_.portfolio_.updatePrevClose(conId, price);
            if (hasDisplayPrice && isWatchlistSymbol) {
              emit session_.watchlistQuoteUpdated(conId, displayPrice);
            }
            if (candidate.conId != 0)
              emit session_.optionCandidateUpdated(candidate);
          },
          Qt::QueuedConnection);
      if (!indexName.isEmpty() && isPrevClose) {
        QMetaObject::invokeMethod(
            &session_,
            [this, conId, price, indexName, isPrevClose] {
              if (isPrevClose) session_.impl_->indexOpens.insert(conId, price);
              const double open = session_.impl_->indexOpens.value(conId);
              if (open > 0.0) {
                emit session_.marketIndexUpdated(indexName, price,
                                                 (price / open - 1.0) * 100.0);
              }
            },
            Qt::QueuedConnection);
      }
    }

    void contractDetails(int requestId,
                         const ContractDetails& details) override {
      QString symbol;
      QString indexName;
      OptionCandidate candidate;
      {
        std::scoped_lock lock(session_.impl_->subscriptionMutex);
        symbol = session_.impl_->watchlistContractRequests.value(requestId);
        indexName = session_.impl_->indexContractRequests.value(requestId);
        candidate =
            session_.impl_->optionQualificationRequests.value(requestId);
      }
      ActivityLog::ibkrResponse(
          "contract_details",
          {{"request_id", requestId},
           {"con_id", details.contract.conId},
           {"symbol", QString::fromStdString(details.contract.symbol)},
           {"security_type", QString::fromStdString(details.contract.secType)},
           {"exchange", QString::fromStdString(details.contract.exchange)},
           {"candidate", candidate.underlying}});
      if (!indexName.isEmpty() && details.contract.secType == "IND") {
        auto contract = std::make_shared<Contract>(details.contract);
        const qint64 conId = contract->conId;
        QMetaObject::invokeMethod(
            &session_,
            [this, requestId, indexName, conId, contract] {
              {
                std::scoped_lock lock(session_.impl_->subscriptionMutex);
                session_.impl_->indexContractRequests.remove(requestId);
                session_.impl_->indexSymbolsByConId.insert(conId, indexName);
                session_.impl_->retainMarketData(conId, contract,
                                                 "index:" + indexName);
              }
            },
            Qt::QueuedConnection);
        ActivityLog::ibkrResponse("index_contract_details",
                                  {{"name", indexName}, {"con_id", conId}});
        return;
      }
      if (!candidate.underlying.isEmpty() &&
          details.contract.secType == "OPT") {
        auto contract = std::make_shared<Contract>(details.contract);
        candidate.conId = static_cast<qint64>(contract->conId);
        candidate.quoteStatus = "Waiting for quote";
        QMetaObject::invokeMethod(
            &session_,
            [this, requestId, candidate, contract] {
              {
                std::scoped_lock lock(session_.impl_->subscriptionMutex);
                session_.impl_->optionQualificationRequests.remove(requestId);
                session_.impl_->contracts.insert(candidate.conId, contract);
                session_.impl_->optionCandidatesByConId.insert(candidate.conId,
                                                               candidate);
                session_.impl_->optionCandidatesBySymbol[candidate.underlying]
                    .insert(candidate.conId);
              }
              session_.subscribeOptionCandidate(candidate.conId);
              emit session_.optionCandidateUpdated(candidate);
            },
            Qt::QueuedConnection);
        return;
      }
      if (symbol.isEmpty() || details.contract.secType != "STK") return;
      auto contract = std::make_shared<Contract>(details.contract);
      const qint64 conId = contract->conId;
      QMetaObject::invokeMethod(
          &session_,
          [this, requestId, symbol, conId, contract] {
            {
              std::scoped_lock lock(session_.impl_->subscriptionMutex);
              session_.impl_->watchlistContractRequests.remove(requestId);
              session_.impl_->contracts.insert(conId, contract);
              session_.impl_->watchlistSymbols.insert(conId, symbol);
            }
            session_.subscribeWatchlistContract(conId);
            emit session_.watchlistContractQualified(symbol, conId);
          },
          Qt::QueuedConnection);
    }

    void historicalData(int requestId, const Bar& bar) override {
      if (bar.close <= 0.0) return;
      const QDate date =
          QDate::fromString(QString::fromStdString(bar.time), "yyyyMMdd");
      ActivityLog::ibkrResponse("historical_bar",
                                {{"request_id", requestId},
                                 {"date", QString::fromStdString(bar.time)},
                                 {"close", bar.close}});
      if (!date.isValid()) return;
      std::scoped_lock lock(session_.impl_->subscriptionMutex);
      if (session_.impl_->historicalRequestContracts.contains(requestId)) {
        session_.impl_->historicalCloses[requestId].append({date, bar.close});
      }
    }

    void historicalDataEnd(int requestId, const std::string&,
                           const std::string&) override {
      qint64 conId = 0;
      QVector<Impl::HistoricalBar> bars;
      {
        std::scoped_lock lock(session_.impl_->subscriptionMutex);
        conId = session_.impl_->historicalRequestContracts.take(requestId);
        session_.impl_->historicalRequestsByContract.remove(conId);
        bars = session_.impl_->historicalCloses.take(requestId);
      }
      if (conId == 0 || bars.isEmpty()) return;
      const QDate today = QDate::currentDate();
      int previousCloseIndex = bars.size() - 1;
      while (previousCloseIndex >= 0 &&
             bars.at(previousCloseIndex).date >= today)
        --previousCloseIndex;
      if (previousCloseIndex < 0) return;
      const double previousClose = bars.at(previousCloseIndex).close;
      const int fiveDayIndex = qMax(0, previousCloseIndex - 5);
      const double fiveDaysAgo = bars.at(fiveDayIndex).close;
      const double latestClose = bars.at(previousCloseIndex).close;
      const double fiveDays =
          fiveDaysAgo > 0.0
              ? (latestClose / fiveDaysAgo - 1.0) * 100.0
              : 0.0;
      QMetaObject::invokeMethod(
          &session_,
          [this, conId, previousClose, fiveDays] {
            emit session_.watchlistHistoryUpdated(conId, previousClose,
                                                  fiveDays);
          },
          Qt::QueuedConnection);
    }

    void securityDefinitionOptionalParameter(
        int requestId, const std::string&, int, const std::string&,
        const std::string&, const std::set<std::string>& expirations,
        const std::set<double>& strikes) override {
      std::scoped_lock lock(session_.impl_->subscriptionMutex);
      const auto request = session_.impl_->optionChainRequests.value(requestId);
      if (request.symbol.isEmpty()) return;
      ActivityLog::ibkrResponse(
          "option_chain_parameters",
          {{"request_id", requestId},
           {"symbol", request.symbol},
           {"expiry_count", static_cast<int>(expirations.size())},
           {"strike_count", static_cast<int>(strikes.size())}});
      const QDate today = QDate::currentDate();
      const QDate targetExpiry =
          request.expiryDate.isValid() && request.expiryDate >= today
              ? request.expiryDate
              : today;
      QDate selectedExpiry;
      int closestDistance = std::numeric_limits<int>::max();
      for (const auto& value : expirations) {
        const QDate expiry = QDate::fromString(
            QString::fromStdString(value).left(8), "yyyyMMdd");
        if (!expiry.isValid() || expiry < today) continue;
        const int distance = qAbs(targetExpiry.daysTo(expiry));
        if (distance < closestDistance) {
          closestDistance = distance;
          selectedExpiry = expiry;
        }
      }
      const QList<double> targets =
          request.showBothSides
              ? QList<double>{request.marketPrice *
                                  (1.0 - request.expectedChange / 100.0),
                              request.marketPrice *
                                  (1.0 + request.expectedChange / 100.0)}
              : QList<double>{request.marketPrice *
                              (1.0 + (request.todayChange >= 0.0
                                          ? request.expectedChange
                                          : -request.expectedChange) /
                                         100.0)};
      for (const double strike : strikes) {
        for (const double target : targets) {
          if (target <= 0.0 || qAbs(strike - target) / target > 0.05) continue;
          if (!request.showBothSides &&
              ((request.todayChange >= 0.0 && strike <= request.marketPrice) ||
               (request.todayChange < 0.0 && strike >= request.marketPrice)))
            continue;
          if (selectedExpiry.isValid()) {
            const QString expiryValue = selectedExpiry.toString("yyyyMMdd");
            const QDate expiry = selectedExpiry;
            for (const QString& right : {QString("C"), QString("P")}) {
              const QString key = expiryValue + "|" +
                                  QString::number(strike, 'f', 4) + "|" + right;
              if (session_.impl_->optionCandidateKeys[requestId].contains(key))
                continue;
              session_.impl_->optionCandidateKeys[requestId].insert(key);
              session_.impl_->optionCandidates[requestId].append({
                  .underlying = request.symbol,
                  .expiry = expiry,
                  .strike = strike,
                  .right = right,
                  .expectedPrice = target,
              });
            }
          }
        }
      }
    }

    void securityDefinitionOptionalParameterEnd(int requestId) override {
      OptionChainRequest request;
      QList<OptionCandidate> candidates;
      {
        std::scoped_lock lock(session_.impl_->subscriptionMutex);
        request = session_.impl_->optionChainRequests.take(requestId);
        candidates = session_.impl_->optionCandidates.take(requestId);
        session_.impl_->optionCandidateKeys.remove(requestId);
      }
      if (request.symbol.isEmpty()) return;
      std::sort(candidates.begin(), candidates.end(),
                [](const OptionCandidate& left, const OptionCandidate& right) {
                  if (left.expiry != right.expiry)
                    return left.expiry < right.expiry;
                  if (!qFuzzyCompare(left.strike + 1.0, right.strike + 1.0))
                    return left.strike < right.strike;
                  return left.right < right.right;
                });
      constexpr int maximumQualifiedCandidates = 32;
      if (candidates.size() > maximumQualifiedCandidates) {
        candidates.erase(candidates.begin() + maximumQualifiedCandidates,
                         candidates.end());
      }
      QMetaObject::invokeMethod(
          &session_,
          [this, symbol = request.symbol, candidates] {
            session_.qualifyOptionCandidates(candidates);
            emit session_.watchlistOptionsReady(symbol, candidates);
          },
          Qt::QueuedConnection);
    }

    void tickOptionComputation(int requestId, TickType, int, double, double,
                               double, double, double, double, double,
                               double underlyingPrice) override {
      if (underlyingPrice <= 0.0) return;
      qint64 conId = 0;
      {
        std::scoped_lock lock(session_.impl_->subscriptionMutex);
        conId = session_.impl_->marketRequestContracts.value(requestId);
      }
      if (conId == 0) return;
      ActivityLog::ibkrResponse("option_underlying_price",
                                {{"request_id", requestId},
                                 {"con_id", conId},
                                 {"underlying_price", underlyingPrice}});
      QMetaObject::invokeMethod(
          &session_,
          [this, conId, underlyingPrice] {
            session_.portfolio_.updateOptionUnderlyingPrice(conId,
                                                            underlyingPrice);
          },
          Qt::QueuedConnection);
    }

    void pnlSingle(int requestId, Decimal, double, double unrealizedPnl, double,
                   double) override {
      qint64 conId = 0;
      {
        std::scoped_lock lock(session_.impl_->subscriptionMutex);
        conId = session_.impl_->pnlRequestContracts.value(requestId);
      }
      if (conId == 0) return;
      //   ActivityLog::ibkrResponse("position_pnl",
      //                             {{"request_id", requestId},
      //                              {"con_id", conId},
      //                              {"unrealized_pnl", unrealizedPnl}});
      QMetaObject::invokeMethod(
          &session_,
          [this, conId, unrealizedPnl] {
            session_.portfolio_.updateUnrealizedPnl(conId, unrealizedPnl);
          },
          Qt::QueuedConnection);
    }

    void execDetails(int, const Contract& contract,
                     const Execution& execution) override {
      const QString assetClass =
          QString::fromStdString(contract.secType) == "OPT" ? "Option"
                                                            : "Equity";
      const QString contractLabel =
          QString::fromStdString(contract.localSymbol).isEmpty()
              ? QString::fromStdString(contract.symbol)
              : QString::fromStdString(contract.localSymbol);
      const QString time = QString::fromStdString(execution.time);
      const QDateTime timestamp =
          QDateTime::fromString(time, "yyyyMMdd  HH:mm:ss");
      const QString side = QString::fromStdString(execution.side).toUpper();
      const QString action = side == "BOT" || side == "BUY"    ? "BUY"
                             : side == "SLD" || side == "SELL" ? "SELL"
                                                               : side;
      TradeRecord record{
          .executionId = QString::fromStdString(execution.execId),
          .conId = static_cast<qint64>(contract.conId),
          .timestamp =
              timestamp.isValid() ? timestamp : QDateTime::currentDateTime(),
          .assetClass = assetClass,
          .contract = contractLabel,
          .action = action,
          .quantity = execution.shares.isUnset()
                          ? 0.0
                          : DecimalFunctions::decimalToDouble(execution.shares),
          .executionPrice = execution.price,
          .realizedPnl = 0.0,
          .provisional = false,
      };
      ActivityLog::ibkrResponse("execution_details",
                                {{"execution_id", record.executionId},
                                 {"con_id", record.conId},
                                 {"asset_class", record.assetClass},
                                 {"contract", record.contract},
                                 {"action", record.action},
                                 {"quantity", record.quantity},
                                 {"price", record.executionPrice}});
      QMetaObject::invokeMethod(
          &session_,
          [this, record] {
            session_.portfolio_.recordExecution(record);
            emit session_.tradeRecordReceived(record);
            emit session_.tradeRecordsUpdated(
                record.assetClass,
                session_.portfolio_.tradeRecords(record.assetClass));
          },
          Qt::QueuedConnection);
    }

    void commissionAndFeesReport(
        const CommissionAndFeesReport& report) override {
      const QString executionId = QString::fromStdString(report.execId);
      const double commission = report.commissionAndFees;
      ActivityLog::ibkrResponse(
          "commission_report",
          {{"execution_id", executionId}, {"commission", commission}});
      QMetaObject::invokeMethod(
          &session_,
          [this, executionId, commission] {
            session_.portfolio_.recordCommission(executionId, commission);
            emit session_.tradeRecordsUpdated(
                "Equity", session_.portfolio_.tradeRecords("Equity"));
            emit session_.tradeRecordsUpdated(
                "Option", session_.portfolio_.tradeRecords("Option"));
          },
          Qt::QueuedConnection);
    }

    void orderStatus(int orderId, const std::string& status, Decimal filled,
                     Decimal remaining, double averageFillPrice,
                     long long permanentId, int parentId, double lastFillPrice,
                     int clientId, const std::string& whyHeld,
                     double marketCapPrice) override {
      const QString responseStatus = QString::fromStdString(status);
      ActivityLog::ibkrResponse(
          "order_status",
          {{"order_id", orderId},
           {"status", responseStatus},
           {"filled",
            filled.isUnset() ? 0.0 : DecimalFunctions::decimalToDouble(filled)},
           {"remaining", remaining.isUnset()
                             ? 0.0
                             : DecimalFunctions::decimalToDouble(remaining)},
           {"average_fill_price", averageFillPrice},
           {"permanent_id", permanentId},
           {"parent_id", parentId},
           {"last_fill_price", lastFillPrice},
           {"client_id", clientId},
           {"why_held", QString::fromStdString(whyHeld)},
           {"market_cap_price", marketCapPrice}});
      QMetaObject::invokeMethod(
          &session_,
          [this, orderId, status = responseStatus] {
            emit session_.stateChanged(
                session_.state_,
                QString("Order %1: %2").arg(orderId).arg(status));
          },
          Qt::QueuedConnection);
    }

    void openOrder(int orderId, const Contract&, const Order&,
                   const OrderState& orderState) override {
      qint64 conId = 0;
      OptionCandidate candidate;
      {
        std::scoped_lock lock(session_.impl_->subscriptionMutex);
        conId = session_.impl_->whatIfOrders.value(orderId);
        session_.impl_->whatIfOrders.remove(orderId);
        candidate = session_.impl_->optionCandidatesByConId.value(conId);
        if (candidate.conId != 0) {
          candidate.maintenanceMargin =
              QString::fromStdString(orderState.maintMarginChange);
          candidate.quoteStatus = "Margin preview ready";
          session_.impl_->optionCandidatesByConId.insert(conId, candidate);
        }
      }
      if (conId == 0) return;
      const QString initial =
          QString::fromStdString(orderState.initMarginChange);
      const QString maintenance =
          QString::fromStdString(orderState.maintMarginChange);
      const QString warnings = QString::fromStdString(orderState.warningText);
      const QString fingerprint =
          session_.impl_->whatIfFingerprints.take(orderId);
      ActivityLog::ibkrResponse("what_if_preview",
                                {{"order_id", orderId},
                                 {"con_id", conId},
                                 {"initial_margin_change", initial},
                                 {"maintenance_margin_change", maintenance},
                                 {"warnings", warnings}});
      if (fingerprint.startsWith("ticket|"))
        session_.impl_->approvedWhatIfFingerprints.insert(fingerprint);
      QMetaObject::invokeMethod(
          &session_,
          [this, conId, fingerprint, initial, maintenance, warnings,
           candidate] {
            emit session_.whatIfPreviewReceived(conId, fingerprint, initial,
                                                maintenance, warnings);
            if (candidate.conId != 0)
              emit session_.optionCandidateUpdated(candidate);
          },
          Qt::QueuedConnection);
    }

    void error(int id, long long, int errorCode, const std::string& errorString,
               const std::string&) override {
      QString rejectedSymbol;
      {
        std::scoped_lock lock(session_.impl_->subscriptionMutex);
        rejectedSymbol = session_.impl_->watchlistContractRequests.take(id);
      }
      const QString detail = QString("IBKR error %1 (%2): %3")
                                 .arg(errorCode)
                                 .arg(id)
                                 .arg(QString::fromStdString(errorString));
      ActivityLog::ibkrResponse(
          "error", {{"request_id", id},
                    {"code", errorCode},
                    {"message", QString::fromStdString(errorString)}});
      QMetaObject::invokeMethod(
          &session_,
          [this, detail] {
            emit session_.stateChanged(session_.state_, detail);
          },
          Qt::QueuedConnection);
      if (!rejectedSymbol.isEmpty()) {
        const QString reason = QString::fromStdString(errorString);
        QMetaObject::invokeMethod(
            &session_,
            [this, rejectedSymbol, reason] {
              emit session_.watchlistContractRejected(rejectedSymbol, reason);
            },
            Qt::QueuedConnection);
      }
    }

    void connectionClosed() override {
      ActivityLog::ibkrResponse("connection_closed");
      QMetaObject::invokeMethod(
          &session_,
          [this] {
            session_.state_ = SessionState::Disconnected;
            emit session_.stateChanged(
                SessionState::Disconnected,
                "TWS or IB Gateway closed the API connection.");
          },
          Qt::QueuedConnection);
    }

   private:
    IbkrSession& session_;
  };

  explicit Impl(IbkrSession& session)
      : callback(session), signal(200), client(&callback, &signal) {}

  CallbackBridge callback;
  EReaderOSSignal signal;
  EClientSocket client;
  std::unique_ptr<EReader> reader;
  std::jthread readerThread;
  struct MarketDataSubscription {
    std::shared_ptr<Contract> contract;
    QSet<QString> owners;
    int requestId = 0;
    double bid = 0.0;
    double ask = 0.0;
    double last = 0.0;
  };
  struct HistoricalBar {
    QDate date;
    double close = 0.0;
  };
  QHash<qint64, std::shared_ptr<Contract>> contracts;
  QHash<qint64, MarketDataSubscription> marketDataSubscriptions;
  QHash<int, qint64> marketRequestContracts;
  QHash<int, qint64> pnlRequestContracts;
  QHash<qint64, int> pnlRequestsByContract;
  QSet<qint64> positionContracts;
  QHash<int, QString> watchlistContractRequests;
  QHash<qint64, QString> watchlistSymbols;
  QHash<int, QString> indexContractRequests;
  QHash<qint64, QString> indexSymbolsByConId;
  QHash<qint64, double> indexOpens;
  QHash<qint64, double> indexPrices;
  QHash<int, qint64> historicalRequestContracts;
  QHash<int, QVector<HistoricalBar>> historicalCloses;
  QHash<qint64, int> historicalRequestsByContract;
  struct OptionChainRequest {
    QString symbol;
    double marketPrice = 0.0;
    double expectedChange = 0.0;
    double todayChange = 0.0;
    bool showBothSides = true;
    QDate expiryDate;
  };
  QHash<int, OptionChainRequest> optionChainRequests;
  QHash<int, QList<OptionCandidate>> optionCandidates;
  QHash<int, QSet<QString>> optionCandidateKeys;
  QHash<int, OptionCandidate> optionQualificationRequests;
  QHash<qint64, OptionCandidate> optionCandidatesByConId;
  QHash<QString, QSet<qint64>> optionCandidatesBySymbol;
  QString selectedAccount;
  int nextRequestId = 100000;
  int nextOrderId = 0;
  QHash<int, qint64> whatIfOrders;
  QHash<int, QString> whatIfFingerprints;
  QSet<QString> approvedWhatIfFingerprints;
  std::mutex subscriptionMutex;

  void retainMarketData(qint64 conId, const std::shared_ptr<Contract>& contract,
                        const QString& owner) {
    if (conId == 0 || !contract) return;
    contracts.insert(conId, contract);
    auto& subscription = marketDataSubscriptions[conId];
    subscription.contract = contract;
    subscription.owners.insert(owner);
    if (subscription.requestId != 0) return;
    subscription.requestId = nextRequestId++;
    marketRequestContracts.insert(subscription.requestId, conId);
    const std::string genericTicks = contract->secType == "OPT" ? "106" : "";
    client.reqMktData(subscription.requestId, *contract, genericTicks, false,
              false, nullptr);
  }

  void releaseMarketData(qint64 conId, const QString& owner) {
    auto it = marketDataSubscriptions.find(conId);
    if (it == marketDataSubscriptions.end()) return;
    it->owners.remove(owner);
    if (!it->owners.isEmpty()) return;
    if (it->requestId != 0) {
      client.cancelMktData(it->requestId);
      marketRequestContracts.remove(it->requestId);
    }
    marketDataSubscriptions.erase(it);
    contracts.remove(conId);
  }
#else
  explicit Impl(IbkrSession&) {}
#endif
};

IbkrSession::IbkrSession(QObject* parent)
    : QObject(parent), portfolio_(this), impl_(std::make_unique<Impl>(*this)) {
  connect(&portfolio_, &PortfolioStore::equitiesChanged, this,
          &IbkrSession::equityPositionsUpdated);
  connect(&portfolio_, &PortfolioStore::optionsChanged, this,
          &IbkrSession::optionPositionsUpdated);
}

IbkrSession::~IbkrSession() { disconnect(); }

SessionState IbkrSession::state() const { return state_; }

void IbkrSession::configureConnection(const ConnectionProfile& profile) {
  disconnect();
  state_ = SessionState::Connecting;
#ifdef IBKR_ENABLE_TWS_API
  if (!impl_->client.eConnect(profile.host.toUtf8().constData(), profile.port,
                              profile.clientId)) {
    state_ = SessionState::Error;
    emit stateChanged(
        state_, QString("Could not connect to %1:%2. Check that TWS or IB "
                        "Gateway is running and API connections are enabled.")
                    .arg(profile.host)
                    .arg(profile.port));
    return;
  }

  impl_->reader = std::make_unique<EReader>(&impl_->client, &impl_->signal);
  impl_->reader->start();
  impl_->readerThread = std::jthread([this](std::stop_token stopToken) {
    while (!stopToken.stop_requested() && impl_->client.isConnected()) {
      impl_->signal.waitForSignal();
      if (!stopToken.stop_requested()) {
        impl_->reader->processMsgs();
      }
    }
  });
  impl_->client.startApi();
  emit stateChanged(state_, QString("Connecting to %1:%2 with client ID %3.")
                                .arg(profile.host)
                                .arg(profile.port)
                                .arg(profile.clientId));
#else
  Q_UNUSED(profile)
  state_ = SessionState::ConfigurationPending;
  emit stateChanged(
      state_,
      QString("Endpoint configured for %1:%2 with client ID %3. The official "
              "TWS API transport is not enabled in this build.")
          .arg(profile.host)
          .arg(profile.port)
          .arg(profile.clientId));
#endif
}

void IbkrSession::selectAccount(const QString& account) {
#ifdef IBKR_ENABLE_TWS_API
  if (state_ != SessionState::Ready || account.isEmpty()) {
    return;
  }
  cancelPositionSubscriptions();
  impl_->selectedAccount = account;
  impl_->client.reqAccountUpdates(true, account.toStdString());
  impl_->client.reqMarketDataType(3);
  portfolio_.clear();
  emit accountSelected(account);
  emit stateChanged(state_,
                    "Subscribed to account updates for " + account + ".");
  requestMarketIndices();
#else
  Q_UNUSED(account)
#endif
}

void IbkrSession::requestMarketIndices() {
#ifdef IBKR_ENABLE_TWS_API
  if (state_ != SessionState::Ready) return;
  struct IndexContract {
    const char* symbol;
    const char* exchange;
    const char* label;
  };
  static constexpr IndexContract
      indices [] = {
/* {"SPX", "CBOE", "S&P 500"},
{"COMP", "NASDAQ", "NASDAQ"},
{"INDU", "NYSE", "DOW JONES"},
{"VIX", "CBOE", "VIX"}, */
};
  std::scoped_lock lock(impl_->subscriptionMutex);
  for (const auto& item : indices) {
    Contract contract;
    contract.symbol = item.symbol;
    contract.secType = "IND";
    contract.exchange = item.exchange;
    contract.currency = "USD";
    const int requestId = impl_->nextRequestId++;
    impl_->indexContractRequests.insert(requestId, item.label);
    impl_->client.reqContractDetails(requestId, contract);
  }
#endif
}

void IbkrSession::subscribeWatchlistSymbol(const QString& symbol) {
#ifdef IBKR_ENABLE_TWS_API
  if (state_ != SessionState::Ready || symbol.trimmed().isEmpty()) {
    emit stateChanged(state_,
                      "Connect before requesting watchlist market data.");
    return;
  }
  Contract contract;
  contract.symbol = symbol.trimmed().toUpper().toStdString();
  contract.secType = "STK";
  contract.exchange = "SMART";
  contract.currency = "USD";
  std::scoped_lock lock(impl_->subscriptionMutex);
  const int requestId = impl_->nextRequestId++;
  impl_->watchlistContractRequests.insert(
      requestId, QString::fromStdString(contract.symbol));
  impl_->client.reqContractDetails(requestId, contract);
#else
  Q_UNUSED(symbol)
#endif
}

void IbkrSession::unsubscribeWatchlistContract(qint64 conId) {
#ifdef IBKR_ENABLE_TWS_API
  std::scoped_lock lock(impl_->subscriptionMutex);
  if (!impl_->watchlistSymbols.remove(conId)) return;
  impl_->releaseMarketData(conId, "watchlist");
#else
  Q_UNUSED(conId)
#endif
}

void IbkrSession::requestWatchlistOptions(const WatchlistEntry& entry) {
#ifdef IBKR_ENABLE_TWS_API
  if (state_ != SessionState::Ready || entry.conId == 0 ||
      entry.marketPrice <= 0.0) {
    emit stateChanged(
        state_,
        "A validated watchlist quote is required before requesting options.");
    return;
  }
  std::scoped_lock lock(impl_->subscriptionMutex);
  const int requestId = impl_->nextRequestId++;
  impl_->optionChainRequests.insert(
      requestId, {
                     .symbol = entry.symbol,
                     .marketPrice = entry.marketPrice,
                     .expectedChange = entry.expectedChangePercent,
                     .todayChange = entry.todayChangePercent,
                     .showBothSides = entry.showBothSides,
                     .expiryDate = entry.expiryDate,
                 });
  impl_->client.reqSecDefOptParams(requestId, entry.symbol.toStdString(), "",
                                   "STK", static_cast<int>(entry.conId));
#else
  Q_UNUSED(entry)
#endif
}

void IbkrSession::qualifyOptionCandidates(QList<OptionCandidate> candidates) {
#ifdef IBKR_ENABLE_TWS_API
  if (state_ != SessionState::Ready) return;
  std::scoped_lock lock(impl_->subscriptionMutex);
  for (const auto& candidate : candidates) {
    Contract contract;
    contract.symbol = candidate.underlying.toStdString();
    contract.secType = "OPT";
    contract.exchange = "SMART";
    contract.currency = "USD";
    contract.lastTradeDateOrContractMonth =
        candidate.expiry.toString("yyyyMMdd").toStdString();
    contract.strike = candidate.strike;
    contract.right = candidate.right.toStdString();
    contract.multiplier = "100";
    const int requestId = impl_->nextRequestId++;
    impl_->optionQualificationRequests.insert(requestId, candidate);
    impl_->client.reqContractDetails(requestId, contract);
  }
#else
  Q_UNUSED(candidates)
#endif
}

void IbkrSession::subscribeOptionCandidate(qint64 conId) {
#ifdef IBKR_ENABLE_TWS_API
  std::scoped_lock lock(impl_->subscriptionMutex);
  const auto contract = impl_->contracts.value(conId);
  if (!contract) return;
  impl_->retainMarketData(conId, contract, "option-candidate");
#else
  Q_UNUSED(conId)
#endif
}

void IbkrSession::requestOptionCandidateMarginPreview(qint64 conId,
                                                      double quantity,
                                                      double price) {
#ifdef IBKR_ENABLE_TWS_API
  requestWhatIfPreview(conId, "BUY", quantity, "Limit", price,
                       QString("candidate|%1|%2|%3")
                           .arg(conId)
                           .arg(quantity, 0, 'f', 8)
                           .arg(price, 0, 'f', 8));
#else
  Q_UNUSED(conId)
  Q_UNUSED(quantity)
  Q_UNUSED(price)
#endif
}

void IbkrSession::unsubscribeWatchlistOptions(const QString& symbol) {
#ifdef IBKR_ENABLE_TWS_API
  std::scoped_lock lock(impl_->subscriptionMutex);
  const auto candidates = impl_->optionCandidatesBySymbol.take(symbol);
  for (const qint64 conId : candidates) {
    impl_->releaseMarketData(conId, "option-candidate");
    impl_->optionCandidatesByConId.remove(conId);
  }
#else
  Q_UNUSED(symbol)
#endif
}

void IbkrSession::subscribePosition(qint64 conId) {
#ifdef IBKR_ENABLE_TWS_API
  if (state_ != SessionState::Ready || impl_->selectedAccount.isEmpty() ||
      conId == 0) {
    return;
  }
  std::scoped_lock lock(impl_->subscriptionMutex);
  const auto contract = impl_->contracts.value(conId);
  if (!contract) {
    return;
  }
  impl_->retainMarketData(conId, contract, "position");
  if (!impl_->pnlRequestsByContract.contains(conId)) {
    const int requestId = impl_->nextRequestId++;
    impl_->pnlRequestsByContract.insert(conId, requestId);
    impl_->pnlRequestContracts.insert(requestId, conId);
    impl_->client.reqPnLSingle(requestId, impl_->selectedAccount.toStdString(),
                               "", static_cast<int>(conId));
  }
#else
  Q_UNUSED(conId)
#endif
}

void IbkrSession::unsubscribePosition(qint64 conId) {
#ifdef IBKR_ENABLE_TWS_API
  std::scoped_lock lock(impl_->subscriptionMutex);
  impl_->positionContracts.remove(conId);
  const int pnlRequestId = impl_->pnlRequestsByContract.take(conId);
  if (pnlRequestId != 0) {
    impl_->pnlRequestContracts.remove(pnlRequestId);
    impl_->client.cancelPnLSingle(pnlRequestId);
  }
  impl_->releaseMarketData(conId, "position");
#else
  Q_UNUSED(conId)
#endif
}

void IbkrSession::subscribeWatchlistContract(qint64 conId) {
#ifdef IBKR_ENABLE_TWS_API
  std::scoped_lock lock(impl_->subscriptionMutex);
  const auto contract = impl_->contracts.value(conId);
  if (!contract) return;
  impl_->retainMarketData(conId, contract, "watchlist");
  if (impl_->historicalRequestsByContract.contains(conId)) return;
  const int historicalRequestId = impl_->nextRequestId++;
  impl_->historicalRequestsByContract.insert(conId, historicalRequestId);
  impl_->historicalRequestContracts.insert(historicalRequestId, conId);
  impl_->client.reqHistoricalData(historicalRequestId, *contract, "", "10 D",
                                  "1 day", "TRADES", 1, 1, false, nullptr);
#else
  Q_UNUSED(conId)
#endif
}

void IbkrSession::cancelPositionSubscriptions() {
#ifdef IBKR_ENABLE_TWS_API
  if (!impl_) return;
  std::scoped_lock lock(impl_->subscriptionMutex);
  for (const auto& subscription : std::as_const(impl_->marketDataSubscriptions))
    if (subscription.requestId != 0)
      impl_->client.cancelMktData(subscription.requestId);
  for (const int requestId : impl_->pnlRequestContracts.keys()) {
    impl_->client.cancelPnLSingle(requestId);
  }
  impl_->contracts.clear();
  impl_->marketDataSubscriptions.clear();
  impl_->marketRequestContracts.clear();
  impl_->pnlRequestContracts.clear();
  impl_->pnlRequestsByContract.clear();
  impl_->positionContracts.clear();
  impl_->watchlistContractRequests.clear();
  impl_->watchlistSymbols.clear();
  impl_->historicalRequestContracts.clear();
  impl_->historicalRequestsByContract.clear();
  impl_->historicalCloses.clear();
  impl_->optionChainRequests.clear();
  impl_->optionCandidates.clear();
  impl_->optionCandidateKeys.clear();
  impl_->optionQualificationRequests.clear();
  impl_->optionCandidatesByConId.clear();
  impl_->optionCandidatesBySymbol.clear();
#endif
}

void IbkrSession::disconnect() {
#ifdef IBKR_ENABLE_TWS_API
  cancelPositionSubscriptions();
  if (impl_ && impl_->readerThread.joinable()) {
    impl_->readerThread.request_stop();
    impl_->client.eDisconnect();
    impl_->readerThread.join();
    impl_->reader.reset();
  } else if (impl_) {
    impl_->client.eDisconnect();
  }
#endif
  state_ = SessionState::Disconnected;
  emit stateChanged(state_, "No active TWS or IB Gateway session");
}

void IbkrSession::requestWhatIfPreview(qint64 conId, const QString& action,
                                       double quantity,
                                       const QString& orderType, double price,
                                       const QString& fingerprint) {
#ifdef IBKR_ENABLE_TWS_API
  if (state_ != SessionState::Ready || impl_->selectedAccount.isEmpty() ||
      quantity <= 0.0) {
    emit stateChanged(state_,
                      "Connect and select an account before requesting an IBKR "
                      "margin preview.");
    return;
  }
  std::scoped_lock lock(impl_->subscriptionMutex);
  const auto contract = impl_->contracts.value(conId);
  if (!contract || impl_->nextOrderId <= 0) {
    emit stateChanged(state_,
                      "The contract is not ready for an IBKR margin preview.");
    return;
  }
  Order order;
  order.account = impl_->selectedAccount.toStdString();
  order.action = action.toStdString();
  order.totalQuantity = DecimalFunctions::doubleToDecimal(quantity);
  order.whatIf = true;
  if (orderType == "Limit") {
    order.orderType = "LMT";
    order.lmtPrice = price;
  } else if (orderType == "Market") {
    order.orderType = "MKT";
  } else if (orderType == "Stop") {
    order.orderType = "STP";
    order.auxPrice = price;
  } else {
    order.orderType = "STP LMT";
    order.lmtPrice = price;
    order.auxPrice = price;
  }
  const int orderId = impl_->nextOrderId++;
  impl_->whatIfOrders.insert(orderId, conId);
  impl_->whatIfFingerprints.insert(orderId, fingerprint);
  impl_->client.placeOrder(orderId, *contract, order);
#else
  Q_UNUSED(conId)
  Q_UNUSED(action)
  Q_UNUSED(quantity)
  Q_UNUSED(orderType)
  Q_UNUSED(price)
  Q_UNUSED(fingerprint)
#endif
}

void IbkrSession::submitOrder(qint64 conId, const QString& action,
                              double quantity, const QString& orderType,
                              double price, const QString& fingerprint) {
#ifdef IBKR_ENABLE_TWS_API
  if (state_ != SessionState::Ready || impl_->selectedAccount.isEmpty() ||
      quantity <= 0.0) {
    emit stateChanged(
        state_, "Connect and select an account before submitting an order.");
    return;
  }
  if (!impl_->approvedWhatIfFingerprints.remove(fingerprint)) {
    emit stateChanged(state_,
                      "Submit blocked: this exact order draft has no valid "
                      "IBKR what-if approval.");
    return;
  }
  std::scoped_lock lock(impl_->subscriptionMutex);
  const auto contract = impl_->contracts.value(conId);
  if (!contract || impl_->nextOrderId <= 0) {
    emit stateChanged(state_,
                      "The contract is not ready for order submission.");
    return;
  }
  Order order;
  order.account = impl_->selectedAccount.toStdString();
  order.action = action.toStdString();
  order.totalQuantity = DecimalFunctions::doubleToDecimal(quantity);
  if (orderType == "Limit") {
    order.orderType = "LMT";
    order.lmtPrice = price;
  } else if (orderType == "Market") {
    order.orderType = "MKT";
  } else if (orderType == "Stop") {
    order.orderType = "STP";
    order.auxPrice = price;
  } else {
    order.orderType = "STP LMT";
    order.lmtPrice = price;
    order.auxPrice = price;
  }
  const int orderId = impl_->nextOrderId++;
  impl_->client.placeOrder(orderId, *contract, order);
  emit orderSubmitted(conId, orderId);
#else
  Q_UNUSED(conId)
  Q_UNUSED(action)
  Q_UNUSED(quantity)
  Q_UNUSED(orderType)
  Q_UNUSED(price)
  Q_UNUSED(fingerprint)
#endif
}

QList<WatchlistEntry> IbkrSession::savedWatchlist() const {
  return portfolio_.savedWatchlist();
}
void IbkrSession::saveWatchlistEntry(const WatchlistEntry& entry) {
  portfolio_.saveWatchlistEntry(entry);
}
void IbkrSession::removeWatchlistEntry(qint64 conId) {
  portfolio_.removeWatchlistEntry(conId);
}
void IbkrSession::saveEarningsDate(qint64 conId, const QDate& earningsDate) {
  portfolio_.setEarningsDate(conId, earningsDate);
}
void IbkrSession::saveStrategy(const QString& label,
                               const QList<OptionPosition>& legs) {
  portfolio_.saveStrategy(label, legs);
}
void IbkrSession::removeStrategy(const QString& identifier) {
  portfolio_.removeStrategy(identifier);
}
QList<TradeRecord> IbkrSession::tradeRecords(const QString& assetClass) const {
  return portfolio_.tradeRecords(assetClass);
}
double IbkrSession::realizedPnl(const QDateTime& start,
                                const QDateTime& end) const {
  return portfolio_.realizedPnl(start, end);
}