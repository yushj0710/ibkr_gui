#pragma once

#include <QJsonObject>
#include <QString>

class ActivityLog final {
 public:
  static void userAction(const QString& event, const QJsonObject& details = {});
  static void ibkrResponse(const QString& event,
                           const QJsonObject& details = {});
  [[nodiscard]] static QString todayLogPath();

 private:
  static void append(const QString& type, const QString& event,
                     const QJsonObject& details);
};