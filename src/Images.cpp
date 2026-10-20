#include "Images.hpp"

#include <QBuffer>
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
    return format == DocumentIo::Format::Odt || format == DocumentIo::Format::Unknown;
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

} // namespace Images
