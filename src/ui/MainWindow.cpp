#include "MainWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableView>
#include <QTextEdit>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>

#include "domain/ActivityLog.h"
#include "ui/LoginDialog.h"
#include "ui/OrderTicketDialog.h"
#include "ui_MainWindow.h"

namespace {
class ChoiceDelegate final : public QStyledItemDelegate {
 public:
  explicit ChoiceDelegate(QStringList choices, QObject* parent = nullptr)
      : QStyledItemDelegate(parent), choices_(std::move(choices)) {}

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                        const QModelIndex&) const override {
    auto* editor = new QComboBox(parent);
    editor->addItems(choices_);
    return editor;
  }

  void setEditorData(QWidget* editor, const QModelIndex& index) const override {
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (combo) combo->setCurrentText(index.data(Qt::EditRole).toString());
  }

  void setModelData(QWidget* editor, QAbstractItemModel* model,
                    const QModelIndex& index) const override {
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (combo) model->setData(index, combo->currentText(), Qt::EditRole);
  }

 private:
  QStringList choices_;
};

class RateDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                        const QModelIndex&) const override {
    auto* editor = new QDoubleSpinBox(parent);
    editor->setRange(0.0, 100.0);
    editor->setDecimals(2);
    editor->setSuffix("%");
    return editor;
  }

  void setEditorData(QWidget* editor, const QModelIndex& index) const override {
    if (auto* spinBox = qobject_cast<QDoubleSpinBox*>(editor))
      spinBox->setValue(index.data(Qt::EditRole).toDouble());
  }

  void setModelData(QWidget* editor, QAbstractItemModel* model,
                    const QModelIndex& index) const override {
    if (auto* spinBox = qobject_cast<QDoubleSpinBox*>(editor))
      model->setData(index, spinBox->value(), Qt::EditRole);
  }
};

class EarningsDateDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                        const QModelIndex&) const override {
    auto* editor = new QDateEdit(parent);
    editor->setCalendarPopup(true);
    editor->setDisplayFormat("yyyy-MM-dd");
    return editor;
  }

  void setEditorData(QWidget* editor, const QModelIndex& index) const override {
    if (auto* dateEdit = qobject_cast<QDateEdit*>(editor)) {
      const QDate date = index.data(Qt::EditRole).toDate();
      dateEdit->setDate(date.isValid() ? date : QDate::currentDate());
    }
  }

  void setModelData(QWidget* editor, QAbstractItemModel* model,
                    const QModelIndex& index) const override {
    if (auto* dateEdit = qobject_cast<QDateEdit*>(editor))
      model->setData(index, dateEdit->date(), Qt::EditRole);
  }
};

class ExpiryDateDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                        const QModelIndex&) const override {
    auto* editor = new QDateEdit(parent);
    editor->setCalendarPopup(true);
    editor->setDisplayFormat("yyyy-MM-dd");
    editor->setDateRange(QDate::currentDate(), QDate(2099, 12, 31));
    return editor;
  }

  void setEditorData(QWidget* editor, const QModelIndex& index) const override {
    if (auto* dateEdit = qobject_cast<QDateEdit*>(editor)) {
      const QDate date = index.data(Qt::EditRole).toDate();
      dateEdit->setDate(date.isValid() && date >= QDate::currentDate()
                            ? date
                            : QDate::currentDate());
    }
  }

  void setModelData(QWidget* editor, QAbstractItemModel* model,
                    const QModelIndex& index) const override {
    if (auto* dateEdit = qobject_cast<QDateEdit*>(editor))
      model->setData(index, dateEdit->date(), Qt::EditRole);
  }
};

class RemoveButtonDelegate final : public QStyledItemDelegate {
 public:
  explicit RemoveButtonDelegate(std::function<void(QModelIndex)> onClicked,
                                QObject* parent = nullptr)
      : QStyledItemDelegate(parent), onClicked_(std::move(onClicked)) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    if (index.data(Qt::DisplayRole).toString().isEmpty()) return;
    QStyleOptionButton button;
    button.rect = option.rect.adjusted(4, 2, -4, -2);
    button.text = QStringLiteral("-");
    button.state = QStyle::State_Enabled;
    if (option.state & QStyle::State_MouseOver)
      button.state |= QStyle::State_MouseOver;
    QApplication::style()->drawControl(QStyle::CE_PushButton, &button, painter);
  }

  bool editorEvent(QEvent* event, QAbstractItemModel*,
                   const QStyleOptionViewItem&,
                   const QModelIndex& index) override {
    if (event->type() != QEvent::MouseButtonRelease) return false;
    const auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::LeftButton) return false;
    onClicked_(index);
    return true;
  }

 private:
  std::function<void(QModelIndex)> onClicked_;
};

