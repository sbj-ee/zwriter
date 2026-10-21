#include "PrintLayout.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QScreen>
#include <QScopedPointer>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextFrameFormat>

namespace PrintLayout {

qreal layoutDpi()
{
    if (QCoreApplication::testAttribute(Qt::AA_Use96Dpi)) {
        return 96.0;
    }
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        return qRound(screen->logicalDotsPerInchY());
    }
    return 100.0; // Qt's own fallback without a screen
}

QSizeF sheetSizeMm(const QPageLayout &layout)
{
    return layout.fullRect(QPageLayout::Millimeter).size();
}

QTextDocument *copyForPrint(const QTextDocument &src)
{
    QTextDocument *doc = src.clone();
    if (src.begin().isValid() && doc->begin().isValid()) {
        QTextCursor(doc->begin()).setBlockFormat(src.begin().blockFormat());
    }
    return doc;
}

void preparePages(QTextDocument *doc, const Page &page)
{
    const qreal ldpi = layoutDpi();
    auto mmToLayout = [ldpi](qreal mm) { return mm * ldpi / 25.4; };
    doc->setDocumentMargin(0);
    doc->setPageSize(QSizeF(mmToLayout(page.sheetMm.width()), mmToLayout(page.sheetMm.height())));
    QTextFrameFormat fmt = doc->rootFrame()->frameFormat();
    fmt.setLeftMargin(mmToLayout(page.marginsMm.left()));
    fmt.setRightMargin(mmToLayout(page.marginsMm.right()));
    fmt.setTopMargin(mmToLayout(page.marginsMm.top()));
    fmt.setBottomMargin(mmToLayout(page.marginsMm.bottom()));
    doc->rootFrame()->setFrameFormat(fmt);
}

int pageOf(QTextDocument *doc, int position)
{
    const QTextBlock b = doc->findBlock(position);
    const qreal h = doc->pageSize().height();
    if (!b.isValid() || h <= 0) {
        return 1;
    }
    const QRectF r = doc->documentLayout()->blockBoundingRect(b);
    // A block's top margin can sit at the end of the previous page; use the
    // first line instead.
    qreal y = r.top();
    if (b.layout() && b.layout()->lineCount() > 0) {
        y += b.layout()->lineAt(0).y();
    }
    return int(y / h) + 1;
}

qreal textWidth(const Page &page)
{
    return (page.sheetMm.width() - page.marginsMm.left() - page.marginsMm.right()) * layoutDpi() / 25.4;
}

int paintDocument(const QTextDocument &src, QPainter *painter, const Page &page,
                  qreal deviceDpiX, qreal deviceDpiY, const NewPageFn &newPage,
                  const OverlayFn &overlay, int maxPages)
{
    if (!painter || page.sheetMm.isEmpty() || deviceDpiX <= 0 || deviceDpiY <= 0) {
        return 0;
    }
    const qreal ldpi = layoutDpi();

    // A clone keeps every format (tables, page breaks, lists) and leaves the
    // editor's document, its layout and its undo stack alone.
    QScopedPointer<QTextDocument> doc(copyForPrint(src));
    doc->setDefaultFont(src.defaultFont());
    doc->setDocumentMargin(0);
    doc->setUndoRedoEnabled(false);

    preparePages(doc.data(), page);
    const QSizeF pageLayoutPx = doc->pageSize();

    int pages = qMax(1, doc->pageCount());
    if (maxPages > 0) {
        pages = qMin(pages, maxPages);
    }
    const qreal sx = deviceDpiX / ldpi;
    const qreal sy = deviceDpiY / ldpi;
    const QRectF sheetDevice(0, 0, page.sheetMm.width() * deviceDpiX / 25.4,
                             page.sheetMm.height() * deviceDpiY / 25.4);

    for (int i = 0; i < pages; ++i) {
        if (i > 0 && newPage && !newPage()) {
            return i;
        }
        painter->save();
        painter->scale(sx, sy);
        const QRectF view(0, i * pageLayoutPx.height(), pageLayoutPx.width(), pageLayoutPx.height());
        painter->setClipRect(QRectF(QPointF(0, 0), pageLayoutPx));
        painter->translate(0, -view.top());
        doc->drawContents(painter, view);
        painter->restore();
        if (overlay) {
            overlay(painter, sheetDevice, i + 1, pages);
        }
    }
    return pages;
}

} // namespace PrintLayout
