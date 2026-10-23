// Generated book table of contents (1.8.0).
//   E entries: book order, numbered chapter titles, headings, skip rules
//   I insert / update in place, unchanged = no edit
//   O ODT: saved as a real text:table-of-content (dot leaders), read back
//     as zwriter's own; a LibreOffice ToC is still kept as it was

#include "BookManifest.hpp"
#include "BookToc.hpp"
#include "DocumentIo.hpp"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QXmlStreamReader>

#include <cstdio>

namespace {
int g_failures = 0;
void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.isEmpty() ? "" : "  -- ", qPrintable(detail));
    if (!ok) {
        ++g_failures;
    }
}
void write(const QString &path, const QByteArray &b)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(b);
}
QString member(const QString &zip, const QString &name)
{
    QProcess p;
    p.start(QStringLiteral("unzip"), {QStringLiteral("-p"), zip, name});
    p.waitForFinished(15000);
    return QString::fromUtf8(p.readAllStandardOutput());
}
QString show(const QList<BookToc::Entry> &es)
{
    QStringList l;
    for (const auto &e : es) {
        l << QStringLiteral("%1:%2").arg(e.level).arg(e.text);
    }
    return l.join(QStringLiteral(" | "));
}
QStringList tocLines(const QTextDocument &d)
{
    QStringList l;
    for (QTextBlock b = d.begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().hasProperty(BookToc::GeneratedProperty)) {
            l << QStringLiteral("%1:%2").arg(b.blockFormat().intProperty(BookToc::GeneratedProperty)).arg(b.text());
        }
    }
    return l;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString dir = tmp.path() + QStringLiteral("/Birds");
    write(dir + QStringLiteral("/01 Introduction.md"), "# Introduction\n\nHello.\n\n## How to use this book\n\n```\n# not a heading\n```\n");
    {
        QTextDocument d;
        d.setHtml(QStringLiteral("<h1>Raptors</h1><p>x</p><h2>Bald Eagle</h2><p>y</p><h3>Nesting</h3><h2>Owls</h2>"));
        DocumentIo::save(&d, dir + QStringLiteral("/02 Raptors.odt"), DocumentIo::Format::Odt);
    }
    write(dir + QStringLiteral("/03 Notes.txt"), "Just notes.\n");
    write(dir + QStringLiteral("/00 Contents.odt"), QByteArray());
    BookManifest book = BookManifest::fromFolder(dir);
    book.save();
    const QString contents = dir + QStringLiteral("/00 Contents.odt");

    const QList<BookToc::Entry> e2 = BookToc::entries(book, 2, contents);
    check(show(e2) == QLatin1String("1:1. Introduction | 2:How to use this book | 1:2. Raptors | 2:Bald Eagle | 2:Owls | 1:3. Notes"),
          "E chapters in book order with their headings; the ToC's own file left out", show(e2));
    check(show(BookToc::entries(book, 3, contents)).contains(QLatin1String("3:Nesting")), "E depth 3 adds sub-headings");
    check(!show(e2).contains(QLatin1String("not a heading")), "E # inside a code fence is not a heading");

    // Insert, then update after a reorder.
    QTextDocument doc;
    QTextCursor c(&doc);
    c.insertText(QStringLiteral("Front matter."));
    c.insertBlock();
    check(BookToc::insertOrUpdate(&doc, &c, e2), "I insert");
    check(tocLines(doc).size() == 6 && doc.firstBlock().text() == QLatin1String("Front matter.")
              && doc.findBlockByNumber(1).text() == QLatin1String("Contents"),
          "I inserted at the cursor, after the text before it", doc.toPlainText().left(80));
    check(!BookToc::insertOrUpdate(&doc, nullptr, e2), "I unchanged book: no edit");
    book.move(2, 1); // Raptors before Introduction
    const QList<BookToc::Entry> moved = BookToc::entries(book, 2, contents);
    check(BookToc::insertOrUpdate(&doc, nullptr, moved) && tocLines(doc).first() == QLatin1String("1:1. Raptors")
              && tocLines(doc).size() == 6,
          "I reorder: updated in place", tocLines(doc).join(QStringLiteral(" | ")));
    int contentsTitles = 0;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        contentsTitles += b.text() == QLatin1String("Contents") ? 1 : 0;
    }
    check(contentsTitles == 1 && doc.firstBlock().text() == QLatin1String("Front matter."), "I still one table, text around it kept");

    // ODT.
    QString err;
    const QString out = tmp.filePath(QStringLiteral("toc.odt"));
    check(DocumentIo::save(&doc, out, DocumentIo::Format::Odt, &err), "O save", err);
    const QString xml = member(out, QStringLiteral("content.xml"));
    check(xml.contains(QLatin1String("<text:table-of-content text:style-name=\"Sect_zwToc\" text:protected=\"true\" text:name=\"zwriter-toc\">"))
              && xml.contains(QLatin1String("style:leader-text=\".\"")) && xml.contains(QLatin1String("<text:p text:style-name=\"Contents_20_2\">Bald Eagle<text:tab/></text:p>")),
          "O a real text:table-of-content with dot-leader tab stops");
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
    }
    check(!r.hasError(), "O content.xml well-formed", r.errorString());
    QTextDocument back;
    DocumentIo::load(&back, out);
    check(tocLines(back) == tocLines(doc) && BookToc::hasToc(&back), "O read back as zwriter's own ToC",
          tocLines(back).join(QStringLiteral(" | ")));
    check(back.property("zwriter.odtTocs").toStringList().isEmpty(), "O ... not as a foreign one to keep verbatim");
    book.move(1, 2);
    check(BookToc::insertOrUpdate(&back, nullptr, BookToc::entries(book, 2, contents))
              && tocLines(back).first() == QLatin1String("1:1. Introduction"),
          "O and it updates after reopening");

    // A LibreOffice ToC (#13) is still kept as it was, not turned into ours.
    QTextDocument lo;
    DocumentIo::load(&lo, QStringLiteral(ZW_SOURCE_DIR "/tests/data/toc-libreoffice.odt"));
    check(!BookToc::hasToc(&lo) && !lo.property("zwriter.odtTocs").toStringList().isEmpty(),
          "O a LibreOffice ToC stays foreign (kept verbatim)");

    const QString soffice = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (!soffice.isEmpty()) {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("HOME"), tmp.path());
        p.setProcessEnvironment(env);
        p.start(soffice, {QStringLiteral("--headless"), QStringLiteral("--convert-to"), QStringLiteral("fodt"),
                          QStringLiteral("--outdir"), tmp.filePath(QStringLiteral("lo")), out});
        p.waitForFinished(120000);
        QFile f(tmp.filePath(QStringLiteral("lo/toc.fodt")));
        f.open(QIODevice::ReadOnly);
        const QString flat = QString::fromUtf8(f.readAll());
        check(flat.contains(QLatin1String("text:name=\"zwriter-toc\"")) && flat.contains(QLatin1String("Bald Eagle")),
              "O LibreOffice reads it as a table of contents");
    } else {
        std::printf("SKIP  LibreOffice not installed\n");
    }
    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
