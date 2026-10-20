// Whole-window tests (ctest, offscreen): the real MainWindow.
//   C  open a document of every supported type WITHOUT editing it: the window
//      is clean (no '*' in the title) after the open settles, and closing it
//      does not ask "Save changes?"
//   E  a real edit makes it dirty and closing asks; Cancel keeps the window
//   S  after Save the window is clean again and closes without asking
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
#include "version.hpp"

#include <QAction>
#include <QApplication>
#include <QBuffer>
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

void testCaptionsPdfMarkdown(const QString &dir)
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
        ed->document()->setModified(false);
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
    testCaptionsPdfMarkdown(dataDir.path());
    testPageGuidesContainText();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
