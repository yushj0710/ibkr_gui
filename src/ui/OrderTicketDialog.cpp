#include "OrderTicketDialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>

#include "domain/ActivityLog.h"
#include "ibkr/IbkrSession.h"
#include "ui_OrderTicketDialog.h"

OrderTicketDialog::OrderTicketDialog(IbkrSession& session, qint64 conId,
                                     QString contract, QString action,
                                     double maximumQuantity,
                                     double initialQuantity,
                                     double initialPrice, QWidget* parent)
    : QDialog(parent),
      action_(std::move(action)),
      maximumQuantity_(maximumQuantity),
      session_(session),
      conId_(conId),
      ui_(new Ui::OrderTicketDialog) {
  ui_->setupUi(this);
  setWindowTitle("Order ticket: " + contract);
  ui_->summaryLabel->setText(
      action_ + " " + contract +
      "\nNo order is sent until a connected IBKR session returns a valid "
      "what-if preview and you explicitly submit.");

  ui_->quantityBox->setRange(0.0, maximumQuantity_);
  ui_->quantityBox->setDecimals(0);
  ui_->quantityBox->setValue(initialQuantity > 0.0
                                 ? qMin(initialQuantity, maximumQuantity_)
                                 : maximumQuantity_);
  ui_->orderTypeBox->addItems({"Limit", "Market", "Stop", "Stop limit"});
  ui_->priceBox->setRange(0.0, 1000000.0);
  ui_->priceBox->setDecimals(2);
  ui_->priceBox->setPrefix("$");
  ui_->priceBox->setValue(initialPrice);

  connect(ui_->quantityBox, &QDoubleSpinBox::valueChanged, this,
          &OrderTicketDialog::validateTicket);
  connect(ui_->orderTypeBox, &QComboBox::currentIndexChanged, this,
          &OrderTicketDialog::validateTicket);
  connect(ui_->previewButton, &QPushButton::clicked, this,
          &OrderTicketDialog::requestPreview);
  connect(&session_, &IbkrSession::whatIfPreviewReceived, this,
          &OrderTicketDialog::showPreview);
  connect(ui_->submitButton, &QPushButton::clicked, this, [this] {
    if (QMessageBox::question(this, "Submit order",
                              "Submit this reviewed order to IBKR?") ==
        QMessageBox::Yes) {
      ActivityLog::userAction("submit_order",
                              {{"con_id", conId_},
                               {"action", action_},
                               {"quantity", ui_->quantityBox->value()},
                               {"order_type", ui_->orderTypeBox->currentText()},
                               {"price", ui_->priceBox->value()}});
      session_.submitOrder(conId_, action_, ui_->quantityBox->value(),
                           ui_->orderTypeBox->currentText(),
                           ui_->priceBox->value(), draftFingerprint());
      ui_->validationLabel->setText(
          "Order submitted to IBKR. Follow the order status in TWS until the "
          "ledger view is available.");
      ui_->submitButton->setEnabled(false);
    }
  });
  connect(ui_->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  validateTicket();
}

OrderTicketDialog::~OrderTicketDialog() { delete ui_; }

void OrderTicketDialog::validateTicket() {
  const bool priceRequired = ui_->orderTypeBox->currentText() != "Market";
  const bool validQuantity = ui_->quantityBox->value() > 0.0 &&
                             ui_->quantityBox->value() <= maximumQuantity_;
  const bool validPrice = !priceRequired || ui_->priceBox->value() > 0.0;
  const bool previewMatchesDraft = !previewFingerprint_.isEmpty() &&
                                   previewFingerprint_ == draftFingerprint();
  ui_->submitButton->setEnabled(validQuantity && validPrice &&
                                previewMatchesDraft);
  ui_->previewButton->setEnabled(validQuantity && validPrice);
  ui_->validationLabel->setText(
      validQuantity && validPrice
          ? (previewMatchesDraft
                 ? "Preview is valid for the current order draft."
                 : "Ready to request an IBKR what-if preview.")
      : priceRequired ? "Enter a positive price before requesting a preview."
                      : "Enter a positive quantity.");
}

void OrderTicketDialog::requestPreview() {
  previewFingerprint_.clear();
  ui_->previewButton->setEnabled(false);
  ui_->submitButton->setEnabled(false);
  ui_->validationLabel->setText(
      "Requesting IBKR what-if margin preview. No order is being transmitted.");
  ActivityLog::userAction("request_order_preview",
                          {{"con_id", conId_},
                           {"action", action_},
                           {"quantity", ui_->quantityBox->value()},
                           {"order_type", ui_->orderTypeBox->currentText()},
                           {"price", ui_->priceBox->value()}});
  session_.requestWhatIfPreview(conId_, action_, ui_->quantityBox->value(),
                                ui_->orderTypeBox->currentText(),
                                ui_->priceBox->value(), draftFingerprint());
}

void OrderTicketDialog::showPreview(qint64 conId, const QString& fingerprint,
                                    const QString& initialMarginChange,
                                    const QString& maintenanceMarginChange,
                                    const QString& warnings) {
  if (conId != conId_ || fingerprint != draftFingerprint()) {
    return;
  }
  previewFingerprint_ = fingerprint;
  ui_->previewButton->setEnabled(true);
  ui_->submitButton->setEnabled(true);
  QString result = "IBKR what-if preview: initial margin change " +
                   initialMarginChange + ", maintenance margin change " +
                   maintenanceMarginChange + ".";
  if (!warnings.isEmpty()) {
    result += " Warning: " + warnings;
  }
  ui_->validationLabel->setText(result);
}

QString OrderTicketDialog::draftFingerprint() const {
  return QString("ticket|%1|%2|%3|%4|%5")
      .arg(conId_)
      .arg(action_)
      .arg(QLocale::c().toString(ui_->quantityBox->value(), 'f', 8))
      .arg(ui_->orderTypeBox->currentText())
      .arg(QLocale::c().toString(ui_->priceBox->value(), 'f', 8));
}