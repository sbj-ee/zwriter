// Images in documents (1.5.0 series).
//   U  ODF lengths convert to CSS px
//   F  which save formats keep images
//   R  an ODT with PNG + JPEG images saves and reopens with the pictures,
//      their displayed sizes and their original bytes (JPEG stays JPEG)
//   W  text wrap (left / right float) saves to ODT graphic styles and reopens
//   N  inserting: column cap, 4000 px limit, PNG/JPEG bytes kept, others to
//      PNG, refusals, one undo step; Image Properties apply (size, wrap) + undo
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
#include <QTextBlock>
#include <QTextFrame>
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

void testWrapRoundTrip(const QString &dir)
{
    QTextDocument doc;
    QTextCursor c(&doc);
    const QString a = Images::addImageData(&doc, encoded(solid(30, 20, QColor(200, 40, 40)), "PNG"));
    const QString b = Images::addImageData(&doc, encoded(solid(30, 20, QColor(40, 160, 90)), "PNG"));
    c.insertText(QStringLiteral("Before. "));
    QTextImageFormat in;
    in.setName(a);
    in.setWidth(60);
    in.setHeight(40);
    c.insertImage(in); // inline, will become Left
    c.insertText(QStringLiteral(" Lorem ipsum dolor sit amet, the text that wraps."));
    c.insertBlock();
    c.insertImage(in); // stays inline
    c.insertBlock();
    QTextImageFormat in2;
    in2.setName(b);
    in2.setWidth(90);
    in2.setHeight(60);
    c.insertImage(in2); // will become Right
    c.insertText(QStringLiteral(" More words beside the right picture."));

    QTextCursor first = Images::imageAt(&doc, 8);
    check(!first.isNull(), "W imageAt finds the first picture");
    Images::applyImage(first, a, 60, 40, Images::Wrap::Left);
    check(Images::wrapAt(&doc, 8) == Images::Wrap::Left, "W left wrap applied");
    int thirdPos = -1;
    for (QTextBlock blk = doc.begin(); blk.isValid(); blk = blk.next()) {
        if (blk.blockNumber() == 2) {
            thirdPos = blk.position();
        }
    }
    QTextCursor third = Images::imageAt(&doc, thirdPos);
    Images::applyImage(third, b, 90, 60, Images::Wrap::Right);
    check(Images::wrapAt(&doc, thirdPos) == Images::Wrap::Right,
          "W right wrap applied");

    const QString path = dir + QStringLiteral("/wrap.odt");
    QString err;
    check(DocumentIo::save(&doc, path, DocumentIo::Format::Odt, &err), "W save", err);
    const QByteArray xml = zipMember(path, QStringLiteral("content.xml"));
    check(xml.contains("style:name=\"zwImageLeft\" style:family=\"graphic\"")
              && xml.contains("style:name=\"zwImageRight\" style:family=\"graphic\""),
          "W content.xml has the left/right graphic styles");
    check(xml.count("text:anchor-type=\"char\"") == 2 && xml.count("text:anchor-type=\"as-char\"") == 1,
          "W floating frames are char-anchored, the inline one stays as-char");
    check(xml.contains("style:wrap=\"right\"") && xml.contains("style:horizontal-pos=\"left\""),
          "W left float = image at left, text wraps on its right");

    QTextDocument back;
    check(DocumentIo::load(&back, path, &err), "W reopen", err);
    QList<Images::Wrap> wraps;
    QList<QTextImageFormat> fmts;
    for (QTextBlock blk = back.begin(); blk.isValid(); blk = blk.next()) {
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat f = it.fragment().charFormat();
            if (f.isImageFormat()) {
                wraps << Images::wrapOf(&back, f);
                fmts << f.toImageFormat();
            }
        }
    }
    check(wraps == QList<Images::Wrap>{Images::Wrap::Left, Images::Wrap::Inline, Images::Wrap::Right},
          "W reopened wraps: left, inline, right", QString::number(wraps.size()));
    check(fmts.size() == 3 && std::abs(fmts[2].width() - 90) <= 1 && std::abs(fmts[2].height() - 60) <= 1,
          "W floated image keeps its size");
    check(back.toPlainText().contains(QLatin1String("text that wraps.")), "W text kept");
}

