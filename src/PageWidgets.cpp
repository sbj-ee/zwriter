#include "PageWidgets.hpp"
#include <cmath>
#include <QTextLayout>
#include "DocumentIo.hpp"
#include "Images.hpp"
#include "TableColumns.hpp"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QUrl>

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
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QtMath>
#include <QStyle>
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
    reapplyContinuousWidth();
}

qreal PageTextEdit::continuousLayoutWidth() const
{
    // The continuous strip wraps at the viewport width; zoomed, the same
    // screen width holds 1/zoom as many document pixels, so lines rewrap and
    // the bigger text never runs off the side.
    return viewport()->width() / m_zoom;
}

void PageTextEdit::reapplyContinuousWidth()
{
    // QTextEdit puts the layout width back to the (unzoomed) viewport width on
    // every relayout, so re-apply ours after it.
    if (m_pageSize.isValid() || !m_continuousInset || !document()) {
        return;
    }
    const qreal want = continuousLayoutWidth();
    if (!qFuzzyCompare(document()->pageSize().width(), want)) {
        document()->setPageSize(QSizeF(want, -1));
    }
    adjustZoomedScrollBars();
}

void PageTextEdit::adjustZoomedScrollBars()
{
    // Scroll bar values stay in document pixels (QTextEdit adds them to
    // pointer positions and subtracts them from cursorRect()), but only
    // viewport/zoom document pixels fit on screen. QTextEdit sizes the ranges
    // for zoom 1, so correct them whenever it recomputes.
    if (!isZoomed() || !document() || m_adjustingBars) {
        return;
    }
    const QScopedValueRollback guard(m_adjustingBars, true);
    const QSizeF docSize = document()->documentLayout()->documentSize();
    const int visH = qMax(1, qRound(viewport()->height() / m_zoom));
    const int visW = qMax(1, qCeil(viewport()->width() / m_zoom));
    QScrollBar *v = verticalScrollBar();
    v->setRange(0, qMax(0, qCeil(docSize.height()) - visH));
    v->setPageStep(visH);
    QScrollBar *h = horizontalScrollBar();
    h->setRange(0, qMax(0, qCeil(docSize.width()) - visW));
    h->setPageStep(visW);
}

void PageTextEdit::ensureZoomedCaretVisible()
{
    // QTextEdit's own ensureCursorVisible() thinks the viewport is zoom 1.
    if (!isZoomed() || m_pageSize.isValid()) {
        return;
    }
    const QRect cr = cursorRect(); // document px, relative to the scroll position
    const int visH = qRound(viewport()->height() / m_zoom);
    const int pad = qRound(8 / m_zoom);
    QScrollBar *v = verticalScrollBar();
    if (cr.top() < 0) {
        v->setValue(v->value() + cr.top() - pad);
    } else if (cr.bottom() > visH) {
        v->setValue(v->value() + cr.bottom() - visH + pad);
    }
}

namespace {
// Local image files in a paste / drop (file manager copies, drags).
QStringList localImageFiles(const QMimeData *source)
{
    QStringList out;
    if (!source || !source->hasUrls()) {
        return out;
    }
    for (const QUrl &u : source->urls()) {
        if (u.isLocalFile() && Images::isImageFile(u.toLocalFile())) {
            out << u.toLocalFile();
        }
    }
    return out;
}
} // namespace

bool PageTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
    if (source && (acceptRichText() || m_plainImageHandler)
        && (source->hasImage() || !localImageFiles(source).isEmpty())) {
        return true;
    }
    return QTextEdit::canInsertFromMimeData(source);
}

int PageTextEdit::insertImagesFromMimeData(const QMimeData *source)
{
    if (!source || (!acceptRichText() && !m_plainImageHandler)) {
        return 0;
    }
    QList<QByteArray> pictures;
    const QStringList files = localImageFiles(source);
    for (const QString &path : files) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            pictures << f.readAll();
        }
    }
    if (pictures.isEmpty() && files.isEmpty() && source->hasImage()) {
        // Image data (a screenshot, "Copy Image" in a browser): store as PNG.
        const QImage img = qvariant_cast<QImage>(source->imageData());
        if (!img.isNull()) {
            QByteArray png;
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
            pictures << png;
        }
    }
    if (pictures.isEmpty()) {
        return 0;
    }
    int inserted = 0;
    if (!acceptRichText()) {
        QTextCursor edit = textCursor();
        edit.beginEditBlock();
        for (const QByteArray &data : std::as_const(pictures)) {
            inserted += m_plainImageHandler(data) ? 1 : 0;
        }
        edit.endEditBlock();
        return inserted;
    }
    QStringList notes;
    QTextCursor c = textCursor();
    c.beginEditBlock(); // one undo step for the whole paste / drop
    for (const QByteArray &data : std::as_const(pictures)) {
        QString note;
        if (Images::insertImage(c, data, &note)) {
            ++inserted;
        }
        if (!note.isEmpty()) {
            notes << note;
        }
    }
    c.endEditBlock();
    setTextCursor(c);
    if (!notes.isEmpty()) {
        emit imageNote(notes.join(QLatin1Char(' ')));
    }
    return inserted;
}

