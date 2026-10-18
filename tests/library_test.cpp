// Library sidebar tests (ctest, headless):
//   H  no folder set: the pane shows a hint and a "Choose Folder…" button, the
//      tree is hidden, and nothing pops a dialog by itself
//   D  the folder chooser is a Qt dialog parented to the MAIN WINDOW (not the
//      child sidebar), window-modal, shown; a second request reuses it; a pick
//      sets the root and emits rootPathChosen; cancelling leaves no dialog
//   B  a big library (thousands of folders and files, symlink loops back to
//      the root) neither stalls the event loop nor keeps the CPU busy after it
//      is shown and one level is expanded
//   V  the "zwriter <ver> (<commit>)" label
// Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "LibrarySidebar.hpp"
#include "version.hpp"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTreeView>

#include <cstdio>
#include <ctime>
#include <functional>

namespace {

int g_failures = 0;

void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.isEmpty() ? "" : "  -- ",
                qPrintable(detail));
    if (!ok) {
        ++g_failures;
    }
}

bool spinUntil(const std::function<bool()> &cond, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

// Block in the event loop for ms (a real wait: no busy polling, so CPU use
// measured around it is the application's own).
void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

double processCpuSeconds()
{
    timespec ts{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) / 1e9;
}

void touch(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write("x\n");
    }
}

void testHintAndDialog(const QString &libRoot)
{
    QMainWindow win;
    auto *split = new QSplitter(&win);
    auto *side = new LibrarySidebar(split);
    split->addWidget(side);
    win.setCentralWidget(split);
    win.resize(800, 500);
    win.show();
    side->show();
    QCoreApplication::processEvents();

    // H
    QPushButton *btn = side->findChild<QPushButton *>(QStringLiteral("libraryChooseFolder"));
    check(side->rootPath().isEmpty() && !side->view()->isVisibleTo(side),
          "H no folder: the tree is hidden");
    check(btn && btn->isVisibleTo(side) && btn == side->chooseButton()
              && btn->text().startsWith(QLatin1String("Choose Folder")),
          "H no folder: a Choose Folder button is shown");
    check(side->folderDialog() == nullptr && QApplication::activeModalWidget() == nullptr,
          "H no folder: no dialog opens by itself");

    // D: the button opens the chooser
    QStringList chosen;
    QObject::connect(side, &LibrarySidebar::rootPathChosen, [&](const QString &p) { chosen << p; });
    btn->click();
    QFileDialog *dlg = side->folderDialog();
    check(dlg != nullptr, "D clicking Choose Folder opens the chooser");
    if (!dlg) {
        return;
    }
    check(dlg->parentWidget() == &win,
          "D the chooser's parent is the main window, not the sidebar",
          dlg->parentWidget() ? QString::fromLatin1(dlg->parentWidget()->metaObject()->className())
                              : QStringLiteral("none"));
    check(dlg->windowModality() == Qt::WindowModal && dlg->isVisible(), "D the chooser is window-modal and shown");
    check(dlg->testOption(QFileDialog::DontUseNativeDialog) && dlg->fileMode() == QFileDialog::Directory
              && dlg->testOption(QFileDialog::ShowDirsOnly),
          "D Qt (non-native) folder-only dialog, so it has a real transient parent");
    side->chooseRootFolder();
    check(side->folderDialog() == dlg, "D asking again reuses the open chooser");

    // pick -> root set, signal emitted
    emit dlg->fileSelected(libRoot);
    check(side->rootPath() == QDir::cleanPath(libRoot) && chosen == QStringList{side->rootPath()},
          "D picking a folder makes it the root and emits rootPathChosen");
    check(side->view()->isVisibleTo(side) && !btn->isVisibleTo(side),
          "D after a pick the tree replaces the hint");
    dlg->close();
    spinUntil([&] { return side->folderDialog() == nullptr; }, 2000);
    check(side->folderDialog() == nullptr, "D the chooser is gone once closed");

    // cancel leaves nothing behind and the root unchanged
    side->chooseRootFolder();
    QFileDialog *d2 = side->folderDialog();
    if (d2) {
        d2->reject();
    }
    spinUntil([&] { return side->folderDialog() == nullptr; }, 2000);
    check(side->folderDialog() == nullptr && side->rootPath() == QDir::cleanPath(libRoot) && chosen.size() == 1,
          "D cancelling keeps the root and leaves no dialog");
}