bool selectDateInterval(QWidget* parent, const QString& title, QDate* start,
                        QDate* end) {
  QDialog dialog(parent);
  dialog.setWindowTitle(title);
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* from = new QDateEdit(QDate::currentDate().addMonths(-1), &dialog);
  auto* to = new QDateEdit(QDate::currentDate(), &dialog);
  from->setCalendarPopup(true);
  to->setCalendarPopup(true);
  form->addRow("From", from);
  form->addRow("Through", to);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted || from->date() > to->date())
    return false;
  *start = from->date();
  *end = to->date();
  return true;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow) {
  ui_->setupUi(this);

  setWindowTitle("IBKR Options Desk");
  resize(1380, 820);
  connectionStateLabel_ = new QLabel("Status: Disconnected", this);
  statusBar()->addPermanentWidget(connectionStateLabel_);
  auto* indexStrip = new QWidget(ui_->tabs);
  auto* indexLayout = new QHBoxLayout(indexStrip);
  indexLayout->setContentsMargins(8, 0, 4, 0);
  indexLayout->setSpacing(12);
  for (const QString& name :
       {QStringLiteral("S&P 500"), QStringLiteral("NASDAQ"),
        QStringLiteral("DOW JONES"), QStringLiteral("VIX")}) {
    auto* label = new QLabel(name + "  --", indexStrip);
    indexLabels_.insert(name, label);
    indexLayout->addWidget(label);
  }
  ui_->tabs->setCornerWidget(indexStrip, Qt::TopRightCorner);
  ui_->globalExpectedRateBox->setRange(0.0, 100.0);
  ui_->globalExpectedRateBox->setDecimals(2);
  ui_->globalExpiryDateEdit->setDateRange(QDate::currentDate(),
                                          QDate(2099, 12, 31));
  ui_->globalSidesBox->addItems({"Both sides", "Trend side"});
  QSettings settings;
  ui_->globalExpectedRateBox->setValue(
      settings.value("watchlist/globalExpectedRate", 5.0).toDouble());
  ui_->globalSidesBox->setCurrentText(
      settings.value("watchlist/globalSides", "Both sides").toString());
  const QDate defaultExpiry = QDate::currentDate().addDays(7);
  const QDate savedExpiry = QDate::fromString(
      settings.value("watchlist/globalExpiryDate").toString(), Qt::ISODate);
  ui_->globalExpiryDateEdit->setDate(savedExpiry.isValid() &&
                                             savedExpiry >= QDate::currentDate()
                                         ? savedExpiry
                                         : defaultExpiry);
  watchlist_.setGlobalRuleValues(
      ui_->globalExpectedRateBox->value(),
      ui_->globalSidesBox->currentText() == "Both sides",
      ui_->globalExpiryDateEdit->date());
  connect(ui_->globalExpectedRateBox,
          qOverload<double>(&QDoubleSpinBox::valueChanged), this,
          [this](double rate) {
            watchlist_.setGlobalRuleValues(
                rate, ui_->globalSidesBox->currentText() == "Both sides",
                ui_->globalExpiryDateEdit->date());
          });
  connect(ui_->globalSidesBox, &QComboBox::currentTextChanged, this,
          [this](const QString& sides) {
            watchlist_.setGlobalRuleValues(ui_->globalExpectedRateBox->value(),
                                           sides == "Both sides",
                                           ui_->globalExpiryDateEdit->date());
          });
  connect(ui_->globalExpiryDateEdit, &QDateEdit::dateChanged, this,
          [this](const QDate& expiryDate) {
            QSettings().setValue("watchlist/globalExpiryDate",
                                 expiryDate.toString(Qt::ISODate));
            watchlist_.setGlobalRuleValues(
                ui_->globalExpectedRateBox->value(),
                ui_->globalSidesBox->currentText() == "Both sides", expiryDate);
          });

  connect(ui_->connectAction, &QAction::triggered, this,
          &MainWindow::showConnectionDialog);
  connect(ui_->showTodayLogAction, &QAction::triggered, this,
          &MainWindow::showTodayLog);
  connect(&session_, &IbkrSession::stateChanged, this,
          &MainWindow::updateSessionState);
  connect(&session_, &IbkrSession::managedAccountsReceived, this,
          &MainWindow::selectAccount);
  connect(&session_, &IbkrSession::accountSelected, this,
          [this](const QString& account) {
            ui_->accountLabel->setText("Account: " + account);
            expandedWatchlistsAwaitingQuote_.clear();
            for (int row = 0; row < watchlist_.rowCount() - 1; ++row) {
              const auto entry = watchlist_.entryAt(row);
              session_.subscribeWatchlistSymbol(entry.symbol);
              if (ui_->watchlistView->isExpanded(watchlist_.index(row, 0)))
                expandedWatchlistsAwaitingQuote_.insert(entry.symbol);
            }
          });
  connect(&session_, &IbkrSession::accountValueUpdated, this,
          &MainWindow::updateAccountValue);
  connect(&session_, &IbkrSession::equityPositionsUpdated, &equityPositions_,
          &EquityPositionModel::replacePositions);
  connect(&session_, &IbkrSession::optionPositionsUpdated, this,
          [this](const QList<OptionStrategy>& strategies) {
            QStringList expandedIdentifiers;
            for (int row = 0; row < optionPositions_.rowCount(); ++row) {
              const QModelIndex parent = optionPositions_.index(row, 0);
              if (ui_->optionView->isExpanded(parent))
                expandedIdentifiers.append(
                    optionPositions_.strategyAt(parent).identifier);
            }
            optionPositions_.replaceStrategies(strategies);
            for (int row = 0; row < optionPositions_.rowCount(); ++row) {
              const QModelIndex parent = optionPositions_.index(row, 0);
              if (expandedIdentifiers.contains(
                      optionPositions_.strategyAt(parent).identifier))
                ui_->optionView->setExpanded(parent, true);
            }
          });
  connect(&session_, &IbkrSession::watchlistContractQualified, &watchlist_,
          &WatchlistTreeModel::addQualifiedSymbol);
  connect(&session_, &IbkrSession::watchlistContractRejected, this,
          [this](const QString& symbol, const QString& reason) {
            statusBar()->showMessage("IBKR rejected " + symbol + ": " + reason);
          });
  connect(&session_, &IbkrSession::watchlistQuoteUpdated, this,
          [this](qint64 conId, double price) {
            watchlist_.updateQuote(conId, price);
            for (int row = 0; row < watchlist_.rowCount() - 1; ++row) {
              const auto entry = watchlist_.entryAt(row);
              if (entry.conId != conId ||
                  !expandedWatchlistsAwaitingQuote_.remove(entry.symbol))
                continue;
              session_.requestWatchlistOptions(entry);
              break;
            }
          });
    connect(&session_, &IbkrSession::watchlistHistoryUpdated, &watchlist_,
      &WatchlistTreeModel::updateHistory);
  connect(&session_, &IbkrSession::optionCandidateUpdated, &watchlist_,
          &WatchlistTreeModel::updateCandidate);
  connect(&session_, &IbkrSession::tradeRecordReceived, this,
          [this](const TradeRecord& record) {
            (record.assetClass == "Option" ? optionTrades_ : equityTrades_)
                .appendRecord(record);
          });
  connect(&session_, &IbkrSession::tradeRecordsUpdated, this,
          [this](const QString& assetClass, const QList<TradeRecord>& records) {
            (assetClass == "Option" ? optionTrades_ : equityTrades_)
                .replaceRecords(records);
          });
  connect(&equityPositions_, &EquityPositionModel::earningsDateChanged,
          &session_, &IbkrSession::saveEarningsDate);
  connect(&watchlist_, &WatchlistTreeModel::entryChanged, &session_,
          &IbkrSession::saveWatchlistEntry);
  connect(&watchlist_, &WatchlistTreeModel::symbolEntryRequested, this,
          [this](const QString& symbol) {
            ActivityLog::userAction("add_watchlist_symbol",
                                    {{"symbol", symbol}});
            session_.subscribeWatchlistSymbol(symbol);
            statusBar()->showMessage("Validating " + symbol +
                                     " with IBKR before adding it.");
          });
  connect(&watchlist_, &WatchlistTreeModel::optionsRefreshRequested, &session_,
          &IbkrSession::requestWatchlistOptions);
  connect(&watchlist_, &WatchlistTreeModel::optionsRefreshRequested, this,
          [](const WatchlistEntry& entry) {
            ActivityLog::userAction(
                "change_watchlist_rule",
                {{"symbol", entry.symbol},
                 {"expected_rate", entry.expectedChangePercent},
                 {"both_sides", entry.showBothSides},
                 {"follows_global", entry.followsGlobalRules}});
          });
  connect(&watchlist_, &WatchlistTreeModel::candidateQuantityChanged, this,
          [this](const OptionCandidate& candidate) {
            const double price =
                candidate.ask > 0.0 ? candidate.ask : candidate.bid;
            if (candidate.conId > 0 && price > 0.0)
              session_.requestOptionCandidateMarginPreview(
                  candidate.conId, candidate.expectedQuantity, price);
          });
  watchlist_.replaceEntries(session_.savedWatchlist());
  const auto globallyUpdated = watchlist_.applyGlobalRules(
      ui_->globalExpectedRateBox->value(),
      ui_->globalSidesBox->currentText() == "Both sides",
      ui_->globalExpiryDateEdit->date());
  for (const auto& entry : globallyUpdated) session_.saveWatchlistEntry(entry);
  equityTrades_.replaceRecords(session_.tradeRecords("Equity"));
  optionTrades_.replaceRecords(session_.tradeRecords("Option"));
  connect(
      &session_, &IbkrSession::watchlistOptionsReady, this,
      [this](const QString& symbol, const QList<OptionCandidate>& candidates) {
        watchlist_.replaceOptions(symbol, candidates);
        for (int row = 0; row < watchlist_.rowCount(); ++row) {
          const auto parent = watchlist_.index(row, 0);
          if (!watchlist_.isAddRow(parent) &&
              watchlist_.entryAt(row).symbol == symbol) {
            ui_->watchlistView->expand(parent);
            break;
          }
        }
        if (candidates.isEmpty())
          statusBar()->showMessage("No near-strike options returned for " +
                                   symbol + ".");
      });

  ui_->equityView->setModel(&equityPositions_);
  ui_->equityView->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui_->equityView->setSelectionMode(QAbstractItemView::SingleSelection);
  ui_->equityView->setAlternatingRowColors(true);
  ui_->equityView->setSortingEnabled(true);
  ui_->equityView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui_->equityView->horizontalHeader()->setStretchLastSection(true);
  ui_->equityView->setItemDelegateForColumn(
      EquityPositionModel::EarningsDate,
      new EarningsDateDelegate(ui_->equityView));
  connect(ui_->equityView, &QTableView::clicked, this,
          [this](const QModelIndex& index) {
            if (index.column() == EquityPositionModel::EarningsDate &&
                index.row() < equityPositions_.rowCount() - 1)
              ui_->equityView->edit(index);
          });
  connect(ui_->equityView, &QTableView::doubleClicked, this,
          &MainWindow::showEquityCloseTicket);

  ui_->optionView->setModel(&optionPositions_);
  ui_->optionView->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui_->optionView->setSelectionMode(QAbstractItemView::SingleSelection);
  ui_->optionView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  ui_->optionView->setAlternatingRowColors(true);
  ui_->optionView->setUniformRowHeights(true);
  ui_->optionView->header()->setStretchLastSection(true);
  ui_->optionView->viewport()->installEventFilter(this);
  ui_->optionView->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui_->optionView, &QTreeView::customContextMenuRequested, this,
          &MainWindow::showOptionContextMenu);
  connect(ui_->optionView, &QTreeView::doubleClicked, this,
          &MainWindow::showOptionCloseTicket);

  ui_->watchlistView->setModel(&watchlist_);
  ui_->watchlistView->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui_->watchlistView->setSelectionMode(QAbstractItemView::SingleSelection);
  ui_->watchlistView->setAlternatingRowColors(true);
  ui_->watchlistView->setUniformRowHeights(true);
  ui_->watchlistView->setRootIsDecorated(true);
  ui_->watchlistView->header()->setStretchLastSection(true);
  ui_->watchlistView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui_->watchlistView->setMouseTracking(true);
  ui_->watchlistView->viewport()->setMouseTracking(true);
  ui_->watchlistView->viewport()->installEventFilter(this);
  ui_->watchlistView->setItemDelegateForColumn(
      WatchlistTreeModel::Sides,
      new ChoiceDelegate({"Both sides", "Trend side"}, ui_->watchlistView));
  ui_->watchlistView->setItemDelegateForColumn(
      WatchlistTreeModel::Expiry, new ExpiryDateDelegate(ui_->watchlistView));
  ui_->watchlistView->setItemDelegateForColumn(
      WatchlistTreeModel::Rule,
      new ChoiceDelegate({"Global", "Customized"}, ui_->watchlistView));
  ui_->watchlistView->setItemDelegateForColumn(
      WatchlistTreeModel::ExpectedChange, new RateDelegate(ui_->watchlistView));
  ui_->watchlistView->setItemDelegateForColumn(
      WatchlistTreeModel::Quantity,
      new QStyledItemDelegate(ui_->watchlistView));
  auto* removeButtonDelegate = new RemoveButtonDelegate(
      [this](const QModelIndex& index) { handleWatchlistClick(index); },
      ui_->watchlistView);
  ui_->watchlistView->setItemDelegateForColumn(WatchlistTreeModel::Remove,
                                               removeButtonDelegate);
  ui_->watchlistView->setColumnWidth(WatchlistTreeModel::Remove, 48);
  ui_->watchlistView->setColumnWidth(WatchlistTreeModel::Contract, 230);
  connect(ui_->watchlistView, &QTreeView::clicked, this,
          [this](const QModelIndex& index) {
            if (watchlist_.flags(index).testFlag(Qt::ItemIsEditable))
              ui_->watchlistView->edit(index);
          });
  connect(ui_->watchlistView, &QTreeView::doubleClicked, this,
          &MainWindow::handleWatchlistDoubleClick);
  connect(ui_->watchlistView, &QTreeView::expanded, this,
          [this](const QModelIndex& index) {
            if (!watchlist_.hasOptions(index))
              session_.requestWatchlistOptions(watchlist_.entryAt(index.row()));
          });
  ui_->equityTradesView->setModel(&equityTrades_);
  ui_->optionTradesView->setModel(&optionTrades_);
  for (QTableView* view : {ui_->equityTradesView, ui_->optionTradesView}) {
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setAlternatingRowColors(true);
    view->setSortingEnabled(true);
    view->horizontalHeader()->setStretchLastSection(true);
    view->viewport()->installEventFilter(this);
  }
  ui_->equityView->viewport()->installEventFilter(this);
  ui_->optionView->viewport()->installEventFilter(this);
  connect(ui_->refreshOptionsButton, &QPushButton::clicked, this,
          &MainWindow::refreshWatchlistOptions);
  connect(ui_->optionStatisticsButton, &QPushButton::clicked, this,
          &MainWindow::showOptionExpiryStatistics);
  connect(ui_->tradeStatisticsButton, &QPushButton::clicked, this,
          &MainWindow::showRealizedStatistics);
  statusBar()->showMessage("No active TWS or IB Gateway session");
  setDisconnectedState();

  setStyleSheet(R"(
        QMainWindow { background: #f6f7f8; }
        QFrame#accountHeader { background: #ffffff; border: 1px solid #d9dde3; border-radius: 6px; }
        QLabel#pageHeading { font-size: 18px; font-weight: 600; color: #17202a; }
        QLabel#portfolioNotice { color: #475467; padding: 4px 0; }
        QTabBar::tab { padding: 9px 18px; }
    )");
}

