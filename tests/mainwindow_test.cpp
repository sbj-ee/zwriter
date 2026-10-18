// Whole-window tests (ctest, offscreen): the real MainWindow.
//   C  open a document of every supported type WITHOUT editing it: the window
//      is clean (no '*' in the title) after the open settles, and closing it
//      does not ask "Save changes?"
//   E  a real edit makes it dirty and closing asks; Cancel keeps the window
//   S  after Save the window is clean again and closes without asking
//   T  the title carries "zwriter <version> (<commit>)"
// Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "DocumentIo.hpp"
#include "MainWindow.hpp"
#include "version.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>

#include <cstdio>
#include <memory>

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

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Counts "Save changes?" boxes; answers each with Cancel (the escape button).
struct PromptWatcher {
    int seen = 0;
    QTimer timer;
    PromptWatcher()
    {
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, [this] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++seen;
                box->reject();
            }
        });
        timer.start();
    }
};

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

QTextEdit *editorOf(MainWindow &win)
{
    // The document editor: an editable QTextEdit (the preview is a read-only
    // QTextBrowser, itself a QTextEdit).
    for (QTextEdit *e : win.findChildren<QTextEdit *>()) {
        if (!e->isReadOnly() && !qobject_cast<QTextBrowser *>(e)) {
            return e;
        }
    }
    return nullptr;
}

bool isDirty(MainWindow &win)
{
    return win.windowTitle().startsWith(QLatin1Char('*'));
}

void makeFixtures(const QString &dir, QStringList *paths)
{
    // ODT with a heading, a list and a table, written by zwriter's own saver.
    {
        QTextDocument doc;
        QTextCursor c(&doc);
        QTextBlockFormat h;
        h.setHeadingLevel(1);
        c.insertBlock(h);
        c.insertText(QStringLiteral("A heading"));
        c.insertBlock(QTextBlockFormat());
        c.insertText(QStringLiteral("Some body text with a mispeled wrod and more words to check."));
        c.insertList(QTextListFormat::ListDisc);
        c.insertText(QStringLiteral("item one"));
        c.insertTable(2, 2);
        const QString odt = dir + QStringLiteral("/doc.odt");
        QString err;
        if (DocumentIo::save(&doc, odt, DocumentIo::Format::Odt, &err)) {
            *paths << odt;
        }
        const QString rtf = dir + QStringLiteral("/doc.rtf");
        if (DocumentIo::save(&doc, rtf, DocumentIo::Format::Rtf, &err)) {
            *paths << rtf;
        }
    }
    const QString txt = dir + QStringLiteral("/doc.txt");
    writeFile(txt, "Plain text\nwith a mispeled wrod.\n\nSecond paragraph.\n");
    *paths << txt;
    const QString md = dir + QStringLiteral("/doc.md");
    writeFile(md, "# Title\n\nSome *emphasis*, **bold**, `code` and a [link](http://x.y).\n\n- a\n- b\n\n```\ncode\n```\n");
    *paths << md;
    const QString crlf = dir + QStringLiteral("/crlf.md");
    writeFile(crlf, "# CRLF\r\n\r\ntext\r\n");
    *paths << crlf;
    const QString conf = dir + QStringLiteral("/app.conf");
    writeFile(conf, "key=value\n");
    *paths << conf;
}

// View changes after an open are layout, not edits: none of them may leave the
// window "modified".
void testViewChangesStayClean(const QString &path, PromptWatcher &prompts)
{
    const QString name = QFileInfo(path).fileName();
    auto win = std::make_unique<MainWindow>();
    win->show();
    win->openExternalFile(path);
    wait(600);
    QTextEdit *ed = editorOf(*win);
    const char *viewSlots[] = {"toggleChrome",         "setThemeDark",       "setThemeInverse",
                           "setThemePaper",        "toggleFullPageView", "toggleFitPageWidth",
                           "toggleTypewriterScroll", "toggleFocusMode",  "toggleSmartQuotes",
                           "toggleSpellCheck",     "toggleSpellCheck",   "togglePageGuides",
                           "toggleMarkdownPreview", "toggleLibrary",     "toggleLibrary",
                           "toggleFullPageView"};
    win->resize(1100, 760);
    wait(150);
    for (const char *slot : viewSlots) {
        QMetaObject::invokeMethod(win.get(), slot);
        wait(80);
        if (isDirty(*win) || (ed && ed->document()->isModified())) {
            check(false, qPrintable(QStringLiteral("V %1: stays clean after %2").arg(name, QLatin1String(slot))),
                  QStringLiteral("%1 docModified=%2").arg(win->windowTitle()).arg(ed && ed->document()->isModified()));
            return;
        }
    }
    win->resize(700, 500);
    wait(200);
    win->showMaximized();
    wait(300);
    prompts.seen = 0;
    const bool closed = win->close();
    check(!isDirty(*win) && closed && prompts.seen == 0,
          qPrintable(QStringLiteral("V %1: resize, theme, view toggles and Library leave it clean").arg(name)),
          QStringLiteral("closed=%1 prompts=%2 title=%3").arg(closed).arg(prompts.seen).arg(win->windowTitle()));
}

