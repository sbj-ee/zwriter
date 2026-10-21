#pragma once

// Whole-book export (1.10.0): every chapter of book.json, in book order, as
// one PDF (also used by Print Book) or one EPUB 3.3.
//
// PDF / print: the chapters are joined into one QTextDocument, each chapter
// starting on a new page under its title, with an optional title page and
// table of contents. The ToC has real page numbers and dot leaders, found by
// laying the joined document out on the target page size first. Pages are
// numbered continuously through the book (not on the title page).
//
// EPUB: one XHTML file per chapter, nav.xhtml, a stylesheet with the
// paragraph styles and indents of every paragraph (ParagraphStyles::blockCss)
// and the pictures as PNG.

#include "PrintLayout.hpp"

#include <QFont>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTextFormat>

#include <memory>

class BookManifest;
class QPrinter;
class QTextDocument;

namespace BookExport {

// Block properties in the joined document.
constexpr int ChapterStartProperty = QTextFormat::UserProperty + 0x5D01; // chapter index + 1
constexpr int TocEntryProperty = QTextFormat::UserProperty + 0x5D02;     // ToC level (1 chapter, 2 heading)
constexpr int TitlePageProperty = QTextFormat::UserProperty + 0x5D03;
constexpr int AnchorProperty = QTextFormat::UserProperty + 0x5D04;       // ToC target number + 1

struct Options {
    bool toc = true;
    bool titlePage = true;
    bool figureNumbers = true; // "Figure <chapter>.<n>." labels; off = labels removed
};

struct TocLine {
    int level = 1;
    QString text;
    int page = 0;
};

struct Result {
    std::unique_ptr<QTextDocument> doc;
    QList<TocLine> toc;     // with page numbers (empty without the ToC option)
    QStringList skipped;    // chapters that could not be read (missing, outside, unreadable)
    int chapters = 0;       // chapters included
};

// The joined book laid out for `page`.
Result build(const BookManifest &book, const Options &options, const PrintLayout::Page &page,
             const QFont &defaultFont);

// Paints a built book onto the printer (PDF or paper). Shared by PDF export
// and Print Book. Returns the page count.
int paint(const QTextDocument &doc, QPrinter *printer, const Options &options);

// build + paint to a PDF file with the printer's page layout.
bool writePdf(const BookManifest &book, const Options &options, const QPageLayout &layout,
              const QFont &defaultFont, const QString &path, int *pages = nullptr,
              QString *error = nullptr);

bool writeEpub(const BookManifest &book, const Options &options, const QString &path,
               QString *error = nullptr, QStringList *skipped = nullptr);

} // namespace BookExport