MainWindow::~MainWindow() { delete ui_; }

void MainWindow::showConnectionDialog() {
  LoginDialog dialog(this);
  if (dialog.exec() == QDialog::Accepted) {
    const auto profile = dialog.profile();
    ActivityLog::userAction("connect",
                            {{"target", static_cast<int>(profile.target)},
                             {"host", profile.host},
                             {"port", profile.port},
                             {"client_id", profile.clientId}});
    session_.configureConnection(profile);
  }
}

void MainWindow::updateSessionState(SessionState state, const QString& detail) {
  if (state == SessionState::Disconnected) {
    setDisconnectedState();
  } else if (state == SessionState::ConfigurationPending ||
             state == SessionState::Connecting) {
    connectionStateLabel_->setText("Status: Connecting");
    ui_->accountLabel->setText("Account: Awaiting TWS response");
    ui_->dataStateLabel->setText("Data: Not subscribed");
  } else if (state == SessionState::Ready) {
    connectionStateLabel_->setText("Status: Connected");
    ui_->dataStateLabel->setText("Data: Account subscription pending");
  }
  statusBar()->showMessage(detail);
}

void MainWindow::setDisconnectedState() {
  connectionStateLabel_->setText("Status: Disconnected");
  ui_->accountLabel->setText("Account: --");
  ui_->dataStateLabel->setText("Data: Unavailable");
  ui_->netLiquidationLabel->setText("Net liquidation\n--");
  ui_->buyingPowerLabel->setText("Buying power\n--");
  ui_->excessLiquidityLabel->setText("Excess liquidity\n--");
  ui_->smaLabel->setText("SMA\n--");
  for (auto it = indexLabels_.begin(); it != indexLabels_.end(); ++it)
    it.value()->setText(it.key() + "  --");
}