void PageTextEdit::insertFromMimeData(const QMimeData *source)
{
    // Image files, or bare image data. Image data that comes with text (some
    // apps add a bitmap rendering of copied text) pastes as the text.
    if (source && (acceptRichText() || m_plainImageHandler)
        && (!localImageFiles(source).isEmpty() || (source->hasImage() && !source->hasText()))) {
        if (insertImagesFromMimeData(source) > 0) {
            return;
        }
    }
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

void PageTextEdit::setMaxColumnWidth(qreal px)
{
    m_maxColumn = qMax(0.0, px);
    updateInset();
}

void PageTextEdit::updateInset()
{
    if (!m_continuousInset) {
        setViewportMargins(0, 0, 0, 0);
        return;
    }
    int left = 0;
    int right = 0;
    if (m_maxColumn > 0.0) {
        // Centred column of at most m_maxColumn px, with a little air at the
        // sides when the window is narrower than that.
        const int bar = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, verticalScrollBar());
        const qreal avail = width() - 2.0 * frameWidth() - bar;
        const qreal minSide = qMax(24.0, width() * 0.05);
        // Zoomed, the cap scales too, so the column keeps its ~66 characters.
        const qreal column = qMax(80.0, qMin(m_maxColumn * m_zoom, avail - 2.0 * minSide));
        const qreal side = qMax(0.0, (avail - column) / 2.0);
        left = qMax(0, qRound(side - m_documentMargins.left() * m_zoom));
        right = qMax(0, qRound(avail - column - side - m_documentMargins.right() * m_zoom));
    } else {
        // Zoomed in, the side gutters shrink so the bigger text gets the room.
        const qreal side = width() * (m_zoom > 1.0 ? qMax(0.04, 0.20 / m_zoom) : 0.20) + 4.0;
        left = qMax(0, qRound(side - m_documentMargins.left() * m_zoom));
        right = qMax(0, qRound(side - m_documentMargins.right() * m_zoom));
    }
    const int top = qMax(0, qRound(52.0 - m_documentMargins.top() * m_zoom));
    const int bottom = qMax(0, qRound(52.0 - m_documentMargins.bottom() * m_zoom));
    const QMargins want(left, top, right, bottom);
    if (viewportMargins() != want) {
        setViewportMargins(want);
    }
    // Continuous layout wraps at the viewport width. QTextEdit only re-applies
    // that on its own resize, so after the document's page size was cleared
    // (switching away from Full Page) or the inset changed, put it back here;
    // otherwise the text is laid out 0 px wide until the next window resize.
    if (!m_pageSize.isValid() && document()
        && !qFuzzyCompare(document()->pageSize().width(), continuousLayoutWidth())) {
        document()->setPageSize(QSizeF(continuousLayoutWidth(), -1));
    }
    adjustZoomedScrollBars();
}

void PageTextEdit::resizeEvent(QResizeEvent *event)
{
    QTextEdit::resizeEvent(event);
    updateInset();
    reapplyPageSize();
    adjustZoomedScrollBars();
}

