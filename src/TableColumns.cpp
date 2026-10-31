#include "TableColumns.hpp"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLength>
#include <QTextTable>

#include <cmath>
#include <numeric>

namespace TableColumns {

QList<double> storedPercents(const QTextTable *table)
{
    QList<double> out;
    if (!table) {
        return out;
    }
    const QList<QTextLength> c = table->format().columnWidthConstraints();
    if (c.size() != table->columns()) {
        return out;
    }
    for (const QTextLength &l : c) {
        if (l.type() != QTextLength::PercentageLength || l.rawValue() <= 0) {
            return {};
        }
        out << l.rawValue();
    }
    return out;
}

QList<double> normalized(QList<double> p)
{
    if (p.isEmpty()) {
        return p;
    }
    for (double &v : p) {
        if (!(v > 0.0) || !std::isfinite(v)) {
            v = 0.5;
        }
    }
    const double sum = std::accumulate(p.cbegin(), p.cend(), 0.0);
    double total = 0.0;
    for (double &v : p) {
        v = std::round(v * 100.0 / sum * 100.0) / 100.0;
        total += v;
    }
    // Put the rounding remainder in the widest column, so the total is 100.
    const auto widest = std::max_element(p.begin(), p.end());
    *widest = std::round((*widest + 100.0 - total) * 100.0) / 100.0;
    return p;
}

QList<double> edges(const QTextTable *table)
{
    QList<double> out;
    if (!table || !table->document() || table->columns() < 1) {
        return out;
    }
    const int n = table->columns();
    QAbstractTextDocumentLayout *layout = table->document()->documentLayout();
    const QRectF frame = layout->frameBoundingRect(const_cast<QTextTable *>(table));
    const QTextTableFormat tf = table->format();
    const double outer = tf.margin() + tf.border();
    const double left = frame.left() + outer;
    const double right = frame.right() - outer;
    // Measured left edge of every column from a cell that starts in it.
    QList<double> x(n + 1, std::nan(""));
    x[0] = left;
    x[n] = right;
    for (int c = 1; c < n; ++c) {
        for (int r = 0; r < table->rows(); ++r) {
            const QTextTableCell cell = table->cellAt(r, c);
            if (!cell.isValid() || cell.column() != c) {
                continue;
            }
            const QTextBlock b = cell.firstCursorPosition().block();
            const QRectF br = layout->blockBoundingRect(b);
            if (br.isNull() && br.left() == 0) {
                continue;
            }
            const QTextTableCellFormat cf = cell.format().toTableCellFormat();
            const double pad = cf.hasProperty(QTextFormat::TableCellLeftPadding) ? cf.leftPadding() : tf.cellPadding();
            const double border = cf.hasProperty(QTextFormat::TableCellLeftBorder) ? cf.leftBorder() : 0.0;
            const double margin = b.blockFormat().leftMargin();
            x[c] = br.left() - margin - pad - border / 2.0;
            break;
        }
    }
    // A column no cell starts in (hidden by merges): from the stored widths, else
    // half-way between its known neighbours.
    const QList<double> stored = storedPercents(table);
    for (int c = 1; c < n; ++c) {
        if (!std::isnan(x[c])) {
            continue;
        }
        if (!stored.isEmpty()) {
            double acc = 0;
            for (int k = 0; k < c; ++k) {
                acc += stored[k];
            }
            x[c] = left + (right - left) * acc / 100.0;
            continue;
        }
        int nx = c + 1;
        while (nx < n && std::isnan(x[nx])) {
            ++nx;
        }
        x[c] = x[c - 1] + (x[nx] - x[c - 1]) / (nx - c + 1);
    }
    return x;
}

QList<double> effectivePercents(const QTextTable *table)
{
    const QList<double> stored = storedPercents(table);
    if (!stored.isEmpty()) {
        return normalized(stored);
    }
    QList<double> out;
    if (!table) {
        return out;
    }
    const int n = table->columns();
    const QList<double> x = edges(table);
    const double w = x.size() == n + 1 ? x[n] - x[0] : 0.0;
    if (w > 1.0) {
        bool ok = true;
        for (int c = 0; c < n; ++c) {
            const double d = x[c + 1] - x[c];
            ok = ok && d > 0.0;
            out << d / w * 100.0;
        }
        if (ok) {
            return normalized(out);
        }
    }
    return normalized(QList<double>(n, 100.0 / n));
}

void setPercents(QTextTable *table, const QList<double> &percents)
{
    if (!table || percents.size() != table->columns()) {
        return;
    }
    QList<QTextLength> c;
    for (double v : normalized(percents)) {
        c << QTextLength(QTextLength::PercentageLength, v);
    }
    QTextTableFormat f = table->format();
    f.setColumnWidthConstraints(c);
    table->setFormat(f);
}

QList<double> dragged(const QList<double> &percents, int border, double delta, double minPercent)
{
    QList<double> p = percents;
    if (border < 1 || border >= p.size()) {
        return p;
    }
    const double pair = p[border - 1] + p[border];
    double leftW = p[border - 1] + delta;
    leftW = qBound(qMin(minPercent, pair / 2), leftW, pair - qMin(minPercent, pair / 2));
    p[border - 1] = leftW;
    p[border] = pair - leftW;
    return p;
}

QList<double> afterInsert(const QList<double> &percents, int at, int count)
{
    if (percents.isEmpty() || count < 1) {
        return percents;
    }
    QList<double> p = percents;
    const double avg = 100.0 / p.size();
    at = qBound(0, at, int(p.size()));
    for (int i = 0; i < count; ++i) {
        p.insert(at, avg);
    }
    return normalized(p);
}

QList<double> afterRemove(const QList<double> &percents, int at, int count)
{
    QList<double> p = percents;
    if (at < 0 || count < 1 || at >= p.size()) {
        return p;
    }
    p.remove(at, qMin(count, int(p.size()) - at));
    return normalized(p);
}

QTextTable *tableAt(const QTextDocument *doc, const QPointF &docPos)
{
    if (!doc) {
        return nullptr;
    }
    const int pos = doc->documentLayout()->hitTest(docPos, Qt::FuzzyHit);
    if (pos < 0) {
        return nullptr;
    }
    QTextCursor c(const_cast<QTextDocument *>(doc));
    c.setPosition(pos);
    QTextTable *t = c.currentTable();
    if (!t) {
        return nullptr;
    }
    // FuzzyHit snaps to the nearest text: the point must really be on the table.
    const QRectF r = doc->documentLayout()->frameBoundingRect(t);
    return r.contains(docPos) ? t : nullptr;
}

int borderAt(const QTextTable *table, const QPointF &docPos, double tolerance)
{
    if (!table || table->columns() < 2) {
        return -1;
    }
    const QTextDocument *doc = table->document();
    const QList<double> x = edges(table);
    if (x.size() != table->columns() + 1) {
        return -1;
    }
    const int pos = doc->documentLayout()->hitTest(docPos, Qt::FuzzyHit);
    if (pos < 0) {
        return -1;
    }
    const QTextTableCell cell = table->cellAt(pos);
    if (!cell.isValid()) {
        return -1;
    }
    int best = -1;
    double bestD = tolerance;
    // The cell's own left and right edges, and those of the cells beside it in
    // the same row (the pointer may be just across the line).
    auto consider = [&](int k) {
        if (k < 1 || k >= table->columns()) {
            return;
        }
        const double d = std::abs(docPos.x() - x[k]);
        if (d <= bestD) {
            bestD = d;
            best = k;
        }
    };
    const int c0 = cell.column();
    const int c1 = cell.column() + cell.columnSpan();
    consider(c0);
    consider(c1);
    if (best >= 0) {
        // The border must separate two different cells in the pointer's row(s).
        const int row = cell.row();
        for (int r = row; r < row + cell.rowSpan(); ++r) {
            const QTextTableCell a = table->cellAt(r, best - 1);
            const QTextTableCell b = table->cellAt(r, best);
            if (a.isValid() && b.isValid() && !(a == b)) {
                return best;
            }
        }
        return -1;
    }
    return -1;
}

} // namespace TableColumns
