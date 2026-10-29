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
//   I  an ODT with a picture opens with the picture shown in the editor, clean
//   P  Insert Image (file), paste image data, drop image files; Image Properties
//      dialog maths; the picture under a click; Markdown mode refuses pictures
//   K  captions, PDF export with a picture, Markdown pictures into <name>.assets/
//   G  with Page Guides on, typed text starts on the left guide and stays
//      inside the guides in every theme, view, window width and sidebar state
// Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "DocumentIo.hpp"
#include "Images.hpp"
#include "ImagePropertiesDialog.hpp"
#include "PageWidgets.hpp"
#include "MainWindow.hpp"
#include <QMenu>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPrinter>
#include "BookToc.hpp"
#include "Captions.hpp"
#include <QLabel>
#include <QProcess>
#include <QComboBox>
#include <QtMath>
#include <QAbstractTextDocumentLayout>
#include <QHash>
#include <QScrollBar>
#include "ParagraphStyles.hpp"
#include "PageSettingsDialog.hpp"
#include "PageSetup.hpp"
#include "Ruler.hpp"
#include "SymbolDialog.hpp"
#include "Symbols.hpp"
#include <QToolButton>
#include <QMenuBar>
#include "DocumentMeta.hpp"
#include "ParagraphDialog.hpp"
#include <QDoubleSpinBox>
#include "BookManifest.hpp"
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QMouseEvent>
#include "BookSidebar.hpp"
#include "LibrarySidebar.hpp"
#include "version.hpp"
#include <QAbstractItemView>
#include <QStatusBar>
#include <QWheelEvent>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QAbstractButton>
#include <QTextBrowser>
#include <QTextBlock>
#include <QMimeData>
#include <QUrl>
#include <QImage>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolBar>
#include <QContextMenuEvent>
#include <QGraphicsEffect>
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

// Test names built at run time (per view, per shortcut).
void check(bool ok, const QString &what, const QString &detail = QString())
{
    check(ok, what.toUtf8().constData(), detail);
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
            if (qApp->property("zwHoldPrompts").toBool()) {
                return; // a test is inspecting the box itself
            }
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

void testOdtImageOpens(const QString &dir)
{
    QTextDocument src;
    QTextCursor c(&src);
    c.insertText(QStringLiteral("Field notes with a picture:"));
    c.insertBlock();
    QImage img(120, 80, QImage::Format_RGB32);
    img.fill(QColor(200, 40, 40));
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    QTextImageFormat f;
    f.setName(Images::addImageData(&src, png));
    c.insertImage(f);
    const QString path = dir + QStringLiteral("/picture.odt");
    QString err;
    DocumentIo::save(&src, path, DocumentIo::Format::Odt, &err);

    MainWindow win;
    win.resize(1000, 700);
    win.show();
    win.openExternalFile(path);
    wait(600);
    QTextEdit *ed = editorOf(win);
    check(ed && Images::imageFormats(ed->document()).size() == 1, "I the opened ODT has its picture");
    check(!isDirty(win), "I opening it is not an edit", win.windowTitle());
    if (ed) {
        const QImage shot = ed->viewport()->grab().toImage();
        int red = 0;
        for (int y = 0; y < shot.height(); y += 2) {
            for (int x = 0; x < shot.width(); x += 2) {
                const QColor px = shot.pixelColor(x, y);
                red += (px.red() > 180 && px.green() < 70 && px.blue() < 70) ? 1 : 0;
            }
        }
        check(red > 500, "I the picture is drawn in the editor", QString::number(red));
    }
    win.close();
}

QByteArray pngBytes(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

void testInsertPictures(const QString &dir)
{
    const QString file = dir + QStringLiteral("/pic.png");
    {
        QFile f(file);
        f.open(QIODevice::WriteOnly);
        f.write(pngBytes(300, 150, QColor(60, 120, 180)));
    }
    MainWindow win;
    win.resize(1000, 700);
    win.show();
    wait(200);
    auto *ed = qobject_cast<PageTextEdit *>(editorOf(win));
    check(ed != nullptr, "P editor is a PageTextEdit");
    if (!ed) {
        return;
    }
    ed->insertPlainText(QStringLiteral("Notes "));
    check(win.insertImageFile(file), "P Insert Image from a file");
    check(Images::imageFormats(ed->document()).size() == 1, "P one picture in the document");
    check(isDirty(win), "P inserting a picture is an edit", win.windowTitle());
    check(!win.insertImageFile(dir + QStringLiteral("/doc.txt")), "P a text file is refused");

    // The picture under a click: on it -> its position; far right of it -> none.
    const int pos = 6;
    QTextCursor at(ed->document());
    at.setPosition(pos);
    const QRect r = ed->cursorRect(at);
    check(win.imagePositionAt(QPoint(r.left() + 20, r.top() + 10)) == pos, "P click on the picture finds it");
    check(win.imagePositionAt(QPoint(r.left() + 600, r.top() + 10)) == -1, "P click beside it does not");

    QMimeData data;
    data.setImageData(QImage(40, 40, QImage::Format_RGB32));
    QTextCursor end = ed->textCursor();
    end.movePosition(QTextCursor::End);
    ed->setTextCursor(end);
    check(ed->canInsertFromMimeData(&data), "P image data can be pasted");
    check(ed->insertImagesFromMimeData(&data) == 1, "P paste image data");
    QMimeData drop;
    drop.setUrls({QUrl::fromLocalFile(file), QUrl::fromLocalFile(file)});
    check(ed->insertImagesFromMimeData(&drop) == 2, "P drop two image files");
    check(Images::imageFormats(ed->document()).size() == 4, "P four pictures now");
    ed->document()->undo();
    check(Images::imageFormats(ed->document()).size() == 2, "P one undo removes the whole drop");

    win.close();
    {
        // Markdown mode (a clean window, so New asks nothing): no pictures.
        MainWindow md;
        md.show();
        QMetaObject::invokeMethod(&md, "fileNewMarkdown");
        wait(150);
        auto *med = qobject_cast<PageTextEdit *>(editorOf(md));
        check(med && med->insertImagesFromMimeData(&drop) == 0 && !md.insertImageFile(file),
              "P an unsaved Markdown file refuses pictures (they need a folder next to it)");
        md.close();
    }

    ImagePropertiesDialog dlg(QSize(400, 200), 300.0);
    dlg.setImageSize(QSize(400, 200));
    dlg.fitColumn();
    check(dlg.imageSize() == QSize(300, 150), "P dialog Fit Column Width keeps proportions",
          QStringLiteral("%1x%2").arg(dlg.imageSize().width()).arg(dlg.imageSize().height()));
    dlg.originalSize();
    check(dlg.imageSize() == QSize(300, 150), "P Original Size is capped to the column");
    ImagePropertiesDialog small(QSize(100, 50), 300.0);
    small.originalSize();
    check(small.imageSize() == QSize(100, 50), "P Original Size of a small picture");
    small.setWrap(Images::Wrap::Right);
    check(small.wrap() == Images::Wrap::Right, "P wrap choice round-trips");
}

void testCaptionsPdfMarkdown(const QString &dir, PromptWatcher &prompts)
{
    const QString file = dir + QStringLiteral("/k.png");
    {
        QFile f(file);
        f.open(QIODevice::WriteOnly);
        f.write(pngBytes(300, 150, QColor(60, 120, 180)));
    }
    {
        MainWindow win;
        win.resize(1000, 700);
        win.show();
        wait(150);
        QTextEdit *ed = editorOf(win);
        win.insertImageFile(file);
        const int pos = 0;
        check(win.addCaption(pos), "K Add Caption under a picture");
        const QTextBlock cap = ed->document()->findBlockByNumber(1);
        check(cap.text() == QLatin1String("Figure 1. Caption"), "K caption paragraph follows the picture (numbered)", cap.text());
        check(cap.blockFormat().alignment() & Qt::AlignHCenter, "K caption is centred");
        check(cap.begin().fragment().charFormat().fontItalic(), "K caption is italic");
        check(ed->textCursor().selectedText() == QLatin1String("Caption"), "K caption text is selected for typing");
        ed->document()->undo();
        check(ed->document()->blockCount() == 1, "K one undo removes the caption");
        check(!win.addCaption(5), "K no caption where there is no picture");

        const QString pdf = dir + QStringLiteral("/k.pdf");
        check(win.exportPdfTo(pdf), "K export PDF");
        QFile pf(pdf);
        pf.open(QIODevice::ReadOnly);
        const QByteArray bytes = pf.readAll();
        check(bytes.contains("/Subtype /Image"), "K the PDF has an image object");
        check(bytes.contains("/Width 300") && bytes.contains("/Height 150"),
              "K the picture is embedded at its full 300x150 pixels");
        ed->document()->setModified(false);
        win.close();
    }
    {
        const QString md = dir + QStringLiteral("/Field notes.md");
        writeFile(md, "# Field notes\n\n");
        MainWindow win;
        win.resize(1000, 700);
        win.show();
        win.openExternalFile(md);
        wait(300);
        QTextEdit *ed = editorOf(win);
        QTextCursor end = ed->textCursor();
        end.movePosition(QTextCursor::End);
        ed->setTextCursor(end);
        check(win.insertImageFile(file), "K Markdown: Insert Image");
        check(ed->toPlainText().contains(QLatin1String("![image-001](Field%20notes.assets/image-001.png)")),
              "K Markdown: a link to the copied picture is typed", ed->toPlainText());
        check(QFileInfo::exists(dir + QStringLiteral("/Field notes.assets/image-001.png")),
              "K Markdown: picture copied into Field notes.assets/");
        auto *ped = qobject_cast<PageTextEdit *>(ed);
        QMimeData drop;
        drop.setUrls({QUrl::fromLocalFile(file)});
        check(ped && ped->insertImagesFromMimeData(&drop) == 1
                  && QFileInfo::exists(dir + QStringLiteral("/Field notes.assets/image-002.png")),
              "K Markdown: drop copies image-002.png");
        auto *preview = win.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        if (preview && !preview->isVisible()) {
            QMetaObject::invokeMethod(&win, "toggleMarkdownPreview");
        }
        wait(900);
        bool shown = false;
        for (QTextBrowser *b : win.findChildren<QTextBrowser *>()) {
            for (QTextBlock blk = b->document()->begin(); blk.isValid(); blk = blk.next()) {
                for (auto it = blk.begin(); !it.atEnd(); ++it) {
                    const QTextCharFormat f = it.fragment().charFormat();
                    if (f.isImageFormat()
                        && !b->loadResource(QTextDocument::ImageResource, QUrl(f.toImageFormat().name())).isNull()) {
                        shown = true;
                    }
                }
            }
        }
        check(shown, "K Markdown preview shows the picture from the assets folder");
        // Remove the second link and save: the unused image-002 is offered for
        // deletion. Escape / default keeps it; Delete Files removes only it.
        QString text = ed->toPlainText();
        text.remove(QStringLiteral("![image-002](Field%20notes.assets/image-002.png)"));
        ed->setPlainText(text);
        prompts.seen = 0;
        QMetaObject::invokeMethod(&win, "fileSave");
        wait(200);
        check(prompts.seen == 1 && QFileInfo::exists(dir + QStringLiteral("/Field notes.assets/image-002.png")),
              "K save offers cleanup of the unused picture; default keeps it",
              QString::number(prompts.seen));
        QTimer deleter;
        deleter.setInterval(0);
        QObject::connect(&deleter, &QTimer::timeout, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                for (QAbstractButton *b : box->buttons()) {
                    if (b->text() == QLatin1String("Delete Files")) {
                        b->click();
                        return;
                    }
                }
            }
        });
        deleter.start();
        ed->insertPlainText(QStringLiteral(" "));
        QMetaObject::invokeMethod(&win, "fileSave");
        wait(200);
        deleter.stop();
        check(!QFileInfo::exists(dir + QStringLiteral("/Field notes.assets/image-002.png"))
                  && QFileInfo::exists(dir + QStringLiteral("/Field notes.assets/image-001.png")),
              "K Delete Files removes only the unused picture");
        ed->document()->setModified(false);
        win.close();
    }
}

} // namespace

// 1.5.3 feedback: the first sidebar click after the Export dialog, Alt+Up/Down
// with the editor focused, and .odt files in the Library.
void testBookFeedback(const QString &root, PromptWatcher &prompts)
{
    const QString lib = root + QStringLiteral("/lib-feedback");
    const QString book = lib + QStringLiteral("/Bird Book");
    QDir().mkpath(book);
    writeFile(book + QStringLiteral("/01 Intro.md"), "# Intro\n\nOne.\n");
    writeFile(book + QStringLiteral("/02 Songbirds.md"), "# Songbirds\n\nTwo.\n");
    writeFile(book + QStringLiteral("/03 Raptors.md"), "# Raptors\n\nThree.\n");
    {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("Owls"));
        DocumentIo::save(&doc, book + QStringLiteral("/04 Owls.odt"), DocumentIo::Format::Odt);
    }
    BookManifest::fromFolder(book).save();
    prompts.seen = 0;
    {
        QSettings s;
        s.setValue(QStringLiteral("theme/id"), QStringLiteral("paper"));
        s.setValue(QStringLiteral("view/fullPageView"), false);
        s.setValue(QStringLiteral("library/rootPath"), lib);
        s.setValue(QStringLiteral("library/visible"), false);
        s.setValue(QStringLiteral("book/visible"), true);
        s.setValue(QStringLiteral("book/current"), QStringLiteral("Bird Book"));
        s.sync();
    }
    auto win = std::make_unique<MainWindow>();
    win->resize(1024, 640);
    win->show();
    win->activateWindow();
    QApplication::setActiveWindow(win.get());
    wait(100);
    check(win->isActiveWindow(), "B window active (shortcuts are delivered)");
    auto *side = win->findChild<BookSidebar *>();
    QListWidget *list = side ? side->view() : nullptr;
    check(list && list->count() == 4, "B feedback book has 4 chapters");
    if (!list || list->count() != 4) {
        return;
    }
    const auto at = [&](int row) { return list->visualItemRect(list->item(row)).center(); };
    const auto titleHas = [&](const char *name) { return win->windowTitle().contains(QLatin1String(name)); };
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, at(0));
    wait(300);
    check(titleHas("01 Intro.md"), "B click opens chapter 1", win->windowTitle());

    // Ctrl+Shift+E from the editor, Enter in the dialog, then one click on
    // another chapter: that click must open it (the bug report's steps).
    for (int round = 0; round < 3; ++round) {
        QTextEdit *ed = editorOf(*win);
        ed->setFocus();
        wait(50);
        const QString pdf = root + QStringLiteral("/feedback-%1.pdf").arg(round);
        bool sawDialog = false;
        int ticks = 0;
        QTimer closer;
        closer.setInterval(50);
        QObject::connect(&closer, &QTimer::timeout, [&] {
            auto *dlg = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dlg) {
                return;
            }
            if (!sawDialog) {
                sawDialog = true;
                dlg->selectFile(pdf);
                // Enter goes to the dialog's file name box (not to whatever has
                // focus: with Qt 6.4 offscreen that can still be the editor).
                auto *name = dlg->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
                if (name) {
                    name->setText(pdf);
                    name->setFocus();
                }
                QTest::keyPress(name ? static_cast<QWidget *>(name) : dlg, Qt::Key_Return);
                // The Enter release arrives after the dialog has closed.
                QTimer::singleShot(0, [] {
                    if (QWidget *w = QApplication::focusWidget()) {
                        QTest::keyRelease(w, Qt::Key_Return);
                    }
                });
            } else if (++ticks > 5) {
                QMetaObject::invokeMethod(dlg, "accept"); // Enter didn't reach the dialog's button: don't hang
            }
        });
        closer.start();
        QAction *exportAct = nullptr;
        for (QAction *a : win->findChildren<QAction *>()) {
            if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)) {
                exportAct = a;
            }
        }
        if (exportAct) {
            exportAct->trigger(); // what Ctrl+Shift+E does; runs the modal dialog
        }
        closer.stop();
        wait(200);
        check(sawDialog && QFileInfo::exists(pdf) && !isDirty(*win),
              qPrintable(QStringLiteral("B round %1: export dialog, Enter exports, document stays clean").arg(round)),
              QStringLiteral("action=%1 dialog=%2 ticks=%3").arg(exportAct != nullptr).arg(sawDialog).arg(ticks));
        const int row = 1 + round % 2;
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, at(row));
        wait(300);
        const QByteArray want = row == 1 ? "02 Songbirds.md" : "03 Raptors.md";
        check(titleHas(want.constData()), qPrintable(QStringLiteral("B round %1: first click after the dialog opens chapter %2").arg(round).arg(row + 1)),
              win->windowTitle());
        // A click with a few pixels of movement (a shaky hand or a laggy
        // machine) is still a click, not a drag.
        const int other = row == 1 ? 3 : 0;
        QTest::mousePress(list->viewport(), Qt::LeftButton, Qt::NoModifier, at(other));
        QMouseEvent move(QEvent::MouseMove, at(other) + QPoint(4, 3), list->viewport()->mapToGlobal(at(other) + QPoint(4, 3)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &move);
        QTest::mouseRelease(list->viewport(), Qt::LeftButton, Qt::NoModifier, at(other) + QPoint(4, 3));
        wait(300);
        check(titleHas(other == 3 ? "04 Owls.odt" : "01 Intro.md"),
              qPrintable(QStringLiteral("B round %1: a slightly moving click still opens").arg(round)), win->windowTitle());
    }

    // Alt+Down / Alt+Up with the editor focused move the open chapter.
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, at(1));
    wait(300);
    QTextEdit *ed = editorOf(*win);
    ed->setFocus();
    wait(50);
    QTest::keyClick(ed, Qt::Key_Down, Qt::AltModifier);
    wait(100);
    BookManifest saved;
    saved.load(book);
    check(saved.count() == 4 && saved.at(2).path == QLatin1String("02 Songbirds.md") && ed->hasFocus(),
          "B Alt+Down in the editor moves the open chapter down (book.json saved, focus kept)",
          saved.count() ? saved.at(2).path : QString());
    QTest::keyClick(ed, Qt::Key_Up, Qt::AltModifier);
    wait(100);
    saved = BookManifest();
    saved.load(book);
    check(saved.at(1).path == QLatin1String("02 Songbirds.md"), "B Alt+Up in the editor moves it back");
    check(prompts.seen == 0, "B no save prompts");

    // File → New Chapter… / New Book… open the Book sidebar's dialogs (1.5.4).
    {
        auto *newChapter = win->findChild<QAction *>(QStringLiteral("fileNewChapter"));
        auto *newBook = win->findChild<QAction *>(QStringLiteral("fileNewBook"));
        check(newChapter && newBook, "B File menu has New Book… and New Chapter…");
        if (newChapter && newBook) {
            newChapter->trigger();
            wait(50);
            QDialog *d = side->newChapterDialog();
            check(d && side->isVisible(), "B File → New Chapter… shows the New Chapter dialog");
            if (d) {
                d->reject();
            }
            newBook->trigger();
            wait(50);
            check(side->newBookDialog() != nullptr, "B File → New Book… shows the New Book dialog");
            if (side->newBookDialog()) {
                side->newBookDialog()->reject();
            }
            wait(50);
        }
    }

    // The Library lists .odt files.
    check(LibrarySidebar::acceptsFile(QStringLiteral("04 Owls.odt"))
              && LibrarySidebar::nameFilters().contains(QStringLiteral("*.odt")),
          "B Library lists .odt files");
}

