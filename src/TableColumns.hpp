#pragma once

// Table column widths (1.16.0): drag-to-resize in the editor, stored as
// percentages of the table width in QTextTableFormat::columnWidthConstraints
// (saved to ODT as rel-column-width, to EPUB as <col style="width:NN%">).

#include <QList>
#include <QPointF>

class QTextDocument;
class QTextTable;

namespace TableColumns {

// Smallest column a drag leaves, in percent of the table width.
constexpr double kMinPercent = 3.0;

// The table's stored percentages (empty when it has no percentage widths for
// every column, i.e. Qt sizes its columns itself).
QList<double> storedPercents(const QTextTable *table);
// Stored percentages, else the widths the layout gives the columns now, else equal.
QList<double> effectivePercents(const QTextTable *table);
// Scale to a 100 % total (each column at least a sliver), rounded to 0.01.
QList<double> normalized(QList<double> percents);
// Store percentages (normalized) as the table's column width constraints.
void setPercents(QTextTable *table, const QList<double> &percents);
// Widths after moving the border left of column `border` (1..n-1) by
// deltaPercent; only the two neighbouring columns change, each keeps minPercent.
QList<double> dragged(const QList<double> &percents, int border, double deltaPercent,
                      double minPercent = kMinPercent);
// Column percentages after inserting `count` columns at `at` (new ones get the
// average width) or removing `count` from `at`, rescaled to 100 %.
QList<double> afterInsert(const QList<double> &percents, int at, int count);
QList<double> afterRemove(const QList<double> &percents, int at, int count);

// Layout geometry, in document coordinates: x of every column edge (n + 1
// values, left edge of column 0 .. right edge of the last column).
QList<double> edges(const QTextTable *table);
// Innermost table under a document point (nullptr if none).
QTextTable *tableAt(const QTextDocument *doc, const QPointF &docPos);
// The inner column border (1..n-1) within `tolerance` px of docPos, or -1.
// Only borders of the cell under the point count, so a border hidden inside a
// merged cell is not grabbed.
int borderAt(const QTextTable *table, const QPointF &docPos, double tolerance);

} // namespace TableColumns