void PageTextEdit::scrollContentsBy(int dx, int dy)
{
    if (!isZoomed()) {
        QTextEdit::scrollContentsBy(dx, dy);
        return;
    }
    // QTextEdit would scroll the viewport pixels by dx/dy document pixels;
    // zoomed, the picture moves zoom times as far. Just repaint.
    viewport()->update();
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
                        // A full relayout reports a ~1e9-sized rect: clip in floating
                        // point first, as int QRect arithmetic on it overflows.
                        const QRectF d = r.translated(-scrollOffset());
                        const QRectF v(d.topLeft() * m_zoom, d.size() * m_zoom);
                        viewport()->update(
                            v.adjusted(-2, -2, 2, 2).intersected(QRectF(viewport()->rect())).toAlignedRect());
                    }
                });
        // QTextEdit re-sizes its scroll bars for zoom 1 from several private
        // paths (document size changes, relayouts, show); correct them each time.
        // It sets the page step right after the range, so fix that once the
        // event loop is back.
        const auto fix = [this]() {
            adjustZoomedScrollBars();
            QMetaObject::invokeMethod(this, [this]() { adjustZoomedScrollBars(); }, Qt::QueuedConnection);
        };
        connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, fix);
        connect(horizontalScrollBar(), &QScrollBar::rangeChanged, this, fix);
        connect(this, &QTextEdit::cursorPositionChanged, this, [this]() {
            if (isZoomed()) {
                restartCaretBlink();
                viewport()->update();
                // After QTextEdit's own (unzoomed) ensureCursorVisible().
                QTimer::singleShot(0, this, [this]() { ensureZoomedCaretVisible(); });
            }
        });
        connect(this, &QTextEdit::selectionChanged, this, [this]() {
            if (isZoomed()) {
                viewport()->update();
            }
        });
    }
    // Keep the same document line at the top of the view across the change.
    const int topDoc = verticalScrollBar()->value();
    m_zoom = zoom;
    if (m_continuousInset) {
        updateInset(); // the column width and the layout width follow the zoom
    }
    if (!isZoomed()) {
        // Let QTextEdit size its scroll bars for zoom 1 again.
        QResizeEvent re(size(), size());
        QTextEdit::resizeEvent(&re);
        updateInset();
        reapplyPageSize();
    }
    adjustZoomedScrollBars();
    verticalScrollBar()->setValue(topDoc);
    restartCaretBlink();
    viewport()->update();
}

QPointF PageTextEdit::scrollOffset() const
{
    return QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
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

// Dot leaders: a table of contents line "entry<TAB>page" (a LibreOffice one kept
// from the file, or zwriter's generated one) gets dots across its tab, as it
// prints. Qt's text layout has no leader tabs, so they are painted on top; the
// document text is not changed.
void PageTextEdit::paintLeaders(QPainter *p, const QRectF &docClip) const
{
    const QTextDocument *doc = document();
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat f = b.blockFormat();
        if (!(f.hasProperty(DocumentIo::TocProperty) || f.hasProperty(DocumentIo::GeneratedTocProperty))) {
            continue;
        }
        const int tab = int(b.text().lastIndexOf(QLatin1Char('\t')));
        if (tab < 0 || tab + 1 >= b.text().size()) {
            continue;
        }
        const QRectF br = layout->blockBoundingRect(b);
        if (!br.intersects(docClip)) {
            continue;
        }
        const QTextLayout *tl = b.layout();
        const QTextLine line = tl->lineForTextPosition(tab);
        if (!line.isValid() || tl->lineForTextPosition(tab + 1).lineNumber() != line.lineNumber()) {
            continue;
        }
        QTextCursor at(b);
        at.setPosition(b.position() + tab + 1);
        const QTextCharFormat cf = at.charFormat();
        const QFontMetricsF fm(cf.font());
        const qreal step = fm.horizontalAdvance(QStringLiteral(". "));
        const qreal from = line.cursorToX(tab) + fm.horizontalAdvance(QLatin1Char(' '));
        const qreal to = line.cursorToX(tab + 1) - fm.horizontalAdvance(QLatin1Char(' '));
        if (step <= 0 || to - from < step) {
            continue;
        }
        p->save();
        p->setFont(cf.font());
        p->setPen(cf.foreground().style() == Qt::NoBrush ? palette().color(QPalette::Text) : cf.foreground().color());
        const qreal y = br.top() + line.y() + line.ascent();
        // On a common grid, so the dots of all lines sit in columns.
        for (qreal x = std::ceil(from / step) * step; x + fm.horizontalAdvance(QLatin1Char('.')) <= to; x += step) {
            p->drawText(QPointF(br.left() + x, y), QStringLiteral("."));
        }
        p->restore();
    }
}

void PageTextEdit::setTableResizeEnabled(bool on)
{
    m_tableResize = on;
    viewport()->setMouseTracking(true);
    if (!on) {
        endTableResize(false);
    }
}

QPointF PageTextEdit::docPoint(const QMouseEvent *event) const
{
    return event->position() / m_zoom + scrollOffset();
}

bool PageTextEdit::beginTableResize(const QPointF &docPos)
{
    if (!m_tableResize || isReadOnly()) {
        return false;
    }
    QTextTable *t = TableColumns::tableAt(document(), docPos);
    const int border = TableColumns::borderAt(t, docPos, 4.0);
    if (border < 0) {
        return false;
    }
    const QList<double> x = TableColumns::edges(t);
    m_resizeTable = t;
    m_resizeBorder = border;
    m_resizeStart = TableColumns::effectivePercents(t);
    m_resizeX0 = x.at(border);
    m_resizeWidth = qMax(1.0, x.last() - x.first());
    m_guideX = m_resizeX0;
    viewport()->update();
    return true;
}

