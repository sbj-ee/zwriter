#pragma once

// Generated table of contents for a book (1.8.0): the chapters in book.json
// order, each with its headings. Inserted into a document as paragraphs
// marked with GeneratedProperty; saved to ODT as a real
// text:table-of-content (dot leaders, page numbers filled in by LibreOffice
// or by zwriter's book export).

#include "DocumentIo.hpp"

#include <QList>
#include <QString>
#include <QTextFormat>

class BookManifest;
class QTextDocument;
class QTextCursor;

namespace BookToc {

// Block property of generated ToC paragraphs: entry level (1 = chapter).
constexpr int GeneratedProperty = DocumentIo::GeneratedTocProperty;
// Block property of the "Contents" title paragraph of a generated ToC.
constexpr int TitleProperty = DocumentIo::GeneratedTocTitleProperty;

struct Entry {
    int level = 1;   // 1 chapter, 2 heading inside it, 3 sub-heading
    QString text;
    int chapter = 0; // index in book.json
    int page = 0;    // page in the whole-book PDF (0 = not known): shown after a tab
};
// "text<TAB>page" (just the text without a page).
QString lineText(const Entry &e);

// Headings of one chapter file: (level, text), in order. .odt by its
// headings, .md by its # lines (code fences skipped), .txt has none.
QList<QPair<int, QString>> headingsOf(const QString &path);

// Chapter titles (numbered by book position, as in the sidebar: "2. Title") and their headings down to maxLevel
// (2 = chapters + their top headings). A chapter's first heading is
// dropped when it repeats the chapter title. skipPath (the document that
// holds the ToC) is left out.
QList<Entry> entries(const BookManifest &book, int maxLevel = 2, const QString &skipPath = QString());

bool hasToc(const QTextDocument *doc);
// Inserts the ToC at the cursor, or replaces the existing one in place.
// Returns false (and changes nothing) when the entries are unchanged.
bool insertOrUpdate(QTextDocument *doc, QTextCursor *at, const QList<Entry> &entries);
// Plain lines, for Markdown/TXT.
QString plainText(const QList<Entry> &entries);

} // namespace BookToc
