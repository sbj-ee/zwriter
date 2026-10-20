// Images in documents (1.5.0 series).
//   U  ODF lengths convert to CSS px
//   F  which save formats keep images
//   R  an ODT with PNG + JPEG images saves and reopens with the pictures,
//      their displayed sizes and their original bytes (JPEG stays JPEG)
//   L  a LibreOffice-shaped ODT: frame sized in cm, SVG with a PNG fallback,
//      a missing picture member
// Run: QT_QPA_PLATFORM=offscreen ./images_test
#include "DocumentIo.hpp"
#include "Images.hpp"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.isEmpty() ? "" : "  -- ",
                qPrintable(detail));
    if (!ok) {
        ++g_failures;
    }
}

QByteArray encoded(const QImage &img, const char *fmt)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, fmt);
    return out;
}

QImage solid(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    return img;
}

QStringList zipMembers(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral("unzip"), {QStringLiteral("-Z1"), path});
    p.waitForFinished(15000);
    return QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QByteArray zipMember(const QString &path, const QString &member)
{
    QProcess p;
    p.start(QStringLiteral("unzip"), {QStringLiteral("-p"), path, member});
    p.waitForFinished(15000);
    return p.readAllStandardOutput();
}

QImage resourceImage(const QTextDocument &doc, const QTextImageFormat &f)
{
    const QVariant v = doc.resource(QTextDocument::ImageResource, QUrl(f.name()));
    if (v.typeId() == QMetaType::QImage) {
        return v.value<QImage>();
    }
    return QImage::fromData(v.toByteArray());
}

void testUnits()
{
    check(std::abs(Images::odfLengthToPx(u"1in") - 96.0) < 0.01, "U 1in = 96 px");
    check(std::abs(Images::odfLengthToPx(u"2.54cm") - 96.0) < 0.01, "U 2.54cm = 96 px");
    check(std::abs(Images::odfLengthToPx(u"25.4mm") - 96.0) < 0.01, "U 25.4mm = 96 px");
    check(std::abs(Images::odfLengthToPx(u"72pt") - 96.0) < 0.01, "U 72pt = 96 px");
    check(std::abs(Images::odfLengthToPx(u"30px") - 30.0) < 0.01, "U 30px = 30 px");
    check(Images::odfLengthToPx(u"") == 0.0 && Images::odfLengthToPx(u"12furlongs") == 0.0,
          "U empty / unknown unit = 0");
}

void testFormats()
{
    check(Images::formatKeepsImages(DocumentIo::Format::Odt), "F ODT keeps images");
    check(!Images::formatKeepsImages(DocumentIo::Format::Txt)
              && !Images::formatKeepsImages(DocumentIo::Format::Rtf)
              && !Images::formatKeepsImages(DocumentIo::Format::Markdown),
          "F TXT / RTF / Markdown don't (the window asks before dropping them)");
}

void testRoundTrip(const QString &dir)
{
    QTextDocument doc;
    QTextCursor c(&doc);
    c.insertText(QStringLiteral("A paragraph before the picture."));
    c.insertBlock();
    const QString png = Images::addImageData(&doc, encoded(solid(40, 30, QColor(200, 40, 40)), "PNG"));
    const QByteArray jpegBytes = encoded(solid(64, 48, QColor(40, 90, 200)), "JPEG");
    const QString jpg = Images::addImageData(&doc, jpegBytes);
    check(png.startsWith(QLatin1String("zw-image-")) && png.endsWith(QLatin1String(".png")),
          "R PNG resource gets a zw-image name", png);
    check(jpg.endsWith(QLatin1String(".jpeg")) || jpg.endsWith(QLatin1String(".jpg")),
          "R JPEG resource keeps its type", jpg);
    check(Images::addImageData(&doc, QByteArrayLiteral("not an image")).isEmpty(),
          "R non-image data is refused");
    QTextImageFormat f1;
    f1.setName(png);
    f1.setWidth(80);
    f1.setHeight(60);
    c.insertImage(f1);
    c.insertText(QStringLiteral(" text after it."));
    c.insertBlock();
    QTextImageFormat f2;
    f2.setName(jpg); // natural size
    c.insertImage(f2);
    check(Images::imageFormats(&doc).size() == 2 && Images::hasImages(&doc), "R two images in the source");

    const QString path = dir + QStringLiteral("/pictures.odt");
    QString err;
    check(DocumentIo::save(&doc, path, DocumentIo::Format::Odt, &err), "R save ODT", err);
    const QStringList members = zipMembers(path);
    const QStringList pics = members.filter(QStringLiteral("Pictures/"));
    check(pics.size() == 2, "R package has two Pictures/ members", members.join(QLatin1Char(' ')));
    const QByteArray manifest = zipMember(path, QStringLiteral("META-INF/manifest.xml"));
    bool listed = !pics.isEmpty();
    for (const QString &p : pics) {
        listed = listed && manifest.contains(p.toUtf8());
    }
    check(listed, "R every picture is in the manifest");
    bool jpegKept = false;
    for (const QString &p : pics) {
        jpegKept = jpegKept || zipMember(path, p) == jpegBytes;
    }
    check(jpegKept, "R the JPEG is stored byte for byte (not re-encoded)");
    check(manifest.contains("image/jpeg") && manifest.contains("image/png"),
          "R manifest media types: one image/png, one image/jpeg");

    QTextDocument back;
    check(DocumentIo::load(&back, path, &err), "R reopen ODT", err);
    const QList<QTextImageFormat> imgs = Images::imageFormats(&back);
    check(imgs.size() == 2, "R reopened document has two images", QString::number(imgs.size()));
    if (imgs.size() == 2) {
        check(std::abs(imgs[0].width() - 80) <= 1 && std::abs(imgs[0].height() - 60) <= 1,
              "R first image keeps its 80x60 display size",
              QStringLiteral("%1x%2").arg(imgs[0].width()).arg(imgs[0].height()));
        const QImage a = resourceImage(back, imgs[0]);
        check(a.size() == QSize(40, 30) && a.pixelColor(5, 5) == QColor(200, 40, 40),
              "R first image's pixels come back", QStringLiteral("%1x%2").arg(a.width()).arg(a.height()));
        const QImage b = resourceImage(back, imgs[1]);
        check(b.size() == QSize(64, 48), "R second (JPEG) image decodes at 64x48");
    }
    check(back.toPlainText().contains(QLatin1String("text after it.")), "R text around the image kept");
    check(!back.isUndoAvailable() && !back.isModified(), "R opening is not an edit");

    const QString path2 = dir + QStringLiteral("/pictures2.odt");
    check(DocumentIo::save(&back, path2, DocumentIo::Format::Odt, &err), "R save the reopened document");
    QTextDocument again;
    DocumentIo::load(&again, path2, &err);
    check(Images::imageFormats(&again).size() == 2, "R second save/open cycle keeps both images");
}

