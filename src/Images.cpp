#include "Images.hpp"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextFrame>
#include <QTextFrameFormat>
#include <QImage>
#include <QImageReader>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QUrl>
#include <QVariant>

namespace Images {

namespace {
constexpr const char *kSeqProperty = "zwriter_imageSeq";
}

bool isDecodable(const QByteArray &data)
{
    if (data.isEmpty()) {
        return false;
    }
    return !QImage::fromData(data).isNull();
}

QString addImageData(QTextDocument *doc, const QByteArray &data, const QString &suffix)
{
    if (!doc || !isDecodable(data)) {
        return QString();
    }
    QString ext = suffix.toLower();
    if (ext.isEmpty()) {
        QByteArray copy = data;
        QBuffer buf(&copy);
        buf.open(QIODevice::ReadOnly);
        ext = QString::fromLatin1(QImageReader(&buf).format()).toLower();
        if (ext.isEmpty()) {
            ext = QStringLiteral("png");
        }
    }
    int seq = doc->property(kSeqProperty).toInt();
    QString name;
    do {
        name = QStringLiteral("zw-image-%1.%2").arg(++seq).arg(ext);
    } while (!doc->resource(QTextDocument::ImageResource, QUrl(name)).isNull());
    doc->setProperty(kSeqProperty, seq);
    doc->addResource(QTextDocument::ImageResource, QUrl(name), data);
    return name;
}

QList<QTextImageFormat> imageFormats(const QTextDocument *doc)
{
    QList<QTextImageFormat> out;
    if (!doc) {
        return out;
    }
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat f = it.fragment().charFormat();
            if (f.isImageFormat()) {
                out << f.toImageFormat();
            }
        }
    }
    return out;
}

bool hasImages(const QTextDocument *doc)
{
    return !imageFormats(doc).isEmpty();
}

bool formatKeepsImages(DocumentIo::Format format)
{
    return format == DocumentIo::Format::Odt || format == DocumentIo::Format::Rtf
        || format == DocumentIo::Format::Unknown;
}

qreal odfLengthToPx(QStringView length)
{
    const QString s = length.trimmed().toString().toLower();
    qsizetype i = 0;
    while (i < s.size() && (s.at(i).isDigit() || s.at(i) == QLatin1Char('.') || s.at(i) == QLatin1Char('-'))) {
        ++i;
    }
    bool ok = false;
    const qreal v = s.left(i).toDouble(&ok);
    if (!ok) {
        return 0.0;
    }
    const QString unit = s.mid(i);
    if (unit == QLatin1String("cm")) return v * 96.0 / 2.54;
    if (unit == QLatin1String("mm")) return v * 96.0 / 25.4;
    if (unit == QLatin1String("in")) return v * 96.0;
    if (unit == QLatin1String("pt")) return v * 96.0 / 72.0;
    if (unit == QLatin1String("pc")) return v * 16.0;
    if (unit == QLatin1String("px") || unit.isEmpty()) return v;
    return 0.0;
}