void testBigLibrary(const QString &root)
{
    // 60 folders x 40 sub-folders x 8 files, symlink loops back to the root in every top folder.
    for (int a = 0; a < 60; ++a) {
        const QString top = QStringLiteral("%1/top%2").arg(root).arg(a);
        for (int b = 0; b < 40; ++b) {
            const QString sub = QStringLiteral("%1/sub%2").arg(top).arg(b);
            QDir().mkpath(sub);
            for (int f = 0; f < 8; ++f) {
                touch(QStringLiteral("%1/f%2.%3").arg(sub).arg(f).arg(f % 2 ? "md" : "bin"));
            }
        }
        QFile::link(root, top + QStringLiteral("/loop"));
    }
    LibrarySidebar side;
    side.resize(260, 600);
    side.show();

    QElapsedTimer t;
    t.start();
    side.setRootPath(root);
    const qint64 setMs = t.elapsed();
    check(setMs < 1000, "B setRootPath on a big library returns promptly", QStringLiteral("%1 ms").arg(setMs));

    // Event-loop responsiveness: a 10 ms heartbeat must never be late by much
    // while the model lists the root and one expanded level.
    const QModelIndex first = side.model()->index(root + QStringLiteral("/top0"));
    side.view()->expand(first);
    qint64 worst = 0;
    QElapsedTimer gap;
    QTimer beat;
    beat.setInterval(10);
    QObject::connect(&beat, &QTimer::timeout, [&] { worst = qMax(worst, gap.restart()); });
    gap.start();
    beat.start();
    wait(2000);
    beat.stop();
    check(worst < 500, "B the event loop is never stalled for long", QStringLiteral("worst gap %1 ms").arg(worst));
    // Once listed, idle means idle: no rescan loop, no busy watcher.
    wait(300);
    const double idle0 = processCpuSeconds();
    wait(1500);
    const double idleCpu = processCpuSeconds() - idle0;
    check(idleCpu < 0.25, "B idle CPU is near zero once the library is listed",
          QStringLiteral("%1 s CPU in 1.5 s").arg(idleCpu, 0, 'f', 2));
}

void testVersionLabel()
{
    const std::string label = zwriter::versionLabel();
    check(label.rfind(zwriter::kVersionString, 0) == 0, "V label starts with the version",
          QString::fromStdString(label));
    const bool known = std::string(zwriter::kGitCommit) != "unknown";
    check(!known || label == std::string(zwriter::kVersionString) + " (" + zwriter::kGitCommit + ")",
          "V label is '<ver> (<commit>)' when the commit is known");
    check(known || label == zwriter::kVersionString, "V label is just the version when the commit is unknown");
    check(std::string(zwriter::kGitCommit).find(' ') == std::string::npos && zwriter::kGitCommit[0] != '\0',
          "V the commit is one token (short hash or 'unknown')");
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    QTemporaryDir libraryDir;
    QTemporaryDir bigDir;
    if (!settingsDir.isValid() || !libraryDir.isValid() || !bigDir.isValid()) {
        std::printf("FAIL  temp dir\n");
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("sbj-ee-test"));
    QApplication::setApplicationName(QStringLiteral("zwriter-library-test"));

    touch(libraryDir.path() + QStringLiteral("/a.md"));
    testHintAndDialog(libraryDir.path());
    testBigLibrary(bigDir.path());
    testVersionLabel();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