bool writeLibreOfficeImageOdt(const QString &path)
{
    QTemporaryDir dir;
    QDir(dir.path()).mkpath(QStringLiteral("META-INF"));
    QDir(dir.path()).mkpath(QStringLiteral("Pictures"));
    auto put = [&](const QString &name, const QByteArray &data) {
        QFile f(dir.filePath(name));
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    };
    put(QStringLiteral("mimetype"), "application/vnd.oasis.opendocument.text");
    put(QStringLiteral("META-INF/manifest.xml"),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
        "<manifest:file-entry manifest:full-path=\"Pictures/10000001.svg\" manifest:media-type=\"image/svg+xml\"/>"
        "<manifest:file-entry manifest:full-path=\"Pictures/10000002.png\" manifest:media-type=\"image/png\"/>"
        "</manifest:manifest>");
    // An "SVG" no Qt image plugin decodes, so the PNG fallback must be used.
    put(QStringLiteral("Pictures/10000001.svg"), "<svg xmlns=\"http://www.w3.org/2000/svg\"><broken");
    put(QStringLiteral("Pictures/10000002.png"), encoded(solid(20, 10, QColor(30, 160, 90)), "PNG"));
    put(QStringLiteral("content.xml"),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
        "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
        "xmlns:xlink=\"http://www.w3.org/1999/xlink\" office:version=\"1.3\">"
        "<office:body><office:text>"
        "<text:p>Field notes, day one.</text:p>"
        "<text:p><draw:frame draw:name=\"Image1\" text:anchor-type=\"as-char\" svg:width=\"2.54cm\" svg:height=\"1.27cm\">"
        "<draw:image xlink:href=\"Pictures/10000001.svg\" xlink:type=\"simple\"/>"
        "<draw:image xlink:href=\"Pictures/10000002.png\" xlink:type=\"simple\"/>"
        "</draw:frame>A sketch of the trail.</text:p>"
        "<text:p><draw:frame draw:name=\"Gone\" svg:width=\"1in\" svg:height=\"1in\">"
        "<draw:image xlink:href=\"Pictures/missing.png\"/></draw:frame>After the missing one.</text:p>"
        "</office:text></office:body></office:document-content>");
    QFile::remove(path);
    QProcess z0;
    z0.setWorkingDirectory(dir.path());
    z0.start(QStringLiteral("zip"), {QStringLiteral("-X"), QStringLiteral("-0"), path, QStringLiteral("mimetype")});
    z0.waitForFinished(15000);
    QProcess z1;
    z1.setWorkingDirectory(dir.path());
    z1.start(QStringLiteral("zip"), {QStringLiteral("-X"), QStringLiteral("-r"), path, QStringLiteral("."),
                                     QStringLiteral("-x"), QStringLiteral("mimetype")});
    z1.waitForFinished(15000);
    return z0.exitCode() == 0 && z1.exitCode() == 0;
}

void testLibreOffice(const QString &dir)
{
    const QString path = dir + QStringLiteral("/lo-images.odt");
    check(writeLibreOfficeImageOdt(path), "L build LibreOffice-shaped fixture");
    QTextDocument doc;
    QString err;
    check(DocumentIo::load(&doc, path, &err), "L open it", err);
    const QList<QTextImageFormat> imgs = Images::imageFormats(&doc);
    check(imgs.size() == 1, "L one image: SVG skipped for its PNG fallback, missing member dropped",
          QString::number(imgs.size()));
    if (!imgs.isEmpty()) {
        check(imgs[0].name() == QLatin1String("Pictures/10000002.png"), "L the PNG fallback is used", imgs[0].name());
        check(std::abs(imgs[0].width() - 96) <= 1 && std::abs(imgs[0].height() - 48) <= 1,
              "L 2.54cm x 1.27cm frame -> 96x48 px",
              QStringLiteral("%1x%2").arg(imgs[0].width()).arg(imgs[0].height()));
        check(resourceImage(doc, imgs[0]).size() == QSize(20, 10), "L picture decodes");
    }
    const QString text = doc.toPlainText();
    check(text.contains(QLatin1String("A sketch of the trail.")) && text.contains(QLatin1String("After the missing one.")),
          "L text around the frames kept");
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir tmp;
    testUnits();
    testFormats();
    testRoundTrip(tmp.path());
    testLibreOffice(tmp.path());
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
