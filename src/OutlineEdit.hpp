#pragma once

// Editing a document by its outline (1.21.0): move a section up or down among
// its siblings, move it to another place among them (drag and drop), and
// promote / demote a heading together with its subsections. Works on any
// QTextDocument: rich text (ODT / RTF: headings are blocks with a heading
// level) and Markdown (headings are "#" lines in plain-text blocks).
//
// Every operation is one undo step. "Section" is as in Outline.hpp: a heading
// and everything up to the next heading of the same or a higher level.
//
// A trailing empty block (a Markdown file's final newline, an empty last
// paragraph) is not part of any section and stays last, so moving sections
// never adds or removes the file's final newline. For the same reason a
// Markdown file's bytes outside the moved text are untouched.

#include "Outline.hpp"

#include <QList>
#include <QString>

#include <functional>

class QTextDocument;

namespace OutlineEdit {

// Deepest heading level the app writes: Heading 3 in rich documents, "######"
// in Markdown.
constexpr int kMaxRichLevel = 3;
constexpr int kMaxMarkdownLevel = 6;

struct Result {
    bool ok = false;
    QString why;      // when !ok: what stopped it, short enough for the status bar
    int position = 0; // when ok: document position of the heading that was moved / changed
};

// The previous / next heading of the same level under the same parent (no
// heading of a lower level in between), or -1.
int previousSibling(const QList<Outline::Item> &items, int index);
int nextSibling(const QList<Outline::Item> &items, int index);

// `position` in all of these is any document position: the section is the one
// of the last heading at or before it (so the caret will do).

// Moves the section above its previous sibling (delta < 0) or below its next
// sibling (delta > 0), subsections with it.
Result moveSection(QTextDocument *doc, bool markdown, int position, int delta);

// Moves the section to just before (after = false) or just after (after =
// true) the section at targetPosition. Both must be siblings of the same
// level; anything else is refused.
Result moveSectionTo(QTextDocument *doc, bool markdown, int position, int targetPosition, bool after);

// Promote (delta < 0) or demote (delta > 0) the heading and every heading in
// its section by one level. Refused when any of them would leave the range.
// Rich documents: setLevel(blockNumber, level) applies a level to a block (the
// window passes a function that also sets the heading's font and size); without
// one only the block's heading level is set. Markdown: a "#" is added to / taken
// from each heading line.
using LevelSetter = std::function<void(int blockNumber, int level)>;
Result changeLevel(QTextDocument *doc, bool markdown, int position, int delta, const LevelSetter &setLevel = {});

// What the actions would do, for enabling menu items: no document changes.
bool canMove(const QList<Outline::Item> &items, int index, int delta);
bool canChangeLevel(const QList<Outline::Item> &items, int index, int delta, bool markdown);

} // namespace OutlineEdit
