// Book manifest + Book sidebar tests (ctest, headless):
//   L  load/save round-trip of book.json: order, every metadata field, unknown
//      keys kept, relative '/' paths, atomic write
//   T  tolerant load: missing manifest, bad JSON (file left untouched), bad
//      fields fall back to defaults, duplicates / empty paths dropped,
//      "chapters" given as plain path strings
//   M  missing chapter files stay in the book, marked missing
//   R  reorder (move, move up/down, drag-drop order) persists to book.json
//   X  remove from book never deletes the file
//   O  chapters outside the book folder: refused when added, invalid when
//      already in book.json (kept, never opened)
//   B  several books in one library (one book.json per subfolder): listed,
//      picked, New Book in an existing or new subfolder, Add to Book
//   S  the sidebar: Create Book from a folder (name order with numbers),
//      Move Up/Down + drag reorder saved, details dialog round-trip, context menu
//   W  word counts
//   V  version
#include "BookChapterDialog.hpp"
#include <QTest>
#include <memory>
#include "BookManifest.hpp"
#include "BookSidebar.hpp"
#include "DocumentIo.hpp"
#include "version.hpp"

#include <QApplication>
#include <QVersionNumber>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QAbstractButton>
#include <QTest>
#include <QMessageBox>
#include <QDialog>
#include <QTextBlock>
#include <QToolButton>
#include <QTextDocument>

#include <cstdio>

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

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

QStringList order(const BookManifest &b)
{
    QStringList out;
    for (const BookChapter &c : b.chapters()) {
        out << c.path;
    }
    return out;
}

QStringList onDisk(const QString &dir)
{
    BookManifest b;
    b.load(dir);
    return order(b);
}

void manifestTests()
{
    QTemporaryDir tmp;
    const QString dir = tmp.path();
    write(dir + "/ch/one.md", "# One\n\nalpha beta gamma\n");
    write(dir + "/ch/two.md", "two words\n");
    write(dir + "/three.txt", "x\n");

    BookManifest b;
    check(!b.load(dir), "L  no book.json: load() is false, no error");
    check(b.count() == 0 && b.bookDir() == QDir::cleanPath(dir), "L  ... empty book for the folder");
    b.setTitle("Test Book");
    check(b.add(dir + "/ch/one.md") == 0, "L  add absolute path");
    check(b.add("ch/two.md") == 1, "L  add relative path");
    check(b.add(dir + "/ch/one.md") == 0 && b.count() == 2, "L  adding twice keeps one entry");
    b.add("three.txt");
    BookChapter c = b.at(0);
    c.title = "Opening";
    c.status = "Revised";
    c.synopsis = "Line one\nline two";
    c.notes = "note";
    c.pov = "Mara";
    c.tags = {"sea", "opening"};
    c.wordTarget = 2500;
    b.setChapter(0, c);
    QString err;
    check(b.save(&err), "L  save()", err);
    const QJsonObject json = QJsonDocument::fromJson(readAll(BookManifest::manifestPath(dir))).object();
    check(json.value("format").toString() == "zwriter-book" && json.value("version").toInt() == 1,
          "L  book.json format + version");
    check(json.value("chapters").toArray().at(0).toObject().value("path").toString() == "ch/one.md",
          "L  paths are stored relative with '/'");

    BookManifest r;
    check(r.load(dir, &err), "L  load() back", err);
    check(r.title() == "Test Book", "L  title round-trips");
    check(order(r) == QStringList({"ch/one.md", "ch/two.md", "three.txt"}), "L  order round-trips");
    check(r.at(0) == c, "L  every chapter field round-trips");
    check(r.at(1).status == "Draft" && r.at(1).wordTarget == 0, "L  defaults for a new chapter");
    check(r.displayTitle(1) == "two", "L  untitled chapter shows the file's base name");

    // Unknown keys survive a load/save.
    QJsonObject withExtra = json;
    withExtra.insert("compile", QJsonObject{{"format", "odt"}});
    QJsonArray arr = withExtra.value("chapters").toArray();
    QJsonObject first = arr.at(0).toObject();
    first.insert("color", "blue");
    arr[0] = first;
    withExtra.insert("chapters", arr);
    write(BookManifest::manifestPath(dir), QJsonDocument(withExtra).toJson());
    r.load(dir);
    r.moveDown(0);
    r.save();
    const QJsonObject after = QJsonDocument::fromJson(readAll(BookManifest::manifestPath(dir))).object();
    check(after.value("compile").toObject().value("format").toString() == "odt",
          "L  unknown book keys are kept");
    check(after.value("chapters").toArray().at(1).toObject().value("color").toString() == "blue",
          "L  unknown chapter keys are kept (and move with their chapter)");

    // R: reorder.
    BookManifest m;
    m.load(dir);
    check(m.moveUp(2) && order(m) == QStringList({"ch/two.md", "three.txt", "ch/one.md"}),
          "R  moveUp");
    check(!m.moveUp(0) && !m.moveDown(2) && !m.move(0, 9), "R  moves past the ends are refused");
    m.reorder({"ch/one.md", "nope.md", "ch/two.md"});
    check(order(m) == QStringList({"ch/one.md", "ch/two.md", "three.txt"}),
          "R  reorder(): unknown ignored, omitted kept at the end");
    m.save();
    check(onDisk(dir) == order(m), "R  new order persists to book.json");

    // X + M.
    check(m.remove(1) && QFile::exists(dir + "/ch/two.md"), "X  remove from book keeps the file");
    m.save();
    check(onDisk(dir) == QStringList({"ch/one.md", "three.txt"}), "X  ... and the entry is gone");
    QFile::remove(dir + "/three.txt");
    BookManifest gone;
    gone.load(dir);
    check(gone.count() == 2 && gone.isMissing(1) && !gone.isMissing(0),
          "M  a missing file stays listed, marked missing");
    check(countWordsInFile(gone.absolutePath(1)) == -1, "M  missing file: word count -1");
}