void MainWindow::showEquityCloseTicket(const QModelIndex& index) {
  if (!index.isValid() || index.row() >= equityPositions_.rowCount() - 1 ||
      index.column() == EquityPositionModel::EarningsDate) {
    return;
  }
  const auto position = equityPositions_.positionAt(index.row());
  ActivityLog::userAction("open_equity_close_ticket",
                          {{"symbol", position.symbol},
                           {"con_id", position.conId},
                           {"quantity", position.quantity}});
  OrderTicketDialog ticket(session_, position.conId, position.symbol,
                           position.quantity < 0.0 ? "BUY" : "SELL",
                           qAbs(position.quantity), 0.0, 0.0, this);
  ticket.exec();
}

void MainWindow::showOptionCloseTicket(const QModelIndex& index) {
  if (!optionPositions_.isLeg(index)) {
    return;
  }
  const auto position = optionPositions_.legAt(index);
  ActivityLog::userAction("open_option_close_ticket",
                          {{"contract", position.localSymbol},
                           {"con_id", position.conId},
                           {"quantity", position.quantity}});
  OrderTicketDialog ticket(session_, position.conId, position.localSymbol,
                           position.quantity < 0.0 ? "BUY" : "SELL",
                           qAbs(position.quantity), 0.0, 0.0, this);
  ticket.exec();
}

