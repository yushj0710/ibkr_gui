#pragma once

#include <QMainWindow>
#include <QSet>

#include "ibkr/IbkrSession.h"
#include "ui/models/EquityPositionModel.h"
#include "ui/models/OptionPositionModel.h"
#include "ui/models/TradeRecordModel.h"
#include "ui/models/WatchlistTreeModel.h"

class QLabel;
class QPoint;

namespace Ui {
class MainWindow;
}

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

 private slots:
  void showConnectionDialog();
  void showTodayLog();
  void updateSessionState(SessionState state, const QString& detail);
  void selectAccount(const QStringList& accounts);
  void updateAccountValue(const QString& key, const QString& value,
                          const QString& currency);
  void showEquityCloseTicket(const QModelIndex& index);
  void showOptionCloseTicket(const QModelIndex& index);
  void handleWatchlistClick(const QModelIndex& index);
  void handleWatchlistDoubleClick(const QModelIndex& index);
  void refreshWatchlistOptions();
  void showOptionOpenTicket(const QModelIndex& index);
  void createStrategy();
  void removeStrategy();
  void showRealizedStatistics();
  void showOptionExpiryStatistics();
  void showOptionContextMenu(const QPoint& position);

 private:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void setDisconnectedState();

  IbkrSession session_;
  EquityPositionModel equityPositions_{this};
  OptionPositionModel optionPositions_{this};
  WatchlistTreeModel watchlist_{this};
  TradeRecordModel equityTrades_{this};
  TradeRecordModel optionTrades_{this};
  Ui::MainWindow* ui_;
  QLabel* connectionStateLabel_ = nullptr;
  QHash<QString, QLabel*> indexLabels_;
  QSet<QString> expandedWatchlistsAwaitingQuote_;
};