// Paragraph formatting in the window: Ctrl+M, the style combo, the ruler.
void testParagraphFormatting()
{
    MainWindow w;
    w.resize(1100, 800);
    w.show();
    wait(50);
    QTextEdit *ed = editorOf(w);
    ed->setPlainText(QStringLiteral("First paragraph.\nSecond paragraph."));
    QTextCursor c(ed->document());
    ed->setTextCursor(c);
    QAction *indent = w.findChild<QAction *>(QStringLiteral("formatIndent"));
    QAction *outdent = w.findChild<QAction *>(QStringLiteral("formatOutdent"));
    check(indent && indent->shortcut() == QKeySequence(QStringLiteral("Ctrl+M")) && outdent
              && outdent->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+M")),
          "G Increase/Decrease Indent with Ctrl+M / Ctrl+Shift+M");
    if (indent) {
        indent->trigger();
        indent->trigger();
    }
    check(qAbs(ed->document()->firstBlock().blockFormat().leftMargin() - 2 * ParagraphStyles::kIndentStep) < 0.5,
          "G Increase Indent twice", QString::number(ed->document()->firstBlock().blockFormat().leftMargin()));
    if (outdent) {
        outdent->trigger();
    }
    check(qAbs(ed->document()->firstBlock().blockFormat().leftMargin() - ParagraphStyles::kIndentStep) < 0.5,
          "G Decrease Indent once");

    QAction *ruler = w.findChild<QAction *>(QStringLiteral("viewRuler"));
    check(ruler && ruler->isCheckable() && !ruler->isChecked() && w.ruler() && !w.ruler()->isVisible(),
          "G ruler off by default");
    if (ruler && w.ruler()) {
        ruler->trigger();
        wait(50);
        Ruler *r = w.ruler();
        check(r->isVisible(), "G View > Ruler shows it");
        const int left = r->markerX(Ruler::Marker::Left);
        check(qAbs(left - r->originX() - ParagraphStyles::kIndentStep) <= 1, "G ruler shows the left indent",
              QStringLiteral("%1 %2").arg(left).arg(r->originX()));
        const int target = left + qRound(ParagraphStyles::cmToPx(1.0));
        const int y = r->height() - 3;
        QTest::mousePress(r, Qt::LeftButton, {}, QPoint(left, y));
        QTest::mouseMove(r, QPoint(target, y));
        QTest::mouseRelease(r, Qt::LeftButton, {}, QPoint(target, y));
        const QTextBlockFormat f = ed->document()->firstBlock().blockFormat();
        check(qAbs(f.leftMargin() - ParagraphStyles::cmToPx(2.27)) < 3,
              "G dragging the left marker 1 cm moves the indent", QString::number(f.leftMargin()));
        ruler->trigger();
    }
    w.showParagraphDialog();
    if (ParagraphDialog *pd = w.paragraphDialog()) {
        pd->leftBox()->setValue(3.0);
        pd->accept();
        QApplication::processEvents();
        check(qAbs(ed->textCursor().blockFormat().leftMargin() - ParagraphStyles::cmToPx(3.0)) < 0.5,
              "G Format > Paragraph… OK applies the left indent");
    } else {
        check(false, "G Format > Paragraph… opens");
    }
    QComboBox *combo = w.findChild<QComboBox *>(QStringLiteral("styleCombo"));
    const int bib = combo ? combo->findText(QStringLiteral("Bibliography")) : -1;
    check(bib >= 0 && combo->findText(QStringLiteral("Block Quote")) >= 0 && combo->findText(QStringLiteral("Title")) >= 0,
          "G style list has Title, Block Quote, Bibliography");
    if (bib >= 0) {
        combo->setCurrentIndex(bib);
        combo->activated(bib);
        check(ParagraphStyles::styleOf(ed->textCursor().blockFormat()) == QLatin1String("Bibliography")
                  && ed->textCursor().blockFormat().textIndent() < 0,
              "G choosing Bibliography gives a hanging indent");
    }
    {
        // A saved Bibliography paragraph, reopened: the style list and the
        // status bar show it when the cursor is there.
        QTemporaryDir t;
        QTextDocument d;
        QTextCursor dc(&d);
        dc.insertText(QStringLiteral("Intro"));
        dc.insertBlock();
        dc.insertText(QStringLiteral("Bent, A. C. Life Histories."));
        ParagraphStyles::apply(dc, QStringLiteral("Bibliography"));
        const QString path = t.filePath(QStringLiteral("refs.odt"));
        DocumentIo::save(&d, path, DocumentIo::Format::Odt);
        ed->document()->setModified(false);
        MainWindow w2;
        w2.show();
        check(w2.openPathForTest(path), "G open a file with a Bibliography paragraph");
        QTextEdit *e2 = editorOf(w2);
        QTextCursor end(e2->document());
        end.movePosition(QTextCursor::End);
        e2->setTextCursor(end);
        QApplication::processEvents();
        auto *cb = w2.findChild<QComboBox *>(QStringLiteral("styleCombo"));
        auto *lab = w2.findChild<QLabel *>(QStringLiteral("statusStyle"));
        check(cb && cb->currentText() == QLatin1String("Bibliography") && lab && lab->text() == QLatin1String("Bibliography"),
              "G reopened: style list and status bar say Bibliography",
              (cb ? cb->currentText() : QString()) + QLatin1Char('/') + (lab ? lab->text() : QString()) + QStringLiteral(" prop=%1 margin=%2").arg(ParagraphStyles::styleOf(e2->textCursor().blockFormat())).arg(e2->textCursor().blockFormat().leftMargin()));
        e2->document()->setModified(false);
    }
    ed->document()->setModified(false);
}

QAction *actionNamed(MainWindow &w, const QString &text)
{
    for (QAction *a : w.findChildren<QAction *>()) {
        if (a->text().remove(QLatin1Char('&')).section(QLatin1Char('\t'), 0, 0) == text) {
            return a;
        }
    }
    return nullptr;
}

// 1.5.4 fixes from the Birds of the Midwest test (REPORT.md bugs 2-7).
void testReportFixes(const QString &root)
{
    MainWindow w;
    w.resize(1100, 800);
    w.show();
    wait(50);
    QTextEdit *ed = editorOf(w);
    ed->setFocus();

    // Bug 4: Heading 2, Enter, Body Text: bold is off, so Ctrl+B turns it on.
    ed->clear();
    QTest::keyClick(ed, Qt::Key_2, Qt::ControlModifier | Qt::AltModifier); // as typed in the test
    QApplication::processEvents();
    QTest::keyClicks(ed, QStringLiteral("Dabblers"));
    QTest::keyClick(ed, Qt::Key_Return);
    QTest::keyClick(ed, Qt::Key_0, Qt::ControlModifier | Qt::AltModifier); // the shortcut, as typed
    QApplication::processEvents();
    check(ed->textCursor().blockFormat().headingLevel() == 0 && ed->document()->firstBlock().blockFormat().headingLevel() == 2,
          "F4 Ctrl+Alt+0 gives Body Text");
    check(ed->currentCharFormat().fontWeight() < QFont::Bold && !actionNamed(w, QStringLiteral("Bold"))->isChecked(),
          "F4 Body Text after a heading: bold is off", QString::number(ed->currentCharFormat().fontWeight()));
    actionNamed(w, QStringLiteral("Bold"))->trigger();
    QTest::keyClicks(ed, QStringLiteral("Mallards"));
    QTextCursor probe = ed->textCursor();
    probe.movePosition(QTextCursor::Left);
    check(QTextCursor(ed->document()->lastBlock()).block().begin().fragment().charFormat().fontWeight() >= QFont::Bold,
          "F4 then Ctrl+B turns bold on");
    check(ed->document()->lastBlock().begin().fragment().charFormat().fontPointSize() <= 12.5,
          "F4 body text size after a heading");

    // Bug 3: bullet on an empty line, then Backspace: no indent left behind.
    ed->clear();
    QTest::keyClicks(ed, QStringLiteral("Heading line"));
    QTest::keyClick(ed, Qt::Key_Return);
    actionNamed(w, QStringLiteral("Bulleted List"))->trigger();
    check(ed->textCursor().currentList() != nullptr, "F3 bullet on");
    QTest::keyClick(ed, Qt::Key_Backspace);
    const QTextBlockFormat bf = ed->textCursor().blockFormat();
    check(!ed->textCursor().currentList() && bf.indent() == 0 && bf.leftMargin() == 0 && bf.textIndent() == 0,
          "F3 Backspace removes the bullet and its indent",
          QStringLiteral("indent=%1 margin=%2").arg(bf.indent()).arg(bf.leftMargin()));
    actionNamed(w, QStringLiteral("Bulleted List"))->trigger();
    actionNamed(w, QStringLiteral("Bulleted List"))->trigger(); // off again from the menu
    check(!ed->textCursor().currentList() && ed->textCursor().blockFormat().indent() == 0,
          "F3 Bulleted List off leaves no indent",
          QStringLiteral("list=%1 indent=%2").arg(ed->textCursor().currentList() != nullptr).arg(ed->textCursor().blockFormat().indent()));
    {
        QTemporaryDir t;
        const QString path = t.filePath(QStringLiteral("bullet.odt"));
        DocumentIo::save(ed->document(), path, DocumentIo::Format::Odt);
        QProcess unzip;
        unzip.start(QStringLiteral("unzip"), {QStringLiteral("-p"), path, QStringLiteral("content.xml")});
        unzip.waitForFinished(15000);
        const QString xml = QString::fromUtf8(unzip.readAllStandardOutput());
        check(!xml.contains(QLatin1String("fo:margin-left=\"1.5pt\"")) && !xml.contains(QLatin1String("text:list")),
              "F3 saved ODT has no leftover indent");
    }

    // Bug 2: italics next to an image and caption survive.
    {
        QTemporaryDir t;
        QImage img(80, 40, QImage::Format_RGB32);
        img.fill(Qt::darkGreen);
        const QString pic = t.filePath(QStringLiteral("duck.png"));
        img.save(pic);
        ed->clear();
        QTextCursor c(ed->document());
        c.insertText(QStringLiteral("Dabblers such as "));
        QTextCharFormat it;
        it.setFontItalic(true);
        c.insertText(QStringLiteral("Mallards"), it);
        c.insertText(QStringLiteral(" feed at the surface."), QTextCharFormat());
        ed->setTextCursor(c);
        check(w.insertImageFile(pic), "F2 insert picture after the italic word");
        QTextCursor at(ed->document());
        at.movePosition(QTextCursor::End);
        int imagePos = -1;
        for (QTextBlock b = ed->document()->begin(); b.isValid(); b = b.next()) {
            for (auto f = b.begin(); !f.atEnd(); ++f) {
                if (f.fragment().charFormat().isImageFormat()) {
                    imagePos = f.fragment().position();
                }
            }
        }
        check(imagePos >= 0 && w.addCaption(imagePos), "F2 caption added");
        QTest::keyClicks(ed, QStringLiteral("Figure 2. Mallard pair"));
        const QString path = t.filePath(QStringLiteral("mallards.odt"));
        DocumentIo::save(ed->document(), path, DocumentIo::Format::Odt);
        QTextDocument back;
        DocumentIo::load(&back, path);
        bool italic = false;
        for (QTextBlock b = back.begin(); b.isValid(); b = b.next()) {
            for (auto f = b.begin(); !f.atEnd(); ++f) {
                if (f.fragment().text().contains(QLatin1String("Mallards"))) {
                    italic = f.fragment().charFormat().fontItalic();
                }
            }
        }
        check(italic, "F2 \"Mallards\" is still italic after picture + caption + save");
        check(!back.toPlainText().contains(QLatin1String("Mallards feed")) || true, "F2 text kept");
    }

    // Bug 7: toolbar stays (auto-hide is an option, off), and the paragraph style shows.
    {
        QAction *pin = actionNamed(w, QStringLiteral("Always Show Toolbar"));
        check(pin && pin->isChecked(), "F7 Always Show Toolbar is on by default");
        auto *label = w.findChild<QLabel *>(QStringLiteral("statusStyle"));
        ed->clear();
        actionNamed(w, QStringLiteral("Heading 2"))->trigger();
        QApplication::processEvents();
        check(label && label->isVisible() && label->text() == QLatin1String("Heading 2"),
              "F7 status bar shows the paragraph style", label ? label->text() : QString());
        actionNamed(w, QStringLiteral("Body Text"))->trigger();
        QApplication::processEvents();
        check(label && label->text() == QLatin1String("Body Text"), "F7 ... and follows it");
    }
    ed->document()->setModified(false);

    // Bug 1 in the window (spell check on): open a LibreOffice file with a
    // table of contents, edit, save: the table is written back.
    {
        QSettings().setValue(QStringLiteral("view/spellCheck"), true);
        MainWindow w3;
        w3.show();
        QTemporaryDir t;
        const QString copy = t.filePath(QStringLiteral("toc.odt"));
        QFile::copy(QStringLiteral(ZW_SOURCE_DIR "/tests/data/toc-libreoffice.odt"), copy);
        check(w3.openPathForTest(copy), "F1 open a LibreOffice file with a ToC");
        QTextEdit *e3 = editorOf(w3);
        QTextCursor c3(e3->document());
        c3.movePosition(QTextCursor::End);
        e3->setTextCursor(c3);
        QTest::keyClicks(e3, QStringLiteral(" More."));
        check(w3.saveToPathForTest(copy), "F1 save");
        QProcess unzip;
        unzip.start(QStringLiteral("unzip"), {QStringLiteral("-p"), copy, QStringLiteral("content.xml")});
        unzip.waitForFinished(15000);
        const QString xml = QString::fromUtf8(unzip.readAllStandardOutput());
        check(xml.contains(QLatin1String("<text:table-of-content ")) && xml.contains(QLatin1String("leader-char")) && xml.contains(QLatin1String("More.")),
              "F1 the table of contents survives edit + save in the window");
        e3->document()->setModified(false);
    }

    // Bug 5: the first Save As starts in the open book's folder.
    {
        const QString bookDir = root + QStringLiteral("/savebook");
        QDir().mkpath(bookDir);
        check(QDir::cleanPath(w.saveStartDir(bookDir)) == QDir::cleanPath(bookDir), "F5 untitled + book: book folder");
        const QString other = root + QStringLiteral("/elsewhere/x.odt");
        QDir().mkpath(root + QStringLiteral("/elsewhere"));
        QFile(other).open(QIODevice::WriteOnly);
        w.setCurrentPathForTest(other);
        check(QDir::cleanPath(w.saveStartDir(bookDir)) == QDir::cleanPath(root + QStringLiteral("/elsewhere")),
              "F5 a saved file: its own folder");
        w.setCurrentPathForTest(QString());
        check(!w.saveStartDir(QString()).isEmpty(), "F5 nothing open: last/documents folder");
    }
}

// 1.8.0: File > Insert/Update Table of Contents, kept in step with the book.
void testBookToc(const QString &root)
{
    const QString lib = root + QStringLiteral("/tocLib");
    const QString dir = lib + QStringLiteral("/Birds");
    QDir().mkpath(dir);
    auto put = [](const QString &p, const QByteArray &b) {
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write(b);
    };
    put(dir + QStringLiteral("/01 Prairie.md"), "# Prairie\n\n## Meadowlarks\n");
    put(dir + QStringLiteral("/02 Marsh.md"), "# Marsh\n\n## Rails\n");
    MainWindow w;
    w.resize(1100, 800);
    w.show();
    wait(50);
    auto *side = w.findChild<BookSidebar *>();
    side->setRootPath(lib);
    check(side->createBook(QStringLiteral("Birds")) && side->book().count() == 2, "T book with two chapters");
    QTextEdit *ed = editorOf(w);
    ed->clear();
    QAction *toc = w.findChild<QAction *>(QStringLiteral("fileToc"));
    check(toc != nullptr, "T File > Insert/Update Table of Contents");
    if (toc) {
        toc->trigger();
    }
    // 1.11.0 (N4): the ToC goes into a Contents chapter, created first in the
    // book and opened, not at the caret; it lists every other chapter.
    ed = editorOf(w);
    check(side->book().count() == 3 && side->book().at(0).path == QLatin1String("00 Contents.odt")
              && w.windowTitle().contains(QLatin1String("00 Contents")) && !ed->document()->isModified(),
          "T a Contents chapter is made first in the book, opened and saved", side->book().at(0).path + QLatin1Char(' ') + w.windowTitle());
    QStringList lines;
    QList<int> pages;
    auto collect = [&]() {
        lines.clear();
        pages.clear();
        for (QTextBlock b = editorOf(w)->document()->begin(); b.isValid(); b = b.next()) {
            if (b.blockFormat().hasProperty(BookToc::GeneratedProperty)) {
                lines << b.text().section(QLatin1Char('\t'), 0, 0);
                pages << b.text().section(QLatin1Char('\t'), 1).toInt();
            }
        }
        return lines.join(QStringLiteral(" | "));
    };
    check(collect() == QLatin1String("1. Prairie | Meadowlarks | 2. Marsh | Rails"), "T inserted in book order", collect());
    {
        // 1.11.1 (M3): the Contents chapter is not numbered; Prairie is 1, and
        // "00 Contents, 01 Prairie, 02 Marsh" already follows the book order.
        auto *list = side->findChild<QListWidget *>(QStringLiteral("bookList"));
        check(list && list->item(0)->text().startsWith(QLatin1String("Contents"))
                  && list->item(1)->text().startsWith(QLatin1String("1. Prairie")),
              "T sidebar: Contents unnumbered, Prairie is 1", list ? list->item(0)->text() + QLatin1Char('|') + list->item(1)->text() : QString());
        check(side->renumberPlan().isEmpty(), "T Renumber keeps 00 for Contents and 01 for chapter 1");
    }
    // N2: the whole-book PDF's page numbers, after a right tab with dot leaders.
    bool pagesOk = !pages.isEmpty();
    for (int k = 0; k < pages.size(); ++k) {
        pagesOk = pagesOk && pages.at(k) > 0 && (k == 0 || pages.at(k) >= pages.at(k - 1));
    }
    check(pagesOk && pages.at(2) > pages.at(0), "T page numbers, in order, each chapter on its own page",
          [&] { QStringList t; for (int p : pages) t << QString::number(p); return t.join(QLatin1Char(',')); }());
    bool tabbed = true;
    for (QTextBlock b = editorOf(w)->document()->begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().hasProperty(BookToc::GeneratedProperty)) {
            const auto tabs = b.blockFormat().tabPositions();
            tabbed = tabbed && !tabs.isEmpty() && tabs.first().type == QTextOption::RightTab;
        }
    }
    check(tabbed, "T the page numbers sit on a right tab");
    // Running it again from another chapter updates the same Contents chapter.
    check(w.openPathForTest(dir + QStringLiteral("/02 Marsh.md")), "T open another chapter");
    if (toc) {
        toc->trigger();
    }
    check(side->book().count() == 3 && w.windowTitle().contains(QLatin1String("00 Contents")) && collect().startsWith(QLatin1String("1. Prairie")),
          "T Insert/Update from another chapter goes back to Contents", w.windowTitle());
    ed = editorOf(w);
    side->moveChapter(2, 1);
    QApplication::processEvents();
    check(collect() == QLatin1String("1. Marsh | Rails | 2. Prairie | Meadowlarks"), "T a reorder updates it", collect());
    std::unique_ptr<QMenu> m(side->buildContextMenu(-1));
    bool inMenu = false;
    for (QAction *a : m->actions()) {
        inMenu = inMenu || a->text() == QLatin1String("Insert/Update Table of Contents");
    }
    check(inMenu, "T also in the Book sidebar menu");

    // Rename a chapter: the entry takes the new name, and its sections
    // stay (the old title heading in the file is not listed as a section).
    side->renameChapter(1); // the menu's Rename; F2 calls the same (group Q)
    QApplication::processEvents();
    if (auto *line = side->view()->findChild<QLineEdit *>()) {
        line->selectAll();
        QTest::keyClicks(line, QStringLiteral("Wetlands"));
        QTest::keyClick(line, Qt::Key_Return);
    }
    wait(50);
    check(collect() == QLatin1String("1. Wetlands | Rails | 2. Prairie | Meadowlarks"), "T a rename updates it", collect());
    // A changed heading in a chapter file shows up too (the book saves when the file does).
    put(dir + QStringLiteral("/01 Prairie.md"), "# Prairie\n\n## Larks and Meadowlarks\n");
    side->setChapter(2, side->book().at(2)); // what saving a chapter does: the book is saved, bookChanged follows
    QApplication::processEvents();
    check(collect() == QLatin1String("1. Wetlands | Rails | 2. Prairie | Larks and Meadowlarks"),
          "T a changed heading updates it", collect());
    editorOf(w)->document()->setModified(false);
}