void MainWindow::handleWatchlistClick(const QModelIndex& index) {
  if (!index.isValid()) return;
  if (watchlist_.isAddRow(index)) return;
  if (!watchlist_.isOption(index) &&
      index.column() == WatchlistTreeModel::Remove) {
    const auto entry = watchlist_.removeEntryAt(index.row());
    if (entry.conId == 0) return;
    ActivityLog::userAction(
        "remove_watchlist_symbol",
        {{"symbol", entry.symbol}, {"con_id", entry.conId}});
    session_.unsubscribeWatchlistContract(entry.conId);
    session_.removeWatchlistEntry(entry.conId);
  }
}

void MainWindow::handleWatchlistDoubleClick(const QModelIndex& index) {
  if (!index.isValid()) return;
  if (watchlist_.isOption(index)) {
    showOptionOpenTicket(index);
  }
}

void MainWindow::refreshWatchlistOptions() {
  const double expectedRate = ui_->globalExpectedRateBox->value();
  const bool bothSides = ui_->globalSidesBox->currentText() == "Both sides";
  const QDate expiryDate = ui_->globalExpiryDateEdit->date();
  QSettings settings;
  settings.setValue("watchlist/globalExpectedRate", expectedRate);
  settings.setValue("watchlist/globalSides",
                    ui_->globalSidesBox->currentText());
  settings.setValue("watchlist/globalExpiryDate",
                    expiryDate.toString(Qt::ISODate));
  ActivityLog::userAction("refresh_watchlist_options",
                          {{"global_expected_rate", expectedRate},
                           {"global_both_sides", bothSides}});
  const auto updated =
      watchlist_.applyGlobalRules(expectedRate, bothSides, expiryDate);
  for (const auto& entry : updated) session_.saveWatchlistEntry(entry);
  for (int row = 0; row < watchlist_.rowCount() - 1; ++row) {
    const auto entry = watchlist_.entryAt(row);
    if (entry.conId == 0) continue;
    ActivityLog::userAction("request_watchlist_options",
                            {{"symbol", entry.symbol},
                             {"expected_rate", entry.expectedChangePercent},
                             {"both_sides", entry.showBothSides}});
    session_.requestWatchlistOptions(entry);
  }
  statusBar()->showMessage(
      "Refreshing qualifying option contracts for the watchlist.");
}

