// An ODT table of contents made in LibreOffice survives an edit and save in
// zwriter (1.5.4): the text:table-of-content element is written back
// unchanged, with the styles it uses (dot leaders) and its namespaces.
// Fixture: tests/data/toc-libreoffice.odt, made by
//   soffice --headless --convert-to odt tests/data/toc-libreoffice.fodt

#include "DocumentIo.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
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
QString member(const QString &zip, const QString &name)
{
    QProcess p;
    p.start(QStringLiteral("unzip"), {QStringLiteral("-p"), zip, name});
    p.waitForFinished(15000);
    return p.exitCode() == 0 ? QString::fromUtf8(p.readAllStandardOutput()) : QString();
}
QString tocOf(const QString &content)
{
    static const QRegularExpression re(QStringLiteral(R"(<text:table-of-content\b.*?</text:table-of-content>)"),
                                       QRegularExpression::DotMatchesEverythingOption);
    return re.match(content).captured(0);
}
bool wellFormed(const QString &xml, QString *err)
{
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
    }
    *err = r.errorString();
    return !r.hasError();
}
int tocBlocks(const QTextDocument &doc)
{
    int n = 0;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        n += b.blockFormat().hasProperty(DocumentIo::TocProperty) ? 1 : 0;
    }
    return n;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString fixture = QStringLiteral(ZW_SOURCE_DIR "/tests/data/toc-libreoffice.odt");
    const QString originalToc = tocOf(member(fixture, QStringLiteral("content.xml")));
    check(!originalToc.isEmpty() && originalToc.contains(QLatin1String("leader-char")), "fixture has a LibreOffice ToC");

    QTextDocument doc;
    QString err;
    check(DocumentIo::load(&doc, fixture, &err), "open fixture", err);
    const QString text = doc.toPlainText();
    check(text.contains(QLatin1String("Contents")) && text.contains(QLatin1String("Meadowlarks\t1")),
          "ToC entries show as text");
    check(!text.contains(QLatin1String("Contents 1")) && text.count(QLatin1String("Contents")) == 1,
          "the entry templates are not shown as text", text.left(120));
    check(tocBlocks(doc) == 4, "the four ToC paragraphs are marked", QString::number(tocBlocks(doc)));

    // Edit something outside the table, then save.
    QTextCursor c(&doc);
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral(" Edited in zwriter."));
    QTemporaryDir tmp;
    const QString out = tmp.filePath(QStringLiteral("saved.odt"));
    check(DocumentIo::save(&doc, out, DocumentIo::Format::Odt, &err), "save after an edit", err);
    const QString content = member(out, QStringLiteral("content.xml"));
    check(tocOf(content) == originalToc, "text:table-of-content written back unchanged");
    check(content.count(QLatin1String("<text:table-of-content ")) == 1 && !content.contains(QLatin1String(">Meadowlarks<text:tab/>1</text:p><text:p text:style-name=\"p")),
          "no flattened copy beside it");
    check(content.contains(QLatin1String("style:name=\"Contents_20_1\"")) && content.contains(QLatin1String("style:leader-text=\".\""))
              && content.contains(QLatin1String("style:name=\"Contents_20_Heading\"")) && content.contains(QLatin1String("style:name=\"Sect1\"")),
          "the ToC's styles (dot-leader tab stops, heading, section) are kept");
    QString xmlErr;
    check(wellFormed(content, &xmlErr), "content.xml is well-formed", xmlErr);
    check(content.contains(QLatin1String("Edited in zwriter.")) && content.indexOf(QLatin1String("Edited in zwriter."))
                                                                         > content.indexOf(QLatin1String("</text:table-of-content>")),
          "the edit is saved, after the table");

    // Open again: still marked; a second save is identical.
    QTextDocument again;
    DocumentIo::load(&again, out);
    check(tocBlocks(again) == 4, "reopened: the ToC is still recognised");
    const QString out2 = tmp.filePath(QStringLiteral("saved2.odt"));
    DocumentIo::save(&again, out2, DocumentIo::Format::Odt);
    check(tocOf(member(out2, QStringLiteral("content.xml"))) == originalToc, "second save keeps it too");

    // Delete the table in zwriter: it stays deleted.
    for (QTextBlock b = again.begin(); b.isValid();) {
        const QTextBlock next = b.next();
        if (b.blockFormat().hasProperty(DocumentIo::TocProperty)) {
            QTextCursor d(b);
            d.select(QTextCursor::BlockUnderCursor);
            if (b == again.begin()) {
                d.setPosition(b.position());
                d.setPosition(next.position(), QTextCursor::KeepAnchor);
            }
            d.removeSelectedText();
        }
        b = next;
    }
    const QString out3 = tmp.filePath(QStringLiteral("deleted.odt"));
    DocumentIo::save(&again, out3, DocumentIo::Format::Odt);
    check(!member(out3, QStringLiteral("content.xml")).contains(QLatin1String("table-of-content")), "a deleted ToC is not resurrected");

    // Bug 2 ("Mallards" lost its italics): a span inside a span. Closing the
    // inner one must not end the outer one's bold, and both italics stay.
    {
        QTextDocument nd;
        DocumentIo::load(&nd, QStringLiteral(ZW_SOURCE_DIR "/tests/data/nested-spans.odt"));
        auto fmtOf = [&](const QString &word) {
            for (auto f = nd.firstBlock().begin(); !f.atEnd(); ++f) {
                if (f.fragment().text().contains(word)) {
                    return f.fragment().charFormat();
                }
            }
            return QTextCharFormat();
        };
        check(fmtOf(QStringLiteral("Mallards")).fontItalic() && fmtOf(QStringLiteral("Mallards")).fontWeight() >= QFont::Bold,
              "nested span: inner italic inside bold");
        check(fmtOf(QStringLiteral("after")).fontWeight() >= QFont::Bold && !fmtOf(QStringLiteral("after")).fontItalic(),
              "nested span: outer bold continues after the inner span");
        check(fmtOf(QStringLiteral("plain")).fontWeight() < QFont::Bold && fmtOf(QStringLiteral("Wood Duck")).fontItalic(),
              "nested span: plain text, then a second italic span");
        const QString out4 = tmp.filePath(QStringLiteral("spans.odt"));
        DocumentIo::save(&nd, out4, DocumentIo::Format::Odt);
        QTextDocument nd2;
        DocumentIo::load(&nd2, out4);
        bool ok = false;
        for (auto f = nd2.firstBlock().begin(); !f.atEnd(); ++f) {
            if (f.fragment().text().contains(QLatin1String("Mallards"))) {
                ok = f.fragment().charFormat().fontItalic();
            }
        }
        check(ok, "nested span: italics survive a zwriter save");
    }

    // LibreOffice (when installed) still sees a table of contents.
    const QString soffice = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (!soffice.isEmpty()) {
        QProcess lo;
        lo.setWorkingDirectory(tmp.path());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("HOME"), tmp.path());
        lo.setProcessEnvironment(env);
        lo.start(soffice, {QStringLiteral("--headless"), QStringLiteral("--convert-to"), QStringLiteral("fodt"),
                           QStringLiteral("--outdir"), tmp.filePath(QStringLiteral("lo")), out});
        lo.waitForFinished(120000);
        QFile f(tmp.filePath(QStringLiteral("lo/saved.fodt")));
        const bool opened = f.open(QIODevice::ReadOnly);
        const QString flat = opened ? QString::fromUtf8(f.readAll()) : QString();
        check(flat.contains(QLatin1String("<text:table-of-content ")) && flat.contains(QLatin1String("Meadowlarks")),
              "LibreOffice reads the saved file's ToC");
    } else {
        std::printf("SKIP  LibreOffice not installed\n");
    }
    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
