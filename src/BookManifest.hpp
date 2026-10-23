#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// A book: an ordered list of chapters / pieces kept in a human-readable
// `book.json` in the book folder. Chapter paths are stored relative to that
// folder (forward slashes), so the folder can be moved or synced. The order of
// the "chapters" array IS the book order.
//
// Tolerant by design: a chapter whose file is gone stays in the book (marked
// missing) instead of being dropped, unknown keys are kept and written back,
// and a malformed field falls back to its default. Removing a chapter from the
// book never touches the file itself.
//
//   {
//     "format": "zwriter-book",
//     "version": 1,
//     "title": "The Harbor",
//     "chapters": [
//       { "path": "Chapters/01 Harbor.md", "title": "Harbor", "status": "Draft",
//         "synopsis": "...", "notes": "...", "pov": "Mara", "tags": ["sea"],
//         "wordTarget": 3000 }
//     ]
//   }
struct BookChapter
{
    QString path;     // relative to the book folder, '/' separators
    QString title;    // empty = shown as the file's base name
    QString status;   // normally one of statuses(); default "Draft"; unknown values are kept
    QString synopsis;
    QString notes;
    QString pov;
    QStringList tags;
    int wordTarget = 0; // 0 = no target
    QJsonObject extra;  // unknown keys, round-tripped untouched

    bool operator==(const BookChapter &o) const;
};

class BookManifest
{
public:
    static QString fileName() { return QStringLiteral("book.json"); }
    static QString manifestPath(const QString &bookDir);
    static QStringList statuses(); // Idea, Outline, Draft, Revised, Final
    static bool isKnownStatus(const QString &status); // false for hand-edited values like "Done"
    static bool exists(const QString &bookDir);

    // Load bookDir/book.json. Missing file: false, empty book, no error.
    // Unreadable / not JSON: false, empty book, *error set.
    bool load(const QString &bookDir, QString *error = nullptr);
    // Atomic write (QSaveFile), pretty-printed, stable key order.
    bool save(QString *error = nullptr) const;

    QString bookDir() const { return m_dir; }
    void setBookDir(const QString &dir);

    QString title() const { return m_title; }
    void setTitle(const QString &t) { m_title = t; }

    const QVector<BookChapter> &chapters() const { return m_chapters; }
    int count() const { return int(m_chapters.size()); }
    const BookChapter &at(int i) const { return m_chapters.at(i); }
    void setChapter(int i, const BookChapter &c);

    // Chapters must live inside the book folder: a relative path with no ".."
    // step and no drive / root. An existing book.json entry that breaks this is
    // kept (so saving does not lose it) but treated as invalid: no absolute
    // path, never opened, shown as outside the book.
    static bool isInsideBook(const QString &relativePath);
    bool isOutside(int i) const;
    // Absolute path of chapter i (empty when outside the book), and whether the
    // file is missing (outside counts as missing).
    QString absolutePath(int i) const;
    bool isMissing(int i) const;
    // Index of the chapter for this absolute or relative path, or -1.
    int indexOf(const QString &path) const;
    // Shown name: title, else the file's base name.
    QString displayTitle(int i) const;
    // The Contents chapter (1.11.1): titled "Contents", or a file named
    // "NN Contents" with no title. It holds the table of contents and is not a
    // numbered chapter. contentsIndex() is the first one, or -1.
    bool isContents(int i) const;
    int contentsIndex() const;
    // Chapter number as shown in the sidebar, the ToC and figure labels: book
    // position counting only real chapters (the Contents chapter is 0).
    int chapterNumber(int i) const;

    // Adds the file (absolute or relative) at the end, or at `at`. Returns the
    // new index, the existing index if it is already in the book, or -1 if it
    // is outside the book folder (or not a .md / .txt / .odt path).
    int add(const QString &path, int at = -1);
    // Drops the entry; the file is left alone.
    bool remove(int i);
    bool move(int from, int to);
    bool moveUp(int i) { return move(i, i - 1); }
    bool moveDown(int i) { return move(i, i + 1); }
    // Reorder to exactly this sequence of relative paths (from a drag-drop).
    // Unknown paths are ignored, omitted chapters keep their relative order at
    // the end.
    void reorder(const QStringList &relativePaths);

    // Chapter files: .md, .txt and .odt (any case).
    static QStringList chapterNameFilters();
    static bool isChapterSuffix(const QString &suffix);

    // A fresh book for a folder: its .md / .txt / .odt files (subfolders included,
    // hidden ones skipped), sorted by relative path with numbers compared as
    // numbers, the folder name as the title.
    static BookManifest fromFolder(const QString &bookDir);

    QString relativePath(const QString &path) const;
    // "2 Middle" < "10 End": digit runs compare as numbers, the rest ignoring case.
    static bool naturalLess(const QString &a, const QString &b);

    QJsonObject toJson() const;
    static BookManifest fromJson(const QJsonObject &obj, const QString &bookDir);

private:
    QString m_dir;
    QString m_title;
    QVector<BookChapter> m_chapters;
    QJsonObject m_extra;
};

// Words in a chapter file. .odt: the text DocumentIo::load() reads from
// content.xml. Plain text / Markdown: whitespace-separated runs that hold
// at least one letter or digit (Markdown markers alone do not count). -1 if
// the file cannot be read.
int countWordsInFile(const QString &path);
// Times a chapter file was actually read to count words (cache misses; for tests).
int wordCountFileReads();
int countWords(const QString &text);
