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
    QString status;   // one of BookManifest::statuses(); default "Draft"
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
    static QStringList statuses(); // Idea, Draft, Revised, Final
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

    // Absolute path of chapter i, and whether the file is there.
    QString absolutePath(int i) const;
    bool isMissing(int i) const;
    // Index of the chapter for this absolute or relative path, or -1.
    int indexOf(const QString &path) const;
    // Shown name: title, else the file's base name.
    QString displayTitle(int i) const;

    // Adds the file (absolute or relative) at the end, or at `at`. Returns the
    // new index, or the existing index if it is already in the book.
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

    // A fresh book for a folder: its top-level .md / .txt files, sorted by
    // name (numbers compared as numbers), the folder name as the title.
    static BookManifest fromFolder(const QString &bookDir);

    QString relativePath(const QString &path) const;

    QJsonObject toJson() const;
    static BookManifest fromJson(const QJsonObject &obj, const QString &bookDir);

private:
    QString m_dir;
    QString m_title;
    QVector<BookChapter> m_chapters;
    QJsonObject m_extra;
};

// Words in a plain-text / Markdown file: whitespace-separated runs that hold
// at least one letter or digit (Markdown markers alone do not count). -1 if
// the file cannot be read.
int countWordsInFile(const QString &path);
int countWords(const QString &text);
