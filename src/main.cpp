#include "MainWindow.hpp"
#include "version.hpp"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("zwriter"));
    QApplication::setOrganizationName(QStringLiteral("sbj-ee"));
    QApplication::setApplicationVersion(QString::fromUtf8(zwriter::kVersionString));

    MainWindow window;
    window.show();

    return app.exec();
}
