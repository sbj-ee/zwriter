#pragma once

#include "DocumentIo.hpp"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringView>
#include <QSize>
#include <QTextCursor>
#include <QTextImageFormat>

class QTextDocument;

// Images in documents. An image is a QTextImageFormat in the text whose name is
// a document image resource holding the file's original bytes (PNG stays PNG,
// JPEG stays JPEG). ODT saves embed them in the package's Pictures/ folder.
namespace Images {

// True if `data` decodes as an image this build of Qt can read.
bool isDecodable(const QByteArray &data);

// Add the bytes as an image resource under a fresh name ("zw-image-<n>.<suffix>")
// and return that name ("" if the data is not an image).
QString addImageData(QTextDocument *doc, const QByteArray &data, const QString &suffix = QString());

// Every image in the document, in document order.
QList<QTextImageFormat> imageFormats(const QTextDocument *doc);
bool hasImages(const QTextDocument *doc);

// Whether a save in this format keeps images (ODT does; TXT, RTF and
// Markdown do not yet, so the window asks before saving without them).
bool formatKeepsImages(DocumentIo::Format format);

// An ODF length ("2.54cm", "1in", "72pt", "25.4mm", "96px", "3pc") in CSS px
// (96 per inch). 0 for an empty or unknown length.
qreal odfLengthToPx(QStringView length);

// How text runs around an image. Left: the image sits at the left of the
// column and text flows down its right side; Right: the mirror image.
enum class Wrap { Inline, Left, Right };

// Image files zwriter inserts (by extension): PNG, JPEG, GIF (first frame),
// WebP, BMP, and SVG when Qt's SVG image plugin is installed.
bool isImageFile(const QString &path);
QString imageFileFilter(); // for QFileDialog

// Largest side an inserted image keeps; bigger ones are scaled down.
constexpr int kMaxImageSide = 4000;

// The text column width in document px (page or view width minus margins).
qreal columnWidth(const QTextDocument *doc);

// Add image bytes for insertion: PNG and JPEG keep their bytes, other types
// become PNG (what ODT stores anyway), anything over kMaxImageSide is scaled
// down. Returns the resource name ("" + *message on failure); *natural gets the
// stored size, *message a note when the image was scaled down.
QString addInsertableImage(QTextDocument *doc, const QByteArray &data, QSize *natural,
                           QString *message);

// Insert an image at the cursor (replacing any selection), in line with the
// text, at its natural size capped to the column width. One undo step.
bool insertImage(QTextCursor &cursor, const QByteArray &data, QString *message = nullptr);

// A cursor selecting the image character at `position` (null if none).
QTextCursor imageAt(QTextDocument *doc, int position);

Wrap wrapOf(const QTextDocument *doc, const QTextCharFormat &format);
// The stored format of the character at `position` (QTextCursor::charFormat()
// drops the object index of floating images).
QTextCharFormat formatAt(const QTextDocument *doc, int position);
Wrap wrapAt(const QTextDocument *doc, int position);

// Replace the selected image character with the same picture at w x h and the
// given wrap, keeping the selection on it. One undo step.
void applyImage(QTextCursor &selection, const QString &name, qreal width, qreal height, Wrap wrap);

// The decoded size of an image resource (invalid if it does not decode).
QSize naturalSize(const QTextDocument *doc, const QString &name);

} // namespace Images
