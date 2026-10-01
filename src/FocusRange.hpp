#pragma once

#include <QPair>
#include <QString>

// Focus mode (sentence scope): which part of a paragraph stays in full ink.
namespace FocusRange {

// The sentence of `text` (one paragraph / QTextBlock) that contains position
// `pos`, as [start, end) offsets into `text`, with surrounding whitespace
// trimmed. Sentence boundaries come from QTextBoundaryFinder (Unicode UAX #29),
// so decimals ("3.5"), ellipses inside a sentence followed by lower case
// ("wait... then") and quotes after the full stop ("“Go.” She") are handled.
// A caret right after the full stop belongs to the sentence it ends; a caret
// at the start of the next sentence belongs to that one. Note UAX #29 still
// breaks after an abbreviation followed by a capital ("Dr. Smith").
QPair<int, int> sentenceRange(const QString &text, int pos);

} // namespace FocusRange
