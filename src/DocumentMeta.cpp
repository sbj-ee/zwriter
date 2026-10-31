#include "OdfXml.hpp"
#include "ZipPackage.hpp"
#include <QSet>
#include <cstdio>
#include "DocumentMeta.hpp"
#include "version.hpp"

#include <QTimeZone>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#ifdef Q_OS_UNIX
#  include <unistd.h>
#  include <pwd.h>
#endif

QString DocumentMeta::defaultAuthor()
{
    const QByteArray env = qgetenv("USER");
    if (!env.isEmpty()) {
        return QString::fromUtf8(env);
    }
#ifdef Q_OS_UNIX
    if (const passwd *pw = getpwuid(getuid())) {
        if (pw->pw_gecos && pw->pw_gecos[0]) {
            // GECOS may be "Full Name,..."
            return QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0);
        }
        if (pw->pw_name) {
            return QString::fromLocal8Bit(pw->pw_name);
        }
    }
#endif
    return QStringLiteral("author");
}

void DocumentMeta::ensureDefaults()
{
    if (author.isEmpty()) {
        author = defaultAuthor();
    }
    if (!created.isValid()) {
        created = QDateTime::currentDateTimeUtc();
    }
    if (!lastEdited.isValid()) {
        lastEdited = created;
    }
    // New documents start with no header/footer; page numbers are opt-in
    // (Format -> Page Numbers).
    headerFooterSeeded = true;
}

bool DocumentMeta::hasHeaderFooter() const
{
    return !(headerLeft.isEmpty() && headerCenter.isEmpty() && headerRight.isEmpty()
             && footerLeft.isEmpty() && footerCenter.isEmpty() && footerRight.isEmpty());
}

void DocumentMeta::touchEdited()
{
    lastEdited = QDateTime::currentDateTimeUtc();
}

namespace OdtMeta {
namespace {

QString toOdfDate(const QDateTime &dt)
{
    return dt.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) + QLatin1Char('Z');
}

QDateTime fromOdfDate(const QString &s)
{
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(s, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
        if (dt.isValid()) {
            dt = QDateTime(dt.date(), dt.time(), QTimeZone::utc());
        }
    }
    return dt.toUTC();
}

QString buildMetaXml(const DocumentMeta &meta)
{
    QString out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.writeStartDocument(QStringLiteral("1.0"));
    w.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:office:1.0"),
                     QStringLiteral("office"));
    w.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:meta:1.0"),
                     QStringLiteral("meta"));
    w.writeNamespace(QStringLiteral("http://purl.org/dc/elements/1.1/"), QStringLiteral("dc"));

    w.writeStartElement(QStringLiteral("office:document-meta"));
    w.writeAttribute(QStringLiteral("office:version"), QStringLiteral("1.2"));
    w.writeStartElement(QStringLiteral("office:meta"));

    if (!meta.author.isEmpty()) {
        w.writeTextElement(QStringLiteral("dc:creator"), meta.author);
        w.writeTextElement(QStringLiteral("meta:initial-creator"), meta.author);
    }
    if (meta.created.isValid()) {
        w.writeTextElement(QStringLiteral("meta:creation-date"), toOdfDate(meta.created));
    }
    if (meta.lastEdited.isValid()) {
        w.writeTextElement(QStringLiteral("dc:date"), toOdfDate(meta.lastEdited));
    }
    w.writeTextElement(QStringLiteral("meta:generator"), QStringLiteral("zwriter/") + QString::fromUtf8(zwriter::kVersionString));

    auto writeUser = [&w](const QString &name, const QString &value) {
        w.writeStartElement(QStringLiteral("meta:user-defined"));
        w.writeAttribute(QStringLiteral("meta:name"), name);
        w.writeCharacters(value);
        w.writeEndElement();
    };
    writeUser(QStringLiteral("zwriter:header-left"), meta.headerLeft);
    writeUser(QStringLiteral("zwriter:header-center"), meta.headerCenter);
    writeUser(QStringLiteral("zwriter:header-right"), meta.headerRight);
    writeUser(QStringLiteral("zwriter:footer-left"), meta.footerLeft);
    writeUser(QStringLiteral("zwriter:footer-center"), meta.footerCenter);
    writeUser(QStringLiteral("zwriter:footer-right"), meta.footerRight);
    if (!meta.styleFonts.isEmpty()) {
        writeUser(QStringLiteral("zwriter:style-fonts"), meta.styleFonts);
    }

    w.writeEndElement(); // office:meta
    w.writeEndElement(); // office:document-meta
    w.writeEndDocument();
    return out;
}

