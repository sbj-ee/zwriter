#include "PageWidgets.hpp"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QTimer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QPainter>
#include <QVBoxLayout>

void PageTextEdit::setFixedPageSize(const QSizeF &size)
{
    m_pageSize = size;
    reapplyPageSize();
}

void PageTextEdit::reapplyPageSize()
{
    // setPageSize() relayouts the whole document even when unchanged, so only
    // call it when Qt has actually overwritten our value.
    if (m_pageSize.isValid() && document() && document()->pageSize() != m_pageSize) {
        document()->setPageSize(m_pageSize);
    }
}

void PageTextEdit::insertFromMimeData(const QMimeData *source)
{
    if (!source || !acceptRichText() || !source->hasHtml()) {
        QTextEdit::insertFromMimeData(source);
        return;
    }
    QTextDocument tmp;
    tmp.setHtml(source->html());
    QTextCursor edit(&tmp);
    edit.beginEditBlock();
    for (QTextBlock b = tmp.begin(); b.isValid(); b = b.next()) {
        QTextBlockFormat bf = b.blockFormat();
        if (bf.hasProperty(QTextFormat::BackgroundBrush)) {
            bf.clearBackground();
            QTextCursor(b).setBlockFormat(bf);
        }
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            QTextCharFormat cf = frag.charFormat();
            if (!cf.hasProperty(QTextFormat::ForegroundBrush)
                && !cf.hasProperty(QTextFormat::BackgroundBrush)) {
                continue;
            }
            cf.clearForeground();
            cf.clearBackground();
            QTextCursor sel(&tmp);
            sel.setPosition(frag.position());
            sel.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);
            sel.setCharFormat(cf);
        }
    }
    edit.endEditBlock();
    QTextCursor c = textCursor();
    c.insertFragment(QTextDocumentFragment(&tmp));
    setTextCursor(c);
    ensureCursorVisible();
}

void PageTextEdit::setContinuousInset(bool on, const QMarginsF &documentMargins)
{
    m_continuousInset = on;
    m_documentMargins = documentMargins;
    updateInset();
}

void PageTextEdit::updateInset()
{
    if (!m_continuousInset) {
        setViewportMargins(0, 0, 0, 0);
        return;
    }
    const qreal side = width() * 0.20 + 4.0;
    const int left = qMax(0, qRound(side - m_documentMargins.left()));
    const int right = qMax(0, qRound(side - m_documentMargins.right()));
    const int top = qMax(0, qRound(52.0 - m_documentMargins.top()));
    const int bottom = qMax(0, qRound(52.0 - m_documentMargins.bottom()));
    const QMargins want(left, top, right, bottom);
    if (viewportMargins() != want) {
        setViewportMargins(want);
    }
}

void PageTextEdit::resizeEvent(QResizeEvent *event)
{
    QTextEdit::resizeEvent(event);
    updateInset();
    reapplyPageSize();
}

void PageTextEdit::changeEvent(QEvent *event)
{
    QTextEdit::changeEvent(event);
    reapplyPageSize();
}

void PageTextEdit::showEvent(QShowEvent *event)
{
    QTextEdit::showEvent(event);
    reapplyPageSize();
}

void PageTextEdit::setZoom(qreal zoom)
{
    zoom = qBound(0.25, zoom, 8.0);
    if (qFuzzyCompare(zoom, m_zoom)) {
        return;
    }
    if (!m_blinkTimer) {
        // QTextEdit asks for repaints in unzoomed coordinates, so while zoomed
        // the regions it invalidates are the wrong ones. Invalidate the zoomed
        // equivalents: layout changes, caret moves and selection changes.
        m_blinkTimer = new QTimer(this);
        connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
            m_caretOn = !m_caretOn;
            viewport()->update(toView(cursorRect()).adjusted(-2, -2, 2, 2));
        });
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::update, this,
                [this](const QRectF &r) {
                    if (isZoomed()) {
                        viewport()->update(toView(r).adjusted(-2, -2, 2, 2));
                    }
                });
        connect(this, &QTextEdit::cursorPositionChanged, this, [this]() {
            if (isZoomed()) {
                restartCaretBlink();
                viewport()->update();
            }
        });
        connect(this, &QTextEdit::selectionChanged, this, [this]() {
            if (isZoomed()) {
                viewport()->update();
            }
        });
    }
    m_zoom = zoom;
    restartCaretBlink();
    viewport()->update();
}

QPoint PageTextEdit::toDocument(const QPoint &viewportPos) const
{
    return (QPointF(viewportPos) / m_zoom).toPoint();
}

QRect PageTextEdit::toView(const QRectF &documentRect) const
{
    return QRectF(documentRect.topLeft() * m_zoom, documentRect.size() * m_zoom)
        .toAlignedRect();
}

void PageTextEdit::restartCaretBlink()
{
    if (!m_blinkTimer) {
        return;
    }
    m_caretOn = true;
    const int flash = QApplication::cursorFlashTime();
    if (isZoomed() && hasFocus() && flash > 0) {
        m_blinkTimer->start(flash / 2);
    } else {
        m_blinkTimer->stop();
    }
}

