#include "ActivityLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

namespace {
QMutex& logMutex() {
  static QMutex mutex;
  return mutex;
}
}  // namespace

void ActivityLog::userAction(const QString& event, const QJsonObject& details) {
  append("user_action", event, details);
}

void ActivityLog::ibkrResponse(const QString& event,
                               const QJsonObject& details) {
  append("ibkr_response", event, details);
}

QString ActivityLog::todayLogPath() {
  const QString directory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
      "/logs";
  return directory + "/events-" +
         QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".jsonl";
}

void ActivityLog::append(const QString& type, const QString& event,
                         const QJsonObject& details) {
  QMutexLocker lock(&logMutex());
  const QString directory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
      "/logs";
  if (!QDir().mkpath(directory)) return;

  QFile file(todayLogPath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;

  QJsonObject record = details;
  record.insert("timestamp",
                QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
  record.insert("type", type);
  record.insert("event", event);
  file.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
  file.write("\n");
}