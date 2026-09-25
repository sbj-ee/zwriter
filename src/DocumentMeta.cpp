#include "DocumentMeta.hpp"
#include "version.hpp"

#include <QTimeZone>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QProcess>
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
            if (key == QLatin1String("zwriter:header-left")) {
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
    QString out = manifest;
    const qsizetype close = out.lastIndexOf(QLatin1String("</manifest:manifest>"));
    if (close < 0) {
        return out;
    }
    QString add;
    for (const QString &p : paths) {
        if (out.contains(QStringLiteral("manifest:full-path=\"%1\"").arg(p))) {
            continue;
        }
        add += QStringLiteral(" <manifest:file-entry manifest:media-type=\"text/xml\" "
                              "manifest:full-path=\"%1\"/>\n").arg(p);
    }
    out.insert(close, add);
    return out;
}


bool readFromOdt(const QString &odtPath, DocumentMeta *meta, QString *error)
{
    if (!meta) {
        return false;
    }
    QProcess proc;
    proc.start(QStringLiteral("unzip"),
               {QStringLiteral("-p"), odtPath, QStringLiteral("meta.xml")});
    if (!proc.waitForFinished(15000) || proc.exitCode() != 0) {
        // Older/minimal packages may lack meta.xml — not fatal.
        if (error) {
            *error = QStringLiteral("No readable meta.xml in ODT (using defaults).");
        }
        return false;
    }
    parseMetaXml(proc.readAllStandardOutput(), meta);
    return true;
}

bool writeToOdt(const QString &odtPath, const DocumentMeta &meta, QString *error,
                const OdtPageStyle &page)
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        if (error) {
            *error = QStringLiteral("Could not create temp dir for ODT metadata.");
        }
        return false;
    }

    // Extract existing package so we can replace meta.xml without rebuilding.
    QProcess unzip;
    unzip.setWorkingDirectory(dir.path());
    unzip.start(QStringLiteral("unzip"), {QStringLiteral("-o"), odtPath});
    if (!unzip.waitForFinished(30000) || unzip.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("Could not unpack ODT to patch metadata.");
        }
        return false;
    }

    const QString metaPath = dir.filePath(QStringLiteral("meta.xml"));
    QFile metaFile(metaPath);
    if (!metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Could not write meta.xml.");
        }
        return false;
    }
    const QByteArray xml = buildMetaXml(meta).toUtf8();
    metaFile.write(xml);
    metaFile.close();

    QFile stylesFile(dir.filePath(QStringLiteral("styles.xml")));
    if (!stylesFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Could not write styles.xml.");
        }
        return false;
    }
    stylesFile.write(buildStylesXml(meta, page).toUtf8());
    stylesFile.close();

    // Every package member must be in the manifest (LibreOffice rejects the
    // file otherwise); Qt's writer lists only content.xml.
    QFile manifestFile(dir.filePath(QStringLiteral("META-INF/manifest.xml")));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("ODT has no META-INF/manifest.xml.");
        }
        return false;
    }
    const QString manifest = manifestWithEntries(QString::fromUtf8(manifestFile.readAll()),
                                                 {QStringLiteral("styles.xml"), QStringLiteral("meta.xml")});
    manifestFile.close();
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Could not update META-INF/manifest.xml.");
        }
        return false;
    }
    manifestFile.write(manifest.toUtf8());
    manifestFile.close();

    // Rezip package (mimetype first, stored; then the rest).
    const QString rebuilt = dir.filePath(QStringLiteral("rebuilt.odt"));
    QFile::remove(rebuilt);
    {
        QProcess z0;
        z0.setWorkingDirectory(dir.path());
        z0.start(QStringLiteral("zip"),
                 {QStringLiteral("-X"), QStringLiteral("-0"), rebuilt, QStringLiteral("mimetype")});
        if (!z0.waitForFinished(15000) || z0.exitCode() != 0) {
            // Some writers omit a separate mimetype member; fall through.
            QFile::remove(rebuilt);
        }
    }
    {
        QProcess z1;
        z1.setWorkingDirectory(dir.path());
        QStringList args = {QStringLiteral("-X"), QStringLiteral("-r"), rebuilt, QStringLiteral(".")};
        if (QFile::exists(rebuilt)) {
            args.insert(2, QStringLiteral("-u")); // update existing archive that has mimetype
        }
        args << QStringLiteral("-x") << QStringLiteral("rebuilt.odt");
        z1.start(QStringLiteral("zip"), args);
        if (!z1.waitForFinished(60000) || z1.exitCode() != 0) {
            if (error) {
                *error = QStringLiteral("Could not rebuild ODT after metadata patch.");
            }
            return false;
        }
    }

    QFile::remove(odtPath);
    if (!QFile::copy(rebuilt, odtPath)) {
        if (error) {
            *error = QStringLiteral("Could not replace ODT with metadata-patched copy.");
        }
        return false;
    }
    return true;
}

} // namespace OdtMeta