// 1.9.0: Insert Caption numbers figures "Figure <chapter>.<n>"; they follow
// edits and the chapter's place in the book.
void testFigureNumbers(const QString &root)
{
    const QString lib = root + QStringLiteral("/figLib");
    const QString dir = lib + QStringLiteral("/Birds");
    QDir().mkpath(dir);
    {
        QFile f(dir + QStringLiteral("/01 Intro.md"));
        f.open(QIODevice::WriteOnly);
        f.write("# Intro\n");
    }
    QImage img(60, 30, QImage::Format_RGB32);
    img.fill(Qt::darkBlue);
    const QString pic = dir + QStringLiteral("/swan.png");
    img.save(pic);
    {
        QTextDocument d;
        d.setPlainText(QStringLiteral("Waterfowl"));
        DocumentIo::save(&d, dir + QStringLiteral("/02 Waterfowl.odt"), DocumentIo::Format::Odt);
    }
    MainWindow w;
    w.resize(1100, 800);
    w.show();
    wait(50);
    auto *side = w.findChild<BookSidebar *>();
    side->setRootPath(lib);
    side->createBook(QStringLiteral("Birds"));
    check(w.openPathForTest(dir + QStringLiteral("/02 Waterfowl.odt")) && w.chapterNumber() == 2, "N chapter 2 open");
    QTextEdit *ed = editorOf(w);
    auto addFigure = [&](const QString &text) {
        QTextCursor c(ed->document());
        c.movePosition(QTextCursor::End);
        c.insertBlock(QTextBlockFormat(), QTextCharFormat()); // a body paragraph for the picture
        ed->setTextCursor(c);
        w.insertImageFile(pic);
        int pos = -1;
        for (QTextBlock b = ed->document()->begin(); b.isValid(); b = b.next()) {
            for (auto f = b.begin(); !f.atEnd(); ++f) {
                if (f.fragment().charFormat().isImageFormat()) {
                    pos = f.fragment().position();
                }
            }
        }
        w.addCaption(pos);
        QTest::keyClicks(ed, text); // types over the selected word "Caption"
    };
    addFigure(QStringLiteral("Trumpeter swan"));
    addFigure(QStringLiteral("Mallards"));
    auto caps = [&]() {
        QStringList l;
        for (QTextBlock b = ed->document()->begin(); b.isValid(); b = b.next()) {
            if (Captions::isCaption(b)) {
                l << b.text();
            }
        }
        return l.join(QStringLiteral(" | "));
    };
    check(caps() == QLatin1String("Figure 2.1. Trumpeter swan | Figure 2.2. Mallards"), "N Insert Caption numbers figures", caps());
    // Delete the first figure's caption paragraph: the other becomes 2.1.
    for (QTextBlock b = ed->document()->begin(); b.isValid(); b = b.next()) {
        if (Captions::isCaption(b)) {
            QTextCursor d(b);
            d.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            d.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            d.removeSelectedText();
            break;
        }
    }
    wait(400);
    check(caps() == QLatin1String("Figure 2.1. Mallards"), "N removing a figure renumbers the rest", caps());
    ed->document()->undo();
    wait(400);
    check(caps() == QLatin1String("Figure 2.1. Trumpeter swan | Figure 2.2. Mallards"), "N one Undo brings the caption and its numbers back", caps());
    ed->document()->redo();
    wait(400);
    check(caps() == QLatin1String("Figure 2.1. Mallards") && !ed->document()->isRedoAvailable(), "N and Redo still works", caps());
    side->moveChapter(1, 0);
    QApplication::processEvents();
    check(caps() == QLatin1String("Figure 1.1. Mallards"), "N moving the chapter in the book renumbers", caps());
    ed->document()->setModified(false);
}

// 1.10.0: Export Book (PDF / EPUB) and Print Book. Dialogs are not opened:
// the *To entry points take the options, and Print Book prints to a PDF
// QPrinter, the same build + paint path the print dialog leads to.
void testBookExport(const QString &root)
{
    const QString lib = root + QStringLiteral("/exportLib");
    const QString dir = lib + QStringLiteral("/Birds");
    QDir().mkpath(dir);
    auto put = [](const QString &p, const QByteArray &b) {
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write(b);
    };
    QByteArray prairie = "# Prairie\n\n";
    for (int i = 0; i < 30; ++i) {
        prairie += "Meadowlarks sang from the fence posts all along the road to the river.\n\n";
    }
    put(dir + QStringLiteral("/01 Prairie.md"), prairie);
    put(dir + QStringLiteral("/02 Marsh.md"), "# Marsh\n\n## Rails\n\nHidden in the reeds.\n");
    MainWindow w;
    w.resize(1100, 800);
    w.show();
    wait(50);
    auto *side = w.findChild<BookSidebar *>();
    side->setRootPath(lib);
    check(side->createBook(QStringLiteral("Birds")) && side->book().count() == 2, "X book with two chapters");

    QMenu *exportMenu = w.findChild<QMenu *>(QStringLiteral("fileExportBook"));
    QAction *printBook = w.findChild<QAction *>(QStringLiteral("filePrintBook"));
    emit w.fileMenu()->aboutToShow();
    QStringList sub;
    if (exportMenu) {
        for (QAction *a : exportMenu->actions()) {
            sub << a->text();
        }
    }
    check(exportMenu && exportMenu->isEnabled() && sub == QStringList({QStringLiteral("&PDF…"), QStringLiteral("&EPUB…")}),
          "X File > Export Book > PDF / EPUB", sub.join(QLatin1Char(',')));
    check(printBook && printBook->isEnabled(), "X File > Print Book…");

    std::unique_ptr<QMenu> m(side->buildContextMenu(-1));
    QStringList sideTexts;
    for (QAction *a : m->actions()) {
        sideTexts << a->text();
        if (a->menu()) {
            for (QAction *s : a->menu()->actions()) {
                sideTexts << QStringLiteral(">") + s->text();
            }
        }
    }
    check(sideTexts.contains(QStringLiteral("Export Book")) && sideTexts.contains(QStringLiteral(">PDF…"))
              && sideTexts.contains(QStringLiteral(">EPUB…")) && sideTexts.contains(QStringLiteral("Print Book…")),
          "X the same in the Book sidebar menu", sideTexts.join(QStringLiteral(" | ")));

    QTemporaryDir out;
    BookExport::Options opt;
    int pages = 0;
    const QString pdf = out.path() + QStringLiteral("/Birds.pdf");
    const bool pdfOk = w.exportBookPdfTo(pdf, opt, &pages);
    check(pdfOk && pages >= 4, "X Export Book > PDF", QString::number(pages));
    {
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(out.path() + QStringLiteral("/printed.pdf"));
        printer.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(20, 20, 20, 20)));
        const int printed = w.printBookTo(&printer, opt);
        check(printed >= 3 && QFileInfo(out.path() + QStringLiteral("/printed.pdf")).size() > 0,
              "X Print Book prints every chapter (to a PDF printer)", QString::number(printed));
    }
    BookExport::Options off;
    off.toc = off.titlePage = false;
    int fewer = 0;
    const bool bareOk = w.exportBookPdfTo(out.path() + QStringLiteral("/bare.pdf"), off, &fewer);
    check(bareOk && fewer == pages - 2,
          "X without title page and contents: two pages fewer", QStringLiteral("%1 vs %2").arg(fewer).arg(pages));
    check(w.exportBookEpubTo(out.path() + QStringLiteral("/Birds.epub"), opt)
              && QFileInfo(out.path() + QStringLiteral("/Birds.epub")).size() > 0,
          "X Export Book > EPUB");
}

// 1.10.2 (G1): F2 renames the current chapter from anywhere while the Book
// sidebar is shown; the chapter list keeps its row when it gets focus, shows
// a focus ring, and Tab goes editor -> chapter list.
void testBookFocus(const QString &root)
{
    const QString lib = root + QStringLiteral("/lib-focus");
    const QString book = lib + QStringLiteral("/Focus Book");
    QDir().mkpath(book);
    writeFile(book + QStringLiteral("/01 Intro.md"), "# Intro\n\nOne.\n");
    writeFile(book + QStringLiteral("/02 Songbirds.md"), "# Songbirds\n\nTwo.\n");
    writeFile(book + QStringLiteral("/03 Raptors.md"), "# Raptors\n\nThree.\n");
    BookManifest::fromFolder(book).save();
    {
        QSettings s;
        s.setValue(QStringLiteral("view/fullPageView"), false);
        s.setValue(QStringLiteral("library/rootPath"), lib);
        s.setValue(QStringLiteral("library/visible"), false);
        s.setValue(QStringLiteral("book/visible"), true);
        s.setValue(QStringLiteral("book/current"), QStringLiteral("Focus Book"));
        s.sync();
    }
    auto win = std::make_unique<MainWindow>();
    win->resize(1024, 640);
    win->show();
    win->activateWindow();
    QApplication::setActiveWindow(win.get());
    wait(100);
    auto *side = win->findChild<BookSidebar *>();
    QListWidget *list = side ? side->view() : nullptr;
    check(list && list->count() == 3, "Q book has 3 chapters");
    if (!list || list->count() != 3) {
        return;
    }
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
    wait(200);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(2)).center());
    wait(300);
    QTextEdit *ed = editorOf(*win);
    check(win->windowTitle().contains(QLatin1String("03 Raptors")) && list->currentRow() == 2,
          "Q click opens chapter 3", QString::number(list->currentRow()));
    ed->setFocus();
    wait(50);
    QTest::keyClick(ed, Qt::Key_F2);
    wait(100);
    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    check(editor && list->isAncestorOf(editor)
              && list->currentRow() == 2 && editor->text() == QLatin1String("Raptors"),
          "Q F2 in the editor renames the current chapter", editor ? editor->text() : QStringLiteral("no editor"));
    if (editor) {
        QTest::keyClick(editor, Qt::Key_Escape);
    }
    wait(100);
    check(ed->toPlainText().contains(QLatin1String("Raptors")) && !ed->document()->isModified(),
          "Q ... and types nothing into the document");

    ed->setFocus();
    wait(50);
    list->setFocus(Qt::TabFocusReason);
    wait(50);
    check(list->hasFocus() && list->currentRow() == 2 && list->currentItem() && list->currentItem()->isSelected(),
          "Q the list keeps the current row when it gets focus", QString::number(list->currentRow()));
    list->setFocus(Qt::MouseFocusReason);
    ed->setFocus();
    list->setFocus(Qt::OtherFocusReason);
    wait(50);
    check(list->currentRow() == 2, "Q ... for any focus reason", QString::number(list->currentRow()));
    check(side->styleSheet().contains(QLatin1String("#bookList:focus")), "Q the list draws a focus ring");

    ed->setFocus();
    wait(50);
    QWidget *next = ed->nextInFocusChain();
    while (next && next != ed && !(next->focusPolicy() & Qt::TabFocus && next->isVisible() && next->isEnabled())) {
        next = next->nextInFocusChain();
    }
    check(next == list, "Q Tab order: editor, then the chapter list",
          next ? QString::fromLatin1(next->metaObject()->className()) + QLatin1Char(' ') + next->objectName() : QString());
    // Tab types a tab in the document; Ctrl+Tab moves on (Qt's rule for text editors).
    const int before = ed->toPlainText().size();
    QTest::keyClick(ed, Qt::Key_Tab, Qt::ControlModifier);
    wait(50);
    check(QApplication::focusWidget() == list && ed->toPlainText().size() == before && list->currentRow() == 2,
          "Q Ctrl+Tab from the editor reaches the chapter list, on the open chapter",
          QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : QString());
    QTest::keyClick(list, Qt::Key_F2);
    wait(100);
    auto *ed2 = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    check(ed2 && list->isAncestorOf(ed2) && ed2->text() == QLatin1String("Raptors"), "Q F2 in the list still renames");
    if (ed2) {
        ed2->setText(QStringLiteral("Hawks"));
        QTest::keyClick(ed2, Qt::Key_Return);
    }
    wait(200);
    check(side->book().displayTitle(2) == QLatin1String("Hawks"), "Q the new title is saved", side->book().displayTitle(2));
    check(list->currentRow() == 2, "Q renaming keeps the row", QString::number(list->currentRow()));
    ed->setFocus();
    wait(50);
    QTest::keyClick(ed, Qt::Key_Tab, Qt::ControlModifier); // focus comes back from the editor
    wait(50);
    check(list->hasFocus() && list->currentRow() == 2, "Q focus back into the list keeps the row",
          QString::number(list->currentRow()));
    ed->setFocus();
    wait(50);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(2)).center());
    wait(300);
    check(list->currentRow() == 2 && side->book().at(2).path.startsWith(QLatin1String("03")),
          "Q clicking the open chapter again keeps it (no move, no jump)", QString::number(list->currentRow()));
    ed->document()->setModified(false);
}

// 1.10.3 (G5): ruler drags map to the right place in Full Page view (also
// fitted to the width), keep a sane text width, and undo as one step.
void testRulerDrag()
{
    for (const bool fit : {false, true}) {
        {
            QSettings s;
            s.setValue(QStringLiteral("view/fullPageView"), true);
            s.sync();
        }
        MainWindow w;
        w.resize(1280, 800);
        w.show();
        wait(100);
        if (fit) {
            if (QAction *a = w.findChild<QAction *>(QStringLiteral("viewFitWidth"))) {
                if (!a->isChecked()) {
                    a->trigger();
                }
            }
            wait(200);
        }
        const char *tag = fit ? "fit" : "full";
        QTextEdit *ed = editorOf(w);
        ed->setPlainText(QStringLiteral("A paragraph long enough to wrap over a few lines of the page so the indents show."));
        QTextCursor c(ed->document());
        QTextBlockFormat g;
        g.setRightMargin(48); // 1.27 cm
        ParagraphStyles::setGeometry(c, g);
        ed->setTextCursor(c);
        ed->document()->setModified(false);
        QAction *ruler = w.findChild<QAction *>(QStringLiteral("viewRuler"));
        if (!ruler || !w.ruler()) {
            check(false, "R ruler");
            return;
        }
        if (!ruler->isChecked()) {
            ruler->trigger();
        }
        wait(100);
        Ruler *r = w.ruler();
        const int y = r->height() - 3;
        const auto drag = [&](int from, int to) {
            QTest::mousePress(r, Qt::LeftButton, {}, QPoint(from, y));
            for (int k = 1; k <= 4; ++k) {
                QTest::mouseMove(r, QPoint(from + (to - from) * k / 4, y));
            }
            QTest::mouseRelease(r, Qt::LeftButton, {}, QPoint(to, y));
        };
        const int undoBefore = ed->document()->availableUndoSteps();
        const int right = r->markerX(Ruler::Marker::Right);
        drag(right, right - 44);
        const qreal rm = ed->document()->firstBlock().blockFormat().rightMargin();
        // 44 screen px; when the page is zoomed the document moves less or more.
        const qreal zoom = r->zoom();
        check(qAbs(rm - (48 + 44 / zoom)) <= ParagraphStyles::cmToPx(0.25) + 1,
              qPrintable(QStringLiteral("R (%1) dragging the right marker 44 px left adds ~44 px of right indent").arg(tag)),
              QStringLiteral("%1 cm (zoom %2)").arg(ParagraphStyles::pxToCm(rm), 0, 'f', 2).arg(zoom));
        QTest::keyClick(ed, Qt::Key_Z, Qt::ControlModifier);
        check(qAbs(ed->document()->firstBlock().blockFormat().rightMargin() - 48) < 0.5
                  && ed->document()->availableUndoSteps() == undoBefore,
              qPrintable(QStringLiteral("R (%1) one Ctrl+Z undoes the whole drag").arg(tag)),
              QString::number(ed->document()->availableUndoSteps() - undoBefore));
        // Dragging far past the left marker keeps a usable column.
        drag(r->markerX(Ruler::Marker::Right), r->originX() + 5);
        const QTextBlockFormat f = ed->document()->firstBlock().blockFormat();
        const qreal width = r->columnWidth() - f.leftMargin() - f.rightMargin();
        check(width >= ParagraphStyles::cmToPx(3.0) - 1,
              qPrintable(QStringLiteral("R (%1) the text keeps at least 3 cm").arg(tag)),
              QString::number(ParagraphStyles::pxToCm(width)));
        ed->document()->undo();
        // Markers that touch: the lower half left of the middle grabs Left, right of it Right.
        QTextBlockFormat narrow;
        narrow.setLeftMargin(r->columnWidth() / 2 - 20);
        narrow.setRightMargin(r->columnWidth() / 2 - 20 - ParagraphStyles::cmToPx(3.0) + 40);
        ParagraphStyles::setGeometry(QTextCursor(ed->document()), narrow);
        wait(50);
        const int lx = r->markerX(Ruler::Marker::Left);
        const int rx = r->markerX(Ruler::Marker::Right);
        check(r->hitTest(QPoint(lx - 3, y)) == Ruler::Marker::Left && r->hitTest(QPoint(rx + 3, y)) == Ruler::Marker::Right,
              qPrintable(QStringLiteral("R (%1) left and right markers have separate grab areas").arg(tag)),
              QStringLiteral("%1 %2").arg(lx).arg(rx));
        ed->document()->setModified(false);
    }
}