void tolerantTests()
{
    QTemporaryDir tmp;
    const QString dir = tmp.path();
    const QByteArray bad = "{ not json";
    write(BookManifest::manifestPath(dir), bad);
    BookManifest b;
    QString err;
    check(!b.load(dir, &err) && !err.isEmpty(), "T  bad JSON: load() false with an error", err);
    check(readAll(BookManifest::manifestPath(dir)) == bad, "T  ... and the file is untouched");

    write(BookManifest::manifestPath(dir),
          R"({"title": 7, "chapters": [
                "a.md",
                {"path": "b.md", "status": "Bogus", "wordTarget": -5, "tags": "x, y ,"},
                {"path": ""}, {"title": "no path"}, 42,
                {"path": "./a.md"},
                {"path": "sub\\c.md", "tags": ["", "t"]}
             ]})");
    check(b.load(dir, &err), "T  odd but valid manifest loads", err);
    check(order(b) == QStringList({"a.md", "b.md", "sub/c.md"}),
          "T  string entries kept; empty, pathless, non-object, duplicate dropped; '\\' → '/'",
          order(b).join(","));
    check(b.title().isEmpty(), "T  non-string title → empty");
    check(b.at(1).status == "Bogus" && !BookManifest::isKnownStatus("Bogus") && b.at(1).wordTarget == 0,
          "T  unknown status kept as written (not Draft); bad target → 0", b.at(1).status);
    check(b.at(0).status == "Draft", "T  no status → Draft");
    check(b.at(1).tags == QStringList({"x", "y"}) && b.at(2).tags == QStringList({"t"}),
          "T  tags from a string or array, blanks dropped");
    check(BookManifest::statuses()
              == QStringList({"Idea", "Outline", "Draft", "Revised", "Final"}),
          "T  statuses");
}

void outsideTests()
{
    QTemporaryDir tmp;
    const QString dir = tmp.path() + "/book";
    write(dir + "/in.md", "in\n");
    write(tmp.path() + "/out.md", "out\n");
    write(dir + "/notes.docx", "x");
    BookManifest b;
    b.setBookDir(dir);
    check(b.add(dir + "/in.md") == 0, "O  file inside the book folder is added");
    check(b.add(tmp.path() + "/out.md") == -1, "O  absolute path outside is refused");
    check(b.add("../out.md") == -1 && b.add("sub/../../out.md") == -1, "O  ../ paths are refused");
    check(b.add("/etc/passwd.md") == -1 && b.add("C:/x.md") == -1, "O  root / drive paths refused");
    check(b.add("notes.docx") == -1, "O  only .md / .txt / .odt");
    check(b.count() == 1, "O  ... nothing else was added");

    write(BookManifest::manifestPath(dir),
          R"({"chapters": ["in.md", "../out.md", "/tmp/abs.md", {"path": "a/../../other.md"}]})");
    b.load(dir);
    check(b.count() == 4, "O  existing outside entries are kept in the book", QString::number(b.count()));
    check(!b.isOutside(0) && b.isOutside(1) && b.isOutside(2) && b.isOutside(3),
          "O  ... marked outside");
    check(b.absolutePath(1).isEmpty() && b.isMissing(1), "O  ... no path, treated as missing");
    b.save();
    check(QString::fromUtf8(readAll(BookManifest::manifestPath(dir))).contains("../out.md"),
          "O  ... and not dropped on save");
}

bool writeOdt(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QTextDocument doc;
    doc.setPlainText(text);
    return DocumentIo::save(&doc, path, DocumentIo::Format::Odt);
}

