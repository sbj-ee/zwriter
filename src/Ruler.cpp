#include "Ruler.hpp"

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

int Ruler::originX() const
{
    const QTextDocument *doc = m_editor->document();
    const QTextFrameFormat ff = doc->rootFrame()->frameFormat();
    const qreal x = ff.leftMargin() + ff.border() + ff.padding() - m_editor->horizontalScrollBar()->value();
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
    switch (m) {
    case Marker::FirstLine:
        return o + qRound(f.leftMargin() + f.textIndent());
    case Marker::Left:
        return o + qRound(f.leftMargin());
    case Marker::Right:
        return o + qRound(columnWidth() - f.rightMargin());
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
    const qreal w = columnWidth();
    p.fillRect(QRect(o, 4, qRound(w), height() - 8), palette().color(QPalette::Base));
    p.setPen(fg);
    QFont small = font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 3));
    p.setFont(small);
    // Ticks every 0.5 cm, numbers every cm.
    for (int half = 1; cmToPx(half * 0.5) <= w; ++half) {
        const int x = o + qRound(cmToPx(half * 0.5));
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
    if (upper && qAbs(pt.x() - markerX(Marker::FirstLine)) <= 6) {
        return Marker::FirstLine;
    }
    if (!upper && qAbs(pt.x() - markerX(Marker::Left)) <= 6) {
        return Marker::Left;
    }
    if (!upper && qAbs(pt.x() - markerX(Marker::Right)) <= 6) {
        return Marker::Right;
    }
    return Marker::None;
}

void Ruler::mousePressEvent(QMouseEvent *e)
{
    m_drag = e->button() == Qt::LeftButton ? hit(e->position().toPoint()) : Marker::None;
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
    // Snap to 0.25 cm.
    const qreal step = cmToPx(0.25);
    const qreal at = qRound((x - originX()) / step) * step;
    QTextCursor c = m_editor->textCursor();
    QTextBlockFormat f = c.blockFormat();
    if (m_drag == Marker::FirstLine) {
        f.setTextIndent(qBound(-f.leftMargin(), at - f.leftMargin(), columnWidth()));
    } else if (m_drag == Marker::Left) {
        f.setLeftMargin(qBound<qreal>(0.0, at, columnWidth() - f.rightMargin() - step));
    } else if (m_drag == Marker::Right) {
        f.setRightMargin(qBound<qreal>(0.0, columnWidth() - at, columnWidth() - f.leftMargin() - step));
    }
    ParagraphStyles::setGeometry(c, f);
    update();
}