void parseMetaXml(const QByteArray &xml, DocumentMeta *meta)
{
    QXmlStreamReader reader(xml);
    bool sawHeaderFooter = false;
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        if (name == QLatin1String("creator") || name == QLatin1String("initial-creator")) {
            const QString v = reader.readElementText();
            if (meta->author.isEmpty()) {
                meta->author = v;
            }
        } else if (name == QLatin1String("creation-date")) {
            meta->created = fromOdfDate(reader.readElementText());
        } else if (name == QLatin1String("date")) {
            meta->lastEdited = fromOdfDate(reader.readElementText());
        } else if (name == QLatin1String("user-defined")) {
            const QString key = reader.attributes().value(QStringLiteral("meta:name")).toString();
            const QString v = reader.readElementText();
            if (key == QLatin1String("zwriter:style-fonts")) {
                meta->styleFonts = v;
            } else if (key == QLatin1String("zwriter:header-left")) {
                meta->headerLeft = v;
                sawHeaderFooter = true;
            } else if (key == QLatin1String("zwriter:header-center")) {
                meta->headerCenter = v;
                sawHeaderFooter = true;
            } else if (key == QLatin1String("zwriter:header-right")) {
                meta->headerRight = v;
                sawHeaderFooter = true;
            } else if (key == QLatin1String("zwriter:footer-left")) {
                meta->footerLeft = v;
                sawHeaderFooter = true;
            } else if (key == QLatin1String("zwriter:footer-center")) {
                meta->footerCenter = v;
                sawHeaderFooter = true;
            } else if (key == QLatin1String("zwriter:footer-right")) {
                meta->footerRight = v;
                sawHeaderFooter = true;
            }
        }
    }
    if (sawHeaderFooter) {
        meta->headerFooterSeeded = true;
    }
}


QString mm(qreal v)
{
    return QString::number(qMax(0.0, v), 'f', 2) + QStringLiteral("mm");
}

// Header/footer band text with zwriter's {page}/{pages} tokens as ODF fields.
void writeBandText(QXmlStreamWriter &w, const QString &pattern)
{
    static const QRegularExpression token(QStringLiteral(R"(\{(page|pages)\})"));
    qsizetype last = 0;
    auto it = token.globalMatch(pattern);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        w.writeCharacters(pattern.mid(last, m.capturedStart() - last));
        if (m.captured(1) == QLatin1String("page")) {
            w.writeStartElement(QStringLiteral("text:page-number"));
            w.writeAttribute(QStringLiteral("text:select-page"), QStringLiteral("current"));
            w.writeCharacters(QStringLiteral("1"));
        } else {
            w.writeStartElement(QStringLiteral("text:page-count"));
            w.writeCharacters(QStringLiteral("1"));
        }
        w.writeEndElement();
        last = m.capturedEnd();
    }
    w.writeCharacters(pattern.mid(last));
}

} // namespace

