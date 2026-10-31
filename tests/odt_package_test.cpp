// In-process ODT package (1.18.0): the zip layer, the XML tree and a full
// save -> reopen of headings, tables and JPEG pictures, with no zip/unzip CLI.
// Run headless (QT_QPA_PLATFORM=offscreen).
#include "DocumentIo.hpp"
#include "OdfXml.hpp"
#include "ZipPackage.hpp"

#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include <cstdio>

namespace {
int g_failures = 0;
void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s  %s%s\n", ok ? "PASS" : "FAIL", what,
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" (") + detail + QLatin1Char(')')));
    if (!ok) {
        ++g_failures;
    }
}
QByteArray jpegBytes()
{
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(QColor(200, 80, 40));
    QByteArray b;
    QBuffer buf(&b);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "JPG", 85);
    return b;
}
QTextTable *firstTable(QTextDocument *doc)
{
    for (QTextFrame *f : doc->rootFrame()->childFrames()) {
        if (auto *t = qobject_cast<QTextTable *>(f)) {
            return t;
        }
    }
    return nullptr;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString dir = tmp.path();
    QString err;

    // Zip layer: mimetype first and stored; members round-trip; bad data fails.
    {
        ZipPackage::Package p;
        p.setMember(QStringLiteral("content.xml"), "<a/>");
        p.setMember(QStringLiteral("mimetype"), "application/vnd.oasis.opendocument.text");
        bool ok = false;
        const QByteArray z = p.toData(&ok, &err);
        check(ok && z.startsWith("PK\x03\x04"), "package builds", err);
        check(z.mid(30, 8) == "mimetype" && z.mid(38, 39) == "application/vnd.oasis.opendocument.text",
              "mimetype is the first member, stored");
        ZipPackage::Package q;
        check(q.readData(z, &err) && q.member(QStringLiteral("content.xml")) == "<a/>", "member round-trips", err);
        ZipPackage::Package bad;
        check(!bad.readData("not a zip"), "garbage is rejected");
        ZipPackage::Testing::setFailWrites(true);
        check(!p.write(dir + QStringLiteral("/x.zip")), "injected write failure");
        ZipPackage::Testing::setFailWrites(false);
    }

    // XML tree: qualified names, attributes and text survive parse/serialize.
    {
        OdfXml::Node root;
        const QByteArray xml = "<office:document-content xmlns:office=\"urn:o\" xmlns:text=\"urn:t\">"
                               "<text:h text:outline-level=\"2\">A &amp; B</text:h><!--c--></office:document-content>";
        check(OdfXml::parse(xml, &root, &err), "parse", err);
        OdfXml::Node again;
        check(OdfXml::parse(OdfXml::serialize(root), &again, &err), "reparse serialized", err);
        const QByteArray out = OdfXml::serialize(again);
        check(out.contains("xmlns:text=\"urn:t\"") && out.contains("<text:h text:outline-level=\"2\">A &amp; B</text:h>"),
              "names, namespaces and text kept", QString::fromUtf8(out));
        check(!OdfXml::parse("<a><b></a>", &root), "malformed XML is rejected");
    }

    // Full document round trip.
    const QString odt = dir + QStringLiteral("/round.odt");
    const QByteArray jpg = jpegBytes();
    {
        QTextDocument doc;
        doc.setHtml(QStringLiteral("<h1>Chapter One</h1><p>Intro text.</p><h2>Section 1.1</h2><p>Body.</p>"));
        QTextCursor c(&doc);
        c.movePosition(QTextCursor::End);
        QTextTableFormat tf;
        tf.setHeaderRowCount(1);
        tf.setColumnWidthConstraints({QTextLength(QTextLength::PercentageLength, 30),
                                      QTextLength(QTextLength::PercentageLength, 70)});
        QTextTable *t = c.insertTable(3, 2, tf);
        const char *cells[] = {"Name", "Value", "alpha", "1", "beta", "2"};
        for (int i = 0; i < 6; ++i) {
            t->cellAt(i / 2, i % 2).firstCursorPosition().insertText(QString::fromLatin1(cells[i]));
        }
        c.movePosition(QTextCursor::End);
        doc.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("photo.jpg")), jpg);
        QTextImageFormat img;
        img.setName(QStringLiteral("photo.jpg"));
        c.insertBlock();
        c.insertImage(img);
        check(DocumentIo::save(&doc, odt, DocumentIo::Format::Odt, &err), "save ODT", err);
    }
    QStringList pictures;
    {
        bool ok = false;
        const QStringList names = ZipPackage::memberNames(odt, &ok);
        check(ok && !names.isEmpty() && names.first() == QStringLiteral("mimetype"), "saved package lists, mimetype first");
        for (const QString &n : names) {
            if (n.startsWith(QStringLiteral("Pictures/"))) {
                pictures << n;
            }
        }
        check(pictures.size() == 1, "one picture in the package",
              pictures.join(QLatin1Char(',')));
        const QByteArray stored = pictures.isEmpty() ? QByteArray() : ZipPackage::readMember(odt, pictures.first());
        check(stored == jpg, "JPEG bytes kept exactly");
        const QByteArray manifest = ZipPackage::readMember(odt, QStringLiteral("META-INF/manifest.xml"));
        check(!pictures.isEmpty() && manifest.contains(("manifest:full-path=\"" + pictures.first() + "\"").toUtf8())
                  && manifest.contains("image/jpeg"),
              "manifest lists the JPEG");
        OdfXml::Node root;
        check(OdfXml::parse(ZipPackage::readMember(odt, QStringLiteral("content.xml")), &root, &err),
              "content.xml well formed", err);
        check(OdfXml::parse(manifest, &root, &err), "manifest well formed", err);
    }
    {
        QTextDocument back;
        check(DocumentIo::load(&back, odt, &err), "reopen ODT", err);
        QStringList heads;
        for (QTextBlock b = back.begin(); b.isValid(); b = b.next()) {
            if (b.blockFormat().headingLevel() > 0) {
                heads << QStringLiteral("%1:%2").arg(b.blockFormat().headingLevel()).arg(b.text());
            }
        }
        check(heads == QStringList{QStringLiteral("1:Chapter One"), QStringLiteral("2:Section 1.1")},
              "headings and levels kept", heads.join(QLatin1Char('|')));
        QTextTable *t = firstTable(&back);
        check(t && t->rows() == 3 && t->columns() == 2, "table shape kept");
        if (t) {
            check(t->cellAt(0, 0).firstCursorPosition().block().text() == QStringLiteral("Name")
                      && t->cellAt(2, 1).firstCursorPosition().block().text() == QStringLiteral("2"),
                  "table cells kept");
            check(t->format().headerRowCount() == 1, "header row kept");
            const auto w = t->format().columnWidthConstraints();
            check(w.size() == 2 && w[0].rawValue() < w[1].rawValue(), "column widths kept");
        }
        bool sawImage = false;
        for (QTextBlock b = back.begin(); b.isValid(); b = b.next()) {
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                if (it.fragment().charFormat().isImageFormat()) {
                    const QString name = it.fragment().charFormat().toImageFormat().name();
                    const QByteArray data = back.resource(QTextDocument::ImageResource, QUrl(name)).toByteArray();
                    sawImage = data == jpg;
                }
            }
        }
        check(sawImage, "picture reopens with its JPEG bytes");
        // Save again: still JPEG, still one picture.
        const QString odt2 = dir + QStringLiteral("/round2.odt");
        check(DocumentIo::save(&back, odt2, DocumentIo::Format::Odt, &err), "re-save", err);
        int jpgs = 0;
        for (const QString &n : ZipPackage::memberNames(odt2)) {
            jpgs += n.startsWith(QStringLiteral("Pictures/")) && ZipPackage::readMember(odt2, n) == jpg;
        }
        check(jpgs == 1, "re-save keeps the JPEG bytes");
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
