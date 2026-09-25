#include "PageWidgets.hpp"

#include <QMimeData>
#include <QPaintEvent>
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