QString buildStylesXml(const DocumentMeta &meta, const OdtPageStyle &page)
{
    const bool header = !(meta.headerLeft.isEmpty() && meta.headerCenter.isEmpty()
                          && meta.headerRight.isEmpty());
    const bool footer = !(meta.footerLeft.isEmpty() && meta.footerCenter.isEmpty()
                          && meta.footerRight.isEmpty());
    // zwriter draws a header/footer line centred in the top/bottom margin and
    // keeps the body at the page margins. ODF puts the header inside the page
    // margins, so split each margin into: outer part + header line + spacing.
    const qreal lineMm = page.fontPointSize * 25.4 / 72.0 * 1.2;
    auto outer = [&](qreal margin) { return qMax(0.0, margin / 2.0 - lineMm / 2.0); };
    auto spacing = [&](qreal margin) { return qMax(0.0, margin - outer(margin) - lineMm); };

    QString out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.writeStartDocument(QStringLiteral("1.0"));
    w.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:office:1.0"), QStringLiteral("office"));
    w.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:style:1.0"), QStringLiteral("style"));
    w.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:text:1.0"), QStringLiteral("text"));
    w.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"), QStringLiteral("fo"));
    w.writeStartElement(QStringLiteral("office:document-styles"));
    w.writeAttribute(QStringLiteral("office:version"), QStringLiteral("1.2"));

    w.writeStartElement(QStringLiteral("office:styles"));
    w.writeStartElement(QStringLiteral("style:style"));
    w.writeAttribute(QStringLiteral("style:name"), QStringLiteral("zwHeaderFooter"));
    w.writeAttribute(QStringLiteral("style:family"), QStringLiteral("paragraph"));
    // One line per band: left <tab> centre <tab> right. (Writer ignores the
    // style:region-* elements, which are a spreadsheet feature.)
    const qreal textWidthMm = page.sheetMm.width() - page.marginsMm.left() - page.marginsMm.right();
    w.writeStartElement(QStringLiteral("style:paragraph-properties"));
    w.writeStartElement(QStringLiteral("style:tab-stops"));
    w.writeEmptyElement(QStringLiteral("style:tab-stop"));
    w.writeAttribute(QStringLiteral("style:position"), mm(textWidthMm / 2.0));
    w.writeAttribute(QStringLiteral("style:type"), QStringLiteral("center"));
    w.writeEmptyElement(QStringLiteral("style:tab-stop"));
    w.writeAttribute(QStringLiteral("style:position"), mm(textWidthMm));
    w.writeAttribute(QStringLiteral("style:type"), QStringLiteral("right"));
    w.writeEndElement(); // tab-stops
    w.writeEndElement(); // paragraph-properties
    w.writeStartElement(QStringLiteral("style:text-properties"));
    w.writeAttribute(QStringLiteral("fo:font-family"), page.fontFamily);
    w.writeAttribute(QStringLiteral("fo:font-size"), QString::number(page.fontPointSize) + QStringLiteral("pt"));
    w.writeEndElement(); // text-properties
    w.writeEndElement(); // style
    w.writeEndElement(); // office:styles

    w.writeStartElement(QStringLiteral("office:automatic-styles"));
    w.writeStartElement(QStringLiteral("style:page-layout"));
    w.writeAttribute(QStringLiteral("style:name"), QStringLiteral("zwPage"));
    w.writeStartElement(QStringLiteral("style:page-layout-properties"));
    w.writeAttribute(QStringLiteral("fo:page-width"), mm(page.sheetMm.width()));
    w.writeAttribute(QStringLiteral("fo:page-height"), mm(page.sheetMm.height()));
    w.writeAttribute(QStringLiteral("style:print-orientation"),
                     page.landscape ? QStringLiteral("landscape") : QStringLiteral("portrait"));
    w.writeAttribute(QStringLiteral("fo:margin-top"), mm(header ? outer(page.marginsMm.top()) : page.marginsMm.top()));
    w.writeAttribute(QStringLiteral("fo:margin-bottom"),
                     mm(footer ? outer(page.marginsMm.bottom()) : page.marginsMm.bottom()));
    w.writeAttribute(QStringLiteral("fo:margin-left"), mm(page.marginsMm.left()));
    w.writeAttribute(QStringLiteral("fo:margin-right"), mm(page.marginsMm.right()));
    w.writeEndElement(); // page-layout-properties
    if (header) {
        w.writeStartElement(QStringLiteral("style:header-style"));
        w.writeStartElement(QStringLiteral("style:header-footer-properties"));
        w.writeAttribute(QStringLiteral("fo:min-height"), mm(lineMm));
        w.writeAttribute(QStringLiteral("fo:margin-bottom"), mm(spacing(page.marginsMm.top())));
        w.writeEndElement();
        w.writeEndElement();
    }
    if (footer) {
        w.writeStartElement(QStringLiteral("style:footer-style"));
        w.writeStartElement(QStringLiteral("style:header-footer-properties"));
        w.writeAttribute(QStringLiteral("fo:min-height"), mm(lineMm));
        w.writeAttribute(QStringLiteral("fo:margin-top"), mm(spacing(page.marginsMm.bottom())));
        w.writeEndElement();
        w.writeEndElement();
    }
    w.writeEndElement(); // page-layout
    w.writeEndElement(); // automatic-styles

    auto band = [&](const QString &element, const QString &l, const QString &c, const QString &r) {
        w.writeStartElement(element);
        w.writeStartElement(QStringLiteral("text:p"));
        w.writeAttribute(QStringLiteral("text:style-name"), QStringLiteral("zwHeaderFooter"));
        writeBandText(w, l);
        w.writeEmptyElement(QStringLiteral("text:tab"));
        writeBandText(w, c);
        w.writeEmptyElement(QStringLiteral("text:tab"));
        writeBandText(w, r);
        w.writeEndElement(); // text:p
        w.writeEndElement();
    };

    w.writeStartElement(QStringLiteral("office:master-styles"));
    w.writeStartElement(QStringLiteral("style:master-page"));
    w.writeAttribute(QStringLiteral("style:name"), QStringLiteral("Standard"));
    w.writeAttribute(QStringLiteral("style:page-layout-name"), QStringLiteral("zwPage"));
    if (header) {
        band(QStringLiteral("style:header"), meta.headerLeft, meta.headerCenter, meta.headerRight);
    }
    if (footer) {
        band(QStringLiteral("style:footer"), meta.footerLeft, meta.footerCenter, meta.footerRight);
    }
    w.writeEndElement(); // master-page
    w.writeEndElement(); // master-styles
    w.writeEndElement(); // document-styles
    w.writeEndDocument();
    return out;
}

