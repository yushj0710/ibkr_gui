#include "LoginDialog.h"

#include "ui_LoginDialog.h"

namespace {
struct TargetDefault {
  ConnectionTarget target;
  const char* label;
  quint16 port;
};

constexpr TargetDefault kTargets[] = {
    {ConnectionTarget::TwsPaper, "TWS Paper", 7497},
    {ConnectionTarget::TwsLive, "TWS Live", 7496},
    {ConnectionTarget::GatewayPaper, "IB Gateway Paper", 4002},
    {ConnectionTarget::GatewayLive, "IB Gateway Live", 4001},
    {ConnectionTarget::Custom, "Custom endpoint", 7497},
};
}  // namespace

LoginDialog::LoginDialog(QWidget* parent)
    : QDialog(parent), ui_(new Ui::LoginDialog) {
  ui_->setupUi(this);
  setWindowTitle("Welcome to IBKR Options GUI");

  setModal(true);
  setMinimumWidth(420);

  ui_->portBox->setRange(1, 65535);
  ui_->clientIdBox->setRange(0, 999999);
  ui_->clientIdBox->setValue(101);

  for (const auto& target : kTargets) {
    ui_->targetBox->addItem(target.label);
  }

  connect(ui_->targetBox, &QComboBox::currentIndexChanged, this,
          &LoginDialog::applyTargetDefaults);
  connect(ui_->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(ui_->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  applyTargetDefaults(ui_->targetBox->currentIndex());
}

LoginDialog::~LoginDialog() { delete ui_; }

ConnectionProfile LoginDialog::profile() const {
  return {
      .target = kTargets[ui_->targetBox->currentIndex()].target,
      .host = ui_->hostEdit->text().trimmed(),
      .port = static_cast<quint16>(ui_->portBox->value()),
      .clientId = ui_->clientIdBox->value(),
  };
}

void LoginDialog::applyTargetDefaults(int index) {
  const auto& selected = kTargets[index];
  const bool custom = selected.target == ConnectionTarget::Custom;
  ui_->portBox->setValue(selected.port);
  ui_->portBox->setReadOnly(!custom);
}