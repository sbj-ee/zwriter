// Whole-window tests (ctest, offscreen): the real MainWindow.
//   C  open a document of every supported type WITHOUT editing it: the window
//      is clean (no '*' in the title) after the open settles, and closing it
//      does not ask "Save changes?"
//   E  a real edit makes it dirty and closing asks; Cancel keeps the window
//   S  after Save the window is clean again and closes without asking
//   K  Book and Library sidebars (1.4.1 bug): clicking a .txt / .md / .odt
//      chapter opens it clean, switching to another chapter never prompts, a
//      real edit still prompts
//   T  the title carries "zwriter <version> (<commit>)"
//   G  with Page Guides on, typed text starts on the left guide and stays
//      inside the guides in every theme, view, window width and sidebar state
// Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "DocumentIo.hpp"
#include "MainWindow.hpp"
#include "BookManifest.hpp"
#include "BookSidebar.hpp"
#include "LibrarySidebar.hpp"
#include "version.hpp"

#include <QAction>
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
#include <QInputMethodEvent>
#include <QTest>
#include <QFileSystemModel>
#include <QListWidget>
#include <QTreeView>

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


// K: open chapters from the Book and the Library sidebars, the way a user does
// (a click on the row), and switch between them.
void testSidebarOpensClean(const QString &root, PromptWatcher &prompts, const QString &theme,
                           bool fullPage)
{
    const QString lib = root + QStringLiteral("/lib-") + theme + (fullPage ? "-page" : "-cont");
    const QString book = lib + QStringLiteral("/Testing Book");
    QDir().mkpath(book);
    writeFile(book + QStringLiteral("/Chapter 1.txt"), "Chapter 1"); // no final newline, as on vertex
    writeFile(book + QStringLiteral("/Chapter 2.txt"), "Chapter 2\nThen there was more...");
    writeFile(book + QStringLiteral("/Chapter 3.md"), "# Chapter 3\n\nSome *words*.\n");
    {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("Chapter 4\n\nAn ODT chapter."));
        DocumentIo::save(&doc, book + QStringLiteral("/Chapter 4.odt"), DocumentIo::Format::Odt);
    }
    BookManifest::fromFolder(book).save();
    {
        QSettings s;
        s.setValue(QStringLiteral("theme/id"), theme);
        s.setValue(QStringLiteral("view/fullPageView"), fullPage);
        s.setValue(QStringLiteral("library/rootPath"), lib);
        s.setValue(QStringLiteral("library/visible"), false);
        s.setValue(QStringLiteral("book/visible"), true);
        s.setValue(QStringLiteral("book/current"), QStringLiteral("Testing Book"));
        // The settings the bug was reported with (vertex, 1.4.0).
        s.setValue(QStringLiteral("view/focusMode"), true);
        s.setValue(QStringLiteral("view/focusSentence"), false);
        s.setValue(QStringLiteral("view/typewriterScroll"), true);
        s.setValue(QStringLiteral("view/fitPageWidth"), true);
        s.setValue(QStringLiteral("view/spellCheck"), true);
        s.setValue(QStringLiteral("view/smartQuotes"), false);
        s.setValue(QStringLiteral("view/keySounds"), false);
        s.setValue(QStringLiteral("view/chromePinned"), true);
        s.sync();
    }
    const QString tag = theme + (fullPage ? QStringLiteral("/page") : QStringLiteral("/continuous"));
    auto win = std::make_unique<MainWindow>();
    win->resize(1024, 640);
    win->show();
    wait(100);
    auto *side = win->findChild<BookSidebar *>();
    QListWidget *list = side ? side->view() : nullptr;
    check(list && list->count() == 4, qPrintable("K " + tag + ": book with 4 chapters shown"));
    if (!list || list->count() != 4) {
        return;
    }
    const auto clickRow = [&](int row) {
        // A real mouse click on the row (press + release on the viewport), as
        // on the desktop: the list takes focus, clicked/activated fire from
        // the release.
        const QPoint at = list->visualItemRect(list->item(row)).center();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        wait(700);
    };
    prompts.seen = 0;
    for (int row : {0, 1, 2, 3, 0}) {
        clickRow(row);
        QTextEdit *ed = editorOf(*win);
        const QString name = list->item(row)->data(Qt::UserRole + 1).toString();
        check(prompts.seen == 0 && !isDirty(*win) && ed && !ed->document()->isModified()
                  && ed->document()->availableUndoSteps() == 0
                  && win->windowTitle().contains(QFileInfo(name).fileName()),
              qPrintable("K " + tag + ": Book click opens " + name + " clean, no prompt"),
              QStringLiteral("prompts=%1 title=%2 undo=%3")
                  .arg(prompts.seen)
                  .arg(win->windowTitle())
                  .arg(ed ? ed->document()->availableUndoSteps() : -1));
    }

    // Changes that are not edits must not make it dirty either: the modified
    // flag set with the text unchanged, an input-method event that replaces a
    // character with the same character (as IBus does on Wayland), an empty
    // edit block. Switching chapters afterwards must not prompt.
    for (int row : {0, 2, 3}) {
        clickRow(row);
        QTextEdit *ed = editorOf(*win);
        ed->setFocus();
        ed->document()->setModified(true);
        {
            QTextCursor c(ed->document());
            c.beginEditBlock();
            c.insertText(QString());
            c.endEditBlock();
        }
        {
            QTextCursor c(ed->document());
            c.setPosition(1);
            ed->setTextCursor(c);
            const QString same = ed->document()->characterAt(1);
            QInputMethodEvent im(QString(), {});
            im.setCommitString(same, 0, 1);
            QApplication::sendEvent(ed, &im);
        }
        wait(100);
        const QString name = list->item(row)->data(Qt::UserRole + 1).toString();
        check(!isDirty(*win), qPrintable("K " + tag + ": non-edits leave " + name + " clean"),
              win->windowTitle());
        prompts.seen = 0;
        clickRow(row == 0 ? 1 : 0);
        check(prompts.seen == 0, qPrintable("K " + tag + ": ... and switching away does not prompt"),
              QStringLiteral("prompts=%1").arg(prompts.seen));
    }

    // Library view: the same files from the tree.
    auto *libSide = win->findChild<LibrarySidebar *>();
    QMetaObject::invokeMethod(win.get(), "toggleLibrary");
    wait(300);
    prompts.seen = 0;
    for (const char *file : {"Chapter 2.txt", "Chapter 3.md", "Chapter 4.odt", "Chapter 1.txt"}) {
        const QString path = book + QLatin1Char('/') + QLatin1String(file);
        emit libSide->fileActivated(path); // what a click on the row emits
        wait(700);
        QTextEdit *ed = editorOf(*win);
        check(prompts.seen == 0 && !isDirty(*win) && ed && !ed->document()->isModified()
                  && win->windowTitle().contains(QLatin1String(file)),
              qPrintable("K " + tag + ": Library opens " + QLatin1String(file) + " clean, no prompt"),
              QStringLiteral("prompts=%1 title=%2").arg(prompts.seen).arg(win->windowTitle()));
    }

    // A real edit still prompts (Cancel keeps the document).
    QTextEdit *ed = editorOf(*win);
    ed->setFocus();
    QTextCursor c(ed->document());
    c.movePosition(QTextCursor::End);
    ed->setTextCursor(c);
    ed->insertPlainText(QStringLiteral(" edited"));
    wait(50);
    prompts.seen = 0;
    emit libSide->fileActivated(book + QStringLiteral("/Chapter 2.txt"));
    wait(300);
    check(isDirty(*win) && prompts.seen == 1 && win->windowTitle().contains("Chapter 1.txt"),
          qPrintable("K " + tag + ": a real edit prompts; Cancel keeps the chapter"),
          QStringLiteral("prompts=%1 title=%2").arg(prompts.seen).arg(win->windowTitle()));
    win.reset(); // destroyed without close(): no prompt left behind
    prompts.seen = 0;
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