QString manifestWithEntries(const QString &manifest, const QStringList &paths)
{
    // Empty when the manifest cannot be parsed.
    OdfXml::Node root;
    if (!OdfXml::parse(manifest.toUtf8(), &root)) {
        return QString();
    }
    QSet<QString> have;
    for (const OdfXml::Node &c : root.children) {
        if (c.isElement(QStringLiteral("manifest:file-entry"))) {
            have.insert(c.attribute(QStringLiteral("manifest:full-path")));
        }
    }
    for (const QString &p : paths) {
        if (!have.contains(p)) {
            root.children.push_back(OdfXml::Node::element(QStringLiteral("manifest:file-entry"),
                                                          {{QStringLiteral("manifest:media-type"), QStringLiteral("text/xml")},
                                                           {QStringLiteral("manifest:full-path"), p}}));
        }
    }
    return QString::fromUtf8(OdfXml::serialize(root));
}

qreal odfLengthToMm(const QString &length)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([0-9]*\\.?[0-9]+)\\s*(mm|cm|in|pt|pc|px)?\\s*$"));
    const QRegularExpressionMatch m = re.match(length);
    if (!m.hasMatch()) {
        return -1.0;
    }
    const qreal v = m.captured(1).toDouble();
    const QString u = m.captured(2);
    if (u == QLatin1String("cm")) return v * 10.0;
    if (u == QLatin1String("in")) return v * 25.4;
    if (u == QLatin1String("pt")) return v * 25.4 / 72.0;
    if (u == QLatin1String("pc")) return v * 25.4 / 6.0;
    if (u == QLatin1String("px")) return v * 25.4 / 96.0;
    return v; // mm (or unitless)
}