void MainWindow::showOptionOpenTicket(const QModelIndex& index) {
  const auto candidate = watchlist_.candidateAt(index);
  if (candidate.conId <= 0) {
    statusBar()->showMessage(
        "Waiting for IBKR to qualify this option contract.");
    return;
  }
  const bool sellAtBid = index.column() == WatchlistTreeModel::Bid;
  const QString action = sellAtBid ? "SELL" : "BUY";
  const double price = sellAtBid ? candidate.bid : candidate.ask;
  if (price <= 0.0) {
    statusBar()->showMessage("No valid " + QString(sellAtBid ? "bid" : "ask") +
                             " quote is available.");
    return;
  }
  const QString contract =
      QString("%1 %2 %3 %4")
          .arg(candidate.underlying, candidate.expiry.toString("yyyy-MM-dd"),
               QString::number(candidate.strike, 'f', 2),
               candidate.right == "C" ? "Call" : "Put");
  ActivityLog::userAction("open_option_open_ticket",
                          {{"contract", contract},
                           {"con_id", candidate.conId},
                           {"action", action},
                           {"quantity", candidate.expectedQuantity},
                           {"price", price}});
  OrderTicketDialog ticket(session_, candidate.conId, contract, action,
                           1000000.0, candidate.expectedQuantity, price, this);
  ticket.exec();
}

