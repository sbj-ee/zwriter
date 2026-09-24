#include "MainWindow.hpp"
#include "version.hpp"

#include <QApplication>
#include <QDir>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("zwriter"));
    QApplication::setOrganizationName(QStringLiteral("sbj-ee"));
    QApplication::setApplicationVersion(QString::fromUtf8(zwriter::kVersionString));

    QString captureDir;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--capture-screenshots") && i + 1 < args.size()) {
            captureDir = args.at(i + 1);
            break;
        }
    }

    MainWindow window;
    window.resize(960, 700);
    window.show();

    if (!captureDir.isEmpty()) {
        QDir().mkpath(captureDir);
        QTimer::singleShot(200, &window, [&window, captureDir]() {
            window.captureDemoScreenshots(captureDir);
        });
    }

    return app.exec();
}
