#include "Ruler.hpp"

#include "PageWidgets.hpp"
#include "ParagraphStyles.hpp"

#include <QAbstractTextDocumentLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFrame>

using namespace ParagraphStyles;

Ruler::Ruler(QTextEdit *editor, QWidget *parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setObjectName(QStringLiteral("ruler"));
    setFixedHeight(22);
    setMouseTracking(true);
    setToolTip(QStringLiteral("Drag the top marker for the first line, the bottom-left one for the "
                              "left indent, the right one for the right indent"));
    connect(editor, &QTextEdit::cursorPositionChanged, this, qOverload<>(&QWidget::update));
    connect(editor->document(), &QTextDocument::contentsChanged, this, qOverload<>(&QWidget::update));
    connect(editor->horizontalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
}

QSize Ruler::sizeHint() const
{
    return {400, 22};
}

qreal Ruler::zoom() const
{
    // Fit Page to Width paints the document scaled; the ruler must follow.
    if (const auto *pe = qobject_cast<const PageTextEdit *>(m_editor)) {
        return pe->zoom();
    }
    return 1.0;
}

int Ruler::originX() const
{
    const QTextDocument *doc = m_editor->document();
    const QTextFrameFormat ff = doc->rootFrame()->frameFormat();
    const qreal x = (ff.leftMargin() + ff.border() + ff.padding()) * zoom() - m_editor->horizontalScrollBar()->value();
    return m_editor->viewport()->mapTo(window(), QPoint(qRound(x), 0)).x() - mapTo(window(), QPoint(0, 0)).x();
}

qreal Ruler::columnWidth() const
{
    const QTextDocument *doc = m_editor->document();
    const QTextFrameFormat ff = doc->rootFrame()->frameFormat();
    const qreal w = doc->documentLayout()->documentSize().width();
    return qMax<qreal>(50.0, w - ff.leftMargin() - ff.rightMargin() - 2 * (ff.border() + ff.padding()));
}

int Ruler::markerX(Marker m) const
{
    const QTextBlockFormat f = m_editor->textCursor().blockFormat();
    const int o = originX();
    const qreal z = zoom();
    switch (m) {
    case Marker::FirstLine:
        return o + qRound((f.leftMargin() + f.textIndent()) * z);
    case Marker::Left:
        return o + qRound(f.leftMargin() * z);
    case Marker::Right:
        return o + qRound((columnWidth() - f.rightMargin()) * z);
    case Marker::None:
        break;
    }
    return -1;
}

void Ruler::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QColor fg = palette().color(QPalette::WindowText);
    p.fillRect(rect(), palette().color(QPalette::Window));
    const int o = originX();
    const qreal z = zoom();
    const qreal w = columnWidth() * z;
    p.fillRect(QRect(o, 4, qRound(w), height() - 8), palette().color(QPalette::Base));
    p.setPen(fg);
    QFont small = font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 3));
    p.setFont(small);
    // Ticks every 0.5 cm, numbers every cm.
    for (int half = 1; cmToPx(half * 0.5) * z <= w; ++half) {
        const int x = o + qRound(cmToPx(half * 0.5) * z);
        if (half % 2 == 0) {
            p.drawText(QRect(x - 10, 4, 20, height() - 8), Qt::AlignCenter, QString::number(half / 2));
        } else {
            p.drawLine(x, height() / 2 - 2, x, height() / 2 + 2);
        }
    }
    auto triangle = [&](int x, bool down) {
        QPainterPath path;
        if (down) { // top marker, pointing down
            path.moveTo(x - 5, 0);
            path.lineTo(x + 5, 0);
            path.lineTo(x, 7);
        } else { // bottom marker, pointing up
            path.moveTo(x - 5, height());
            path.lineTo(x + 5, height());
            path.lineTo(x, height() - 7);
        }
        path.closeSubpath();
        p.fillPath(path, fg);
    };
    triangle(markerX(Marker::FirstLine), true);
    triangle(markerX(Marker::Left), false);
    triangle(markerX(Marker::Right), false);
}

Ruler::Marker Ruler::hit(const QPoint &pt) const
{
    const bool upper = pt.y() < height() / 2;
    if (upper) {
        return qAbs(pt.x() - markerX(Marker::FirstLine)) <= 7 ? Marker::FirstLine : Marker::None;
    }
    // Lower half: the nearer of Left and Right, so markers that touch each
    // still have their own side (left of the middle Left, right of it Right).
    const int dl = qAbs(pt.x() - markerX(Marker::Left));
    const int dr = qAbs(pt.x() - markerX(Marker::Right));
    if (qMin(dl, dr) > 7) {
        return Marker::None;
    }
    if (dl == dr) {
        return pt.x() <= markerX(Marker::Left) ? Marker::Left : Marker::Right;
    }
    return dl < dr ? Marker::Left : Marker::Right;
}

void Ruler::mousePressEvent(QMouseEvent *e)
{
    m_drag = e->button() == Qt::LeftButton ? hit(e->position().toPoint()) : Marker::None;
    m_dragChanged = false;
}

void Ruler::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag != Marker::None) {
        dragTo(e->position().toPoint().x());
    } else {
        setCursor(hit(e->position().toPoint()) != Marker::None ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }
}

void Ruler::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_drag != Marker::None) {
        dragTo(e->position().toPoint().x());
    }
    m_drag = Marker::None;
}

void Ruler::dragTo(int x)
{
    // Screen x -> document px (zoom), snapped to 0.25 cm.
    const qreal step = cmToPx(0.25);
    const qreal at = qRound((x - originX()) / zoom() / step) * step;
    // The text keeps at least 3 cm (or what the column allows).
    const qreal minText = qMin(cmToPx(3.0), columnWidth() / 2);
    QTextCursor c = m_editor->textCursor();
    const QTextBlockFormat old = c.blockFormat();
    QTextBlockFormat f = old;
    if (m_drag == Marker::FirstLine) {
        f.setTextIndent(qBound(-f.leftMargin(), at - f.leftMargin(), columnWidth() - f.leftMargin() - f.rightMargin() - minText));
    } else if (m_drag == Marker::Left) {
        f.setLeftMargin(qBound<qreal>(0.0, at, qMax<qreal>(0.0, columnWidth() - f.rightMargin() - minText)));
    } else if (m_drag == Marker::Right) {
        f.setRightMargin(qBound<qreal>(0.0, columnWidth() - at, qMax<qreal>(0.0, columnWidth() - f.leftMargin() - minText)));
    }
    if (f.leftMargin() == old.leftMargin() && f.rightMargin() == old.rightMargin() && f.textIndent() == old.textIndent()) {
        return;
    }
    // One drag = one undo step: later changes join the first one.
    if (m_dragChanged) {
        c.joinPreviousEditBlock();
    } else {
        c.beginEditBlock();
    }
    ParagraphStyles::setGeometry(c, f);
    c.endEditBlock();
    m_dragChanged = true;
    update();
}