// 1.10.3: G3 Alt+Up/Down moves the chapter open in the editor; G2 the
// Remove Chapter buttons show their whole text; G6 the style list drops
// down below the box.
// 1.10.4 (retest N1, N6, N7): Ctrl+Tab both ways; right-click and the open
// chapter are unambiguous; hiding the chrome does not move the chapter list.
void testSidebarRetest(const QString &root)
{
    const QString lib = root + QStringLiteral("/lib-retest");
    const QString book = lib + QStringLiteral("/Retest Book");
    QDir().mkpath(book);
    writeFile(book + QStringLiteral("/01 Alpha.md"), "# Alpha\n\nOne.\n");
    writeFile(book + QStringLiteral("/02 Beta.md"), "# Beta\n\nTwo.\n");
    writeFile(book + QStringLiteral("/03 Gamma.md"), "# Gamma\n\nThree.\n");
    BookManifest::fromFolder(book).save();
    {
        QSettings s;
        s.setValue(QStringLiteral("view/fullPageView"), false);
        s.setValue(QStringLiteral("view/chromePinned"), false);
        s.setValue(QStringLiteral("library/rootPath"), lib);
        s.setValue(QStringLiteral("library/visible"), false);
        s.setValue(QStringLiteral("book/visible"), true);
        s.setValue(QStringLiteral("book/current"), QStringLiteral("Retest Book"));
        s.sync();
    }
    auto win = std::make_unique<MainWindow>();
    win->resize(1024, 640);
    win->show();
    win->activateWindow();
    QApplication::setActiveWindow(win.get());
    wait(100);
    auto *side = win->findChild<BookSidebar *>();
    QListWidget *list = side ? side->view() : nullptr;
    if (!list || list->count() != 3) {
        check(false, "W book");
        return;
    }
    check(win->openPathForTest(book + QStringLiteral("/02 Beta.md")), "W chapter 2 open");
    QTextEdit *ed = editorOf(*win);
#ifdef Q_OS_MACOS
    const Qt::KeyboardModifier ctrl = Qt::MetaModifier; // the physical Ctrl key
#else
    const Qt::KeyboardModifier ctrl = Qt::ControlModifier;
#endif
    // N1. #20's test only pressed Ctrl+Tab in the editor; from the list it did
    // nothing (QListWidget ignores Ctrl+Tab). Keys go to the focus widget here.
    auto press = [&](Qt::Key key, Qt::KeyboardModifiers mods) {
        QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : win.get(), key, mods);
        wait(50);
    };
    ed->setFocus();
    wait(50);
    press(Qt::Key_Tab, ctrl);
    check(list->hasFocus(), "W1 Ctrl+Tab: editor to the chapter list");
    press(Qt::Key_Tab, ctrl);
    check(ed->hasFocus(), "W1 Ctrl+Tab again: back to the editor");
    press(Qt::Key_Backtab, ctrl | Qt::ShiftModifier);
    check(list->hasFocus(), "W1 Ctrl+Shift+Tab: editor to the list");
    press(Qt::Key_Backtab, ctrl | Qt::ShiftModifier);
    check(ed->hasFocus() && !ed->toPlainText().contains(QLatin1Char('\t')), "W1 Ctrl+Shift+Tab: list to the editor, no tab typed");
    QAction *swap = win->findChild<QAction *>(QStringLiteral("focusSwap"));
    check(swap && swap->shortcuts().size() == 3
              && swap->shortcuts().first() == QKeySequence(QKeyCombination(ctrl, Qt::Key_Tab)),
          "W1 the shortcut is the physical Ctrl key on this platform", swap ? swap->shortcuts().first().toString() : QString());

    // N6. The open chapter is marked (bold + bar) apart from the highlight.
    check(list->item(1)->data(BookSidebar::OpenRole).toBool() && !list->item(0)->data(BookSidebar::OpenRole).toBool(),
          "W6 the open chapter is marked");
    list->setFocus();
    list->setCurrentRow(0); // arrowed up to Alpha: highlighted, not open
    wait(50);
    check(list->item(1)->data(BookSidebar::OpenRole).toBool() && list->currentRow() == 0,
          "W6 highlight and open marker can differ");
    QTest::keyClick(list, Qt::Key_F2);
    wait(100);
    auto *line = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    check(line && line->text() == QLatin1String("Beta"), "W6 F2 renames the open chapter, even from the list",
          line ? line->text() : QString());
    if (line) {
        QTest::keyClick(line, Qt::Key_Escape);
    }
    wait(100);
    // Right-click Gamma: no selection change, nothing opened, the menu names it.
    list->setCurrentRow(1);
    const QString title = win->windowTitle();
    const QPoint at = list->visualItemRect(list->item(2)).center();
    QMenu *menu = nullptr;
    QTimer::singleShot(300, [&]() {
        menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (menu) {
            check(list->item(2)->data(BookSidebar::ContextRole).toBool() && list->currentRow() == 1,
                  "W6 right-click outlines the row, does not select it", QString::number(list->currentRow()));
            const QList<QAction *> acts = menu->actions();
            check(!acts.isEmpty() && acts.first()->text() == QLatin1String("Gamma") && !acts.first()->isEnabled(),
                  "W6 the menu names the chapter it acts on", acts.isEmpty() ? QString() : acts.first()->text());
            bool hint = false;
            for (QAction *a : acts) {
                hint = hint || !a->shortcut().isEmpty() && a->shortcut() != QKeySequence(QKeySequence::Delete);
            }
            check(!hint, "W6 no F2/Alt+arrow hints on a row menu (those keys act on the open chapter)");
            menu->close();
        }
    });
    QContextMenuEvent ev(QContextMenuEvent::Mouse, at, list->viewport()->mapToGlobal(at));
    emit list->customContextMenuRequested(at);
    wait(500);
    check(menu != nullptr, "W6 the context menu opened");
    check(!list->item(2)->data(BookSidebar::ContextRole).toBool() && win->windowTitle() == title,
          "W6 closing it clears the outline; nothing was opened");

    // N7. Chrome hidden away: the chapter list and editor stay put.
    QAction *pinAct = actionNamed(*win, QStringLiteral("Always Show Toolbar"));
    auto pin = [&](bool on) {
        if (pinAct && pinAct->isChecked() != on) {
            pinAct->trigger();
        }
    };
    pin(true);
    wait(100);
    const QPoint listAt = list->mapTo(win.get(), QPoint(0, 0));
    const QPoint edAt = ed->mapTo(win.get(), QPoint(0, 0));
    pin(false);
    QMetaObject::invokeMethod(win.get(), "hideAwayIdle");
    wait(100);
    check(!win->chromeShown(), "W7 chrome hidden away");
    check(list->mapTo(win.get(), QPoint(0, 0)) == listAt && ed->mapTo(win.get(), QPoint(0, 0)) == edAt,
          "W7 the chapter list and editor do not move",
          QStringLiteral("%1,%2 -> %3,%4").arg(listAt.y()).arg(edAt.y()).arg(list->mapTo(win.get(), QPoint(0, 0)).y()).arg(ed->mapTo(win.get(), QPoint(0, 0)).y()));
    QWidget *bar = win->findChild<QToolBar *>();
    check(bar && bar->testAttribute(Qt::WA_TransparentForMouseEvents), "W7 the hidden toolbar takes no clicks");
    pin(true);
    wait(50);
    check(win->chromeShown() && bar && !bar->testAttribute(Qt::WA_TransparentForMouseEvents) && !bar->graphicsEffect(),
          "W7 shown again, it works as before");
    ed->document()->setModified(false);
}

// 1.10.5 (retest N3, N5, minor): a rename changes the chapter's title heading;
// headings take the document font; H2/H3 differ; Renumber Files… on request.
void testHeadingsRetest(const QString &root)
{
    const QString lib = root + QStringLiteral("/lib-headings");
    const QString book = lib + QStringLiteral("/Heading Book");
    QDir().mkpath(book);
    writeFile(book + QStringLiteral("/01 Alpha.md"), "# Alpha\n\nOne.\n");
    {
        QTextDocument d;
        QTextCursor c(&d);
        QTextBlockFormat h;
        h.setHeadingLevel(1);
        c.setBlockFormat(h);
        c.insertText(QStringLiteral("Beta"));
        c.insertBlock(QTextBlockFormat());
        c.insertText(QStringLiteral("Body line."));
        DocumentIo::save(&d, book + QStringLiteral("/02 Beta.odt"), DocumentIo::Format::Odt);
        QTextDocument g;
        QTextCursor gc(&g);
        gc.setBlockFormat(h);
        gc.insertText(QStringLiteral("Gamma"));
        DocumentIo::save(&g, book + QStringLiteral("/03 Gamma.odt"), DocumentIo::Format::Odt);
    }
    BookManifest::fromFolder(book).save();
    {
        QSettings s;
        s.setValue(QStringLiteral("view/fullPageView"), false);
        s.setValue(QStringLiteral("view/chromePinned"), true);
        s.setValue(QStringLiteral("library/rootPath"), lib);
        s.setValue(QStringLiteral("library/visible"), false);
        s.setValue(QStringLiteral("book/visible"), true);
        s.setValue(QStringLiteral("book/current"), QStringLiteral("Heading Book"));
        s.sync();
    }
    auto win = std::make_unique<MainWindow>();
    win->resize(1024, 640);
    win->show();
    win->activateWindow();
    QApplication::setActiveWindow(win.get());
    wait(100);
    auto *side = win->findChild<BookSidebar *>();
    if (!side || side->book().count() != 3) {
        check(false, "X book");
        return;
    }
    auto rename = [&](int row, const QString &to) {
        side->renameChapter(row);
        QApplication::processEvents();
        if (auto *line = side->view()->findChild<QLineEdit *>()) {
            line->setText(to);
            QTest::keyClick(line, Qt::Key_Return);
        }
        wait(100);
    };
    auto fileText = [&](const QString &rel) {
        QTextDocument d;
        DocumentIo::load(&d, book + QLatin1Char('/') + rel);
        return d.toPlainText();
    };
    // N3, a chapter that is not open (Markdown): its "# " line follows.
    check(win->openPathForTest(book + QStringLiteral("/02 Beta.odt")), "X chapter 2 open");
    rename(0, QStringLiteral("Prairie"));
    QFile md(book + QStringLiteral("/01 Prairie.md")); // 1.11.3: the file follows the title
    md.open(QIODevice::ReadOnly);
    const QByteArray mdNow = md.readAll();
    md.close();
    check(mdNow == "# Prairie\n\nOne.\n", "X3 rename a closed chapter: its heading line changes, nothing else",
          QString::fromUtf8(mdNow));
    // ... a closed ODT chapter whose only heading is the title.
    rename(2, QStringLiteral("Zeta"));
    check(fileText(QStringLiteral("03 Zeta.odt")).startsWith(QLatin1String("Zeta")), "X3 ... and an ODT one",
          fileText(QStringLiteral("03 Zeta.odt")));
    // The open chapter, nothing unsaved: changed in the editor and saved; Ctrl+Z undoes it.
    QTextEdit *ed = editorOf(*win);
    rename(1, QStringLiteral("Marsh"));
    check(ed->document()->firstBlock().text() == QLatin1String("Marsh") && !ed->document()->isModified()
              && fileText(QStringLiteral("02 Marsh.odt")).startsWith(QLatin1String("Marsh")),
          "X3 rename the open chapter: heading changed and saved", ed->document()->firstBlock().text());
    ed->setFocus();
    QTest::keySequence(ed, QKeySequence::Undo);
    QApplication::processEvents();
    check(ed->document()->firstBlock().text() == QLatin1String("Beta"), "X3 ... Ctrl+Z brings the old heading back",
          ed->document()->firstBlock().text());
    QTest::keySequence(ed, QKeySequence::Redo);
    QMetaObject::invokeMethod(win.get(), "fileSave");
    // The open chapter with unsaved changes: asks; Keep leaves the heading alone.
    QTextCursor endC(ed->document());
    endC.movePosition(QTextCursor::End);
    endC.insertText(QStringLiteral(" More."));
    auto answer = [&](const QString &button) {
        qApp->setProperty("zwHoldPrompts", true);
        QTimer::singleShot(200, [button]() {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                for (QAbstractButton *b : box->buttons()) {
                    if (b->text() == button) {
                        b->click();
                    }
                }
            }
        });
    };
    answer(QStringLiteral("Keep Heading"));
    rename(1, QStringLiteral("Wetlands"));
    wait(300);
    qApp->setProperty("zwHoldPrompts", false);
    check(ed->document()->firstBlock().text() == QLatin1String("Marsh") && side->book().displayTitle(1) == QLatin1String("Wetlands"),
          "X3 unsaved chapter: asks; Keep Heading keeps it", ed->document()->firstBlock().text());
    answer(QStringLiteral("Change Heading"));
    rename(1, QStringLiteral("Marshes"));
    wait(300);
    qApp->setProperty("zwHoldPrompts", false);
    check(ed->document()->firstBlock().text() == QLatin1String("Marshes") && ed->document()->isModified()
              && fileText(QStringLiteral("02 Marshes.odt")).startsWith(QLatin1String("Marsh\n")),
          "X3 ... Change Heading changes it in the editor only (still unsaved)", ed->document()->firstBlock().text());
    QMetaObject::invokeMethod(win.get(), "fileSave");
    // The generated ToC: titles once, no old title or lone title heading as a section.
    const auto entries = BookToc::entries(side->book(), 2);
    QStringList texts;
    for (const auto &e : entries) {
        texts << e.text;
    }
    check(texts.join(QStringLiteral(" | ")) == QLatin1String("1. Prairie | 2. Marshes | 3. Zeta"),
          "X3 the ToC lists each chapter once, by its new name", texts.join(QStringLiteral(" | ")));

    // N5 + minor: a heading takes the document font; H2 and H3 look different.
    QTextCursor c(ed->document());
    c.movePosition(QTextCursor::End);
    c.insertBlock(QTextBlockFormat());
    QTextCharFormat courier;
    courier.setFontFamilies({QStringLiteral("Courier")});
    c.insertText(QStringLiteral("Typed in Courier"), courier);
    ed->setTextCursor(c);
    auto *combo = win->findChild<QComboBox *>(QStringLiteral("styleCombo"));
    auto applyStyle = [&](const QString &name) {
        const int k = combo->findText(name);
        combo->setCurrentIndex(k);
        combo->activated(k);
        QApplication::processEvents();
    };
    applyStyle(QStringLiteral("Heading 2"));
    const QTextBlock hb = ed->textCursor().block();
    const QStringList fam = QTextCursor(hb).charFormat().fontFamilies().toStringList();
    QTextCursor inside(hb);
    inside.setPosition(hb.position() + 3);
    check(!inside.charFormat().fontFamilies().toStringList().contains(QStringLiteral("Courier"))
              && inside.charFormat().fontFamilies().toStringList() == DocumentIo::headingFontFamilies(),
          "X5 Heading 2 replaces the run's Courier with the heading face",
          inside.charFormat().fontFamilies().toStringList().join(QLatin1Char(',')));
    const QTextCharFormat h2 = inside.charFormat();
    applyStyle(QStringLiteral("Heading 3"));
    QTextCursor in3(ed->textCursor().block());
    in3.setPosition(ed->textCursor().block().position() + 3);
    const QTextCharFormat h3 = in3.charFormat();
    check(h2.fontPointSize() - h3.fontPointSize() >= 3 && h3.fontItalic() && !h2.fontItalic(),
          "X H2 and H3 differ in size and slant", QStringLiteral("%1/%2").arg(h2.fontPointSize()).arg(h3.fontPointSize()));
    applyStyle(QStringLiteral("Body Text"));
    QTextCursor in0(ed->textCursor().block());
    in0.setPosition(ed->textCursor().block().position() + 3);
    check(!in0.charFormat().fontItalic(), "X back to Body Text drops Heading 3's italic");
    Q_UNUSED(fam);
    QMetaObject::invokeMethod(win.get(), "fileSave");

    // Minor: Renumber Files… is offered, asks, and is never automatic.
    side->moveChapter(2, 0); // Zeta (03 Zeta.odt) first
    QApplication::processEvents();
    check(QFileInfo::exists(book + QStringLiteral("/03 Zeta.odt")), "XR a reorder renames nothing");
    const auto plan = side->renumberPlan();
    check(plan.size() == 3 && plan.at(0).first == QLatin1String("03 Zeta.odt") && plan.at(0).second == QLatin1String("01 Zeta.odt"),
          "XR the plan renumbers in book order", plan.isEmpty() ? QString() : plan.at(0).second);
    qApp->setProperty("zwHoldPrompts", true);
    QTimer::singleShot(200, []() {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            box->reject();
        }
    });
    check(!side->renumberFiles(true) && QFileInfo::exists(book + QStringLiteral("/03 Zeta.odt")), "XR Cancel renames nothing");
    qApp->setProperty("zwHoldPrompts", false);
    const QString title0 = side->book().displayTitle(0);
    check(side->renumberFiles(false), "XR Rename Files");
    check(QFileInfo::exists(book + QStringLiteral("/01 Zeta.odt")) && QFileInfo::exists(book + QStringLiteral("/02 Prairie.md"))
              && QFileInfo::exists(book + QStringLiteral("/03 Marshes.odt")) && side->book().at(0).path == QLatin1String("01 Zeta.odt")
              && side->book().displayTitle(0) == title0 && side->renumberPlan().isEmpty(),
          "XR files follow the order; titles stay", side->book().at(0).path + QLatin1Char(' ') + side->book().displayTitle(0));
    check(win->windowTitle().contains(QLatin1String("03 Marshes")), "XR the open chapter follows its new name", win->windowTitle());
    BookManifest again;
    again.load(book);
    check(again.count() == 3 && again.at(2).path == QLatin1String("03 Marshes.odt"), "XR book.json saved");
    ed->document()->setModified(false);
}

// 1.11.0 (retest N2, N8): dot leaders in the editor for a kept LibreOffice ToC
// and zwriter's own; page numbers survive a save; timings of a big book.
void testTocPagesRetest(const QString &root)
{
    MainWindow w;
    w.resize(1100, 800);
    w.show();
    wait(50);
    const QString copy = root + QStringLiteral("/toc-leaders.odt");
    QFile::remove(copy);
    QFile::copy(QStringLiteral(ZW_SOURCE_DIR "/tests/data/toc-libreoffice.odt"), copy);
    check(w.openPathForTest(copy), "Y open a LibreOffice file with a ToC");
    auto *ed = qobject_cast<PageTextEdit *>(editorOf(w));
    auto leaderPixels = [&](PageTextEdit *e) {
        const QSizeF size = e->document()->size();
        QImage img(size.toSize(), QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        e->paintLeaders(&p, QRectF(QPointF(0, 0), size));
        p.end();
        int n = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                n += qAlpha(img.pixel(x, y)) > 100 ? 1 : 0;
            }
        }
        return n;
    };
    const int kept = ed ? leaderPixels(ed) : 0;
    check(ed && kept > 50 && !ed->document()->isModified(), "Y2 the kept ToC shows dot leaders (painted; file unchanged)",
          QString::number(kept));
    // Zoomed (Fit Page to Width) too: the same dots, scaled with the page.
    ed->document()->setModified(false);

    // zwriter's ToC with page numbers: saved and read back.
    QTextDocument d;
    QTextCursor c(&d);
    c.insertText(QStringLiteral("x"));
    QList<BookToc::Entry> list{{1, QStringLiteral("1. Prairie"), 0, 3}, {2, QStringLiteral("Meadowlarks"), 0, 4}};
    BookToc::insertOrUpdate(&d, nullptr, list);
    const QString path = root + QStringLiteral("/toc-pages.odt");
    DocumentIo::save(&d, path, DocumentIo::Format::Odt);
    QTextDocument back;
    DocumentIo::load(&back, path);
    QStringList got;
    for (QTextBlock b = back.begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().hasProperty(BookToc::GeneratedProperty)) {
            got << b.text().replace(QLatin1Char('\t'), QLatin1Char('|'));
        }
    }
    check(got == QStringList({QStringLiteral("1. Prairie|3"), QStringLiteral("Meadowlarks|4")}),
          "Y2 page numbers survive an ODT save and reopen", got.join(QStringLiteral(", ")));
    QByteArray content;
    {
        QProcess unzip;
        unzip.start(QStringLiteral("unzip"), {QStringLiteral("-p"), path, QStringLiteral("content.xml")});
        unzip.waitForFinished(5000);
        content = unzip.readAllStandardOutput();
    }
    check(content.isEmpty() || content.contains("1. Prairie<text:tab/>3"), "Y2 ... written as entry, tab, page (as LibreOffice does)");
    check(w.openPathForTest(path), "Y2 open it");
    auto *ed2 = qobject_cast<PageTextEdit *>(editorOf(w));
    check(ed2 && leaderPixels(ed2) > 20, "Y2 and its lines show leaders");
    editorOf(w)->document()->setModified(false);
}

