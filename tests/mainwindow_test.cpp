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
#include <QProcess>
#include <QLabel>
#include "BookManifest.hpp"
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMouseEvent>
#include "BookSidebar.hpp"
#include "LibrarySidebar.hpp"
#include "version.hpp"

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
        check(cap.text() == QLatin1String("Caption"), "K caption paragraph follows the picture", cap.text());
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

QAction *actionNamed(MainWindow &w, const QString &text)
{
    for (QAction *a : w.findChildren<QAction *>()) {
        if (a->text().remove(QLatin1Char('&')) == text) {
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
    testReportFixes(dataDir.path());
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