// D: .odt chapters (1.4.1): discovery, add, count via the ODT load path,
// targets, open, missing.
void odtTests()
{
    QTemporaryDir tmp;
    const QString lib = tmp.path() + "/Library";
    const QString dir = lib + "/Mixed";
    check(writeOdt(dir + "/02 Second.odt", "One two three four five.\n\nSix seven."),
          "D  wrote a real .odt (DocumentIo::save)");
    write(dir + "/01 First.md", "# First\n\nalpha beta\n");
    write(dir + "/03 Third.txt", "gamma\n");
    write(dir + "/04 Notes.docx", "x");
    check(countWordsInFile(dir + "/02 Second.odt") == 7, "D  .odt word count from content.xml",
          QString::number(countWordsInFile(dir + "/02 Second.odt")));
    write(dir + "/broken.odt", "not a zip");
    check(countWordsInFile(dir + "/broken.odt") == -1, "D  unreadable .odt: -1");
    QFile::remove(dir + "/broken.odt");

    const BookManifest found = BookManifest::fromFolder(dir);
    check(order(found) == QStringList({"01 First.md", "02 Second.odt", "03 Third.txt"}),
          "D  Create/New Book discovers .odt with .md/.txt in name order", order(found).join(","));

    QMainWindow win;
    auto *side = new BookSidebar(&win);
    win.setCentralWidget(side);
    win.show();
    side->setRootPath(lib);
    check(side->createBook("Mixed") && side->view()->count() == 3, "D  New Book lists the .odt");
    check(side->view()->item(1)->text().contains("Draft · 7 words"), "D  row shows the .odt count",
          side->view()->item(1)->text());
    BookChapter c = side->book().at(1);
    c.wordTarget = 14;
    side->setChapter(1, c);
    check(side->view()->item(1)->text().contains("7 / 14 words"), "D  .odt target", side->view()->item(1)->text());
    side->editChapter(1);
    if (BookChapterDialog *dlg = side->chapterDialog()) {
        check(dlg->progressLabel()->text() == "7 words of 14 (50%)", "D  details dialog .odt progress",
              dlg->progressLabel()->text());
        dlg->reject();
    }
    QStringList opened;
    QObject::connect(side, &BookSidebar::fileActivated, [&opened](const QString &p) { opened << p; });
    emit side->view()->itemClicked(side->view()->item(1));
    check(opened == QStringList({QDir::cleanPath(dir + "/02 Second.odt")}), "D  clicking opens the .odt",
          opened.join(","));

    side->removeChapter(1);
    check(side->addChapters({dir + "/02 Second.odt", dir + "/04 Notes.docx"}) == 1
              && onDisk(dir).last() == "02 Second.odt",
          "D  Add Chapters takes .odt, still refuses other types");
    QFile::remove(dir + "/02 Second.odt");
    side->reload();
    check(side->view()->item(2)->text().contains("Missing: 02 Second.odt"), "D  missing .odt marked",
          side->view()->item(2)->text());
    opened.clear();
    emit side->view()->itemClicked(side->view()->item(2));
    check(opened.isEmpty(), "D  a missing .odt is not opened");
}

void wordTests()
{
    check(countWords("") == 0, "W  empty");
    check(countWords("# Title\n\nOne *two* three — four.\n- five") == 6,
          "W  Markdown markers alone are not words");
}

QStringList chooserText(BookSidebar *side)
{
    QStringList out;
    for (int i = 0; i < side->picker()->count(); ++i) {
        if (!side->picker()->itemData(i).toString().isEmpty()) { // books only (not "New Book…")
            out << side->picker()->itemText(i);
        }
    }
    return out;
}

// 1.5.3 feedback: New Book skips subfolders; an unknown status survives a save
// and the dialog, and the sidebar marks it.
void feedbackTests()
{
    QTemporaryDir tmp;
    const QString lib = tmp.path() + "/Library";
    const QString dir = lib + "/Birds";
    write(dir + "/01.md", "one\n");
    write(dir + "/02 Songbirds.md", "two\n");
    write(dir + "/images/README.md", "image sources\n");
    write(dir + "/notes/todo.txt", "notes\n");
    const BookManifest found = BookManifest::fromFolder(dir);
    check(order(found) == QStringList({"01.md", "02 Songbirds.md"}),
          "F  New Book lists top-level files only (images/, notes/ skipped)", order(found).join(","));
    // A file in a subfolder can still be added by hand.
    BookManifest withSub = found;
    withSub.add("images/README.md");
    check(withSub.count() == 3, "F  ... a subfolder file can still be added explicitly");

    write(BookManifest::manifestPath(dir),
          R"({"format":"zwriter-book","version":1,"title":"Birds","chapters":[
                {"path":"01.md","status":"Done"},{"path":"02 Songbirds.md","status":"Final"}]})");
    BookManifest b;
    QString err;
    const bool loaded = b.load(dir, &err);
    check(loaded, "F  manifest with an unknown status loads", err);
    if (!loaded || b.count() < 2) {
        return;
    }
    check(b.at(0).status == "Done", "F  hand-edited status \"Done\" loads as Done", b.at(0).status);
    check(b.save(), "F  save");
    check(readAll(BookManifest::manifestPath(dir)).contains("\"status\": \"Done\""), "F  ... and is saved back unchanged",
          QString::fromUtf8(readAll(BookManifest::manifestPath(dir))));

    QMainWindow win;
    auto *side = new BookSidebar(&win);
    win.setCentralWidget(side);
    win.show();
    side->setRootPath(lib);
    side->selectBook("Birds");
    check(side->view()->count() == 2 && side->view()->item(0)->text().contains("Done (unknown status)")
              && !side->view()->item(1)->text().contains("unknown"),
          "F  sidebar marks the unknown status", side->view()->count() ? side->view()->item(0)->text() : QString());
    side->editChapter(0);
    if (BookChapterDialog *dlg = side->chapterDialog()) {
        check(dlg->statusBox()->currentText() == "Done" && dlg->statusBox()->count() == 6
                  && dlg->chapter().status == "Done",
              "F  details dialog keeps \"Done\" selectable and selected", dlg->statusBox()->currentText());
        dlg->reject();
    } else {
        check(false, "F  details dialog opens");
    }
}