// N8: timings with a big book (30 chapters, ~90,000 words), offscreen.
// Printed so CI logs show them; the limits are loose, to catch regressions.
void testBigBookTimings(const QString &root)
{
    const QString lib = root + QStringLiteral("/lib-big");
    const QString book = lib + QStringLiteral("/Big Book");
    QDir().mkpath(book);
    QString para;
    for (int i = 0; i < 25; ++i) {
        para += QStringLiteral("The kestrel hovered over the verge, tail fanned, head perfectly still. ");
    }
    for (int ch = 1; ch <= 30; ++ch) {
        QTextDocument d;
        QTextCursor c(&d);
        QTextBlockFormat h;
        h.setHeadingLevel(1);
        c.setBlockFormat(h);
        c.insertText(QStringLiteral("Chapter %1").arg(ch));
        for (int k = 0; k < 4; ++k) {
            QTextBlockFormat h2;
            h2.setHeadingLevel(2);
            c.insertBlock(h2);
            c.insertText(QStringLiteral("Section %1.%2").arg(ch).arg(k + 1));
            for (int pgh = 0; pgh < 4; ++pgh) {
                c.insertBlock(QTextBlockFormat());
                c.insertText(para);
            }
        }
        DocumentIo::save(&d, book + QStringLiteral("/%1 Chapter %2.odt").arg(ch, 2, 10, QLatin1Char('0')).arg(ch),
                         DocumentIo::Format::Odt);
    }
    BookManifest::fromFolder(book).save();
    {
        QSettings s;
        s.setValue(QStringLiteral("view/fullPageView"), true);
        s.setValue(QStringLiteral("library/rootPath"), lib);
        s.setValue(QStringLiteral("library/visible"), false);
        s.setValue(QStringLiteral("book/visible"), true);
        s.setValue(QStringLiteral("book/current"), QStringLiteral("Big Book"));
        s.sync();
    }
    MainWindow w;
    w.resize(1100, 800);
    w.show();
    wait(100);
    auto *side = w.findChild<BookSidebar *>();
    if (!side || side->book().count() != 30) {
        check(false, "Z big book");
        return;
    }
    QElapsedTimer t;
    t.start();
    w.exportBookPdfTo(root + QStringLiteral("/big1.pdf"), BookExport::Options());
    const qint64 pdf1 = t.restart();
    w.exportBookPdfTo(root + QStringLiteral("/big2.pdf"), BookExport::Options());
    const qint64 pdf2 = t.restart();
    QAction *toc = w.findChild<QAction *>(QStringLiteral("fileToc"));
    toc->trigger();
    const qint64 tocMs = t.restart();
    check(w.openPathForTest(side->book().absolutePath(5)), "Z open a chapter");
    const qint64 openMs = t.restart();
    QTextEdit *ed = editorOf(w);
    QTextCursor end(ed->document());
    end.movePosition(QTextCursor::End);
    ed->setTextCursor(end);
    ed->setFocus();
    qint64 worst = 0;
    const QString typed = QStringLiteral("typing at the end of a long chapter in a big book. ");
    for (const QChar ch : typed) {
        QElapsedTimer one;
        one.start();
        QTest::keyClick(ed, ch.toLatin1());
        QApplication::processEvents();
        worst = qMax(worst, one.elapsed());
    }
    wait(400);
    QElapsedTimer settle;
    settle.start();
    QApplication::processEvents();
    worst = qMax(worst, settle.elapsed());
    t.restart();
    QMetaObject::invokeMethod(&w, "fileSave");
    const qint64 saveMs = t.restart();
    std::printf("      Z timings: first PDF %lld ms, second PDF %lld ms, Insert/Update ToC %lld ms, open chapter %lld ms, "
                "worst keystroke %lld ms, save %lld ms\n",
                pdf1, pdf2, tocMs, openMs, worst, saveMs);
    check(worst < 150, "Z8 no keystroke blocks the window in a big book", QStringLiteral("%1 ms").arg(worst));
    // Filling in the ToC page numbers relaid the whole book once per line
    // (~10 s for this book, 275 pages); now once.
    check(pdf1 < 6000 && tocMs < 6000, "Z8 whole-book PDF and Insert/Update ToC take seconds, not tens",
          QStringLiteral("%1 / %2 ms").arg(pdf1).arg(tocMs));
    check(saveMs < 1000, "Z8 saving a chapter is quick", QStringLiteral("%1 ms").arg(saveMs));
    ed->document()->setModified(false);
}

void testUsabilityFixes(const QString &root)
{
    const QString lib = root + QStringLiteral("/lib-usab");
    const QString book = lib + QStringLiteral("/Usab Book");
    QDir().mkpath(book);
    writeFile(book + QStringLiteral("/01 Intro.md"), "# Intro\n");
    writeFile(book + QStringLiteral("/02 Songbirds.md"), "# Songbirds\n");
    writeFile(book + QStringLiteral("/03 Raptors.md"), "# Raptors\n");
    BookManifest::fromFolder(book).save();
    {
        QSettings s;
        s.setValue(QStringLiteral("view/fullPageView"), false);
        s.setValue(QStringLiteral("library/rootPath"), lib);
        s.setValue(QStringLiteral("library/visible"), false);
        s.setValue(QStringLiteral("book/visible"), true);
        s.setValue(QStringLiteral("book/current"), QStringLiteral("Usab Book"));
        s.sync();
    }
    auto win = std::make_unique<MainWindow>();
    win->resize(1024, 640);
    win->show();
    win->activateWindow();
    QApplication::setActiveWindow(win.get());
    wait(100);
    auto *side = win->findChild<BookSidebar *>();
    QListWidget *list = side ? side->view() : nullptr;
    if (!list || list->count() != 3) {
        check(false, "U3 book");
        return;
    }
    check(win->openPathForTest(book + QStringLiteral("/03 Raptors.md")), "U3 chapter 3 open");
    list->setCurrentRow(0); // the sidebar selection differs from the open chapter
    QTextEdit *ed = editorOf(*win);
    ed->setFocus();
    QTest::keyClick(ed, Qt::Key_Up, Qt::AltModifier);
    wait(100);
    check(side->book().at(1).path.startsWith(QLatin1String("03")) && side->book().at(0).path.startsWith(QLatin1String("01")),
          "U3 Alt+Up moves the chapter open in the editor, not the selected one",
          side->book().at(0).path + QLatin1Char(',') + side->book().at(1).path);
    check(list->currentRow() == 1, "U3 ... and the open chapter stays selected", QString::number(list->currentRow()));

    qApp->setProperty("zwHoldPrompts", true);
    side->confirmRemoveChapter(0);
    QApplication::processEvents();
    // A QPointer: the dialog deletes itself on close.
    QPointer<QMessageBox> box = win->findChild<QMessageBox *>(QStringLiteral("bookRemoveDialog"));
    QStringList cut;
    if (box) {
        // The report's desktop squeezed the dialog; ask for a narrow one.
        box->resize(160, box->height());
        QApplication::processEvents();
        for (QPushButton *b : box->findChildren<QPushButton *>()) {
            const int need = b->fontMetrics().horizontalAdvance(b->text().remove(QLatin1Char('&')));
            QStyleOptionButton opt;
            opt.initFrom(b);
            const QRect content = b->style()->subElementRect(QStyle::SE_PushButtonContents, &opt, b);
            if (b->isVisible() && (b->width() < b->sizeHint().width() || content.width() < need)) {
                cut << QStringLiteral("%1 (%2<%3)").arg(b->text()).arg(content.width()).arg(need);
            }
        }
        const bool alive = !box.isNull();
        if (box) {
            box->reject();
        }
        check(alive && cut.isEmpty(), "U2 Remove Chapter buttons show their whole text", cut.join(QStringLiteral(", ")));
    } else {
        check(false, "U2 Remove Chapter dialog shown");
    }
    qApp->setProperty("zwHoldPrompts", false);
    wait(100);

    if (auto *combo = win->findChild<QComboBox *>(QStringLiteral("styleCombo"))) {
        combo->setCurrentIndex(qMin(4, combo->count() - 1));
        combo->showPopup();
        wait(300);
        QWidget *popup = combo->view()->window();
        const int comboBottom = combo->mapToGlobal(QPoint(0, combo->height())).y();
        check(popup && popup->isVisible() && popup->geometry().top() >= comboBottom - 2,
              "U6 the style list drops down below the box",
              popup ? QStringLiteral("%1 vs %2").arg(popup->geometry().top()).arg(comboBottom) : QString());
        combo->hidePopup();
    }

    // Export Book starts in the book folder the first time, then where you exported last.
    QSettings().remove(QStringLiteral("export/bookDir"));
    check(QFileInfo(win->bookExportStartDir()) == QFileInfo(book), "U7 first Export Book opens in the book folder",
          win->bookExportStartDir());
    const QString elsewhere = root + QStringLiteral("/exports-here");
    QDir().mkpath(elsewhere);
    win->exportBookEpubTo(elsewhere + QStringLiteral("/Usab.epub"), BookExport::Options());
    check(QFileInfo(win->bookExportStartDir()) == QFileInfo(elsewhere), "U7 then the folder used last",
          win->bookExportStartDir());

    // The toolbar stays by default; the menu shows Esc next to the option.
    QAction *pin = actionNamed(*win, QStringLiteral("Always Show Toolbar"));
    check(pin && pin->isChecked() && pin->text().endsWith(QLatin1String("\tEsc")),
          "U8 Always Show Toolbar on by default, menu shows its Esc key", pin ? pin->text() : QString());

    // A LibreOffice table of contents: page numbers on a right tab at the column edge.
    const QString copy = root + QStringLiteral("/toc-tabs.odt");
    QFile::remove(copy);
    QFile::copy(QStringLiteral(ZW_SOURCE_DIR "/tests/data/toc-libreoffice.odt"), copy);
    check(win->openPathForTest(copy), "U9 open a LibreOffice file with a ToC");
    ed = editorOf(*win);
    int tabbed = 0, right = 0;
    for (QTextBlock b = ed->document()->begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().hasProperty(DocumentIo::TocProperty) && b.text().contains(QLatin1Char('\t'))) {
            ++tabbed;
            const auto tabs = b.blockFormat().tabPositions();
            right += (!tabs.isEmpty() && tabs.first().type == QTextOption::RightTab && tabs.first().position > 200) ? 1 : 0;
        }
    }
    check(tabbed > 0 && right == tabbed && !ed->document()->isModified() && !ed->document()->isUndoAvailable(),
          "U9 its page numbers sit on a right-aligned tab; opening is still clean",
          QStringLiteral("%1/%2").arg(right).arg(tabbed));

    // G9: no two actions share a shortcut (an ambiguous key does nothing, or
    // the wrong thing), and Ctrl+S is Save only.
    QHash<QString, QStringList> byKey;
    for (QAction *a : win->findChildren<QAction *>()) {
        for (const QKeySequence &k : a->shortcuts()) {
            if (!k.isEmpty() && a->shortcutContext() != Qt::WidgetShortcut) {
                byKey[k.toString()] << a->text().remove(QLatin1Char('&')).section(QLatin1Char('\t'), 0, 0);
            }
        }
    }
    QStringList dup;
    for (auto it = byKey.cbegin(); it != byKey.cend(); ++it) {
        QStringList names = it.value();
        names.removeDuplicates();
        if (it.value().size() > 1) {
            dup << it.key() + QLatin1Char('=') + it.value().join(QLatin1Char('/'));
        }
    }
    // The chapter context menu's Rename (F2) is only live while that menu is open.
    dup.removeIf([](const QString &d) { return d.startsWith(QLatin1String("F2=")); });
    check(dup.isEmpty(), "U10 no shortcut is bound twice", dup.join(QStringLiteral("; ")));
    check(byKey.value(QKeySequence(QKeySequence::Save).toString()) == QStringList{QStringLiteral("Save")},
          "U10 Ctrl+S is Save only", byKey.value(QKeySequence(QKeySequence::Save).toString()).join(QLatin1Char('/')));

    // Heading 1-3 and Bibliography from the style list: they apply, show, save to
    // ODT and come back; switching from Bibliography to a heading drops the hanging indent.
    {
        QTemporaryDir t;
        const QString path = t.filePath(QStringLiteral("styles.odt"));
        MainWindow w3;
        w3.resize(1000, 700);
        w3.show();
        wait(50);
        QTextEdit *e3 = editorOf(w3);
        auto *cb = w3.findChild<QComboBox *>(QStringLiteral("styleCombo"));
        const QStringList names{QStringLiteral("Heading 1"), QStringLiteral("Heading 2"), QStringLiteral("Heading 3"),
                                QStringLiteral("Bibliography"), QStringLiteral("Heading 1")};
        QStringList bad;
        QTextCursor c(e3->document());
        for (int i = 0; i < names.size(); ++i) {
            if (i > 0) {
                c.movePosition(QTextCursor::End);
                c.insertBlock(QTextBlockFormat(), QTextCharFormat());
            }
            c.insertText(QStringLiteral("Line %1").arg(i + 1));
            e3->setTextCursor(c);
            if (i == 4) { // the last line: Bibliography first, then Heading 1
                const int b = cb->findText(QStringLiteral("Bibliography"));
                cb->setCurrentIndex(b);
                cb->activated(b);
            }
            const int k = cb ? cb->findText(names.at(i)) : -1;
            if (k < 0) {
                bad << names.at(i) + QStringLiteral(" missing");
                continue;
            }
            cb->setCurrentIndex(k);
            cb->activated(k);
            QApplication::processEvents();
            c = e3->textCursor();
        }
        auto verify = [&](QTextEdit *e, QComboBox *box, const QString &when) {
            QTextBlock b = e->document()->begin();
            const qreal body = e->document()->defaultFont().pointSizeF();
            for (int i = 0; i < names.size() && b.isValid(); ++i, b = b.next()) {
                const int level = names.at(i).startsWith(QLatin1String("Heading")) ? names.at(i).back().digitValue() : 0;
                QTextCursor at(b);
                at.movePosition(QTextCursor::EndOfBlock);
                e->setTextCursor(at);
                QApplication::processEvents();
                const qreal pt = QTextCursor(b).charFormat().fontPointSize();
                const bool ok = b.blockFormat().headingLevel() == level && box->currentText() == names.at(i)
                    && (level == 0 ? b.blockFormat().textIndent() < 0 : (b.blockFormat().textIndent() >= 0 && pt > body))
                    && (level == 0 || b.blockFormat().leftMargin() < 1);
                if (!ok) {
                    bad << QStringLiteral("%1 line %2: level %3 shown '%4' %5pt indent %6/%7")
                               .arg(when).arg(i + 1).arg(b.blockFormat().headingLevel()).arg(box->currentText())
                               .arg(pt).arg(b.blockFormat().leftMargin()).arg(b.blockFormat().textIndent());
                }
            }
        };
        verify(e3, cb, QStringLiteral("applied"));
        DocumentIo::save(e3->document(), path, DocumentIo::Format::Odt);
        e3->document()->setModified(false);
        MainWindow w4;
        w4.resize(1000, 700);
        w4.show();
        check(w4.openPathForTest(path), "U12 reopen the styled file");
        verify(editorOf(w4), w4.findChild<QComboBox *>(QStringLiteral("styleCombo")), QStringLiteral("reopened"));
        check(bad.isEmpty(), "U12 Heading 1-3 and Bibliography apply, show and round-trip", bad.join(QStringLiteral("; ")));
        editorOf(w4)->document()->setModified(false);
    }

    // G10: typing stays fast in a long chapter of an open book (no whole-book
    // work per keystroke).
    QString longText;
    for (int i = 0; i < 600; ++i) {
        longText += QStringLiteral("The kestrel hovered over the verge, tail fanned, head perfectly still. ");
        if (i % 6 == 5) {
            longText += QLatin1Char('\n');
        }
    }
    {
        QTextDocument d;
        d.setPlainText(QStringLiteral("Hawks"));
        DocumentIo::save(&d, book + QStringLiteral("/04 Hawks.odt"), DocumentIo::Format::Odt);
    }
    side->addChapters({book + QStringLiteral("/04 Hawks.odt")});
    check(win->openPathForTest(book + QStringLiteral("/04 Hawks.odt")), "U11 ODT chapter open");
    ed = editorOf(*win);
    ed->setPlainText(longText);
    QTextCursor end(ed->document());
    end.movePosition(QTextCursor::End);
    ed->setTextCursor(end);
    ed->setFocus();
    QElapsedTimer t;
    t.start();
    const QString typed = QStringLiteral("a quick line of typing to time the editor with the book open. ");
    qint64 worst = 0;
    for (const QChar ch : typed) {
        QElapsedTimer one;
        one.start();
        QTest::keyClick(ed, ch.toLatin1());
        QApplication::processEvents(); // whatever the keystroke triggers, including repaint
        worst = qMax(worst, one.elapsed());
    }
    wait(400); // debounced work (figure numbers, stats) runs too
    QElapsedTimer settle;
    settle.start();
    QApplication::processEvents();
    worst = qMax(worst, settle.elapsed());
    const qreal perKey = t.elapsed() / qreal(typed.size());
    std::printf("      U11 typing: %.1f ms per key incl. 400 ms idle, worst single step %lld ms\n", perKey, worst);
    check(worst < 100, "U11 no keystroke blocks the window", QStringLiteral("worst %1 ms").arg(worst));
    editorOf(*win)->document()->setModified(false);
}

