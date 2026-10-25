#include "app/AppController.h"
#include "settings/Settings.h"
#include "ui/HelpDialog.h"
#include "ui/MainWindow.h"

#include <QApplication>

#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NetAnalyzer"));
    QApplication::setOrganizationDomain(QStringLiteral("netanalyzer.local"));
    QApplication::setApplicationName(QStringLiteral("NetAnomalyAnalyzer"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    // Иконка приложения: из ресурсов (окно, трей, Alt-Tab).
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/appicon/app.ico")));

    // Настройки должны существовать до создания контроллера.
    Settings::instance().reload();

    AppController controller;
    MainWindow *window = MainWindow::create(&controller);
    window->show();

    // Витрина: показать встроенное окно справки (для снимков/проверок).
    if (app.arguments().contains(QStringLiteral("--show-help")))
        HelpDialog::openSmart(window);

    return app.exec();
}