// 1.5.4: New Chapter (sidebar "+" menu, "New", context menu), file names,
// formats, book.json, and "New" never reopening New Book over an open book.
void newChapterTests()
{
    check(BookSidebar::chapterFileName(3, "The Pass", "md") == "03 The Pass.md", "N  file name: number + title");
    check(BookSidebar::chapterFileName(12, " a/b:c?  ", "odt") == "12 a b c.odt", "N  unsafe characters dropped",
          BookSidebar::chapterFileName(12, " a/b:c?  ", "odt"));
    check(BookSidebar::chapterFileName(4, "...", "txt") == "04 Chapter 4.txt", "N  empty title → Chapter N");

    QTemporaryDir tmp;
    const QString lib = tmp.path() + "/Library";
    const QString dir = lib + "/Birds";
    write(dir + "/01 Intro.md", "# Intro\n");
    QMainWindow win;
    auto *side = new BookSidebar(&win);
    win.setCentralWidget(side);
    win.show();
    side->setRootPath(lib);
    check(side->createBook("Birds") && side->view()->count() == 1, "N  book with one chapter");

    QStringList opened;
    QObject::connect(side, &BookSidebar::fileActivated, [&opened](const QString &p) { opened << p; });
    const QString md = side->createChapter("Songbirds", "md");
    check(md.endsWith("/Birds/02 Songbirds.md") && readAll(md) == "# Songbirds\n\n", "N  .md chapter written with its heading",
          md);
    const QString odt = side->createChapter("Raptors", "odt");
    QTextDocument doc;
    check(DocumentIo::load(&doc, odt) && doc.firstBlock().text() == "Raptors"
              && doc.firstBlock().blockFormat().headingLevel() == 1,
          "N  .odt chapter opens with the title as Heading 1", doc.toPlainText());
    const QString txt = side->createChapter("Raptors", "txt");
    check(txt.endsWith("/04 Raptors.txt") && readAll(txt) == "Raptors\n\n", "N  .txt chapter", txt);
    BookManifest saved;
    saved.load(dir);
    check(order(saved) == QStringList({"01 Intro.md", "02 Songbirds.md", "03 Raptors.odt", "04 Raptors.txt"}),
          "N  each new chapter is appended to book.json", order(saved).join(","));
    check(opened.size() == 3 && opened.last() == QDir::cleanPath(txt), "N  the new chapter is opened");
    check(side->view()->currentRow() == 3, "N  ... and selected in the sidebar");
    check(side->createChapter("x", "docx").isEmpty(), "N  other formats refused");

    // Header "New" with a book open: the New Chapter dialog, never New Book.
    auto *newButton = side->findChild<QToolButton *>("bookNew");
    newButton->click();
    QDialog *dlg = side->newChapterDialog();
    check(dlg && !side->newBookDialog(), "N  'New' with a book open asks for a chapter, not a book");
    if (dlg) {
        auto *format = dlg->findChild<QComboBox *>("chapterFormat");
        check(format && format->currentData().toString() == "md", "N  format defaults to the book's usual one (.md)",
              format ? format->currentData().toString() : QString());
        dlg->findChild<QLineEdit *>("chapterTitle")->setText("Owls");
        dlg->accept();
        QCoreApplication::processEvents();
        check(QFileInfo::exists(dir + "/05 Owls.md") && side->view()->count() == 5, "N  Create writes and lists it");
    }
    // "+" is a menu with both actions.
    QToolButton *add = side->addButton();
    QStringList addTexts;
    if (add && add->menu()) {
        for (QAction *a : add->menu()->actions()) {
            addTexts << a->text();
        }
    }
    check(add && add->popupMode() == QToolButton::InstantPopup
              && addTexts == QStringList({"New Chapter…", "Add Existing Chapters…"}),
          "N  '+' opens a menu: New Chapter…, Add Existing Chapters…", addTexts.join(" | "));
}