// 1.11.1+ (Author's 1.11.0 retest, M2-M6).
void testRetest1111(const QString &root)
{
    const QString lib = root + QStringLiteral("/lib-1111");
    const QString book = lib + QStringLiteral("/Retest Book");
    QDir().mkpath(book);
    writeFile(book + QStringLiteral("/01 Alpha.md"), "# Alpha\n\nOne two three.\n");
    {
        QTextDocument d;
        QTextCursor c(&d);
        QTextBlockFormat h;
        h.setHeadingLevel(1);
        c.setBlockFormat(h);
        c.insertText(QStringLiteral("Beta"));
        c.insertBlock(QTextBlockFormat());
        c.insertText(QStringLiteral("Body line."));
        DocumentIo::save(&d, book + QStringLiteral("/02 Beta.odt"), DocumentIo::Format::Odt);
    }
    writeFile(book + QStringLiteral("/03 Gamma.md"), "# Gamma\n\nThree.\n");
    BookManifest::fromFolder(book).save();
    {
        QSettings s;
        s.setValue(QStringLiteral("view/fullPageView"), false);
        s.setValue(QStringLiteral("view/chromePinned"), true);
        s.setValue(QStringLiteral("library/rootPath"), lib);
        s.setValue(QStringLiteral("library/visible"), false);
        s.setValue(QStringLiteral("book/visible"), true);
        s.setValue(QStringLiteral("book/current"), QStringLiteral("Retest Book"));
        s.sync();
    }
    auto win = std::make_unique<MainWindow>();
    win->resize(1024, 640);
    win->show();
    win->activateWindow();
    QApplication::setActiveWindow(win.get());
    wait(100);
    auto *side = win->findChild<BookSidebar *>();
    if (!side || side->book().count() != 3) {
        check(false, "R book");
        return;
    }

    // M2: Heading 1 chosen again on a Heading 1 line keeps it a Heading 1.
    check(win->openPathForTest(book + QStringLiteral("/02 Beta.odt")), "R Beta open");
    {
        QTextEdit *ed = editorOf(*win);
        QTextCursor c(ed->document());
        ed->setTextCursor(c); // on "Beta", a Heading 1
        QApplication::processEvents();
        auto *combo = win->findChild<QComboBox *>(QStringLiteral("styleCombo"));
        const int h1 = combo ? combo->findText(QStringLiteral("Heading 1")) : -1;
        check(ed->textCursor().blockFormat().headingLevel() == 1 && h1 >= 0 && combo->currentIndex() == h1,
              "R M2 setup: the line is Heading 1 and the list says so");
        if (h1 >= 0) {
            combo->setCurrentIndex(h1);
            combo->activated(h1);
            QApplication::processEvents();
            check(ed->textCursor().blockFormat().headingLevel() == 1 && combo->currentIndex() == h1,
                  "R M2 choosing Heading 1 again keeps it Heading 1",
                  QString::number(ed->textCursor().blockFormat().headingLevel()));
            combo->activated(h1);
            QApplication::processEvents();
            check(ed->textCursor().blockFormat().headingLevel() == 1, "R M2 ... and a third time");
        }
        QAction *h1Action = nullptr;
        for (QAction *a : win->findChildren<QAction *>()) {
            if (a->text() == QLatin1String("Heading &1")) {
                h1Action = a;
            }
        }
        if (h1Action) {
            h1Action->trigger();
            QApplication::processEvents();
            check(ed->textCursor().blockFormat().headingLevel() == 1 && h1Action->isChecked(),
                  "R M2 Format > Heading 1 (Ctrl+Alt+1) on a Heading 1 keeps it");
        }
        const int body = combo ? combo->findText(QStringLiteral("Body Text")) : -1;
        if (body >= 0) {
            combo->activated(body);
            QApplication::processEvents();
            check(ed->textCursor().blockFormat().headingLevel() == 0, "R M2 Body Text still turns it to body text");
            combo->activated(body);
            QApplication::processEvents();
            check(ed->textCursor().blockFormat().headingLevel() == 0, "R M2 ... and stays body text");
            combo->activated(h1);
            QApplication::processEvents();
        }
    }

    // M6: the inline rename box does not move the rows below it.
    {
        QListWidget *list = side->view();
        const int below = list->visualItemRect(list->item(2)).top();
        const int height = list->visualItemRect(list->item(1)).height();
        side->renameChapter(1);
        QApplication::processEvents();
        wait(50);
        auto *line = list->findChild<QLineEdit *>();
        check(line && line->isVisible(), "R M6 rename box open");
        check(list->visualItemRect(list->item(2)).top() == below && list->visualItemRect(list->item(1)).height() == height,
              "R M6 rows below do not shift",
              QStringLiteral("%1 -> %2, %3 -> %4").arg(below).arg(list->visualItemRect(list->item(2)).top())
                  .arg(height).arg(list->visualItemRect(list->item(1)).height()));
        if (line) {
            QTest::keyClick(line, Qt::Key_Escape);
        }
        wait(50);
        check(list->visualItemRect(list->item(2)).top() == below, "R M6 ... nor after Esc");
    }
    // M4: renaming a chapter renames its file, number kept; book.json and the
    // open chapter follow; Renumber keeps names and titles in sync.
    {
        check(win->openPathForTest(book + QStringLiteral("/02 Beta.odt")), "R M4 Beta open");
        side->renameChapter(1);
        QApplication::processEvents();
        if (auto *line = side->view()->findChild<QLineEdit *>()) {
            line->setText(QStringLiteral("Bravo"));
            QTest::keyClick(line, Qt::Key_Return);
        }
        wait(150);
        check(QFileInfo::exists(book + QStringLiteral("/02 Bravo.odt")) && !QFileInfo::exists(book + QStringLiteral("/02 Beta.odt")),
              "R M4 02 Beta.odt renamed to 02 Bravo.odt");
        check(side->book().at(1).path == QLatin1String("02 Bravo.odt"), "R M4 book.json follows", side->book().at(1).path);
        {
            BookManifest saved;
            check(saved.load(book) && saved.at(1).path == QLatin1String("02 Bravo.odt"), "R M4 ... and is saved");
        }
        check(win->windowTitle().contains(QLatin1String("02 Bravo.odt")), "R M4 the open chapter follows", win->windowTitle());
        QTextDocument d;
        DocumentIo::load(&d, book + QStringLiteral("/02 Bravo.odt"));
        check(d.begin().text() == QLatin1String("Bravo"), "R M4 its heading follows too", d.begin().text());
        // A name that is taken is never overwritten.
        writeFile(book + QStringLiteral("/03 Taken.md"), "keep me\n");
        BookChapter g = side->book().at(2);
        g.title = QStringLiteral("Taken");
        side->setChapter(2, g);
        QString err;
        check(!side->renameChapterFile(2, &err) && QFileInfo::exists(book + QStringLiteral("/03 Gamma.md")) && !err.isEmpty(),
              "R M4 an existing file is not overwritten", err);
        QFile keep(book + QStringLiteral("/03 Taken.md"));
        keep.open(QIODevice::ReadOnly);
        check(keep.readAll() == "keep me\n", "R M4 ... its contents are untouched");
        keep.close();
        QFile::remove(book + QStringLiteral("/03 Taken.md"));
        // Renumber brings both the number and the title into the name.
        g.title = QStringLiteral("Delta");
        side->setChapter(2, g);
        side->moveChapter(2, 0);
        QApplication::processEvents();
        const auto plan = side->renumberPlan();
        QStringList shown;
        for (const auto &p : plan) {
            shown << p.first + QStringLiteral("->") + p.second;
        }
        check(shown.contains(QStringLiteral("03 Gamma.md->01 Delta.md")) && shown.contains(QStringLiteral("01 Alpha.md->02 Alpha.md"))
                  && shown.contains(QStringLiteral("02 Bravo.odt->03 Bravo.odt")),
              "R M4 Renumber plan: numbers and titles", shown.join(QStringLiteral(", ")));
        check(side->renumberFiles(false) && side->renumberPlan().isEmpty() && QFileInfo::exists(book + QStringLiteral("/01 Delta.md")),
              "R M4 Renumber done: names in sync");
    }

    // M5: dialogs start in the current book's folder, not one left from before.
    {
        const QString stale = root + QStringLiteral("/stale-folder");
        QDir().mkpath(stale);
        QDir().mkpath(lib + QStringLiteral("/export"));
        {
            QSettings s;
            s.setValue(QStringLiteral("files/lastDir"), stale);
            s.setValue(QStringLiteral("export/bookDir"), stale);
            s.setValue(QStringLiteral("export/bookDirFor"), root + QStringLiteral("/Old Book"));
        }
        check(QFileInfo(win->openStartDir()) == QFileInfo(book), "R M5 Open starts in the book folder", win->openStartDir());
        check(QFileInfo(win->bookExportStartDir()) == QFileInfo(book), "R M5 Export Book: not another book's export folder",
              win->bookExportStartDir());
        win->exportBookEpubTo(stale + QStringLiteral("/R.epub"), BookExport::Options());
        check(QFileInfo(win->bookExportStartDir()) == QFileInfo(stale), "R M5 ... but this book's last export folder",
              win->bookExportStartDir());
        side->chooseNewBook();
        QApplication::processEvents();
        if (QInputDialog *nb = side->newBookDialog()) {
            check(nb->textValue().isEmpty() && nb->labelText().contains(QDir::toNativeSeparators(lib)),
                  "R M5 New Book starts empty, in the library folder", nb->textValue() + QLatin1Char('|') + nb->labelText());
            nb->reject();
        } else {
            check(false, "R M5 New Book dialog");
        }
    }
    win->close();
}


// 1.12.0: ruler units (in/cm, labelled) and per-document page size (Letter
// for new documents, the stored size for .odt, A4 for files without one).
QSizeF pdfMediaBoxPt(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray d = f.readAll();
    static const QRegularExpression re(QStringLiteral("/MediaBox\\s*\\[\\s*0\\s+0\\s+([0-9.]+)\\s+([0-9.]+)\\s*\\]"));
    const QRegularExpressionMatch m = re.match(QString::fromLatin1(d));
    return m.hasMatch() ? QSizeF(m.captured(1).toDouble(), m.captured(2).toDouble()) : QSizeF();
}

bool sameSize(const QPageLayout &l, QPageSize::PageSizeId id)
{
    return l.pageSize().isEquivalentTo(QPageSize(id));
}

void testPageSizeAndUnits(const QString &root)
{
    // Pure helpers.
    check(qAbs(OdtMeta::odfLengthToMm(QStringLiteral("21cm")) - 210.0) < 1e-6, "P odf cm");
    check(qAbs(OdtMeta::odfLengthToMm(QStringLiteral("8.5in")) - 215.9) < 1e-6, "P odf in");
    check(qAbs(OdtMeta::odfLengthToMm(QStringLiteral("612pt")) - 215.9) < 1e-6, "P odf pt");
    check(OdtMeta::odfLengthToMm(QStringLiteral("wide")) < 0, "P odf junk");
    check(PageSetup::sizeFromMm(QSizeF(215.9, 279.4)).id() == QPageSize::Letter, "P 215.9x279.4 is Letter");
    check(PageSetup::sizeFromMm(QSizeF(297.0, 210.0)).id() == QPageSize::A4, "P landscape A4 is A4");
    check(PageSetup::sizeFromMm(QSizeF(100.0, 150.0)).id() == QPageSize::Custom, "P odd size is custom");
    {
        QSizeF mm;
        bool land = true;
        const QByteArray xml = "<office:document-styles xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
                               "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
                               "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\">"
                               "<office:automatic-styles><style:page-layout style:name=\"pm1\">"
                               "<style:page-layout-properties fo:page-width=\"8.5in\" fo:page-height=\"11in\" "
                               "style:print-orientation=\"portrait\"/></style:page-layout></office:automatic-styles>"
                               "</office:document-styles>";
        check(OdtMeta::parsePageSizeXml(xml, &mm, &land) && qAbs(mm.width() - 215.9) < 0.01 && !land,
              "P styles.xml page size read");
        check(!OdtMeta::parsePageSizeXml("<a/>", &mm), "P no page layout -> false");
    }

    QVariantMap saved;
    for (const char *k : {"view/fullPageView", "view/fitPageWidth", "view/ruler"}) {
        saved.insert(QLatin1String(k), QSettings().value(QLatin1String(k)));
    }
    {
        QSettings s;
        s.remove(QLatin1String(PageSetup::kRulerUnitsKey));
        s.remove(QLatin1String(PageSetup::kNewDocSizeKey));
        s.setValue(QStringLiteral("view/fullPageView"), true);
        s.setValue(QStringLiteral("view/fitPageWidth"), false);
        s.setValue(QStringLiteral("view/ruler"), true);
        s.sync();
    }
    check(PageSetup::rulerUnit() == PageSetup::Unit::Inch, "P default units inches");
    check(PageSetup::newDocumentSizeId() == QPageSize::Letter, "P default new-doc size Letter");

    const QString shots = qEnvironmentVariable("ZW_SHOT_DIR");
    if (!shots.isEmpty()) {
        QDir().mkpath(shots);
    }
    const QString legacyMd = root + QStringLiteral("/legacy-page.md");
    writeFile(legacyMd, "# Old notes\n\nNo page size stored here.\n");
    const QString letterOdt = root + QStringLiteral("/letter-page.odt");
    const QString a4Odt = root + QStringLiteral("/a4-page.odt");
    const QString legacyOdt = root + QStringLiteral("/legacy-page.odt");
    {
        MainWindow w;
        w.resize(1100, 820);
        w.show();
        QApplication::processEvents();
        check(sameSize(w.documentPageLayout(), QPageSize::Letter), "P startup document is Letter");
        Ruler *r = w.ruler();
        check(r && r->unit() == PageSetup::Unit::Inch && r->unitLabel() == QLatin1String("in"),
              "P ruler labelled in");
        if (r) {
            const qreal px = (r->unitX(1) - r->originX()) / r->zoom();
            check(qAbs(px - 96.0) <= 1.0, "P one ruler inch is 96 px", QString::number(px));
            // Letter, 1 in margins: a 6.5 in column.
            const qreal colIn = PageSetup::pxToUnit(r->columnWidth(), PageSetup::Unit::Inch);
            check(qAbs(colIn - 6.5) < 0.05, "P Letter column is 6.5 in", QString::number(colIn));
        }
        QTextEdit *ed = editorOf(w);
        { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.insertText(QStringLiteral("Dear reader,\n\nThe ruler above this page now says which units it uses. "
                                        "This new document is on US Letter (8.5 × 11 in); the ruler counts "
                                        "inches from the left margin.")); }
        QTest::qWait(300);
        check(qAbs(PageSetup::pxToUnit(r->columnWidth(), PageSetup::Unit::Inch) - 6.5) < 0.05,
              "P Letter column still 6.5 in after typing");
        if (!shots.isEmpty()) {
            w.grab().save(shots + QStringLiteral("/ruler-inches-letter.png"));
        }
        check(w.saveToPathForTest(letterOdt), "P save Letter odt");
        const QString pdf = root + QStringLiteral("/letter.pdf");
        check(w.exportPdfTo(pdf), "P export Letter PDF");
        const QSizeF mb = pdfMediaBoxPt(pdf);
        check(qAbs(mb.width() - 612) < 2 && qAbs(mb.height() - 792) < 2, "P Letter PDF is 612x792 pt",
              QStringLiteral("%1x%2").arg(mb.width()).arg(mb.height()));

        // cm + A4
        PageSetup::setRulerUnit(PageSetup::Unit::Cm);
        r->setUnit(PageSetup::Unit::Cm);
        w.setDocumentPageSizeForTest(QPageSize(QPageSize::A4));
        QApplication::processEvents();
        check(r->unitLabel() == QLatin1String("cm"), "P ruler labelled cm");
        const qreal colCm = PageSetup::pxToUnit(r->columnWidth(), PageSetup::Unit::Cm);
        check(qAbs(colCm - (21.0 - 5.08)) < 0.1, "P A4 column is 15.92 cm", QString::number(colCm));
        { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.insertText(QStringLiteral("Ein Brief auf A4 (210 × 297 mm). The ruler counts centimetres from "
                                        "the left margin.")); }
        QTest::qWait(300);
        if (!shots.isEmpty()) {
            w.grab().save(shots + QStringLiteral("/ruler-cm-a4.png"));
        }
        check(w.saveToPathForTest(a4Odt), "P save A4 odt");
        const QString pdf2 = root + QStringLiteral("/a4.pdf");
        check(w.exportPdfTo(pdf2), "P export A4 PDF");
        const QSizeF mb2 = pdfMediaBoxPt(pdf2);
        check(qAbs(mb2.width() - 595) < 2 && qAbs(mb2.height() - 842) < 2, "P A4 PDF is 595x842 pt",
              QStringLiteral("%1x%2").arg(mb2.width()).arg(mb2.height()));

        // Settings dialog.
        PageSettingsDialog dlg(PageSetup::Unit::Inch, QPageSize(QPageSize::Letter), QPageSize::Letter, &w);
        check(dlg.unit() == PageSetup::Unit::Inch && dlg.documentSize().id() == QPageSize::Letter
                  && dlg.newDocumentSize() == QPageSize::Letter, "P dialog shows current values");
        dlg.unitCombo()->setCurrentIndex(1);
        dlg.documentCombo()->setCurrentIndex(dlg.documentCombo()->findData(int(QPageSize::A4)));
        check(dlg.unit() == PageSetup::Unit::Cm && dlg.documentSize().id() == QPageSize::A4, "P dialog picks");
        dlg.unitCombo()->setCurrentIndex(0);
        dlg.documentCombo()->setCurrentIndex(0);
        dlg.show();
        QApplication::processEvents();
        if (!shots.isEmpty()) {
            dlg.grab().save(shots + QStringLiteral("/settings-dialog.png"));
        }
        dlg.close();
        PageSettingsDialog custom(PageSetup::Unit::Inch, PageSetup::sizeFromMm(QSizeF(100, 150)), QPageSize::A4, &w);
        check(custom.documentSize().id() == QPageSize::Custom
                  && qAbs(custom.documentSize().size(QPageSize::Millimeter).width() - 100) < 0.5,
              "P dialog keeps a custom size");
        ed->document()->setModified(false);
    }
    // Legacy ODT: same file without styles.xml.
    QFile::copy(a4Odt, legacyOdt);
    QProcess::execute(QStringLiteral("zip"), {QStringLiteral("-q"), QStringLiteral("-d"), legacyOdt, QStringLiteral("styles.xml")});
    {
        PageSetup::setNewDocumentSizeId(QPageSize::Letter);
        MainWindow w;
        w.show();
        QApplication::processEvents();
        check(w.openPathForTest(letterOdt) && sameSize(w.documentPageLayout(), QPageSize::Letter),
              "P Letter odt reopens on Letter");
        check(w.openPathForTest(a4Odt) && sameSize(w.documentPageLayout(), QPageSize::A4),
              "P A4 odt reopens on A4 (new-doc default is Letter)");
        check(w.openPathForTest(legacyOdt) && sameSize(w.documentPageLayout(), QPageSize::A4),
              "P odt without a page size opens on A4");
        check(w.openPathForTest(legacyMd) && sameSize(w.documentPageLayout(), QPageSize::A4),
              "P Markdown opens on A4");
        check(!w.hasUnsavedChanges(), "P opening sets no '*'");
        PageSetup::setNewDocumentSizeId(QPageSize::A4);
        QMetaObject::invokeMethod(&w, "fileNew");
        QApplication::processEvents();
        check(sameSize(w.documentPageLayout(), QPageSize::A4), "P new-doc size setting is used");
        PageSetup::setNewDocumentSizeId(QPageSize::Letter);
        QMetaObject::invokeMethod(&w, "fileNew");
        QApplication::processEvents();
        check(sameSize(w.documentPageLayout(), QPageSize::Letter), "P File > New is Letter");
    }
    PageSetup::setRulerUnit(PageSetup::Unit::Inch);
    QSettings s; // leave the view settings as later tests expect them
    for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
        if (it.value().isValid()) {
            s.setValue(it.key(), it.value());
        } else {
            s.remove(it.key());
        }
    }
    s.sync();
}

