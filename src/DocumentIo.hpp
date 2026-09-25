#pragma once

#include <QString>
#include <QStringList>

class QColor;
class QTextDocument;
class QTextTable;

// Native open/save helpers. Default save format is ODT.
// PDF is export-only (see MainWindow::exportPdf) — not handled here.
// No proprietary .zwriter format; DOCX intentionally omitted for v1.
namespace DocumentIo {

enum class Format {
    Odt,
    Txt,
    Rtf,
    Unknown,
};

Format formatFromPath(const QString &path);
QString formatName(Format format);
QByteArray writerFormat(Format format); // for QTextDocumentWriter where applicable

// Filters for QFileDialog. Open lists ODT/TXT/RTF; save prefers ODT first.
QString openFilter();
QString saveFilter();
Format formatFromFilter(const QString &selectedFilter, const QString &pathHint);

// The document font's family list: Courier, then Courier-class monospace faces.
QStringList defaultFontFamilies();

// zwriter's table look (Insert Table, reopened ODT/RTF tables): a single
// 1 px light-grey grid with collapsed borders, 8 px padding, 100 % width.
// Call again after inserting rows/columns so the new cells get borders.
constexpr qreal kTableBorderPx = 1.0;
const QColor &tableBorderColor();
void styleTable(QTextTable *table);

bool load(QTextDocument *doc, const QString &path, QString *error = nullptr);
bool save(QTextDocument *doc, const QString &path, Format format, QString *error = nullptr);

} // namespace DocumentIo