void PageTextEdit::moveTableResize(const QPointF &docPos)
{
    if (m_resizeBorder < 0) {
        return;
    }
    // Clamp the guide to where the widths can actually go.
    const QList<double> p = TableColumns::dragged(m_resizeStart, m_resizeBorder,
                                                  (docPos.x() - m_resizeX0) / m_resizeWidth * 100.0);
    m_guideX = m_resizeX0 + (p.at(m_resizeBorder - 1) - m_resizeStart.at(m_resizeBorder - 1)) / 100.0 * m_resizeWidth;
    viewport()->update();
}

void PageTextEdit::endTableResize(bool apply)
{
    if (m_resizeBorder < 0) {
        return;
    }
    const int border = m_resizeBorder;
    m_resizeBorder = -1;
    viewport()->update();
    if (!apply || !m_resizeTable || m_resizeStart.size() != m_resizeTable->columns()
        || qFuzzyCompare(m_guideX, m_resizeX0)) {
        return;
    }
    const QList<double> p = TableColumns::dragged(m_resizeStart, border,
                                                  (m_guideX - m_resizeX0) / m_resizeWidth * 100.0);
    QTextCursor edit(document());
    edit.beginEditBlock(); // one undo step
    TableColumns::setPercents(m_resizeTable, p);
    edit.endEditBlock();
    emit tableColumnsResized();
}

void PageTextEdit::paintTableResizeGuide()
{
    if (m_resizeBorder < 0 || !m_resizeTable) {
        return;
    }
    const QRectF frame = document()->documentLayout()->frameBoundingRect(m_resizeTable);
    const QPointF off = scrollOffset();
    QPainter p(viewport());
    QPen pen(palette().color(QPalette::Highlight), 1.0, Qt::DashLine);
    p.setPen(pen);
    const double x = (m_guideX - off.x()) * m_zoom;
    p.drawLine(QPointF(x, (frame.top() - off.y()) * m_zoom), QPointF(x, (frame.bottom() - off.y()) * m_zoom));
}

void PageTextEdit::paintEvent(QPaintEvent *event)
{
    if (!isZoomed()) {
        QTextEdit::paintEvent(event);
        {
            QPainter p(viewport());
            const QPointF offset(horizontalScrollBar()->value(), verticalScrollBar()->value());
            p.translate(-offset);
            paintLeaders(&p, QRectF(event->rect()).translated(offset));
        }
        paintTableResizeGuide();
        return;
    }
    // Draw the true-size layout scaled, with the caret and selections QTextEdit
    // would draw (its own painter cannot take a transform).
    // Scroll offsets are in document pixels (continuous view; Full Page
    // never scrolls the editor itself, so they are 0 there).
    QPainter p(viewport());
    p.scale(m_zoom, m_zoom);
    const QPointF offset = scrollOffset();
    p.translate(-offset);
    const QRectF r = event->rect();
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.clip = QRectF(r.topLeft() / m_zoom, r.size() / m_zoom).translated(offset).adjusted(-1, -1, 1, 1);
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
    paintLeaders(&p, ctx.clip);
    p.end();
    paintTableResizeGuide();
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
    if (event->button() == Qt::LeftButton && beginTableResize(docPoint(event))) {
        event->accept();
        return;
    }
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
    if (m_resizeBorder >= 0) {
        moveTableResize(docPoint(event));
        event->accept();
        return;
    }
    if (m_tableResize && event->buttons() == Qt::NoButton) {
        const QPointF d = docPoint(event);
        const bool over = !isReadOnly()
            && TableColumns::borderAt(TableColumns::tableAt(document(), d), d, 4.0) >= 0;
        if (over != m_resizeCursor) {
            m_resizeCursor = over;
            viewport()->setCursor(over ? Qt::SplitHCursor : Qt::IBeamCursor);
        }
    }
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
    if (m_resizeBorder >= 0 && event->button() == Qt::LeftButton) {
        moveTableResize(docPoint(event));
        endTableResize(true);
        event->accept();
        return;
    }
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
    const QPointF pos = (event->position().toPoint() / m_zoom).toPointF(); // QPointF ctor; the QPoint one is deprecated
#else
    const QPoint pos = event->position().toPoint() / m_zoom;
#endif
    QDragMoveEvent e(pos, event->possibleActions(),
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
