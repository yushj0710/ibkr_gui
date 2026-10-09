#pragma once

#include <QDialog>

#include "domain/ConnectionProfile.h"

namespace Ui {
class LoginDialog;
}

class LoginDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit LoginDialog(QWidget* parent = nullptr);
  ~LoginDialog() override;

  [[nodiscard]] ConnectionProfile profile() const;

 private slots:
  void applyTargetDefaults(int index);

 private:
  Ui::LoginDialog* ui_;
};