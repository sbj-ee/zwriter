// Atomic save (1.17.1): DocumentIo::save() builds the file in a temp file next
// to the target, patches and checks it there, keeps "<path>.bak" and renames
// over the target only when every step worked. A failed step fails the save
// and leaves the target (and its .bak) exactly as they were.
// Zip handling is in-process (1.18.0). Run headless (QT_QPA_PLATFORM=offscreen).
#include "DocumentIo.hpp"
#include "DocumentMeta.hpp"
#include "ZipPackage.hpp"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTextDocument>

#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

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

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Anything save() may leave behind besides the target and its .bak.
QStringList leftovers(const QString &dir)
{
    return QDir(dir).entryList({QStringLiteral("*.tmp"), QStringLiteral("*.tmp.bak")},
                               QDir::Files | QDir::Hidden);
}

QString loadedText(const QString &path)
{
    QTextDocument d;
    return DocumentIo::load(&d, path) ? d.toPlainText() : QStringLiteral("<load failed>");
}

// Makes every zip write fail (as a full disk would) while alive.
struct BrokenZip {
    BrokenZip() { ZipPackage::Testing::setFailWrites(true); }
    ~BrokenZip() { ZipPackage::Testing::setFailWrites(false); }
};

void formatRoundTrip(const QString &dir, DocumentIo::Format format, const QString &name)
{
    const QString path = dir + QLatin1Char('/') + name;
    const QByteArray label = name.toUtf8();
    QTextDocument v1;
    v1.setPlainText(QStringLiteral("first version"));
    QString err;
    check(DocumentIo::save(&v1, path, format, &err), (label + " first save").constData(), err);
    check(!QFileInfo::exists(DocumentIo::backupPath(path)), (label + " no .bak for a new file").constData());
    const QByteArray first = readAll(path);

    QTextDocument v2;
    v2.setPlainText(QStringLiteral("second version"));
    check(DocumentIo::save(&v2, path, format, &err), (label + " second save").constData(), err);
    check(readAll(DocumentIo::backupPath(path)) == first, (label + " .bak holds the previous file").constData());
    check(loadedText(path).contains(QStringLiteral("second version")), (label + " target has the new text").constData());

    // A failing patch step fails the save; target and .bak untouched.
    const QByteArray second = readAll(path);
    QTextDocument v3;
    v3.setPlainText(QStringLiteral("third version"));
    const bool ok = DocumentIo::save(&v3, path, format, &err, [](const QString &, QString *e) {
        *e = QStringLiteral("simulated patch failure");
        return false;
    });
    check(!ok, (label + " patch failure fails the save").constData());
    check(err.contains(QStringLiteral("simulated patch failure")), (label + " error is reported").constData(), err);
    check(readAll(path) == second, (label + " target untouched after failure").constData());
    check(readAll(DocumentIo::backupPath(path)) == first, (label + " .bak untouched after failure").constData());
    check(leftovers(dir).isEmpty(), (label + " no temp files left").constData(), leftovers(dir).join(','));
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir tmp;
    check(tmp.isValid(), "temp dir");
    const QString dir = tmp.path();

    formatRoundTrip(dir, DocumentIo::Format::Odt, QStringLiteral("doc.odt"));
    formatRoundTrip(dir, DocumentIo::Format::Txt, QStringLiteral("doc.txt"));
    formatRoundTrip(dir, DocumentIo::Format::Markdown, QStringLiteral("doc.md"));
    formatRoundTrip(dir, DocumentIo::Format::Rtf, QStringLiteral("doc.rtf"));

    const QString odt = dir + QStringLiteral("/doc.odt");
    QString err;

    // The metadata patch (as MainWindow uses it) works on the temp file.
    {
        DocumentMeta meta;
        meta.author = QStringLiteral("Atomic Author");
        QTextDocument d;
        d.setPlainText(QStringLiteral("with metadata"));
        const bool ok = DocumentIo::save(&d, odt, DocumentIo::Format::Odt, &err,
                                         [&meta](const QString &t, QString *e) { return OdtMeta::writeToOdt(t, meta, e); });
        check(ok, "ODT save with meta patch", err);
        DocumentMeta back;
        check(OdtMeta::readFromOdt(odt, &back) && back.author == QStringLiteral("Atomic Author"),
              "meta.xml written into the saved file");
        check(leftovers(dir).isEmpty(), "no temp files after meta patch");
    }

    // The zip write fails: the ODT save and the meta patch both fail.
    {
        const QByteArray before = readAll(odt);
        const QByteArray bakBefore = readAll(DocumentIo::backupPath(odt));
        BrokenZip broken;
        QTextDocument d;
        d.setHtml(QStringLiteral("<h1>Heading</h1><p>zip is broken</p>"));
        err.clear();
        check(!DocumentIo::save(&d, odt, DocumentIo::Format::Odt, &err), "zip failure fails the ODT save");
        check(!err.isEmpty(), "zip failure has an error message", err);
        check(readAll(odt) == before, "zip failure: target untouched");
        check(readAll(DocumentIo::backupPath(odt)) == bakBefore, "zip failure: .bak untouched");
        check(leftovers(dir).isEmpty(), "zip failure: no temp files left");

        DocumentMeta meta;
        err.clear();
        check(!OdtMeta::writeToOdt(odt, meta, &err), "writeToOdt reports a zip failure");
        check(readAll(odt) == before, "writeToOdt zip failure: target untouched");
    }

    // Write fails: the directory is read-only (skipped as root, who can write anyway).
    if (::geteuid() != 0) {
        const QString roDir = dir + QStringLiteral("/readonly");
        QDir().mkpath(roDir);
        const QString path = roDir + QStringLiteral("/locked.odt");
        QTextDocument d;
        d.setPlainText(QStringLiteral("original"));
        check(DocumentIo::save(&d, path, DocumentIo::Format::Odt, &err), "save before locking", err);
        const QByteArray before = readAll(path);
        QFile::setPermissions(roDir, QFile::ReadOwner | QFile::ExeOwner);
        d.setPlainText(QStringLiteral("changed"));
        err.clear();
        check(!DocumentIo::save(&d, path, DocumentIo::Format::Odt, &err), "read-only dir fails the save");
        check(!err.isEmpty(), "read-only dir has an error message", err);
        check(readAll(path) == before, "read-only dir: target untouched");
        check(!QFileInfo::exists(DocumentIo::backupPath(path)), "read-only dir: no .bak");
        err.clear();
        check(!DocumentIo::save(&d, roDir + QStringLiteral("/new.txt"), DocumentIo::Format::Txt, &err),
              "read-only dir: new TXT fails");
        QFile::setPermissions(roDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        check(leftovers(roDir).isEmpty(), "read-only dir: no temp files left");
    } else {
        std::printf("SKIP  read-only dir (running as root)\n");
    }

    // Permissions of the replaced file are kept.
    {
        const QString path = dir + QStringLiteral("/perm.txt");
        QTextDocument d;
        d.setPlainText(QStringLiteral("a"));
        DocumentIo::save(&d, path, DocumentIo::Format::Txt, &err);
        const QFile::Permissions p = QFile::ReadOwner | QFile::WriteOwner;
        QFile::setPermissions(path, p);
        d.setPlainText(QStringLiteral("b"));
        check(DocumentIo::save(&d, path, DocumentIo::Format::Txt, &err), "save over 0600 file", err);
        const int got = int(QFile::permissions(path));
        check((got & 0xF000) == int(p) && (got & 0x00FF) == 0, "permissions kept");
    }

    // A corrupt package: the metadata patch fails and leaves it alone; load fails.
    {
        const QString bad = dir + QStringLiteral("/corrupt.odt");
        QFile f(bad);
        check(f.open(QIODevice::WriteOnly) && f.write("PK\x03\x04 not really a zip") > 0, "write corrupt.odt");
        f.close();
        const QByteArray before = readAll(bad);
        DocumentMeta meta;
        err.clear();
        check(!OdtMeta::writeToOdt(bad, meta, &err), "writeToOdt fails on a corrupt package", err);
        check(readAll(bad) == before, "corrupt package untouched");
        QTextDocument d;
        check(!DocumentIo::load(&d, bad, &err), "load fails on a corrupt package", err);
        // A save whose meta patch hits the corrupt temp is impossible to
        // construct directly; a patch that corrupts the temp must fail verify.
        const QByteArray odtBefore = readAll(odt);
        QTextDocument d2;
        d2.setPlainText(QStringLiteral("will be corrupted"));
        check(!DocumentIo::save(&d2, odt, DocumentIo::Format::Odt, &err, [](const QString &t, QString *) {
                  QFile c(t);
                  return c.open(QIODevice::WriteOnly | QIODevice::Truncate) && c.write("garbage") == 7;
              }),
              "a corrupted temp file fails the check");
        check(readAll(odt) == odtBefore, "corrupted temp: target untouched");
    }

    // A new file gets the usual mode (0666 less the umask), not 0600.
    {
        const mode_t mask = ::umask(022);
        const QString path = dir + QStringLiteral("/fresh.odt");
        QTextDocument d;
        d.setPlainText(QStringLiteral("fresh"));
        check(DocumentIo::save(&d, path, DocumentIo::Format::Odt, &err), "save a new file", err);
        struct stat st {};
        check(::stat(QFile::encodeName(path).constData(), &st) == 0 && (st.st_mode & 0777) == 0644,
              "new file is 0644 under umask 022");
        ::umask(mask);
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
