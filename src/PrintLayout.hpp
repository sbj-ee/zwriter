#pragma once

#include <QMarginsF>
#include <QPageLayout>
#include <QRectF>
#include <QSizeF>

#include <functional>

class QPainter;
class QTextDocument;

// Paginated painting of a QTextDocument onto a paged device (printer, PDF,
// print preview) so the output matches the Full Page view.
//
// QTextDocument lays text out in "layout pixels": point sizes are converted at
// the resolution fonts get when no paint device is set (the screen's logical
// DPI, normally 96; 72 on macOS). A printer is 300-1200 dpi, so the layout is
// built at layout DPI and the painter is scaled by deviceDpi / layoutDpi.
// Without that scale 12 pt body text comes out at ~1 pt on a 1200-dpi printer.
namespace PrintLayout {

// DPI QTextDocument uses for point sizes when it has no paint device
// (mirrors Qt's qt_defaultDpiY()).
qreal layoutDpi();

// Physical sheet size in mm, honouring the orientation (QPageLayout::pageSize()
// is always portrait).
QSizeF sheetSizeMm(const QPageLayout &layout);

struct Page {
    QSizeF sheetMm;     // oriented sheet size
    QMarginsF marginsMm; // text area margins
};

using NewPageFn = std::function<bool()>;
// Called after the body of each page is painted, in device coordinates
// (pageRect is the whole sheet in device pixels), for headers/footers.
using OverlayFn = std::function<void(QPainter *, const QRectF &pageRect, int page, int pages)>;

// Lays out a clone of `src` at layout DPI for `page` and paints every page.
// `painter` must be active on a device of deviceDpiX/Y whose origin is the
// sheet's top-left corner (QPrinter with fullPage(true)). Returns the page count.
int paintDocument(const QTextDocument &src, QPainter *painter, const Page &page,
                  qreal deviceDpiX, qreal deviceDpiY, const NewPageFn &newPage,
                  const OverlayFn &overlay = OverlayFn(), int maxPages = -1);

} // namespace PrintLayout
