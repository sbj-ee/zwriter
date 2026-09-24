#pragma once

#include <QString>

class QTextDocument;

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

bool load(QTextDocument *doc, const QString &path, QString *error = nullptr);
bool save(QTextDocument *doc, const QString &path, Format format, QString *error = nullptr);

} // namespace DocumentIo