// 1.6.0 usability: titles without the file's number prefix, "+ Chapter",
// the picker's New Book… entry, the empty state, F2 rename, Delete with
// confirmation (remove by default, delete the file only after a second yes),
// keyboard focus and the drag message.
void usabilityTests()
{
    QTemporaryDir tmp;
    qputenv("XDG_DATA_HOME", (tmp.path() + "/data").toUtf8()); // trash goes here on Linux
    const QString lib = tmp.path() + "/Library";
    const QString dir = lib + "/Birds";
    write(dir + "/01 Introduction.md", "# Introduction\n");
    write(dir + "/02_Songbirds.md", "# Songbirds\n");
    write(dir + "/03 Raptors.md", "# Raptors\n");
    write(dir + "/2024.md", "# A year\n");
    QDir().mkpath(lib + "/Empty");
    write(lib + "/Empty/book.json", R"({"format":"zwriter-book","version":1,"title":"Empty","chapters":[]})");
    QMainWindow win;
    auto *side = new BookSidebar(&win);
    win.setCentralWidget(side);
    win.resize(400, 600);
    win.show();
    side->setRootPath(lib);
    side->createBook("Birds");
    QStringList messages;
    QObject::connect(side, &BookSidebar::message, [&messages](const QString &m) { messages << m; });
    QListWidget *list = side->view();
    check(list->count() == 4 && list->item(0)->text().startsWith("1. Introduction\n")
              && list->item(1)->text().startsWith("2. Songbirds\n"),
          "U  titles drop the file's number prefix (the sidebar numbers chapters)", list->item(0)->text());
    check(list->item(3)->text().startsWith("4. 2024\n"), "U  a name that is only a number is kept",
          list->item(3)->text());
    check(side->addButton()->text() == "+ Chapter" && side->addButton()->isVisibleTo(side)
              && !side->findChild<QToolButton *>("bookNew")->isVisibleTo(side),
          "U  one labelled '+ Chapter' button; no second 'New' button");
    {
        // Double-click and Enter open a chapter; each row shows its status and
        // word count, and the footer the book total.
        QStringList opened;
        auto conn = QObject::connect(side, &BookSidebar::fileActivated, [&opened](const QString &p) { opened << p; });
        QApplication::setActiveWindow(&win);
        QTest::qWaitForWindowExposed(&win);
        list->scrollToItem(list->item(2));
        const QRect r = list->visualItemRect(list->item(2));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, r.center());
        const int afterClick = opened.size();
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, {}, r.center());
        const int afterDouble = int(opened.size());
        list->setCurrentRow(1);
        emit list->itemActivated(list->item(1)); // Enter / Return
        QObject::disconnect(conn);
        check(afterClick == 1 && afterDouble == 2 && opened.value(1) == QDir::cleanPath(dir + "/03 Raptors.md")
                  && opened.value(2) == QDir::cleanPath(dir + "/02_Songbirds.md"),
              "U  click, double-click and Enter open the chapter (once each)", opened.join(","));
        check(list->item(0)->text().contains("Draft · 1 word"), "U  row badge: status and word count", list->item(0)->text());
        auto *footer = side->findChild<QLabel *>("bookFooter");
        check(footer && footer->text().startsWith("4 chapters · 5 words"), "U  footer: book total",
              footer ? footer->text() : QString());
    }
    check(list->focusPolicy() == Qt::StrongFocus, "U  the list takes keyboard focus (Tab)");
    {
        auto *files = side->findChild<QToolButton *>("bookToLibrary");
        check(files && files->text() == "Files" && files->toolTip().contains("all files"),
              "U  the file-tree button says Files, not Library");
        std::unique_ptr<QMenu> m(side->buildContextMenu(-1));
        bool change = false;
        for (QAction *a : m->actions()) {
            change = change || a->text() == QStringLiteral("Change Library Folder…");
        }
        check(change, "U  right-click menu: Change Library Folder…");
    }

    // Picker: the last entry starts a new book and leaves the current one picked.
    QComboBox *picker = side->picker();
    const int newBook = picker->findText("New Book…");
    check(newBook == picker->count() - 1, "U  picker ends with New Book…");
    emit picker->activated(newBook);
    check(side->newBookDialog() && side->currentBook() == "Birds"
              && picker->currentData().toString() == "Birds",
          "U  picking New Book… asks for the subfolder; the book stays picked");
    if (side->newBookDialog()) {
        side->newBookDialog()->reject();
    }
    QCoreApplication::processEvents();

    // F2: rename in place; Enter saves the title, Esc leaves it.
    list->setFocus();
    list->setCurrentRow(1);
    QTest::keyClick(list, Qt::Key_F2);
    auto *editor = list->viewport()->findChild<QLineEdit *>();
    check(editor && editor->text() == "Songbirds", "U  F2 opens an editor with the title",
          editor ? editor->text() : QString());
    if (editor) {
        editor->setText("Songbirds of the Prairie");
        QTest::keyClick(editor, Qt::Key_Return);
    }
    QTest::qWait(50);
    BookManifest saved;
    saved.load(dir);
    check(saved.at(1).title == "Songbirds of the Prairie" && list->item(1)->text().startsWith("2. Songbirds of the Prairie\n"),
          "U  Enter saves the new title to book.json", saved.at(1).title);
    check(QFile::exists(dir + "/02_Songbirds.md"), "U  ... the file keeps its name");
    QTest::keyClick(list, Qt::Key_F2);
    editor = list->viewport()->findChild<QLineEdit *>();
    if (editor) {
        editor->setText("Something else");
        QTest::keyClick(editor, Qt::Key_Escape);
    }
    QTest::qWait(50);
    saved = BookManifest();
    saved.load(dir);
    check(saved.at(1).title == "Songbirds of the Prairie" && list->item(1)->text().contains("Prairie"),
          "U  Esc cancels the rename");

    // Delete: remove from the book (default), file kept.
    list->setFocus();
    list->setCurrentRow(2);
    QTest::keyClick(list, Qt::Key_Delete);
    QMessageBox *box = side->removeDialog();
    check(box && box->defaultButton() && box->defaultButton()->text() == "Remove from Book",
          "U  Delete asks; Remove from Book is the default");
    if (box) {
        box->defaultButton()->click();
    }
    QTest::qWait(50);
    check(list->count() == 3 && QFile::exists(dir + "/03 Raptors.md"), "U  ... removed from the book, file kept");

    // Delete File…: a second question, Cancel by default.
    list->setCurrentRow(2); // 2024.md
    QTest::keyClick(list, Qt::Key_Delete);
    box = side->removeDialog();
    QAbstractButton *del = nullptr;
    for (QAbstractButton *b : box ? box->buttons() : QList<QAbstractButton *>()) {
        if (b->text() == "Delete File…") {
            del = b;
        }
    }
    check(del != nullptr, "U  the question also offers Delete File…");
    if (del) {
        del->click();
    }
    QTest::qWait(50);
    QMessageBox *sure = side->removeDialog();
    check(sure && sure->objectName() == "bookDeleteDialog" && sure->defaultButton()
              && sure->defaultButton()->text() != "Delete File",
          "U  Delete File… asks again, and the default is Cancel");
    if (sure) {
        sure->defaultButton()->click(); // Cancel
    }
    QTest::qWait(50);
    check(list->count() == 3 && QFile::exists(dir + "/2024.md"), "U  Cancel keeps the file and the chapter");
    check(side->deleteChapterFile(2) && !QFile::exists(dir + "/2024.md") && list->count() == 2,
          "U  confirmed delete: file moved to the trash, chapter removed");

    // Drag feedback: a status message after a move by drag.
    messages.clear();
    list->setCurrentRow(0);
    list->model()->moveRow(QModelIndex(), 0, QModelIndex(), 2); // what a drop does
    QTest::qWait(50);
    check(!messages.isEmpty() && messages.last().startsWith("Moved Introduction to position 2"),
          "U  a drop says where the chapter went", messages.join(" | "));

    // Empty book: a hint and the two ways to add a chapter.
    side->selectBook("Empty");
    QCoreApplication::processEvents();
    check(side->emptyBox()->isVisibleTo(side) && !list->isVisibleTo(side)
              && side->emptyBox()->findChild<QPushButton *>("bookEmptyNewChapter")
              && side->emptyBox()->findChild<QPushButton *>("bookEmptyAdd"),
          "U  empty book: 'no chapters yet' with New Chapter… and Add Existing Chapters…");
    side->emptyBox()->findChild<QPushButton *>("bookEmptyNewChapter")->click();
    check(side->newChapterDialog() != nullptr, "U  ... New Chapter… opens the dialog");
    if (side->newChapterDialog()) {
        side->newChapterDialog()->reject();
    }
    QCoreApplication::processEvents();
}