void testInsert()
{
    QTextDocument doc;
    doc.setTextWidth(500);
    const qreal col = Images::columnWidth(&doc);
    check(col > 400 && col <= 500, "N column width from the text width", QString::number(col));
    QTextCursor c(&doc);
    c.insertText(QStringLiteral("Start "));
    QString note;
    const QByteArray big = encoded(solid(5000, 2500, QColor(90, 90, 160)), "PNG");
    check(Images::insertImage(c, big, &note), "N insert a 5000x2500 PNG");
    check(note.contains(QLatin1String("5000")) && note.contains(QLatin1String("4000")), "N scaled-down note", note);
    QList<QTextImageFormat> imgs = Images::imageFormats(&doc);
    check(imgs.size() == 1 && Images::naturalSize(&doc, imgs[0].name()) == QSize(4000, 2000),
          "N stored at 4000x2000");
    check(!imgs.isEmpty() && std::abs(imgs[0].width() - col) <= 1 && std::abs(imgs[0].height() - col / 2) <= 1,
          "N displayed at the column width, in proportion",
          imgs.isEmpty() ? QString() : QStringLiteral("%1x%2").arg(imgs[0].width()).arg(imgs[0].height()));
    check(Images::wrapAt(&doc, 6) == Images::Wrap::Inline, "N inserted in line");

    const QByteArray jpeg = encoded(solid(50, 40, QColor(10, 120, 200)), "JPEG");
    Images::insertImage(c, jpeg, &note);
    imgs = Images::imageFormats(&doc);
    check(imgs.size() == 2 && doc.resource(QTextDocument::ImageResource, QUrl(imgs[1].name())).toByteArray() == jpeg,
          "N a small JPEG keeps its bytes");
    check(imgs.size() == 2 && imgs[1].width() == 50 && imgs[1].height() == 40, "N small image at natural size");

    const QByteArray gif = encoded(solid(16, 16, QColor(250, 200, 0)), "GIF");
    if (!gif.isEmpty()) {
        Images::insertImage(c, gif, &note);
        imgs = Images::imageFormats(&doc);
        check(imgs.size() == 3 && imgs[2].name().endsWith(QLatin1String(".png"))
                  && doc.resource(QTextDocument::ImageResource, QUrl(imgs[2].name())).toByteArray().startsWith("\x89PNG"),
              "N GIF is stored as PNG");
    }
    const int before = int(Images::imageFormats(&doc).size());
    check(!Images::insertImage(c, QByteArrayLiteral("plain words"), &note) && !note.isEmpty(),
          "N non-image refused with a message", note);
    check(!Images::insertImage(c, QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\"><oops"), &note)
              && note.contains(QLatin1String("SVG")),
          "N undecodable SVG refused with an SVG message", note);
    check(int(Images::imageFormats(&doc).size()) == before, "N refusals insert nothing");

    doc.undo();
    check(int(Images::imageFormats(&doc).size()) == before - 1, "N one undo removes the last inserted picture");

    // Image Properties: resize + wrap in one undo step.
    QTextCursor sel = Images::imageAt(&doc, 6);
    const QString name = sel.charFormat().toImageFormat().name();
    const int n = int(Images::imageFormats(&doc).size());
    Images::applyImage(sel, name, 200, 100, Images::Wrap::Right);
    QTextCursor after = Images::imageAt(&doc, 6);
    check(!after.isNull() && after.charFormat().toImageFormat().width() == 200
              && Images::wrapAt(&doc, 6) == Images::Wrap::Right
              && int(Images::imageFormats(&doc).size()) == n,
          "N properties: 200x100, wrap right, same picture count");
    doc.undo();
    QTextCursor undone = Images::imageAt(&doc, 6);
    check(!undone.isNull() && Images::wrapAt(&doc, 6) == Images::Wrap::Inline
              && std::abs(undone.charFormat().toImageFormat().width() - col) <= 1,
          "N one undo restores size and wrap");
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
        "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
        "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
        "xmlns:xlink=\"http://www.w3.org/1999/xlink\" office:version=\"1.3\">"
        "<office:automatic-styles>"
        "<style:style style:name=\"fr1\" style:family=\"graphic\" style:parent-style-name=\"Graphics\">"
        "<style:graphic-properties style:wrap=\"parallel\" style:horizontal-pos=\"right\" "
        "style:horizontal-rel=\"paragraph\"/></style:style>"
        "</office:automatic-styles>"
        "<office:body><office:text>"
        "<text:p>Field notes, day one.</text:p>"
        "<text:p><draw:frame draw:style-name=\"fr1\" draw:name=\"Image2\" text:anchor-type=\"paragraph\" "
        "svg:width=\"1in\" svg:height=\"0.5in\"><draw:image xlink:href=\"Pictures/10000002.png\"/></draw:frame>"
        "Text beside a right-floated picture.</text:p>"
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
    check(imgs.size() == 2, "L two images: SVG skipped for its PNG fallback, missing member dropped",
          QString::number(imgs.size()));
    if (imgs.size() == 2) {
        check(std::abs(imgs[1].width() - 96) <= 1 && std::abs(imgs[1].height() - 48) <= 1, "L floated frame 1in x 0.5in");
        int pos = -1;
        for (QTextBlock blk = doc.begin(); blk.isValid(); blk = blk.next()) {
            if (blk.text().contains(QLatin1String("right-floated"))) {
                pos = blk.position();
            }
        }
        check(Images::wrapAt(&doc, pos) == Images::Wrap::Right,
              "L paragraph-anchored frame, wrap=parallel, horizontal-pos=right -> floats right");
        check(Images::wrapAt(&doc, doc.find(QStringLiteral("A sketch")).block().position())
                  == Images::Wrap::Inline,
              "L as-char frame stays in line");
    }
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
    testWrapRoundTrip(tmp.path());
    testInsert();
    testLibreOffice(tmp.path());
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
