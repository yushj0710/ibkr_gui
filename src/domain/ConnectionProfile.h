#pragma once

#include <QString>
#include <QtGlobal>

enum class ConnectionTarget {
  TwsPaper,
  TwsLive,
  GatewayPaper,
  GatewayLive,
  Custom,
};

struct ConnectionProfile {
  ConnectionTarget target = ConnectionTarget::TwsPaper;
  QString host = "127.0.0.1";
  quint16 port = 7497;
  int clientId = 101;
};