void sidebarTests()
{
    QTemporaryDir tmp;
    const QString lib = tmp.path() + "/Library";
    const QString dir = lib + "/Novel";
    write(dir + "/10 End.md", "the end\n");
    write(dir + "/2 Middle.md", "middle bit here\n");
    write(dir + "/1 Start.txt", "start\n");
    write(dir + "/cover.png", "x");
    write(lib + "/loose.md", "top level\n");
    write(lib + "/Essays/b.md", "b\n");

    QMainWindow win;
    auto *side = new BookSidebar(&win);
    win.setCentralWidget(side);
    win.resize(400, 600);
    win.show();

    side->setRootPath(QString());
    check(!side->hasBook() && !side->createButton()->isVisibleTo(side),
          "S  no library folder: no New Book, a hint instead");
    side->setRootPath(lib);
    check(side->books().isEmpty() && !side->hasBook() && side->createButton()->isVisibleTo(side),
          "B  library without books: New Book offered, nothing written");
    check(!BookManifest::exists(dir) && !BookManifest::exists(lib), "B  ... no book.json yet");
    check(side->candidateFolders() == QStringList({"Essays", "Novel"}), "B  candidate subfolders",
          side->candidateFolders().join(","));
    check(!side->createBook("../x") && !side->createBook("") && !side->createBook(".hidden"),
          "B  New Book refuses names that are not one subfolder");
    check(side->createBook("Novel"), "B  New Book in an existing subfolder");
    check(onDisk(dir) == QStringList({"1 Start.txt", "2 Middle.md", "10 End.md"}),
          "S  created in name order, numbers as numbers, only .md/.txt", onDisk(dir).join(","));
    check(!BookManifest::exists(lib), "B  no book.json at the library top level");
    check(side->view()->count() == 3 && side->view()->isVisibleTo(side), "S  list shows 3 chapters");

    check(side->createBook("Poems") && QDir(lib + "/Poems").exists() && side->currentBook() == "Poems"
              && side->view()->count() == 0,
          "B  New Book with a new name makes the subfolder");
    check(side->books() == QStringList({"Novel", "Poems"}) && side->picker()->isVisibleTo(side),
          "B  both books listed", side->books().join(","));
    check(side->selectBook("Novel") && side->currentBook() == "Novel" && side->view()->count() == 3,
          "B  pick a book");
    check(!side->selectBook("Essays"), "B  a folder without book.json is not a book");
    {
        BookManifest named;
        named.load(lib + "/Poems");
        named.setTitle("Collected Poems");
        named.save();
        side->reload();
        check(chooserText(side) == QStringList({"Novel", "Collected Poems"}),
              "B  picker shows book titles", chooserText(side).join(","));
        check(side->currentBook() == "Novel", "B  reload keeps the picked book");
    }
    BookSidebar other;
    other.selectBookOnLoad("Poems");
    other.setRootPath(lib);
    check(other.currentBook() == "Poems", "B  saved book is picked on load");
    other.selectBookOnLoad("Gone");
    other.setRootPath(lib);
    check(other.currentBook() == "Novel", "B  saved book gone: the first book");

    check(side->moveChapter(2, 0) && onDisk(dir).first() == "10 End.md", "S  move saved at once");
    side->view()->setCurrentRow(0);
    side->setFocus();
    for (QAction *a : side->actions()) {
        if (a->text() == "Move Down") {
            a->trigger();
        }
    }
    check(onDisk(dir) == QStringList({"1 Start.txt", "10 End.md", "2 Middle.md"}),
          "S  Move Down (Alt+Down) saved", onDisk(dir).join(","));

    QAbstractItemModel *model = side->view()->model();
    check(model->moveRow(QModelIndex(), 2, QModelIndex(), 0), "S  drag-drop row move");
    QApplication::processEvents();
    check(onDisk(dir) == QStringList({"2 Middle.md", "1 Start.txt", "10 End.md"}),
          "S  drag-drop order saved", onDisk(dir).join(","));
    check(side->view()->item(0)->text().startsWith("1. Middle"),
          "S  list renumbered after the drop", side->view()->item(0)->text());

    side->editChapter(1);
    BookChapterDialog *dlg = side->chapterDialog();
    check(dlg && dlg->isVisible(), "S  Edit Details… opens the dialog");
    if (dlg) {
        check(dlg->progressLabel()->text() == "1 word", "S  shows the current count",
              dlg->progressLabel()->text());
        check(dlg->statusBox()->count() == 5 && dlg->statusBox()->itemText(1) == "Outline",
              "S  status list includes Outline");
        dlg->titleEdit()->setText("  Start  ");
        dlg->statusBox()->setCurrentText("Outline");
        dlg->povEdit()->setText("Ivo");
        dlg->tagsEdit()->setText("a, b, a,");
        dlg->targetBox()->setValue(4);
        check(dlg->progressLabel()->text() == "1 word of 4 (25%)", "S  progress against target",
              dlg->progressLabel()->text());
        dlg->synopsisEdit()->setPlainText("syn");
        dlg->notesEdit()->setPlainText("notes");
        dlg->accept();
        QApplication::processEvents();
    }
    BookManifest b;
    b.load(dir);
    const BookChapter c = b.at(1);
    check(c.path == "1 Start.txt" && c.title == "Start" && c.status == "Outline" && c.pov == "Ivo"
              && c.tags == QStringList({"a", "b"}) && c.wordTarget == 4 && c.synopsis == "syn"
              && c.notes == "notes",
          "S  details saved to book.json");

    QMenu *menu = side->buildContextMenu(0);
    QStringList texts;
    for (QAction *a : menu->actions()) {
        if (!a->isSeparator()) {
            texts << a->text() + (a->isEnabled() ? "" : "(off)");
        }
    }
    delete menu;
    check(texts == QStringList({"Open", "Edit Details…", "Rename", "Move Up(off)", "Move Down",
                                "Remove from Book…", "New Chapter…", "Add Existing Chapters…",
                                "New Book…", "Insert/Update Table of Contents", "Export Book", "Print Book…", "Change Library Folder…"}),
          "S  context menu", texts.join(" | "));

    // Outside paths: refused with a message, the book unchanged.
    QStringList messages;
    QObject::connect(side, &BookSidebar::message, [&messages](const QString &m) { messages << m; });
    check(side->addChapters({lib + "/loose.md", lib + "/Essays/b.md"}) == 0
              && onDisk(dir).size() == 3 && messages.size() == 1,
          "O  sidebar refuses files outside the book, with a message");

    // Library → Add to Book.
    check(!side->addFileToBook(lib + "/loose.md").isEmpty(),
          "B  Add to Book: a top-level library file is refused");
    check(side->addFileToBook(lib + "/Essays/b.md").isEmpty() && side->currentBook() == "Essays"
              && onDisk(lib + "/Essays") == QStringList({"b.md"}),
          "B  Add to Book: the subfolder becomes a book, the file is in it");
    write(dir + "/extra.md", "extra\n");
    check(side->addFileToBook(dir + "/extra.md").isEmpty() && side->currentBook() == "Novel"
              && onDisk(dir).last() == "extra.md",
          "B  Add to Book: picks the file's own book");
    side->removeChapter(3);

    QFile::remove(dir + "/10 End.md");
    side->reload();
    check(side->view()->item(2)->text().contains("Missing: 10 End.md"), "S  missing file marked",
          side->view()->item(2)->text());
    check(side->removeChapter(1) && QFile::exists(dir + "/1 Start.txt")
              && onDisk(dir) == QStringList({"2 Middle.md", "10 End.md"}),
          "S  Remove from Book keeps the file");
    check(side->addChapters({dir + "/1 Start.txt", dir + "/2 Middle.md"}) == 1
              && onDisk(dir).last() == "1 Start.txt",
          "S  Add Chapters appends, skipping ones already in the book");

    // An outside entry written by hand: listed as outside, clicking does nothing.
    QJsonObject json = QJsonDocument::fromJson(readAll(BookManifest::manifestPath(dir))).object();
    QJsonArray arr = json.value("chapters").toArray();
    arr.append(QJsonObject{{"path", "../loose.md"}});
    json.insert("chapters", arr);
    write(BookManifest::manifestPath(dir), QJsonDocument(json).toJson());
    side->reload();
    QStringList opened;
    QObject::connect(side, &BookSidebar::fileActivated, [&opened](const QString &p) { opened << p; });
    QListWidgetItem *outside = side->view()->item(3);
    check(outside && outside->text().contains("Outside the book folder: ../loose.md"),
          "O  outside entry listed as outside", outside ? outside->text() : QString());
    emit side->view()->itemClicked(outside);
    check(opened.isEmpty(), "O  ... and is never opened");

    // Bad book.json: shown as a hint, never overwritten.
    write(BookManifest::manifestPath(dir), "{");
    side->setRootPath(lib);
    side->selectBook("Novel");
    check(!side->hasBook() && !side->createButton()->isVisibleTo(side)
              && readAll(BookManifest::manifestPath(dir)) == "{",
          "S  unreadable book.json is left alone");
}

} // namespace

