#pragma once

#include "DocumentIo.hpp"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringView>
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

} // namespace Images
