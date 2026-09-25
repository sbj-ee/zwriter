#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QTextBlock>

class QTextCursor;

// Paragraph alignment: the four choices and applying them to the paragraphs a
// cursor touches (QtGui only; the actions are in AlignmentActions.hpp).
namespace Alignment {

enum class Kind { Left, Center, Right, Justify };

// The choice a block's alignment reads as. Anything that is not centred,
// justified or right-aligned (including "no alignment set") is Left.
Kind kindOf(Qt::Alignment alignment);
Kind kindOf(const QTextBlock &block);
// The flags zwriter stores for a choice. Left and Right are absolute, so they
// are written to ODT as "left" / "right" and stay put in right-to-left text.
Qt::Alignment flags(Kind kind);

// Every paragraph the cursor touches: the caret's paragraph, each paragraph a
// selection overlaps, or the paragraphs of the selected table cells.
QList<QTextBlock> touchedBlocks(const QTextCursor &cursor);

// Align the touched paragraphs. All changes form one undo step; when every
// paragraph already has that alignment nothing is done (no undo step, the
// document is not modified) and false is returned.
bool apply(const QTextCursor &cursor, Kind kind);

// Toolbar icon: four lines of text in the given ink, drawn for the theme.
// `checkedInk` is used for the checked (On) state, `disabledInk` when disabled.
QIcon icon(Kind kind, const QColor &ink, const QColor &checkedInk, const QColor &disabledInk);

} // namespace Alignment
