#pragma once

// New document from an outline (1.22.0): the user types a plain outline, one
// heading per line, and gets a document with those headings and an empty
// paragraph under each, ready to write.
//
// A line's level comes from, in this order:
//   "#", "##", ... in front of the text       (Markdown style)
//   its indentation: a tab or any deeper indent than the line above is one
//   level down; going back to an earlier indent is back to that level
// A leading bullet ("-", "*", "+") or number ("1.", "2.1", "IV)") is dropped.
// Blank lines are ignored. A line is never more than one level below the one
// before it, and never deeper than maxLevel (rich documents: 3; Markdown: 6).

#include <QList>
#include <QString>

class QTextDocument;

namespace OutlineDraft {

struct Entry {
    int level = 1;
    QString title;
};

struct Parsed {
    QList<Entry> entries;
    int clamped = 0; // lines whose level was pulled up to fit
};

Parsed parse(const QString &text, int maxLevel);

// "# One\n\n## Two\n\n": a blank line under every heading, to type into.
QString toMarkdown(const QList<Entry> &entries);

} // namespace OutlineDraft
