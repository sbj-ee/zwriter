#include "PageSetup.hpp"

#include <QSettings>

namespace PageSetup {

Unit rulerUnit()
{
    const QString v = QSettings().value(QLatin1String(kRulerUnitsKey), QStringLiteral("in")).toString();
    return v == QLatin1String("cm") ? Unit::Cm : Unit::Inch;
}

void setRulerUnit(Unit unit)
{
    QSettings().setValue(QLatin1String(kRulerUnitsKey), unitSuffix(unit));
}

QString unitSuffix(Unit unit)
{
    return unit == Unit::Cm ? QStringLiteral("cm") : QStringLiteral("in");
}

QString unitName(Unit unit)
{
    return unit == Unit::Cm ? QStringLiteral("Centimetres") : QStringLiteral("Inches");
}

QPageSize::PageSizeId newDocumentSizeId()
{
    const int v = QSettings().value(QLatin1String(kNewDocSizeKey), int(QPageSize::Letter)).toInt();
    const auto id = static_cast<QPageSize::PageSizeId>(v);
    return offeredSizes().contains(id) ? id : QPageSize::Letter;
}

void setNewDocumentSizeId(QPageSize::PageSizeId id)
{
    QSettings().setValue(QLatin1String(kNewDocSizeKey), int(id));
}

QList<QPageSize::PageSizeId> offeredSizes()
{
    return {QPageSize::Letter, QPageSize::A4, QPageSize::Legal, QPageSize::A5};
}

QString sizeLabel(QPageSize::PageSizeId id)
{
    switch (id) {
    case QPageSize::Letter:
        return QStringLiteral("US Letter (8.5 × 11 in)");
    case QPageSize::Legal:
        return QStringLiteral("US Legal (8.5 × 14 in)");
    case QPageSize::A4:
        return QStringLiteral("A4 (210 × 297 mm)");
    case QPageSize::A5:
        return QStringLiteral("A5 (148 × 210 mm)");
    default:
        break;
    }
    const QPageSize ps(id);
    const QSizeF mm = ps.size(QPageSize::Millimeter);
    return QStringLiteral("%1 (%2 × %3 mm)").arg(ps.name()).arg(qRound(mm.width())).arg(qRound(mm.height()));
}

QPageSize sizeFromMm(const QSizeF &sheetMm)
{
    const qreal w = qMin(sheetMm.width(), sheetMm.height());
    const qreal h = qMax(sheetMm.width(), sheetMm.height());
    const QList<QPageSize::PageSizeId> known = {QPageSize::Letter, QPageSize::A4, QPageSize::Legal,
                                                QPageSize::A5, QPageSize::A3, QPageSize::B5,
                                                QPageSize::Executive};
    for (QPageSize::PageSizeId id : known) {
        const QSizeF mm = QPageSize(id).size(QPageSize::Millimeter);
        if (qAbs(mm.width() - w) <= 1.0 && qAbs(mm.height() - h) <= 1.0) {
            return QPageSize(id);
        }
    }
    return QPageSize(QSizeF(w, h), QPageSize::Millimeter, QStringLiteral("Custom"), QPageSize::ExactMatch);
}

RulerScale rulerScale(Unit unit)
{
    if (unit == Unit::Cm) {
        return {96.0 / 2.54, 2, 0, 96.0 / 2.54 / 4.0};
    }
    return {96.0, 8, 4, 96.0 / 16.0};
}

qreal unitToPx(qreal value, Unit unit)
{
    return value * rulerScale(unit).pxPerUnit;
}

qreal pxToUnit(qreal px, Unit unit)
{
    return px / rulerScale(unit).pxPerUnit;
}

} // namespace PageSetup