void testOpenClean(const QStringList &paths, PromptWatcher &prompts)
{
    for (const QString &path : paths) {
        const QString name = QFileInfo(path).fileName();
        auto win = std::make_unique<MainWindow>();
        win->show();
        wait(50);
        prompts.seen = 0;
        win->openExternalFile(path);
        check(!isDirty(*win), qPrintable(QStringLiteral("C %1: clean right after open").arg(name)),
              win->windowTitle());
        // Timers, deferred highlighting, spell check, status updates: all settle.
        wait(1200);
        QTextEdit *ed = editorOf(*win);
        check(!isDirty(*win), qPrintable(QStringLiteral("C %1: still clean after the open settles").arg(name)),
              win->windowTitle());
        check(ed && !ed->document()->isModified(),
              qPrintable(QStringLiteral("C %1: QTextDocument is not modified").arg(name)));
        const bool closed = win->close();
        check(closed && prompts.seen == 0,
              qPrintable(QStringLiteral("C %1: closing does not prompt").arg(name)),
              QStringLiteral("closed=%1 prompts=%2").arg(closed).arg(prompts.seen));
        win.reset();
    }
}

void testEditAndSave(const QString &dir, PromptWatcher &prompts)
{
    const QString path = dir + QStringLiteral("/edit.txt");
    writeFile(path, "hello\n");
    auto win = std::make_unique<MainWindow>();
    win->show();
    win->openExternalFile(path);
    wait(800);
    QTextEdit *ed = editorOf(*win);
    if (!ed) {
        check(false, "E editor found");
        return;
    }
    ed->moveCursor(QTextCursor::End);
    ed->textCursor().insertText(QStringLiteral("more"));
    wait(50);
    check(isDirty(*win), "E a real edit marks the window dirty", win->windowTitle());
    prompts.seen = 0;
    const bool closed = win->close();
    check(!closed && prompts.seen == 1, "E closing a dirty window asks, and Cancel keeps it open",
          QStringLiteral("closed=%1 prompts=%2").arg(closed).arg(prompts.seen));

    // Undo back to the saved text: still reported dirty (conservative: another
    // change, e.g. properties, may be pending), and closing asks.
    ed->undo();
    wait(50);
    prompts.seen = 0;
    const bool closed2 = win->close();
    check(isDirty(*win) == !closed2 && (closed2 || prompts.seen == 1),
          "E undo to the opened text: close behaviour is consistent with the title",
          QStringLiteral("dirty=%1 closed=%2 prompts=%3").arg(isDirty(*win)).arg(closed2).arg(prompts.seen));
    win.reset();

    // S: edit, save (Ctrl+S path via the slot), clean, closes without asking.
    auto win2 = std::make_unique<MainWindow>();
    win2->show();
    win2->openExternalFile(path);
    wait(800);
    QTextEdit *ed2 = editorOf(*win2);
    ed2->moveCursor(QTextCursor::End);
    ed2->textCursor().insertText(QStringLiteral(" saved"));
    wait(50);
    check(isDirty(*win2), "S edited again: dirty");
    QMetaObject::invokeMethod(win2.get(), "fileSave");
    wait(500);
    check(!isDirty(*win2) && !ed2->document()->isModified(), "S after Save the window is clean",
          win2->windowTitle());
    prompts.seen = 0;
    const bool closed3 = win2->close();
    check(closed3 && prompts.seen == 0, "S a saved window closes without asking",
          QStringLiteral("closed=%1 prompts=%2").arg(closed3).arg(prompts.seen));
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    check(f.readAll().contains("saved"), "S the file on disk has the edit");
}

void testTitle()
{
    MainWindow win;
    win.show();
    const QString want = QStringLiteral("zwriter %1").arg(QString::fromStdString(zwriter::versionLabel()));
    check(win.windowTitle().endsWith(want), "T title ends with 'zwriter <version> (<commit>)'", win.windowTitle());
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    if (!settingsDir.isValid() || !dataDir.isValid()) {
        std::printf("FAIL  temp dir\n");
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("sbj-ee-test"));
    QApplication::setApplicationName(QStringLiteral("zwriter-mainwindow-test"));

    PromptWatcher prompts;
    QStringList paths;
    makeFixtures(dataDir.path(), &paths);
    check(paths.size() == 6, "fixtures created", QString::number(paths.size()));
    testOpenClean(paths, prompts);
    const QStringList sample{paths.at(0), paths.at(2), paths.at(3)}; // odt, txt, md
    for (const QString &path : sample) {
        testViewChangesStayClean(path, prompts);
    }
    // Same again with the settings the bug was reported with: continuous
    // view, typewriter scroll, key sounds, spell check on, fit-width on.
    {
        QSettings s;
        s.setValue(QStringLiteral("view/typewriterScroll"), true);
        s.setValue(QStringLiteral("view/keySounds"), true);
        s.setValue(QStringLiteral("view/spellCheck"), true);
        s.setValue(QStringLiteral("view/fitPageWidth"), true);
        s.setValue(QStringLiteral("view/fullPageView"), false);
        s.sync();
    }
    testOpenClean(sample, prompts);
    {
        QSettings s;
        s.setValue(QStringLiteral("view/fullPageView"), true);
        s.sync();
    }
    testOpenClean(sample, prompts);
    for (const QString &path : sample) {
        testViewChangesStayClean(path, prompts);
    }
    testEditAndSave(dataDir.path(), prompts);
    testTitle();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
