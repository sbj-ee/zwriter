#include "MainWindow.hpp"
#include "version.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QProcess>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <cstring>

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

// Files the OS asks a running app to open (macOS Finder / `open -a`).
class FileOpenFilter : public QObject
{
public:
    explicit FileOpenFilter(MainWindow *window) : QObject(window), m_window(window) {}

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const auto *open = static_cast<QFileOpenEvent *>(event);
            m_window->openExternalFile(open->file());
            return true;
        }
        return false;
    }

private:
    MainWindow *m_window;
};

} // namespace

int main(int argc, char *argv[])
{
    // Answer --version / --help before touching Qt's GUI: no window, no display needed.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            std::printf("zwriter %s\n", zwriter::kVersionString);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("zwriter %s — distraction-free writing\n\n"
                        "Usage: zwriter [FILE...]\n\n"
                        "  FILE...              open ODT, TXT or RTF documents (first in this window,\n"
                        "                       each further one in its own window)\n"
                        "  -v, --version        print the version and exit\n"
                        "  -h, --help           print this help and exit\n"
                        "  --capture-screenshots DIR\n"
                        "                       write the documentation screenshots to DIR and exit\n",
                        zwriter::kVersionString);
            return 0;
        }
    }

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

    // Command line: `zwriter [--capture-screenshots DIR] [FILE...]`. The desktop
    // entry runs `zwriter %F`, so this is how a file manager hands us documents.
    QString captureDir;
    QStringList files;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("--capture-screenshots") && i + 1 < args.size()) {
            captureDir = args.at(++i);
        } else if (arg.startsWith(QLatin1String("file:"))) {
            files << QUrl(arg).toLocalFile();
        } else if (!arg.startsWith(QLatin1Char('-'))) {
            files << arg;
        }
    }

    MainWindow window;
    if (!captureDir.isEmpty()) {
        window.resize(960, 700); // fixed size for reproducible screenshots
    }
    window.show();
    app.installEventFilter(new FileOpenFilter(&window));

    if (captureDir.isEmpty() && !files.isEmpty()) {
        // First file opens in this window (after the event loop starts, so any
        // error box has a visible parent); each further file gets its own window.
        QTimer::singleShot(0, &window, [&window, files]() {
            window.openExternalFile(files.first());
            for (int i = 1; i < files.size(); ++i) {
                QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                        {QFileInfo(files.at(i)).absoluteFilePath()});
            }
        });
    }

    if (!captureDir.isEmpty()) {
        QDir().mkpath(captureDir);
        QTimer::singleShot(200, &window, [&window, captureDir]() {
            window.captureDemoScreenshots(captureDir);
        });
    }

    return app.exec();
}