void MainWindow::selectAccount(const QStringList& accounts) {
  if (accounts.isEmpty()) {
    statusBar()->showMessage("IBKR returned no managed accounts.");
    return;
  }
  bool accepted = false;
  const QString account =
      accounts.size() == 1
          ? accounts.first()
          : QInputDialog::getItem(this, "Select IBKR account", "Account",
                                  accounts, 0, false, &accepted);
  if (accounts.size() == 1 || accepted) {
    ActivityLog::userAction("select_account", {{"account", account}});
    session_.selectAccount(account);
  }
}

void MainWindow::updateAccountValue(const QString& key, const QString& value,
                                    const QString& currency) {
  const QString amount = currency.isEmpty() ? value : value + " " + currency;
  if (key == "NetLiquidation") {
    ui_->netLiquidationLabel->setText("Net liquidation\n" + amount);
  } else if (key == "BuyingPower") {
    ui_->buyingPowerLabel->setText("Buying power\n" + amount);
  } else if (key == "ExcessLiquidity") {
    ui_->excessLiquidityLabel->setText("Excess liquidity\n" + amount);
  } else if (key == "SMA") {
    ui_->smaLabel->setText("SMA\n" + amount);
  }
}

void MainWindow::createStrategy() {
  QList<OptionPosition> legs;
  for (const auto& index : ui_->optionView->selectionModel()->selectedRows()) {
    if (optionPositions_.isLeg(index))
      legs.append(optionPositions_.legAt(index));
  }
  if (legs.size() < 2) {
    statusBar()->showMessage(
        "Select at least two option-leg rows to create a local strategy.");
    return;
  }
  const QString underlying = legs.first().underlying;
  if (std::any_of(legs.cbegin(), legs.cend(),
                  [&underlying](const OptionPosition& leg) {
                    return leg.underlying != underlying;
                  })) {
    QMessageBox::information(this, "Group option legs",
                             "Selected option legs must share one underlying.");
    return;
  }
  const QString base = underlying + " strategy";
  QSet<QString> existing;
  for (int row = 0; row < optionPositions_.rowCount(); ++row)
    existing.insert(
        optionPositions_.strategyAt(optionPositions_.index(row, 0)).label);
  QString label = base;
  int suffix = 2;
  while (existing.contains(label))
    label = base + " " + QString::number(suffix++);
  ActivityLog::userAction("group_option_legs", {{"label", label},
                                                {"underlying", underlying},
                                                {"leg_count", legs.size()}});
  session_.saveStrategy(label, legs);
}