void PageTextEdit::paintEvent(QPaintEvent *event)
{
    if (!isZoomed()) {
        QTextEdit::paintEvent(event);
        return;
    }
    // Draw the true-size layout scaled, with the caret and selections QTextEdit
    // would draw (its own painter cannot take a transform).
    QPainter p(viewport());
    p.scale(m_zoom, m_zoom);
    const QRectF r = event->rect();
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.clip = QRectF(r.topLeft() / m_zoom, r.size() / m_zoom).adjusted(-1, -1, 1, 1);
    ctx.palette = palette();
    if (m_caretOn && hasFocus() && (textInteractionFlags() & Qt::TextEditable)) {
        ctx.cursorPosition = textCursor().position();
    }
    const QList<ExtraSelection> extras = extraSelections();
    for (const ExtraSelection &es : extras) {
        ctx.selections.append({es.cursor, es.format});
    }
    const QTextCursor tc = textCursor();
    if (tc.hasSelection()) {
        const QPalette::ColorGroup group = hasFocus() ? QPalette::Active : QPalette::Inactive;
        QTextCharFormat sel;
        sel.setBackground(palette().brush(group, QPalette::Highlight));
        sel.setForeground(palette().brush(group, QPalette::HighlightedText));
        ctx.selections.append({tc, sel});
    }
    p.setClipRect(ctx.clip);
    document()->documentLayout()->draw(&p, ctx);
}

namespace {
QMouseEvent unzoomed(const QMouseEvent *e, qreal zoom)
{
    return QMouseEvent(e->type(), e->position() / zoom, e->scenePosition(), e->globalPosition(),
                       e->button(), e->buttons(), e->modifiers(), e->pointingDevice());
}
} // namespace

void PageTextEdit::mousePressEvent(QMouseEvent *event)
{
    if (!isZoomed()) {
        QTextEdit::mousePressEvent(event);
        return;
    }
    QMouseEvent e = unzoomed(event, m_zoom);
    QTextEdit::mousePressEvent(&e);
    event->setAccepted(e.isAccepted());
}

void PageTextEdit::mouseMoveEvent(QMouseEvent *event)
{
    if (!isZoomed()) {
        QTextEdit::mouseMoveEvent(event);
        return;
    }
    QMouseEvent e = unzoomed(event, m_zoom);
    QTextEdit::mouseMoveEvent(&e);
    event->setAccepted(e.isAccepted());
}

void PageTextEdit::mouseReleaseEvent(QMouseEvent *event)
{
    if (!isZoomed()) {
        QTextEdit::mouseReleaseEvent(event);
        return;
    }
    QMouseEvent e = unzoomed(event, m_zoom);
    QTextEdit::mouseReleaseEvent(&e);
    event->setAccepted(e.isAccepted());
}

void PageTextEdit::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!isZoomed()) {
        QTextEdit::mouseDoubleClickEvent(event);
        return;
    }
    QMouseEvent e = unzoomed(event, m_zoom);
    QTextEdit::mouseDoubleClickEvent(&e);
    event->setAccepted(e.isAccepted());
}

void PageTextEdit::dragMoveEvent(QDragMoveEvent *event)
{
    if (!isZoomed()) {
        QTextEdit::dragMoveEvent(event);
        return;
    }
    QDragMoveEvent e(event->position().toPoint() / m_zoom, event->possibleActions(),
                     event->mimeData(), event->buttons(), event->modifiers());
    QTextEdit::dragMoveEvent(&e);
    event->setDropAction(e.dropAction());
    event->setAccepted(e.isAccepted());
    viewport()->update(); // the drop caret moved
}

void PageTextEdit::dropEvent(QDropEvent *event)
{
    if (!isZoomed()) {
        QTextEdit::dropEvent(event);
        return;
    }
    QDropEvent e(event->position() / m_zoom, event->possibleActions(), event->mimeData(),
                 event->buttons(), event->modifiers());
    QTextEdit::dropEvent(&e);
    event->setDropAction(e.dropAction());
    event->setAccepted(e.isAccepted());
}

void PageTextEdit::focusInEvent(QFocusEvent *event)
{
    QTextEdit::focusInEvent(event);
    if (isZoomed()) {
        restartCaretBlink();
        viewport()->update();
    }
}

void PageTextEdit::focusOutEvent(QFocusEvent *event)
{
    QTextEdit::focusOutEvent(event);
    if (isZoomed()) {
        restartCaretBlink();
        viewport()->update();
    }
}

QVariant PageTextEdit::inputMethodQuery(Qt::InputMethodQuery query) const
{
    QVariant v = QTextEdit::inputMethodQuery(query);
    if (isZoomed()
        && (query == Qt::ImCursorRectangle || query == Qt::ImAnchorRectangle)) {
        // Where the input-method popup goes: the caret as drawn, not as laid out.
        return QVariant(QRectF(toView(v.toRectF())));
    }
    return v;
}

PageCanvas::PageCanvas(QWidget *page, QWidget *parent)
    : QWidget(parent)
    , m_page(page)
    , m_layout(new QVBoxLayout(this))
{
    m_layout->setSpacing(0);
    m_layout->addWidget(page);
    setPaperMode(false);
}

void PageCanvas::setPaperMode(bool paper)
{
    m_paper = paper;
    if (paper) {
        m_layout->setContentsMargins(28, 28, 28, 36);
        m_layout->setAlignment(m_page, Qt::AlignHCenter | Qt::AlignTop);
    } else {
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setAlignment(m_page, Qt::Alignment());
    }
    update();
}

void PageCanvas::setPageBorderColor(const QColor &color)
{
    m_border = color;
    update();
}

void PageCanvas::paintEvent(QPaintEvent *event)
{
    if (!m_paper) {
        return;
    }
    // Soft drop shadow: a few translucent, growing rounded rects behind the page.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(event->rect());
    p.setPen(Qt::NoPen);
    const QRect page = m_page->geometry();
    for (int i = 14; i >= 1; --i) {
        p.setBrush(QColor(0, 0, 0, 5));
        p.drawRoundedRect(page.adjusted(-i, -i + 5, i, i + 5), 3 + i * 0.4, 3 + i * 0.4);
    }
    // 1 px outline just outside the sheet (the page frame itself has no border,
    // so the editor inside is exactly the page size).
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setBrush(Qt::NoBrush);
    p.setPen(m_border);
    p.drawRect(page.adjusted(-1, -1, 0, 0));
}
