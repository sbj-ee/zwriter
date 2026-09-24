#include "DocumentMeta.hpp"
#include "version.hpp"

#include <QTimeZone>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QProcess>
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

} // namespace

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

bool writeToOdt(const QString &odtPath, const DocumentMeta &meta, QString *error)
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
