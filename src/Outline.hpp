#pragma once

// Document outline (1.20.0): the headings of the open document as a flat list
// in document order, each with the extent of its section.
//
// A heading is a block with a heading level (ODT / RTF: Heading 1-3) or, in a
// Markdown document, an ATX line ("# Title", levels 1-6; fenced code is not
// scanned for headings, same rule as the editor's highlighting). A section is
// the heading plus everything up to the next heading of the same or a higher
// level, so its subsections belong to it. The generated table of contents and
// empty headings are not part of the outline.

#include <QList>
#include <QString>
#include <QStringView>

class QTextDocument;

namespace Outline {

struct Item {
    int level = 1;       // 1 = top level
    QString text;        // heading text, whitespace simplified
    int blockNumber = 0; // heading block
    int position = 0;    // document position of the start of the heading block
    int endBlock = 0;    // first block after the section (blockCount() for the last)
    int words = 0;       // words in the section body (subsections included, heading line not)
};

// Headings of `doc` in order. `markdown` picks the scanner: ATX lines (true) or
// block heading levels (false).
QList<Item> scan(const QTextDocument *doc, bool markdown);

// The section the position is in: the last heading that starts at or before
// `position`, or -1 when the position is above the first heading.
int indexAt(const QList<Item> &items, int position);

// Words in text: whitespace-separated runs that contain a letter or digit, so
// Markdown markers ("-", "##", "---") are not counted.
int wordCount(QStringView text);

} // namespace Outline
