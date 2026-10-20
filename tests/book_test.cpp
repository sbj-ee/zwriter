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
#include "BookManifest.hpp"
#include "BookSidebar.hpp"
#include "version.hpp"

#include <QApplication>
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
    check(b.at(1).status == "Draft" && b.at(1).wordTarget == 0, "T  bad status / target → defaults");
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
    write(dir + "/notes.odt", "x");
    BookManifest b;
    b.setBookDir(dir);
    check(b.add(dir + "/in.md") == 0, "O  file inside the book folder is added");
    check(b.add(tmp.path() + "/out.md") == -1, "O  absolute path outside is refused");
    check(b.add("../out.md") == -1 && b.add("sub/../../out.md") == -1, "O  ../ paths are refused");
    check(b.add("/etc/passwd.md") == -1 && b.add("C:/x.md") == -1, "O  root / drive paths refused");
    check(b.add("notes.odt") == -1, "O  only .md / .txt");
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
        out << side->picker()->itemText(i);
    }
    return out;
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
    check(side->view()->item(0)->text().startsWith("1. 2 Middle"),
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
    check(texts == QStringList({"Open", "Edit Details…", "Move Up(off)", "Move Down",
                                "Remove from Book", "Add Chapters…"}),
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

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("zwriter-test");
    QCoreApplication::setApplicationName("book_test");
    manifestTests();
    tolerantTests();
    outsideTests();
    wordTests();
    sidebarTests();
    check(QString::fromLatin1(zwriter::kVersionString) == "1.4.0", "V  version 1.4.0",
          QString::fromLatin1(zwriter::kVersionString));
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