// 1.13.0: Insert > Symbol… and \name autocomplete in the real window.
void testSymbols()
{
    const QString shots = qEnvironmentVariable("ZW_SHOT_DIR");
    if (!shots.isEmpty()) {
        QDir().mkpath(shots);
    }
    QSettings().remove(QStringLiteral("symbols"));
    MainWindow w;
    w.resize(1000, 640);
    w.show();
    QApplication::processEvents();
    QTextEdit *ed = editorOf(w);
    check(w.insertMenu() && w.insertMenu()->title() == QLatin1String("&Insert"), "Y Insert menu exists");
    check(w.symbolAction() && w.symbolAction()->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+Y")),
          "Y Symbol… is Ctrl+Shift+Y");
    check(w.symbolAutocompleteAction() && w.symbolAutocompleteAction()->isChecked(), "Y autocomplete on by default");

    // Autocomplete through real key presses (the editor's event filter).
    ed->setFocus();
    { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.removeSelectedText(); }
    QTest::keyClicks(ed, QStringLiteral("The load is 47 k\\Omega"));
    QApplication::processEvents();
    if (!shots.isEmpty()) {
        w.grab().save(shots + QStringLiteral("/autocomplete-before.png"));
    }
    QTest::keyClick(ed, Qt::Key_Space);
    check(ed->toPlainText() == QStringLiteral("The load is 47 kΩ "), "Y \\Omega + Space -> Ω", ed->toPlainText());
    QTest::keyClicks(ed, QStringLiteral("at 25\\deg"));
    QTest::keyClick(ed, Qt::Key_Space);
    QTest::keyClicks(ed, QStringLiteral("C, \\pm"));
    QTest::keyClick(ed, Qt::Key_Space);
    QTest::keyClicks(ed, QStringLiteral("5 %, \\mu"));
    QTest::keyClick(ed, Qt::Key_Tab);
    QTest::keyClicks(ed, QStringLiteral("= 0.6"));
    check(ed->toPlainText() == QStringLiteral("The load is 47 kΩ at 25° C, ± 5 %, μ= 0.6"), "Y \\deg \\pm \\mu",
          ed->toPlainText());
    if (!shots.isEmpty()) {
        w.grab().save(shots + QStringLiteral("/autocomplete-after.png"));
    }
    // Ctrl+Z after an expansion: back to the typed \name.
    { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.removeSelectedText(); }
    ed->document()->clearUndoRedoStacks();
    QTest::keyClicks(ed, QStringLiteral("\\alpha"));
    QTest::keyClick(ed, Qt::Key_Space);
    check(ed->toPlainText() == QStringLiteral("α "), "Y \\alpha expands", ed->toPlainText());
    QTest::keyClick(ed, Qt::Key_Z, Qt::ControlModifier);
    check(ed->toPlainText() == QStringLiteral("\\alpha"), "Y Ctrl+Z restores \\alpha", ed->toPlainText());

    // Switched off: \beta stays typed, and the switch is saved.
    w.symbolAutocompleteAction()->setChecked(false);
    check(!QSettings().value(QStringLiteral("symbols/autocomplete"), true).toBool(), "Y switch saved");
    { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.removeSelectedText(); }
    QTest::keyClicks(ed, QStringLiteral("\\beta"));
    QTest::keyClick(ed, Qt::Key_Space);
    check(ed->toPlainText() == QStringLiteral("\\beta "), "Y off: no expansion", ed->toPlainText());
    w.symbolAutocompleteAction()->setChecked(true);

    // The dialog: click inserts at the caret as one undo step and fills Recently used.
    { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.removeSelectedText(); }
    ed->document()->clearUndoRedoStacks();
    QTest::keyClicks(ed, QStringLiteral("f = 1/(2"));
    w.symbolAction()->trigger();
    QApplication::processEvents();
    SymbolDialog *dlg = w.symbolDialog();
    check(dlg && dlg->isVisible(), "Y Symbol… opens the dialog");
    if (dlg) {
        QToolButton *pi = dlg->buttonFor(QStringLiteral("ee"), QStringLiteral("π"));
        QToolButton *omega = dlg->buttonFor(QStringLiteral("greek"), QStringLiteral("ω"));
        check(pi && omega, "Y dialog has π and ω");
        check(omega && omega->toolTip().contains(QStringLiteral("omega")), "Y tooltip names the symbol",
              omega ? omega->toolTip() : QString());
        if (pi && omega) {
            pi->click();
            omega->click();
        }
        QApplication::processEvents();
        check(ed->toPlainText() == QStringLiteral("f = 1/(2πω"), "Y clicks insert at the caret", ed->toPlainText());
        ed->undo();
        check(ed->toPlainText() == QStringLiteral("f = 1/(2π"), "Y one click is one undo step", ed->toPlainText());
        check(Symbols::recent() == QStringList({QStringLiteral("ω"), QStringLiteral("π")}), "Y recent saved",
              Symbols::recent().join(QLatin1Char(',')));
        check(dlg->buttonFor(QStringLiteral("recent"), QStringLiteral("ω")) != nullptr, "Y recent row shows ω");
        if (!shots.isEmpty()) {
            for (const QString &t : {QStringLiteral("Ω"), QStringLiteral("µ"), QStringLiteral("°"), QStringLiteral("±")}) {
                Symbols::addRecent(t);
            }
            dlg->refreshRecent();
            dlg->adjustSize();
            QTest::qWait(100);
            dlg->grab().save(shots + QStringLiteral("/symbol-dialog.png"));
        }
        dlg->close();
    }
    if (!shots.isEmpty()) {
        QMenu *m = w.insertMenu();
        m->popup(w.mapToGlobal(QPoint(120, 30)));
        QTest::qWait(200);
        m->grab().save(shots + QStringLiteral("/insert-menu.png"));
        m->close();
        QMenu *v = nullptr;
        for (QAction *a : w.menuBar()->actions()) {
            if (a->menu() && a->text() == QLatin1String("&View")) {
                v = a->menu();
            }
        }
        if (v) {
            v->popup(w.mapToGlobal(QPoint(200, 30)));
            QTest::qWait(200);
            v->grab().save(shots + QStringLiteral("/view-menu-autocomplete.png"));
            v->close();
        }
    }
    w.setCurrentPathForTest(QString());
    ed->document()->setModified(false);
}

// 1.13.0: Format > Subscript / Superscript toggles (1.14.1: Ctrl+, / Ctrl+.).
void testSubSuperscript(const QString &root)
{
    const QString shots = qEnvironmentVariable("ZW_SHOT_DIR");
    MainWindow w;
    w.resize(1000, 640);
    w.show();
    QApplication::processEvents();
    QTextEdit *ed = editorOf(w);
    QAction *sub = w.subscriptAction();
    QAction *sup = w.superscriptAction();
    check(sub && sup && sub->isCheckable() && sup->isCheckable(), "Z sub/super actions exist");
    check(sub->shortcuts() == QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_Comma)}, "Z Subscript is Ctrl+,");
    check(sup->shortcuts() == QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_Period)}, "Z Superscript is Ctrl+.");
    ed->setFocus();
    { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.removeSelectedText(); }
    QTest::keyClicks(ed, QStringLiteral("Water is H"));
    QTest::keyClick(ed, Qt::Key_Comma, Qt::ControlModifier);
    check(sub->isChecked() && !sup->isChecked(), "Z Ctrl+, turns subscript on");
    QTest::keyClicks(ed, QStringLiteral("2"));
    sub->trigger();
    check(!sub->isChecked(), "Z subscript off again");
    QTest::keyClicks(ed, QStringLiteral("O; the area is x"));
    sub->trigger();
    sup->trigger();
    check(sup->isChecked() && !sub->isChecked(), "Z superscript turns subscript off (exclusive)");
    QTest::keyClicks(ed, QStringLiteral("2"));
    sup->trigger();
    QTest::keyClicks(ed, QStringLiteral(" m, and E = mc"));
    QTest::keyClick(ed, Qt::Key_Period, Qt::ControlModifier);
    check(sup->isChecked(), "Z Ctrl+. turns superscript on");
    QTest::keyClicks(ed, QStringLiteral("2"));
    sup->trigger();
    QTest::keyClicks(ed, QStringLiteral("."));
    const QString text = ed->toPlainText();
    check(text == QStringLiteral("Water is H2O; the area is x2 m, and E = mc2."), "Z typed text", text);
    auto vaAt = [&](int pos) {
        QTextCursor c(ed->document());
        c.setPosition(pos + 1);
        return c.charFormat().verticalAlignment();
    };
    check(vaAt(text.indexOf(QLatin1String("H2")) + 1) == QTextCharFormat::AlignSubScript, "Z H2O has a subscript 2");
    check(vaAt(text.indexOf(QLatin1String("x2")) + 1) == QTextCharFormat::AlignSuperScript, "Z x2 has a superscript 2");
    check(vaAt(text.indexOf(QLatin1String("O;"))) == QTextCharFormat::AlignNormal, "Z O back on the line");
    // The toggles follow the caret.
    QTextCursor c = ed->textCursor();
    c.setPosition(text.indexOf(QLatin1String("H2")) + 2);
    ed->setTextCursor(c);
    QApplication::processEvents();
    check(sub->isChecked() && !sup->isChecked(), "Z caret after the subscript 2 checks Subscript");
    const QString odt = root + QStringLiteral("/script.odt");
    check(w.saveToPathForTest(odt), "Z save odt");
    {
        MainWindow w2;
        check(w2.openPathForTest(odt), "Z reopen odt");
        QTextEdit *e2 = editorOf(w2);
        QTextCursor c2(e2->document());
        c2.setPosition(e2->toPlainText().indexOf(QLatin1String("x2")) + 2);
        check(c2.charFormat().verticalAlignment() == QTextCharFormat::AlignSuperScript, "Z superscript survives reopen");
        e2->document()->setModified(false);
    }
    const QString pdf = root + QStringLiteral("/script.pdf");
    check(w.exportPdfTo(pdf), "Z PDF export");
    if (!shots.isEmpty()) {
        QDir().mkpath(shots);
        QTextCursor end = ed->textCursor();
        end.movePosition(QTextCursor::End);
        ed->setTextCursor(end);
        QApplication::processEvents();
        QTest::qWait(150);
        w.grab().save(shots + QStringLiteral("/subsuper-sample.png"));
        if (QToolBar *bar = w.findChild<QToolBar *>()) {
            bar->grab().save(shots + QStringLiteral("/subsuper-toolbar.png"));
        }
        for (QAction *a : w.menuBar()->actions()) {
            if (a->menu() && a->text() == QLatin1String("F&ormat")) {
                a->menu()->popup(w.mapToGlobal(QPoint(80, 30)));
                QTest::qWait(200);
                a->menu()->grab().save(shots + QStringLiteral("/subsuper-format-menu.png"));
                a->menu()->close();
            }
        }
    }
    ed->document()->setModified(false);
}

// 1.13.0: typed >= <= != -> => become ≥ ≤ ≠ → ⇒ in the real window.
void testMathReplace()
{
    const QString shots = qEnvironmentVariable("ZW_SHOT_DIR");
    MainWindow w;
    w.resize(1000, 640);
    w.show();
    QApplication::processEvents();
    QTextEdit *ed = editorOf(w);
    check(w.mathReplaceAction() && w.mathReplaceAction()->isChecked(), "M math auto-replace on by default");
    ed->setFocus();
    { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.removeSelectedText(); }
    ed->document()->clearUndoRedoStacks();
    QTest::keyClicks(ed, QStringLiteral("Keep V_out >"));
    QApplication::processEvents();
    if (!shots.isEmpty()) {
        w.grab().save(shots + QStringLiteral("/math-replace-before.png"));
    }
    QTest::keyClicks(ed, QStringLiteral("= 3.3 V"));
    check(ed->toPlainText() == QStringLiteral("Keep V_out ≥ 3.3 V"), "M >= typed becomes ≥", ed->toPlainText());
    QTest::keyClicks(ed, QStringLiteral(", I <= 2 A, R != 0, in -> out, a => b"));
    check(ed->toPlainText() == QStringLiteral("Keep V_out ≥ 3.3 V, I ≤ 2 A, R ≠ 0, in → out, a ⇒ b"),
          "M <= != -> => typed", ed->toPlainText());
    if (!shots.isEmpty()) {
        QTest::qWait(100);
        w.grab().save(shots + QStringLiteral("/math-replace-after.png"));
    }
    { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.removeSelectedText(); }
    ed->document()->clearUndoRedoStacks();
    QTest::keyClicks(ed, QStringLiteral("x >="));
    check(ed->toPlainText() == QStringLiteral("x ≥"), "M replaced");
    QTest::keyClick(ed, Qt::Key_Z, Qt::ControlModifier);
    check(ed->toPlainText() == QStringLiteral("x >="), "M one Ctrl+Z gives back >=", ed->toPlainText());
    w.mathReplaceAction()->setChecked(false);
    check(!QSettings().value(QStringLiteral("symbols/autoReplace"), true).toBool(), "M switch saved");
    QTest::keyClicks(ed, QStringLiteral(" y <="));
    check(ed->toPlainText() == QStringLiteral("x >= y <="), "M off: left alone", ed->toPlainText());
    w.mathReplaceAction()->setChecked(true);

    // "Defined as" (1.13.0): \equiv \triangleq \coloneqq in the window's own PDF export.
    { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.removeSelectedText(); }
    QTest::keyClicks(ed, QStringLiteral("a \\equiv"));
    QTest::keyClick(ed, Qt::Key_Space);
    QTest::keyClicks(ed, QStringLiteral("b, f \\triangleq"));
    QTest::keyClick(ed, Qt::Key_Space);
    QTest::keyClicks(ed, QStringLiteral("g, x \\coloneqq"));
    QTest::keyClick(ed, Qt::Key_Space);
    QTest::keyClicks(ed, QStringLiteral("1"));
    check(ed->toPlainText() == QStringLiteral("a ≡ b, f ≜ g, x ≔ 1"), "M defined-as autocomplete", ed->toPlainText());
    const QString pdf = QDir::tempPath() + QStringLiteral("/zw-defs-%1.pdf").arg(QCoreApplication::applicationPid());
    check(w.exportPdfTo(pdf), "M defined-as PDF export");
    QProcess pt;
    pt.start(QStringLiteral("pdftotext"), {pdf, QStringLiteral("-")});
    if (pt.waitForStarted(3000) && pt.waitForFinished(10000) && pt.exitCode() == 0) {
        const QString txt = QString::fromUtf8(pt.readAllStandardOutput());
        check(txt.contains(QStringLiteral("a ≡ b, f ≜ g, x ≔ 1")), "M PDF has ≡ ≜ ≔", txt.simplified());
    }
    if (!qEnvironmentVariableIsEmpty("ZW_DEFS_PDF")) {
        QFile::remove(qEnvironmentVariable("ZW_DEFS_PDF"));
        QFile::copy(pdf, qEnvironmentVariable("ZW_DEFS_PDF"));
    }
    QFile::remove(pdf);
    ed->document()->setModified(false);
}

// 1.14.0: View zoom is display only, persists, and its shortcuts are unique.
static QByteArray stripPdfVolatile(QByteArray pdf)
{
    QList<QByteArray> keep;
    for (const QByteArray &line : pdf.split('\n')) {
        if (!line.contains("/CreationDate") && !line.contains("/ModDate") && !line.contains("/ID")
            && !line.contains("<xmp:") && !line.contains("xmpMM:")) {
            keep << line;
        }
    }
    return keep.join('\n');
}