void MainWindow::removeStrategy() {
  const auto strategy =
      optionPositions_.strategyAt(ui_->optionView->currentIndex());
  if (strategy.identifier.isEmpty() || strategy.identifier.toLongLong() != 0) {
    statusBar()->showMessage(
        "Select a locally created strategy parent to remove its grouping "
        "metadata.");
    return;
  }
  if (QMessageBox::question(this, "Remove strategy",
                            "Remove only the local grouping metadata? IBKR "
                            "positions are unchanged.") == QMessageBox::Yes) {
    ActivityLog::userAction(
        "ungroup_option_legs",
        {{"strategy", strategy.label}, {"identifier", strategy.identifier}});
    session_.removeStrategy(strategy.identifier);
  }
}

void MainWindow::showRealizedStatistics() {
  QDate start;
  QDate end;
  if (!selectDateInterval(this, "Realized P&L interval", &start, &end)) return;
  ActivityLog::userAction("show_realized_statistics",
                          {{"from", start.toString(Qt::ISODate)},
                           {"through", end.toString(Qt::ISODate)}});
  const double total = session_.realizedPnl(
      QDateTime(start, QTime(0, 0)), QDateTime(end.addDays(1), QTime(0, 0)));
  QMessageBox::information(
      this, "Realized P&L",
      QString("Captured FIFO realized P&L: %1\nOnly executions observed by "
              "this application are included.")
          .arg(QLocale().toCurrencyString(total, "$", 2)));
}

void MainWindow::showOptionExpiryStatistics() {
  QDate start;
  QDate end;
  if (!selectDateInterval(this, "Open option expiry P&L", &start, &end)) return;
  ActivityLog::userAction("show_option_expiry_statistics",
                          {{"from", start.toString(Qt::ISODate)},
                           {"through", end.toString(Qt::ISODate)}});
  double total = 0.0;
  const QString rows =
      optionPositions_.unrealizedPnlByExpiry(start, end, &total);
  QMessageBox::information(
      this, "Open option unrealized P&L",
      rows + "\n\nTotal: " + QLocale().toCurrencyString(total, "$", 2));
}

void MainWindow::showOptionContextMenu(const QPoint& position) {
  const QModelIndex clicked = ui_->optionView->indexAt(position);
  if (!clicked.isValid()) return;
  if (!ui_->optionView->selectionModel()->isSelected(clicked)) {
    ui_->optionView->selectionModel()->clearSelection();
    ui_->optionView->selectionModel()->select(
        clicked, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  }
  QMenu menu(this);
  if (!optionPositions_.isLeg(clicked)) {
    const auto strategy = optionPositions_.strategyAt(clicked);
    if (strategy.identifier.toLongLong() == 0) {
      ui_->optionView->setCurrentIndex(clicked);
      QAction* ungroup = menu.addAction("Ungroup all legs");
      if (menu.exec(ui_->optionView->viewport()->mapToGlobal(position)) ==
          ungroup)
        removeStrategy();
    }
    return;
  }
  int selectedLegs = 0;
  for (const QModelIndex& row :
       ui_->optionView->selectionModel()->selectedRows(0))
    selectedLegs += optionPositions_.isLeg(row) ? 1 : 0;
  if (selectedLegs < 2) return;
  QAction* group = menu.addAction("Group selected legs");
  if (menu.exec(ui_->optionView->viewport()->mapToGlobal(position)) == group)
    createStrategy();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress) {
    const auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::LeftButton) {
      const QList<QAbstractItemView*> views = {
          ui_->equityView, ui_->optionView, ui_->watchlistView,
          ui_->equityTradesView, ui_->optionTradesView};
      for (auto* view : views) {
        if (watched == view->viewport() &&
            !view->indexAt(mouse->pos()).isValid()) {
          view->clearSelection();
          break;
        }
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::showTodayLog() {
  const QString path = ActivityLog::todayLogPath();
  QFile file(path);
  QString contents;
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    contents = QString::fromUtf8(file.readAll());
  } else {
    contents = "No log entries have been recorded today.";
  }

  QDialog dialog(this);
  dialog.setWindowTitle("Today's activity log");
  dialog.resize(900, 600);
  auto* layout = new QVBoxLayout(&dialog);
  auto* pathLabel = new QLabel(path, &dialog);
  pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto* logView = new QTextEdit(&dialog);
  logView->setReadOnly(true);
  logView->setLineWrapMode(QTextEdit::NoWrap);
  logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  logView->setPlainText(contents);
  layout->addWidget(pathLabel);
  layout->addWidget(logView, 1);
  dialog.exec();
}