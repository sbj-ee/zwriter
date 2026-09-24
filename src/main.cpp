#include "MainWindow.hpp"
#include "version.hpp"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QTimer>

namespace {

// Icon embedded in the binary (src/icons.qrc) so it is present wherever the
// app runs from: build tree, .deb install, or .dmg.
QIcon appIcon()
{
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256, 512}) {
        icon.addFile(QStringLiteral(":/icons/zwriter-%1.png").arg(size), QSize(size, size));
    }
    return icon;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("zwriter"));
    QApplication::setOrganizationName(QStringLiteral("sbj-ee"));
    QApplication::setApplicationVersion(QString::fromUtf8(zwriter::kVersionString));
    QApplication::setApplicationDisplayName(QStringLiteral("zwriter"));
    // Blink the caret at a steady 1 s cycle regardless of the desktop setting.
    QApplication::setCursorFlashTime(1000);
    // Must match zwriter.desktop: on Wayland/GNOME this is the app id the shell
    // uses to pick the dock/taskbar icon and hover name.
    QGuiApplication::setDesktopFileName(QStringLiteral("zwriter"));
    QApplication::setWindowIcon(appIcon());

    QString captureDir;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--capture-screenshots") && i + 1 < args.size()) {
            captureDir = args.at(i + 1);
            break;
        }
    }

    MainWindow window;
    if (!captureDir.isEmpty()) {
        window.resize(960, 700); // fixed size for reproducible screenshots
    }
    window.show();

    if (!captureDir.isEmpty()) {
        QDir().mkpath(captureDir);
        QTimer::singleShot(200, &window, [&window, captureDir]() {
            window.captureDemoScreenshots(captureDir);
        });
    }

    return app.exec();
}