bool isImageFile(const QString &path)
{
    static const QStringList exts{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                  QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("bmp"),
                                  QStringLiteral("svg")};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

QString imageFileFilter()
{
    return QStringLiteral("Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg);;All files (*)");
}

qreal columnWidth(const QTextDocument *doc)
{
    if (!doc) {
        return 600.0;
    }
    qreal w = doc->pageSize().width();
    if (w <= 1.0) {
        w = doc->textWidth();
    }
    if (w <= 1.0) {
        return 600.0;
    }
    const QTextFrameFormat fmt = doc->rootFrame()->frameFormat();
    w -= fmt.leftMargin() + fmt.rightMargin() + 2.0 * (fmt.border() + fmt.padding())
        + 2.0 * doc->documentMargin();
    return qMax(40.0, w);
}

QString addInsertableImage(QTextDocument *doc, const QByteArray &data, QSize *natural, QString *message)
{
    auto fail = [&](const QString &msg) {
        if (message) {
            *message = msg;
        }
        return QString();
    };
    if (!doc) {
        return fail(QStringLiteral("No document."));
    }
    QByteArray copy = data;
    QBuffer buf(&copy);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    const QByteArray type = reader.format().toLower();
    QImage img = reader.read();
    if (img.isNull()) {
        const bool svg = data.left(512).contains("<svg");
        return fail(svg ? QStringLiteral("This SVG can't be shown: Qt's SVG image plugin is not installed.")
                        : QStringLiteral("That file is not an image zwriter can show."));
    }
    QString note;
    const bool tooBig = img.width() > kMaxImageSide || img.height() > kMaxImageSide;
    if (tooBig) {
        const QSize from = img.size();
        img = img.scaled(kMaxImageSide, kMaxImageSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        note = QStringLiteral("Image scaled down from %1×%2 to %3×%4.")
                   .arg(from.width()).arg(from.height()).arg(img.width()).arg(img.height());
    }
    QByteArray bytes;
    QString suffix;
    const bool jpeg = type == "jpeg" || type == "jpg";
    if (!tooBig && (type == "png" || jpeg)) {
        bytes = data;
        suffix = jpeg ? QStringLiteral("jpeg") : QStringLiteral("png");
    } else {
        QBuffer out(&bytes);
        out.open(QIODevice::WriteOnly);
        if (jpeg) {
            img.save(&out, "JPEG", 90);
            suffix = QStringLiteral("jpeg");
        } else {
            img.save(&out, "PNG");
            suffix = QStringLiteral("png");
        }
    }
    const QString name = addImageData(doc, bytes, suffix);
    if (name.isEmpty()) {
        return fail(QStringLiteral("That file is not an image zwriter can show."));
    }
    if (natural) {
        *natural = img.size();
    }
    if (message) {
        *message = note;
    }
    return name;
}

bool insertImage(QTextCursor &cursor, const QByteArray &data, QString *message)
{
    QTextDocument *doc = cursor.document();
    QSize natural;
    const QString name = addInsertableImage(doc, data, &natural, message);
    if (name.isEmpty()) {
        return false;
    }
    qreal w = natural.width();
    qreal h = natural.height();
    const qreal col = columnWidth(doc);
    if (w > col) {
        h = h * col / w;
        w = col;
    }
    QTextImageFormat f;
    f.setName(name);
    f.setWidth(qRound(w));
    f.setHeight(qRound(h));
    cursor.beginEditBlock();
    cursor.insertImage(f);
    cursor.endEditBlock();
    return true;
}

QTextCursor imageAt(QTextDocument *doc, int position)
{
    if (!doc || position < 0 || position >= doc->characterCount()) {
        return QTextCursor();
    }
    QTextCursor c(doc);
    c.setPosition(position);
    c.setPosition(position + 1, QTextCursor::KeepAnchor);
    if (!c.charFormat().isImageFormat() || c.selectedText() != QString(QChar::ObjectReplacementCharacter)) {
        return QTextCursor();
    }
    return c;
}

Wrap wrapOf(const QTextDocument *doc, const QTextCharFormat &format)
{
    if (!doc || !format.isImageFormat() || format.objectIndex() < 0) {
        return Wrap::Inline;
    }
    const auto *frame = qobject_cast<const QTextFrame *>(doc->object(format.objectIndex()));
    if (!frame) {
        return Wrap::Inline;
    }
    switch (frame->frameFormat().position()) {
    case QTextFrameFormat::FloatLeft:
        return Wrap::Left;
    case QTextFrameFormat::FloatRight:
        return Wrap::Right;
    default:
        return Wrap::Inline;
    }
}

QTextCharFormat formatAt(const QTextDocument *doc, int position)
{
    if (!doc) {
        return QTextCharFormat();
    }
    const QTextBlock b = doc->findBlock(position);
    for (auto it = b.begin(); !it.atEnd(); ++it) {
        const QTextFragment f = it.fragment();
        if (position >= f.position() && position < f.position() + f.length()) {
            return f.charFormat();
        }
    }
    return QTextCharFormat();
}

Wrap wrapAt(const QTextDocument *doc, int position)
{
    return wrapOf(doc, formatAt(doc, position));
}

void applyImage(QTextCursor &selection, const QString &name, qreal width, qreal height, Wrap wrap)
{
    if (selection.isNull()) {
        return;
    }
    const int at = qMin(selection.position(), selection.anchor());
    selection.beginEditBlock();
    selection.removeSelectedText();
    if (wrap == Wrap::Inline) {
        QTextImageFormat f;
        f.setName(name);
        f.setWidth(qRound(width));
        f.setHeight(qRound(height));
        selection.insertImage(f);
    } else {
        // A floating image is a positioned object (an empty float frame
        // anchored to the image character); Qt's HTML importer is the public
        // way to create one.
        selection.insertHtml(QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\" style=\"float: %4\" />")
                                 .arg(name.toHtmlEscaped())
                                 .arg(qRound(width))
                                 .arg(qRound(height))
                                 .arg(wrap == Wrap::Left ? QStringLiteral("left") : QStringLiteral("right")));
    }
    selection.endEditBlock();
    selection.setPosition(at);
    selection.setPosition(at + 1, QTextCursor::KeepAnchor);
}

QSize naturalSize(const QTextDocument *doc, const QString &name)
{
    if (!doc) {
        return QSize();
    }
    const QVariant v = doc->resource(QTextDocument::ImageResource, QUrl(name));
    if (v.typeId() == QMetaType::QImage) {
        return v.value<QImage>().size();
    }
    if (v.typeId() == QMetaType::QPixmap) {
        return QSize();
    }
    QByteArray bytes = v.toByteArray();
    QBuffer buf(&bytes);
    buf.open(QIODevice::ReadOnly);
    return QImageReader(&buf).size();
}

QString copyToMarkdownAssets(const QString &markdownPath, const QByteArray &data, QString *message)
{
    auto fail = [&](const QString &msg) {
        if (message) {
            *message = msg;
        }
        return QString();
    };
    if (markdownPath.isEmpty()) {
        return fail(QStringLiteral("Save the Markdown file first: its pictures go in a folder next to it."));
    }
    // Same conversion rules as rich documents (scratch document, bytes only).
    QTextDocument scratch;
    QSize natural;
    QString note;
    const QString name = addInsertableImage(&scratch, data, &natural, &note);
    if (name.isEmpty()) {
        return fail(note);
    }
    const QByteArray bytes = scratch.resource(QTextDocument::ImageResource, QUrl(name)).toByteArray();
    const QString suffix = QFileInfo(name).suffix() == QLatin1String("jpeg") ? QStringLiteral("jpg")
                                                                             : QFileInfo(name).suffix();
    const QFileInfo md(markdownPath);
    const QString folderName = md.completeBaseName() + QStringLiteral(".assets");
    QDir dir(md.absolutePath());
    if (!dir.mkpath(folderName)) {
        return fail(QStringLiteral("Could not create the folder %1.").arg(folderName));
    }
    QString file;
    for (int n = 1; n < 100000; ++n) {
        const QString stem = QStringLiteral("image-%1").arg(n, 3, 10, QLatin1Char('0'));
        file = stem + QLatin1Char('.') + suffix;
        // One number per picture, whatever its type.
        if (QDir(dir.filePath(folderName)).entryList({stem + QStringLiteral(".*")}, QDir::Files).isEmpty()) {
            break;
        }
    }
    QSaveFile out(dir.filePath(folderName + QLatin1Char('/') + file));
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        return fail(QStringLiteral("Could not write the picture into %1.").arg(folderName));
    }
    if (message) {
        *message = note;
    }
    return folderName + QLatin1Char('/') + file;
}

QString markdownImageLink(const QString &relativePath, const QString &alt)
{
    QString target = relativePath;
    target.replace(QLatin1Char(' '), QStringLiteral("%20"));
    target.replace(QLatin1Char('('), QStringLiteral("%28"));
    target.replace(QLatin1Char(')'), QStringLiteral("%29"));
    // Qt's Markdown reader drops images with empty alt text: default to the
    // file name without its extension.
    QString text = alt.isEmpty() ? QFileInfo(relativePath).completeBaseName() : alt;
    text.replace(QLatin1Char('['), QStringLiteral("\\[")).replace(QLatin1Char(']'), QStringLiteral("\\]"));
    return QStringLiteral("![%1](%2)").arg(text, target);
}

} // namespace Images
