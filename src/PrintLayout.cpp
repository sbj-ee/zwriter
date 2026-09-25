#include "PrintLayout.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPainter>
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

int paintDocument(const QTextDocument &src, QPainter *painter, const Page &page,
                  qreal deviceDpiX, qreal deviceDpiY, const NewPageFn &newPage,
                  const OverlayFn &overlay, int maxPages)
{
    if (!painter || page.sheetMm.isEmpty() || deviceDpiX <= 0 || deviceDpiY <= 0) {
        return 0;
    }
    const qreal ldpi = layoutDpi();
    auto mmToLayout = [ldpi](qreal mm) { return mm * ldpi / 25.4; };

    // A clone keeps every format (tables, page breaks, lists) and leaves the
    // editor's document, its layout and its undo stack alone.
    QScopedPointer<QTextDocument> doc(src.clone());
    doc->setDefaultFont(src.defaultFont());
    doc->setDocumentMargin(0);
    doc->setUndoRedoEnabled(false);

    const QSizeF pageLayoutPx(mmToLayout(page.sheetMm.width()), mmToLayout(page.sheetMm.height()));
    doc->setPageSize(pageLayoutPx);
    QTextFrameFormat fmt = doc->rootFrame()->frameFormat();
    fmt.setLeftMargin(mmToLayout(page.marginsMm.left()));
    fmt.setRightMargin(mmToLayout(page.marginsMm.right()));
    fmt.setTopMargin(mmToLayout(page.marginsMm.top()));
    fmt.setBottomMargin(mmToLayout(page.marginsMm.bottom()));
    doc->rootFrame()->setFrameFormat(fmt);

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