// Page guides vs the text column (1.3.2 bug: in the continuous view the guides
// sat at a fixed tenth of the viewport while the text used the page margins,
// so typed text started left of the left guide).
void testPageGuidesContainText()
{
    const char *themes[] = {"setThemeDark", "setThemePaper", "setThemeInverse"};
    const int widths[] = {640, 800, 1024, 1280, 1600, 1920};
    for (int fullPage = 0; fullPage < 2; ++fullPage) {
        {
            QSettings s;
            s.setValue(QStringLiteral("view/fullPageView"), fullPage == 1);
            s.setValue(QStringLiteral("view/fitPageWidth"), false);
            s.setValue(QStringLiteral("view/typewriterScroll"), false);
            s.sync();
        }
        MainWindow win;
        win.show();
        wait(100);
        QTextEdit *ed = editorOf(win);
        QAction *guides = nullptr;
        for (QAction *a : win.findChildren<QAction *>()) {
            if (a->text() == QStringLiteral("Page &Guides")) {
                guides = a;
            }
        }
        check(ed && guides, "G editor and Page Guides action found");
        if (!ed || !guides) {
            return;
        }
        guides->setChecked(true);
        QMetaObject::invokeMethod(&win, "togglePageGuides");
        ed->setFocus();
        QTextCursor c(ed->document());
        c.insertText(QStringLiteral("test"));
        c.movePosition(QTextCursor::Start);
        ed->setTextCursor(c);
        for (int library = 0; library < 2; ++library) {
            for (const char *theme : themes) {
                QMetaObject::invokeMethod(&win, theme);
                for (int w : widths) {
                    win.resize(w, 700);
                    wait(60);
                    const QPair<qreal, qreal> col = win.pageGuideColumn();
                    const qreal caretX = ed->cursorRect(c).left();
                    QTextCursor end(ed->document());
                    end.movePosition(QTextCursor::End);
                    const qreal endX = ed->cursorRect(end).left();
                    const bool ok = caretX >= col.first - 2.0 && caretX <= col.first + 3.0
                        && endX <= col.second + 1.0 && col.second > col.first;
                    check(ok,
                          qPrintable(QStringLiteral("G %1 %2 %3 w=%4: text starts on the left guide")
                                         .arg(fullPage ? QStringLiteral("full-page") : QStringLiteral("continuous"),
                                              QLatin1String(theme),
                                              library ? QStringLiteral("library-toggled") : QStringLiteral("default"))
                                         .arg(w)),
                          QStringLiteral("caret=%1 end=%2 guides=%3..%4").arg(caretX).arg(endX).arg(col.first).arg(col.second));
                }
            }
            QMetaObject::invokeMethod(&win, "toggleLibrary");
            wait(60);
        }
        win.close();
    }
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
    for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("paper")}) {
        for (bool page : {false, true}) {
            testSidebarOpensClean(dataDir.path(), prompts, theme, page);
        }
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
    testPageGuidesContainText();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