bool parsePageSizeXml(const QByteArray &stylesXml, QSizeF *sheetMm, bool *landscape)
{
    // The first page layout with a width and height (zwriter writes one,
    // "zwPage"; LibreOffice's master page normally uses the first too).
    QXmlStreamReader r(stylesXml);
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement() || r.name() != QLatin1String("page-layout-properties")) {
            continue;
        }
        const QXmlStreamAttributes a = r.attributes();
        const qreal w = odfLengthToMm(a.value(QLatin1String("fo:page-width")).toString());
        const qreal h = odfLengthToMm(a.value(QLatin1String("fo:page-height")).toString());
        if (w < 20.0 || h < 20.0 || w > 2000.0 || h > 2000.0) {
            continue;
        }
        if (sheetMm) {
            *sheetMm = QSizeF(w, h);
        }
        if (landscape) {
            const QString o = a.value(QLatin1String("style:print-orientation")).toString();
            *landscape = o.isEmpty() ? w > h : o == QLatin1String("landscape");
        }
        return true;
    }
    return false;
}

bool readPageSizeFromOdt(const QString &odtPath, QSizeF *sheetMm, bool *landscape)
{
    bool ok = false;
    const QByteArray xml = ZipPackage::readMember(odtPath, QStringLiteral("styles.xml"), &ok);
    return ok && parsePageSizeXml(xml, sheetMm, landscape);
}

bool readFromOdt(const QString &odtPath, DocumentMeta *meta, QString *error)
{
    if (!meta) {
        return false;
    }
    bool ok = false;
    const QByteArray xml = ZipPackage::readMember(odtPath, QStringLiteral("meta.xml"), &ok);
    if (!ok) {
        // Older/minimal packages may lack meta.xml — not fatal.
        if (error) {
            *error = QStringLiteral("No readable meta.xml in ODT (using defaults).");
        }
        return false;
    }
    parseMetaXml(xml, meta);
    return true;
}

bool writeToOdt(const QString &odtPath, const DocumentMeta &meta, QString *error,
                const OdtPageStyle &page)
{
    auto fail = [error](const QString &msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };
    ZipPackage::Package pkg;
    QString zipError;
    if (!pkg.read(odtPath, &zipError)) {
        return fail(QStringLiteral("Could not read the ODT to patch metadata (%1).").arg(zipError));
    }
    bool found = false;
    const QByteArray manifest = pkg.member(QStringLiteral("META-INF/manifest.xml"), &found);
    if (!found) {
        return fail(QStringLiteral("ODT has no META-INF/manifest.xml."));
    }
    // Every package member must be in the manifest (LibreOffice rejects the
    // file otherwise); Qt's writer lists only content.xml.
    const QString patchedManifest = manifestWithEntries(QString::fromUtf8(manifest),
                                                        {QStringLiteral("styles.xml"), QStringLiteral("meta.xml")});
    if (patchedManifest.isEmpty()) {
        return fail(QStringLiteral("Could not update META-INF/manifest.xml."));
    }
    pkg.setMember(QStringLiteral("meta.xml"), buildMetaXml(meta).toUtf8());
    pkg.setMember(QStringLiteral("styles.xml"), buildStylesXml(meta, page).toUtf8());
    pkg.setMember(QStringLiteral("META-INF/manifest.xml"), patchedManifest.toUtf8());

    // Written next to the file and renamed over it in one step. (DocumentIo::
    // save() calls this on its own temp file, so the user's file is never
    // touched here; a failure fails that save.)
    const QString part = odtPath + QStringLiteral(".part");
    if (!pkg.write(part, &zipError)) {
        QFile::remove(part);
        return fail(QStringLiteral("Could not rebuild ODT after metadata patch (%1).").arg(zipError));
    }
    if (std::rename(QFile::encodeName(part).constData(), QFile::encodeName(odtPath).constData()) != 0) {
        QFile::remove(part);
        return fail(QStringLiteral("Could not replace ODT with metadata-patched copy."));
    }
    return true;
}

} // namespace OdtMeta