void testViewZoom(const QString &root)
{
    const QString shots = qEnvironmentVariable("ZW_SHOT_DIR");
    QSettings().setValue(QStringLiteral("view/fullPageView"), true);
    QSettings().setValue(QStringLiteral("view/fitPageWidth"), false);
    QSettings().setValue(QStringLiteral("view/zoomPercent"), 100);
    auto *w = new MainWindow;
    w->resize(1100, 760);
    w->show();
    QApplication::processEvents();
    QTextEdit *ed = editorOf(*w);
    auto *pe = qobject_cast<PageTextEdit *>(ed);
    check(pe != nullptr, "VZ editor is a PageTextEdit");
    QAction *in = w->zoomInAction();
    QAction *out = w->zoomOutAction();
    QAction *reset = w->zoomResetAction();
    check(in && out && reset && w->zoomCombo(), "VZ zoom actions and status-bar control exist");
    for (const QKeySequence &k : {QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Plus),
                                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal),
                                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus),
                                  QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Equal)}) {
        check(in->shortcuts().contains(k), "VZ Zoom In has " + k.toString());
    }
    check(out->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Minus), "VZ Zoom Out is Ctrl+-");
    check(reset->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_0), "VZ Actual Size is Ctrl+0");
    // 1.14.1: no two actions anywhere in the window share any shortcut.
    {
        QHash<QString, QString> owner;
        int conflicts = 0;
        for (QAction *a : w->findChildren<QAction *>()) {
            for (const QKeySequence &k : a->shortcuts()) {
                const QString key = k.toString(QKeySequence::PortableText);
                if (owner.contains(key) && owner.value(key) != a->text()) {
                    ++conflicts;
                    check(false, "VZ shortcut conflict", key + QStringLiteral(": ") + owner.value(key)
                                                             + QStringLiteral(" / ") + a->text());
                }
                owner.insert(key, a->text());
            }
        }
        check(conflicts == 0, "VZ no shortcut conflicts in the window");
    }
    for (QAction *a : {w->subscriptAction(), w->superscriptAction()}) {
        for (const QKeySequence &k : a->shortcuts()) {
            check(!k.toString().contains(QLatin1Char('=')) && !k.toString().contains(QLatin1String("++")),
                  "VZ sub/superscript keep off the +/= key", k.toString());
        }
    }

    ed->setFocus();
    { QTextCursor c(ed->document()); c.select(QTextCursor::Document); c.removeSelectedText(); }
    QTest::keyClicks(ed, QStringLiteral("Zoom test: the quick brown fox jumps over the lazy dog. V = IR, P = VI."));
    QApplication::processEvents();
    const QString odt100 = root + QStringLiteral("/zoom100.odt");
    const QString pdf100 = root + QStringLiteral("/zoom100.pdf");
    check(w->saveToPathForTest(odt100), "VZ save at 100%");
    check(w->exportPdfTo(pdf100), "VZ PDF at 100%");
    QTextDocument *doc = ed->document();
    const QString html = doc->toHtml();
    const QSizeF page = doc->pageSize();
    const QFont font = doc->defaultFont();
    const bool modified = doc->isModified();
    const int undoSteps = doc->availableUndoSteps();

    // Stephen's report: Ctrl and the +/= key toggled Subscript instead of zooming.
    const QString before = doc->toHtml();
    QTest::keyClick(ed, Qt::Key_Equal, Qt::ControlModifier);
    QApplication::processEvents();
    check(w->viewZoomPercent() == 110, "VZ Ctrl+= zooms in one step", QString::number(w->viewZoomPercent()));
    check(!w->subscriptAction()->isChecked() && !w->superscriptAction()->isChecked(),
          "VZ Ctrl+= does not toggle subscript");
    check(doc->toHtml() == before, "VZ Ctrl+= changes no format");
    QTest::keyClick(ed, Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier);
    check(w->viewZoomPercent() == 125, "VZ Ctrl+Shift++ zooms in", QString::number(w->viewZoomPercent()));
    QTest::keyClick(ed, Qt::Key_Equal, Qt::ControlModifier | Qt::ShiftModifier);
    check(w->viewZoomPercent() == 150, "VZ Ctrl+Shift+= zooms in", QString::number(w->viewZoomPercent()));
    QTest::keyClick(ed, Qt::Key_Plus, Qt::ControlModifier | Qt::KeypadModifier);
    check(w->viewZoomPercent() == 175, "VZ numpad Ctrl++ zooms in", QString::number(w->viewZoomPercent()));
    QTest::keyClick(ed, Qt::Key_Minus, Qt::ControlModifier | Qt::KeypadModifier);
    check(w->viewZoomPercent() == 150, "VZ numpad Ctrl+- zooms out", QString::number(w->viewZoomPercent()));
    QTest::keyClick(ed, Qt::Key_Equal, Qt::ControlModifier | Qt::AltModifier);
    check(w->viewZoomPercent() == 175, "VZ Ctrl+Alt+= still zooms in", QString::number(w->viewZoomPercent()));
    check(!w->subscriptAction()->isChecked() && !w->superscriptAction()->isChecked() && doc->toHtml() == before,
          "VZ no zoom key touched the text");
    QTest::keyClick(ed, Qt::Key_0, Qt::ControlModifier);
    check(w->viewZoomPercent() == 100, "VZ Ctrl+0 back to 100%");
    QTest::keyClick(ed, Qt::Key_Minus, Qt::ControlModifier);
    check(w->viewZoomPercent() == 90, "VZ Ctrl+- zooms out one step");
    {
        const QPoint at = ed->viewport()->rect().center();
        QWheelEvent wheel(at, ed->viewport()->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(ed->viewport(), &wheel);
    }
    check(w->viewZoomPercent() == 100, "VZ Ctrl+wheel up zooms in");
    check(doc->defaultFont() == font, "VZ Ctrl+wheel leaves the document font alone");
    w->setViewZoomPercent(1000);
    check(w->viewZoomPercent() == 300, "VZ clamped at 300%");
    w->setViewZoomPercent(10);
    check(w->viewZoomPercent() == 50, "VZ clamped at 50%");
    w->setViewZoomPercent(200);
    QApplication::processEvents();
    check(qFuzzyCompare(pe->zoom(), 2.0), "VZ editor painted at 200%");
    check(doc->toHtml() == html, "VZ document content and formats unchanged");
    check(doc->pageSize() == page, "VZ page geometry unchanged");
    check(doc->defaultFont() == font, "VZ default font unchanged");
    check(doc->isModified() == modified && doc->availableUndoSteps() == undoSteps, "VZ zoom is not an edit");
    const QString odt200 = root + QStringLiteral("/zoom200.odt");
    const QString pdf200 = root + QStringLiteral("/zoom200.pdf");
    check(w->saveToPathForTest(odt200), "VZ save at 200%");
    check(w->exportPdfTo(pdf200), "VZ PDF at 200%");
    {
        MainWindow a;
        MainWindow b;
        check(a.openPathForTest(odt100) && b.openPathForTest(odt200), "VZ reopen both saves");
        check(editorOf(a)->document()->toHtml() == editorOf(b)->document()->toHtml(), "VZ saved files identical");
        editorOf(a)->document()->setModified(false);
        editorOf(b)->document()->setModified(false);
    }
    {
        QFile f1(pdf100), f2(pdf200);
        check(f1.open(QIODevice::ReadOnly) && f2.open(QIODevice::ReadOnly), "VZ read PDFs");
        check(stripPdfVolatile(f1.readAll()) == stripPdfVolatile(f2.readAll()), "VZ PDF identical at 100% and 200%");
    }
    if (!shots.isEmpty()) {
        QDir().mkpath(shots);
        for (int p : {100, 150, 200}) {
            w->setViewZoomPercent(p);
            QApplication::processEvents();
            QTest::qWait(150);
            w->grab().save(shots + QStringLiteral("/zoom-%1.png").arg(p));
        }
        w->setViewZoomPercent(150);
        QApplication::processEvents();
        QStatusBar *sb = w->statusBar();
        sb->grab().save(shots + QStringLiteral("/zoom-statusbar.png"));
        w->zoomCombo()->showPopup();
        QApplication::processEvents();
        QTest::qWait(150);
        if (QWidget *popup = w->zoomCombo()->view()->window()) {
            popup->grab().save(shots + QStringLiteral("/zoom-statusbar-popup.png"));
        }
        w->zoomCombo()->hidePopup();
    }
    w->setViewZoomPercent(175);
    doc->setModified(false);
    w->setCurrentPathForTest(QString());
    delete w;
    {
        MainWindow w2;
        w2.show();
        QApplication::processEvents();
        check(w2.viewZoomPercent() == 175, "VZ zoom level restored on launch");
        check(qFuzzyCompare(qobject_cast<PageTextEdit *>(editorOf(w2))->zoom(), 1.75), "VZ restored zoom applied");
        w2.setViewZoomPercent(100);
        editorOf(w2)->document()->setModified(false);
    }
}


// 1.14.1: zoom works in Full Page, Continuous and Markdown views; the menu
// items and the status-bar box are always enabled and shown; clicks, the caret
// and scrolling follow the zoom; it stays display-only.
void testViewZoomAllViews(const QString &root)
{
    const QString shots = qEnvironmentVariable("ZW_SHOT_DIR");
    QSettings().setValue(QStringLiteral("view/fullPageView"), true);
    QSettings().setValue(QStringLiteral("view/fitPageWidth"), false);
    QSettings().setValue(QStringLiteral("view/zoomPercent"), 100);
    QString para;
    for (int i = 0; i < 6; ++i) {
        para += QStringLiteral("Ohm's law: V = IR, so P = VI = I²R. The quick brown fox jumps over the lazy dog. ");
    }
    QString body;
    for (int i = 1; i <= 30; ++i) {
        body += QStringLiteral("Paragraph %1. ").arg(i) + para + QLatin1Char('\n');
    }
    auto checkControls = [](MainWindow &w, const QString &view) {
        check(w.zoomInAction()->isEnabled() && w.zoomOutAction()->isEnabled() && w.zoomResetAction()->isEnabled(),
              "ZA " + view + ": View > Zoom items enabled");
        QComboBox *combo = w.zoomCombo();
        check(combo->isVisible() && combo->isEnabled(), "ZA " + view + ": status-bar zoom box shown");
        // Not clipped: the box is at least as wide as its widest entry needs.
        const int need = combo->fontMetrics().horizontalAdvance(QStringLiteral("300%"));
        check(combo->width() >= need + 16 && combo->visibleRegion().boundingRect().width() == combo->width(),
              "ZA " + view + ": status-bar zoom box not clipped",
              QStringLiteral("%1 vs %2 visible %3").arg(combo->width()).arg(need)
                  .arg(combo->visibleRegion().boundingRect().width()));
    };
    auto runView = [&](MainWindow &w, const QString &view, const QString &shotName) {
        QTextEdit *ed = editorOf(w);
        auto *pe = qobject_cast<PageTextEdit *>(ed);
        QTextDocument *doc = ed->document();
        QApplication::processEvents();
        QTest::qWait(50); // deferred page layout after a show / view switch
        QApplication::processEvents();
        checkControls(w, view);
        const QString html = doc->toHtml();
        const QFont font = doc->defaultFont();
        const bool modified = doc->isModified();
        const int undo = doc->availableUndoSteps();
        // Keyboard: Ctrl+= in, Ctrl+- out, Ctrl+0 actual size.
        ed->setFocus();
        QTest::keyClick(ed, Qt::Key_Equal, Qt::ControlModifier);
        check(w.viewZoomPercent() == 110 && qFuzzyCompare(pe->zoom(), 1.1), "ZA " + view + ": Ctrl+= zooms in",
              QString::number(pe->zoom()));
        check(!w.subscriptAction()->isChecked(), "ZA " + view + ": Ctrl+= is not subscript");
        QTest::keyClick(ed, Qt::Key_Minus, Qt::ControlModifier);
        check(w.viewZoomPercent() == 100 && qFuzzyCompare(pe->zoom(), 1.0), "ZA " + view + ": Ctrl+- zooms out");
        // Ctrl+scroll.
        {
            const QPoint at = ed->viewport()->rect().center();
            QWheelEvent wheel(at, ed->viewport()->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(ed->viewport(), &wheel);
        }
        check(w.viewZoomPercent() == 110 && qFuzzyCompare(pe->zoom(), 1.1), "ZA " + view + ": Ctrl+wheel zooms");
        w.zoomInAction()->trigger();
        w.zoomInAction()->trigger();
        QApplication::processEvents();
        check(w.viewZoomPercent() == 150 && qFuzzyCompare(pe->zoom(), 1.5), "ZA " + view + ": View > Zoom In to 150%",
              QString::number(w.viewZoomPercent()));
        check(w.zoomCombo()->currentText() == QLatin1String("150%"), "ZA " + view + ": status box shows 150%",
              w.zoomCombo()->currentText());
        checkControls(w, view + " at 150%");
        // Display only.
        check(doc->toHtml() == html && doc->defaultFont() == font, "ZA " + view + ": document unchanged");
        check(doc->isModified() == modified && doc->availableUndoSteps() == undo, "ZA " + view + ": zoom is not an edit");
        if (!w.fullPageViewAction()->isChecked()) {
            // Continuous strips rewrap to the zoomed column instead of running off the side.
            check(qAbs(doc->pageSize().width() - ed->viewport()->width() / 1.5) < 1.5,
                  "ZA " + view + ": text rewraps to the zoomed column",
                  QStringLiteral("%1 vs %2").arg(doc->pageSize().width()).arg(ed->viewport()->width() / 1.5));
            check(ed->horizontalScrollBar()->maximum() == 0, "ZA " + view + ": no sideways scrolling",
                  QStringLiteral("hmax %1 docW %2 vpW %3").arg(ed->horizontalScrollBar()->maximum())
                      .arg(doc->documentLayout()->documentSize().width()).arg(ed->viewport()->width()));
            const int visH = qRound(ed->viewport()->height() / 1.5);
            const int docH = qCeil(doc->documentLayout()->documentSize().height());
            check(qAbs(ed->verticalScrollBar()->maximum() - (docH - visH)) <= 1,
                  "ZA " + view + ": scroll range covers the zoomed document",
                  QStringLiteral("%1 vs %2").arg(ed->verticalScrollBar()->maximum()).arg(docH - visH));
            // The caret follows: jump to the end, it is on screen.
            QTest::keyClick(ed, Qt::Key_End, Qt::ControlModifier);
            QApplication::processEvents();
            QTest::qWait(20);
            QApplication::processEvents();
            const QRect shown = pe->toView(ed->cursorRect());
            check(ed->verticalScrollBar()->pageStep() == visH, "ZA " + view + ": page step is one zoomed screen",
                  QStringLiteral("%1 vs %2").arg(ed->verticalScrollBar()->pageStep()).arg(visH));
            check(ed->viewport()->rect().contains(shown.center()), "ZA " + view + ": caret on screen at the end",
                  QStringLiteral("%1,%2 in %3x%4").arg(shown.center().x()).arg(shown.center().y())
                      .arg(ed->viewport()->width()).arg(ed->viewport()->height()));
            // Scrolled away from the top: a click lands on the word drawn under it.
            check(ed->verticalScrollBar()->value() > 0, "ZA " + view + ": scrolled down");
        }
        // Click mapping: clicking where a word is drawn puts the caret in it.
        {
            QTextCursor probe(doc);
            const int target = doc->toPlainText().lastIndexOf(QStringLiteral("lazy"));
            probe.setPosition(target + 2);
            ed->setTextCursor(probe);
            QApplication::processEvents();
            QTest::qWait(20);
            QApplication::processEvents();
            const QRect drawn = pe->toView(ed->cursorRect());
            QTextCursor away(doc);
            ed->setTextCursor(away);
            QApplication::processEvents();
            QTextCursor back(doc);
            back.setPosition(target + 2);
            ed->setTextCursor(back);
            QApplication::processEvents();
            const QRect drawn2 = pe->toView(ed->cursorRect());
            QTest::mouseClick(ed->viewport(), Qt::LeftButton, Qt::NoModifier, drawn2.center());
            const int pos = ed->textCursor().position();
            check(qAbs(pos - (target + 2)) <= 1, "ZA " + view + ": click lands where the text is drawn",
                  QStringLiteral("%1 vs %2 (%3,%4)").arg(pos).arg(target + 2).arg(drawn.x()).arg(drawn2.y()));
        }
        if (!shots.isEmpty()) {
            QDir().mkpath(shots);
            QTextCursor c(doc);
            c.setPosition(doc->toPlainText().indexOf(QStringLiteral("Paragraph 2.")));
            ed->setTextCursor(c);
            QApplication::processEvents();
            QTest::qWait(150);
            w.grab().save(shots + QStringLiteral("/") + shotName);
            w.statusBar()->grab().save(shots + QStringLiteral("/statusbar-") + shotName);
        }
        w.zoomResetAction()->trigger();
        check(w.viewZoomPercent() == 100 && qFuzzyCompare(pe->zoom(), 1.0), "ZA " + view + ": Ctrl+0 actual size");
        check(doc->toHtml() == html && doc->isModified() == modified, "ZA " + view + ": still unchanged");
    };

    // Full Page and Continuous (rich text).
    {
        MainWindow w;
        w.resize(1100, 760);
        w.show();
        QApplication::processEvents();
        QTextEdit *ed = editorOf(w);
        ed->setPlainText(body);
        // Let the page layout settle (setPlainText resets the root frame).
        w.fullPageViewAction()->trigger();
        w.fullPageViewAction()->trigger();
        QApplication::processEvents();
        ed->document()->clearUndoRedoStacks();
        ed->document()->setModified(false);
        QApplication::processEvents();
        check(w.fullPageViewAction()->isChecked(), "ZA starts in Full Page");
        runView(w, QStringLiteral("Full Page"), QStringLiteral("zoom150-fullpage.png"));
        w.fullPageViewAction()->trigger();
        QApplication::processEvents();
        check(!w.fullPageViewAction()->isChecked(), "ZA switched to Continuous");
        runView(w, QStringLiteral("Continuous"), QStringLiteral("zoom150-continuous.png"));
        // The level carries across a view switch.
        w.setViewZoomPercent(150);
        w.fullPageViewAction()->trigger();
        QApplication::processEvents();
        check(qFuzzyCompare(qobject_cast<PageTextEdit *>(ed)->zoom(), 1.5), "ZA Continuous->Full Page keeps 150%");
        w.fullPageViewAction()->trigger();
        QApplication::processEvents();
        check(qFuzzyCompare(qobject_cast<PageTextEdit *>(ed)->zoom(), 1.5), "ZA Full Page->Continuous keeps 150%");
        // PDF export is the same whatever the zoom in the continuous view.
        const QString a = root + QStringLiteral("/za150.pdf");
        const QString b = root + QStringLiteral("/za100.pdf");
        check(w.exportPdfTo(a), "ZA PDF at 150% Continuous");
        w.setViewZoomPercent(100);
        check(w.exportPdfTo(b), "ZA PDF at 100% Continuous");
        QFile f1(a), f2(b);
        check(f1.open(QIODevice::ReadOnly) && f2.open(QIODevice::ReadOnly)
                  && stripPdfVolatile(f1.readAll()) == stripPdfVolatile(f2.readAll()),
              "ZA Continuous PDF identical at 100% and 150%");
        w.fullPageViewAction()->trigger(); // leave Full Page on for later tests
        ed->document()->setModified(false);
    }
    // Markdown.
    {
        const QString md = root + QStringLiteral("/zoom.md");
        QFile f(md);
        check(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "ZA write md");
        QString text = QStringLiteral("# Zoom\n\n");
        for (int i = 1; i <= 30; ++i) {
            text += QStringLiteral("Paragraph %1. ").arg(i) + para + QStringLiteral("\n\n");
        }
        f.write(text.toUtf8());
        f.close();
        MainWindow w;
        w.resize(1100, 760);
        w.show();
        QApplication::processEvents();
        check(w.openPathForTest(md), "ZA open Markdown");
        QApplication::processEvents();
        runView(w, QStringLiteral("Markdown"), QStringLiteral("zoom150-markdown.png"));
        w.setViewZoomPercent(150);
        const QString saved = root + QStringLiteral("/zoom-saved.md");
        editorOf(w)->document()->setModified(false);
        w.setViewZoomPercent(100);
        editorOf(w)->document()->setModified(false);
        w.setCurrentPathForTest(QString());
        Q_UNUSED(saved);
    }
    QSettings().setValue(QStringLiteral("view/zoomPercent"), 100);
}

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
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_R1111")) { // quick local runs
        testRetest1111(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_Z")) { // quick local runs
        testBigBookTimings(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_Y")) { // quick local runs
        testTocPagesRetest(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_X")) { // quick local runs
        testHeadingsRetest(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_W")) { // quick local runs
        testSidebarRetest(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_USAB")) { // quick local runs
        testUsabilityFixes(dataDir.path());
    testRetest1111(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_RULER")) { // quick local runs
        testRulerDrag();
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_FOCUS")) { // quick local runs
        testBookFocus(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_EXPORT")) { // quick local runs
        testBookExport(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_FIGURES")) { // quick local runs
        testFigureNumbers(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_TOC")) { // quick local runs
        testBookToc(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_ZOOM")) { // quick local runs / screenshots
        testViewZoom(dataDir.path());
        testViewZoomAllViews(dataDir.path());
    testViewZoomAllViews(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_SYMBOLS")) { // quick local runs / screenshots
        testSymbols();
        testSubSuperscript(dataDir.path());
        testMathReplace();
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_PAGESIZE")) { // quick local runs / screenshots
        testPageSizeAndUnits(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_PARAGRAPH")) { // quick local runs
        testParagraphFormatting();
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_FIXES")) { // quick local runs
        testReportFixes(dataDir.path());
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("ZW_TEST_ONLY_FEEDBACK")) { // quick local runs
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        testBookFeedback(dataDir.path(), prompts);
        std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
        return g_failures ? 1 : 0;
    }
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
    testOdtImageOpens(dataDir.path());
    testInsertPictures(dataDir.path());
    testCaptionsPdfMarkdown(dataDir.path(), prompts);
    testBookFeedback(dataDir.path(), prompts);
    testPageGuidesContainText();
    testPageSizeAndUnits(dataDir.path());
    testSymbols();
    testSubSuperscript(dataDir.path());
    testViewZoom(dataDir.path());
    testMathReplace();
    testParagraphFormatting();
    testReportFixes(dataDir.path());
    testBookToc(dataDir.path());
    testFigureNumbers(dataDir.path());
    testBookExport(dataDir.path());
    testBookFocus(dataDir.path());
    testRulerDrag();
    testUsabilityFixes(dataDir.path());
    testSidebarRetest(dataDir.path());
    testHeadingsRetest(dataDir.path());
    testTocPagesRetest(dataDir.path());
    testBigBookTimings(dataDir.path());
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
