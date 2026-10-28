#pragma once

// Page size and ruler units (1.12.0).
//
// - The ruler shows inches or centimetres (View settings, default inches) and
//   always says which ("in" / "cm") at its left end.
// - Every document has its own page size. ODT files store it in styles.xml
//   (zwriter already writes it there on every save); a file that does not say, and
//   any Markdown or plain-text file, opens on A4, the size zwriter always used
//   before, so nothing changes for existing documents.
// - New documents start on the "new document" size, US Letter by default.

#include <QList>
#include <QPageSize>
#include <QSizeF>
#include <QString>

namespace PageSetup {

enum class Unit { Inch, Cm };

// Settings keys.
inline constexpr const char *kRulerUnitsKey = "view/rulerUnits";     // "in" | "cm"
inline constexpr const char *kNewDocSizeKey = "page/newDocumentSize"; // QPageSize::PageSizeId

Unit rulerUnit();             // from QSettings, default Inch
void setRulerUnit(Unit unit); // to QSettings
QString unitSuffix(Unit unit);  // "in" / "cm"
QString unitName(Unit unit);    // "Inches" / "Centimetres"

// Page size for documents created from now on (default US Letter).
QPageSize::PageSizeId newDocumentSizeId();
void setNewDocumentSizeId(QPageSize::PageSizeId id);
// Size given to a document that does not store one (legacy ODT, Markdown, text).
inline constexpr QPageSize::PageSizeId kLegacySizeId = QPageSize::A4;

// Sizes offered in the settings dialog, Letter first.
QList<QPageSize::PageSizeId> offeredSizes();
// "US Letter (8.5 × 11 in)", "A4 (210 × 297 mm)", ...
QString sizeLabel(QPageSize::PageSizeId id);

// The standard size whose portrait dimensions match `portraitMm` within 1 mm
// (either orientation), else a custom QPageSize of exactly that size.
QPageSize sizeFromMm(const QSizeF &sheetMm);

// Ruler geometry, in document pixels at 96 dpi.
struct RulerScale {
    qreal pxPerUnit;  // one inch or one centimetre
    int ticksPerUnit; // 8 (1/8 in) or 2 (0.5 cm)
    int midTick;      // tick index drawn longer (4 = 1/2 in), 0 = none
    qreal snapPx;     // drag snap: 1/16 in or 0.25 cm
};
RulerScale rulerScale(Unit unit);
qreal unitToPx(qreal value, Unit unit);
qreal pxToUnit(qreal px, Unit unit);

} // namespace PageSetup
