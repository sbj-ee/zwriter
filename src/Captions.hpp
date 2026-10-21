#pragma once

// Automatic figure numbers (1.9.0). Every paragraph in the Caption style
// starts with a label, "Figure <chapter>.<n>. " (or "Figure <n>. " outside
// a book), kept in step with the order of the captions and the chapter's
// place in the book. The label is ordinary text, recognised by its form, so
// it survives every file format (ODT also keeps the Caption style).

#include <QTextFormat>

class QTextDocument;
class QTextBlock;

namespace Captions {

bool isCaption(const QTextBlock &block);
// "Figure 3.2. " (chapter > 0) or "Figure 2. ".
QString label(int chapter, int n);
// Length of the label at the start of the block (0 = none).
int labelLength(const QTextBlock &block);
// Renumbers every caption; chapter <= 0 = not in a book. Returns the number
// of labels changed (0 = document untouched). One undo step, or part of the
// previous one (joinPrevious: undoing an edit also undoes its renumbering).
int renumber(QTextDocument *doc, int chapter, bool joinPrevious = false);
// Caption count (figures) in the document.
int count(const QTextDocument *doc);

} // namespace Captions
