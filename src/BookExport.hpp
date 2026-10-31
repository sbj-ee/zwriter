#pragma once

#include <functional>

class QTextBlock;
class QTextTable;

#include "StyleFonts.hpp"

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
    // The table of contents. When the book has a Contents chapter (1.11.1) the
    // table is put there, in its place in the book, instead of a second one
    // after the title page; with this off the Contents chapter is left out.
    bool toc = true;
    bool titlePage = true;
    bool figureNumbers = true; // "Figure <chapter>.<n>." labels; off = labels removed
    // Without the ToC: still find each chapter's and heading's page (Result::toc),
    // for the editor's own generated table of contents.
    bool pagesOnly = false;
    // Style fonts (1.15.0) for the EPUB's CSS: a style whose face or size
    // differs from the built-in default gets a font-family / font-size rule.
    // PDF and print need nothing here: the chapters' text carries its fonts.
    StyleFonts::Map styleFonts = StyleFonts::builtinDefaults();
};

struct TocLine {
    int level = 1;
    QString text;
    int page = 0;
    int chapter = -1; // book.json index of the chapter the line belongs to
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

// The EPUB's style-font rules (body, h1-h6) for `fonts`; empty when every
// style uses its built-in face and size (exposed for the tests).
QString epubStyleFontCss(const StyleFonts::Map &fonts);
// One table as EPUB XHTML (1.16.0): <colgroup><col style="width:NN%"/> from the
// stored column widths, header rows as <thead>/<th>, merged cells as
// colspan/rowspan; `paragraph` renders each paragraph of a cell.
QString tableXhtml(const QTextTable *table, const std::function<QString(const QTextBlock &)> &paragraph);
bool writeEpub(const BookManifest &book, const Options &options, const QString &path,
               QString *error = nullptr, QStringList *skipped = nullptr);

} // namespace BookExport
