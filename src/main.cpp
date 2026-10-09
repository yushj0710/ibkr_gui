#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);
  application.setApplicationName("IBKR Options Desk");
  application.setOrganizationName("IBKR Options Desk");

  MainWindow window;
  window.show();

  return application.exec();
}