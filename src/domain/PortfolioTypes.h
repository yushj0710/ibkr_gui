#pragma once

#include <QDate>
#include <QString>

struct EquityPosition {
  qint64 conId = 0;
  QString symbol;
  double lastPrice = 0.0;
  double prevClose = 0.0;
  double todayChange = 0.0;
  double averageCost = 0.0;
  double quantity = 0.0;
  double unrealizedPnl = 0.0;
  QDate earningsDate;
};

struct OptionPosition {
  qint64 conId = 0;
  QString underlying;
  QString localSymbol;
  QString right;
  double lastPrice = 0.0;
  double averageCost = 0.0;
  double quantity = 0.0;
  double unrealizedPnl = 0.0;
  double strike = 0.0;
  QDate expiry;
  double underlyingPrice = 0.0;
  double maintenanceMargin = 0.0;
};

struct OptionStrategy {
  QString identifier;
  QString label;
  bool verified = false;
  QList<OptionPosition> legs;
};

struct WatchlistEntry {
  qint64 conId = 0;
  QString symbol;
  double marketPrice = 0.0;
  double todayChangePercent = 0.0;
  double fiveDayChangePercent = 0.0;
  double expectedChangePercent = 5.0;
  bool showBothSides = true;
  QDate expiryDate;
  bool followsGlobalRules = true;
};

struct OptionCandidate {
  qint64 conId = 0;
  QString underlying;
  QDate expiry;
  double strike = 0.0;
  QString right;
  double expectedPrice = 0.0;
  double bid = 0.0;
  double ask = 0.0;
  double expectedQuantity = 1.0;
  QString maintenanceMargin;
  QString quoteStatus = "Qualification pending";
};

struct TradeRecord {
  QString executionId;
  qint64 conId = 0;
  QDateTime timestamp;
  QString assetClass;
  QString contract;
  QString action;
  double quantity = 0.0;
  double executionPrice = 0.0;
  double realizedPnl = 0.0;
  bool provisional = false;
};