#pragma once

#include <QString>
#include <QStringList>

#include <functional>

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
    Markdown, // .md / .markdown: plain-text model, saved back byte-faithfully
    Unknown,
};

Format formatFromPath(const QString &path);
QString formatName(Format format);
QByteArray writerFormat(Format format); // for QTextDocumentWriter where applicable

// Filters for QFileDialog. Open lists ODT/TXT/RTF/Markdown; save prefers ODT first.
QString openFilter();
QString saveFilter();
Format formatFromFilter(const QString &selectedFilter, const QString &pathHint);

// The document font's family list: Courier, then Courier-class monospace faces.
QStringList defaultFontFamilies();
// The face headings use (Heading 1-3, and a new chapter's title): the
// application's UI sans face, then common sans faces. Body text stays Courier.
QStringList headingFontFamilies();

// zwriter's table look (Insert Table, reopened ODT/RTF tables): a single
// 1 px light-grey grid with collapsed borders, 8 px padding, 100 % width.
// Call again after inserting rows/columns so the new cells get borders.
constexpr qreal kTableBorderPx = 1.0;
const QColor &tableBorderColor();
void styleTable(QTextTable *table);

// Markdown files are plain text: load() puts the text in the document as-is
// (no Markdown is rendered) and remembers the file's line ending (LF or CRLF)
// and UTF-8 BOM on the document; save() writes the text back with them, so an
// unedited valid-UTF-8 file with one line-ending style saves byte for byte as
// it was read. (A file mixing LF and CRLF is written with the style of its
// first line break; see markdownLoadWasLossy().)
// Line separators (Shift+Enter) are written as line endings.
QString markdownText(const QTextDocument *doc);
// Forget a loaded file's line ending / BOM (new documents: LF, no BOM).
void resetMarkdownFileFormat(QTextDocument *doc);
// True when the file just loaded cannot come back byte for byte: it mixes
// line endings / has lone CRs, or is not valid UTF-8 (shown as U+FFFD).
bool markdownLoadWasLossy(const QTextDocument *doc);

// Block property of paragraphs that came from an ODT text:table-of-content
// (value: index of the table in the file). Saving to ODT writes the original
// table back in their place, unchanged (see the "zwriter.odtTocs" property).
constexpr int TocProperty = 0x100000 + 0x5B01; // QTextFormat::UserProperty + 0x5B01
// A table of contents zwriter generated (BookToc): entry paragraphs carry
// their level (1 = chapter), the "Contents" title paragraph TocTitleProperty.
// Saved to ODT as a text:table-of-content named "zwriter-toc", read back the same.
constexpr int GeneratedTocProperty = 0x100000 + 0x5C01;
constexpr int GeneratedTocTitleProperty = 0x100000 + 0x5C02;
bool load(QTextDocument *doc, const QString &path, QString *error = nullptr);
// Saving is atomic: the file is built in a hidden temp file next to the
// target (".<name>.XXXXXX.tmp"), patched and checked there, and only then
// renamed over the target. The previous file is kept as "<path>.bak". If any
// step fails (write, ODT heading/picture patch, the caller's patch, the check,
// the backup or the rename) save() returns false with *error set, the temp
// file is removed and the target is left exactly as it was.
// `patch`, when set, runs on the temp file after zwriter's own ODT patches
// (MainWindow uses it to write meta.xml / styles.xml); returning false fails
// the save.
using SavePatch = std::function<bool(const QString &tempPath, QString *error)>;
bool save(QTextDocument *doc, const QString &path, Format format, QString *error = nullptr);
bool save(QTextDocument *doc, const QString &path, Format format, QString *error, const SavePatch &patch);
// The backup save() keeps of the file it replaced: path + ".bak".
QString backupPath(const QString &path);

} // namespace DocumentIo
