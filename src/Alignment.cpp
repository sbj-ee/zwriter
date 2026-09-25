#include "Alignment.hpp"

#include <QPainter>
#include <QPixmap>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

namespace Alignment {

Kind kindOf(Qt::Alignment alignment)
{
    const Qt::Alignment h = alignment & Qt::AlignHorizontal_Mask;
    if (h & Qt::AlignHCenter) {
        return Kind::Center;
    }
    if (h & Qt::AlignJustify) {
        return Kind::Justify;
    }
    if (h & Qt::AlignRight) {
        return Kind::Right;
    }
    return Kind::Left;
}

Kind kindOf(const QTextBlock &block)
{
    return kindOf(block.blockFormat().alignment());
}

Qt::Alignment flags(Kind kind)
{
    switch (kind) {
    case Kind::Center:
        return Qt::AlignHCenter;
    case Kind::Right:
        return Qt::AlignRight | Qt::AlignAbsolute;
    case Kind::Justify:
        return Qt::AlignJustify;
    case Kind::Left:
        break;
    }
    return Qt::AlignLeft | Qt::AlignAbsolute;
}

QList<QTextBlock> touchedBlocks(const QTextCursor &cursor)
{
    QList<QTextBlock> out;
    QTextDocument *doc = cursor.document();
    if (!doc) {
        return out;
    }
    if (cursor.hasComplexSelection()) {
        // A rectangle of table cells: only the paragraphs inside those cells.
        if (QTextTable *table = cursor.currentTable()) {
            int row = 0, rows = 0, col = 0, cols = 0;
            cursor.selectedTableCells(&row, &rows, &col, &cols);
            for (int r = row; r < row + rows; ++r) {
                for (int c = col; c < col + cols; ++c) {
                    const QTextTableCell cell = table->cellAt(r, c);
                    if (!cell.isValid() || cell.row() != r || cell.column() != c) {
                        continue; // covered by a merged cell, visited at its origin
                    }
                    for (auto it = cell.begin(); !it.atEnd(); ++it) {
                        if (it.currentBlock().isValid()) {
                            out.append(it.currentBlock());
                        }
                    }
                }
            }
            return out;
        }
    }
    const QTextBlock first = doc->findBlock(cursor.selectionStart());
    const QTextBlock last = doc->findBlock(cursor.selectionEnd());
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        out.append(b);
        if (b == last) {
            break;
        }
    }
    return out;
}

bool apply(const QTextCursor &cursor, Kind kind)
{
    QTextDocument *doc = cursor.document();
    if (!doc) {
        return false;
    }
    QList<QTextBlock> todo;
    for (const QTextBlock &b : touchedBlocks(cursor)) {
        if (kindOf(b) != kind) {
            todo.append(b);
        }
    }
    if (todo.isEmpty()) {
        return false; // already aligned: not an edit
    }
    QTextBlockFormat fmt;
    fmt.setAlignment(flags(kind));
    QTextCursor edit(doc);
    edit.beginEditBlock();
    for (const QTextBlock &b : std::as_const(todo)) {
        QTextCursor c(b);
        c.mergeBlockFormat(fmt);
    }
    edit.endEditBlock();
    return true;
}

namespace {

// Four "lines of text" of alternating length in a 16x16 box.
QPixmap drawIcon(Kind kind, const QColor &ink, qreal dpr)
{
    const int logical = 16;
    QPixmap pm(qRound(logical * dpr), qRound(logical * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    const qreal full = 14.0;  // long line
    const qreal part = 9.0;   // short line
    const qreal left = 1.0;
    const qreal thick = 2.0;
    const qreal ys[] = {1.0, 5.0, 9.0, 13.0};
    for (int i = 0; i < 4; ++i) {
        const bool longLine = (i % 2 == 0);
        qreal w = longLine ? full : part;
        qreal x = left;
        switch (kind) {
        case Kind::Left:
            break;
        case Kind::Center:
            x = left + (full - w) / 2.0;
            break;
        case Kind::Right:
            x = left + full - w;
            break;
        case Kind::Justify:
            // Every line full width except the last, as justified text looks.
            w = (i == 3) ? part : full;
            break;
        }
        p.fillRect(QRectF(x, ys[i], w, thick), ink);
    }
    return pm;
}

} // namespace

QIcon icon(Kind kind, const QColor &ink, const QColor &checkedInk, const QColor &disabledInk)
{
    QIcon ic;
    for (const qreal dpr : {1.0, 2.0}) {
        ic.addPixmap(drawIcon(kind, ink, dpr), QIcon::Normal, QIcon::Off);
        ic.addPixmap(drawIcon(kind, ink, dpr), QIcon::Active, QIcon::Off);
        ic.addPixmap(drawIcon(kind, checkedInk, dpr), QIcon::Normal, QIcon::On);
        ic.addPixmap(drawIcon(kind, checkedInk, dpr), QIcon::Active, QIcon::On);
        ic.addPixmap(drawIcon(kind, disabledInk, dpr), QIcon::Disabled, QIcon::Off);
        ic.addPixmap(drawIcon(kind, disabledInk, dpr), QIcon::Disabled, QIcon::On);
    }
    return ic;
}

} // namespace Alignment