// E (1.5.4): Edit Details stays responsive. The sidebar used to load every
// .odt chapter (unzip + pictures) on each refresh, twice; with a few
// chapters the window lagged for seconds and fast typing went astray.
void detailsTests()
{
    QTemporaryDir tmp;
    const QString lib = tmp.path() + "/Library";
    const QString dir = lib + "/Birds";
    for (int i = 1; i <= 6; ++i) {
        writeOdt(dir + QStringLiteral("/%1 Chapter.odt").arg(i, 2, 10, QLatin1Char('0')),
                 QStringLiteral("Words in chapter %1 here.").arg(i));
    }
    QMainWindow win;
    auto *side = new BookSidebar(&win);
    win.setCentralWidget(side);
    win.show();
    side->setRootPath(lib);
    check(side->createBook("Birds") && side->view()->count() == 6, "E  book with six .odt chapters");
    const int afterOpen = wordCountFileReads();
    side->reload();
    BookChapter c = side->book().at(2);
    c.status = QStringLiteral("Revised");
    side->setChapter(2, c);
    check(wordCountFileReads() == afterOpen, "E  refresh and a details change read no chapter files again",
          QStringLiteral("%1 -> %2").arg(afterOpen).arg(wordCountFileReads()));
    check(side->view()->item(2)->text().contains("Revised"), "E  the list shows the change at once");
    QTest::qWait(20);
    writeOdt(dir + "/03 Chapter.odt", "Now this chapter has quite a few more words in it.");
    side->reload();
    check(wordCountFileReads() == afterOpen + 1 && side->view()->item(2)->text().contains("11 words"),
          "E  a changed file is recounted (only that one)", side->view()->item(2)->text());

    side->editChapter(0);
    BookChapterDialog *dlg = side->chapterDialog();
    check(dlg != nullptr, "E  Edit Details opens");
    if (dlg) {
        QTest::qWait(20);
        check(dlg->focusWidget() == dlg->titleEdit(), "E  typing starts in Title");
        check(dlg->titleEdit()->nextInFocusChain() == dlg->statusBox()
                  && dlg->statusBox()->nextInFocusChain() == dlg->povEdit()
                  && dlg->povEdit()->nextInFocusChain() == dlg->tagsEdit()
                  && dlg->tagsEdit()->nextInFocusChain() == dlg->targetBox(),
              "E  Tab goes Title, Status, POV, Tags, Target");
        QTest::keyClicks(dlg->titleEdit(), "Waterfowl and Wetlands");
        QTest::keyClicks(dlg->tagsEdit(), "toc, ducks, swans");
        QTest::keyClicks(dlg->synopsisEdit(), "Dabblers and divers.");
        dlg->accept();
        const BookChapter got = side->book().at(0);
        check(got.title == "Waterfowl and Wetlands" && got.tags == QStringList({"toc", "ducks", "swans"})
                  && got.synopsis == "Dabblers and divers.",
              "E  fast typing lands in the right fields", got.title + " | " + got.tags.join(",") + " | " + got.synopsis);
        check(side->view()->item(0)->text().startsWith("1. Waterfowl and Wetlands"), "E  title shown at once");
        check(wordCountFileReads() == afterOpen + 1, "E  saving details reads no chapter files");
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("zwriter-test");
    QCoreApplication::setApplicationName("book_test");
    manifestTests();
    tolerantTests();
    outsideTests();
    odtTests();
    feedbackTests();
    newChapterTests();
    usabilityTests();
    wordTests();
    sidebarTests();
    detailsTests();
    check(QVersionNumber::fromString(QString::fromLatin1(zwriter::kVersionString)) >= QVersionNumber(1, 4, 0),
          "V  version is 1.4.0 or later (the Book view shipped in 1.4.0)",
          QString::fromLatin1(zwriter::kVersionString));
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
