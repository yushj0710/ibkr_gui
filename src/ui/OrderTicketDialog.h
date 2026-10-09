#pragma once

#include <QDialog>

class IbkrSession;

namespace Ui {
class OrderTicketDialog;
}

class OrderTicketDialog final : public QDialog {
  Q_OBJECT

 public:
  OrderTicketDialog(IbkrSession& session, qint64 conId, QString contract,
                    QString action, double maximumQuantity,
                    double initialQuantity = 0.0, double initialPrice = 0.0,
                    QWidget* parent = nullptr);
  ~OrderTicketDialog() override;

 private slots:
  void validateTicket();
  void requestPreview();
  void showPreview(qint64 conId, const QString& fingerprint,
                   const QString& initialMarginChange,
                   const QString& maintenanceMarginChange,
                   const QString& warnings);

 private:
  [[nodiscard]] QString draftFingerprint() const;
  QString action_;
  double maximumQuantity_;
  IbkrSession& session_;
  qint64 conId_;
  Ui::OrderTicketDialog* ui_;
  QString previewFingerprint_;
};