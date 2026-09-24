// ODT round-trip regression test for DocumentIo (save -> open -> save -> open).
// Needs `unzip` and `zip` on PATH (as the app does). Run headless:
//   QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
#include "DocumentIo.hpp"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

// Structural fingerprint of a document: one line per block.
QString dump(const QTextDocument &doc)
{
    QString out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat bf = b.blockFormat();
        QString line;
        if (const QTextList *l = b.textList()) {
            line += QStringLiteral("[list %1/%2]").arg(int(l->format().style())).arg(l->format().indent());
        }
        QTextCursor c(b);
        if (QTextTable *t = c.currentTable()) {
            const QTextTableCell cell = t->cellAt(c);
            line += QStringLiteral("[cell %1,%2 %3x%4 of %5x%6]")
                        .arg(cell.row()).arg(cell.column()).arg(cell.rowSpan()).arg(cell.columnSpan())
                        .arg(t->rows()).arg(t->columns());
        }
        if (bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) {
            line += QStringLiteral("[break]");
        }
        if (bf.headingLevel() > 0) {
            line += QStringLiteral("[h%1]").arg(bf.headingLevel());
        }
        if (bf.alignment() & Qt::AlignHCenter) {
            line += QStringLiteral("[center]");
        }
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (cf.fontUnderline()) {
                line += QStringLiteral("[u]");
            }
            if (bf.headingLevel() > 0) {
                line += QStringLiteral("[%1pt w%2]").arg(cf.fontPointSize()).arg(cf.fontWeight());
            }
            break;
        }
        QString text = b.text();
        text.replace(QChar::LineSeparator, QStringLiteral("<LS>")).replace(QLatin1Char('\t'), QStringLiteral("<TAB>"));
        out += line + QLatin1Char('|') + text + QLatin1Char('\n');
    }
    return out;
}

bool anyExplicitForeground(const QTextDocument &doc)
{
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (b.charFormat().hasProperty(QTextFormat::ForegroundBrush)) {
            return true;
        }
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().hasProperty(QTextFormat::ForegroundBrush)) {
                return true;
            }
        }
    }
    return false;
}

void heading(QTextCursor &c, int level, qreal pt, int weight, const QString &text)
{
    QTextBlockFormat bf;
    bf.setHeadingLevel(level);
    c.setBlockFormat(bf);
    QTextCharFormat cf;
    cf.setFontPointSize(pt);
    cf.setFontWeight(weight);
    c.insertText(text, cf);
}

void buildSample(QTextDocument *doc)
{
    QTextCursor c(doc);
    heading(c, 1, 22, QFont::Bold, QStringLiteral("Title & <escapes> \"q\""));
    c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    QTextCharFormat u;
    u.setFontUnderline(true);
    c.insertText(QStringLiteral("underlined"), u);
    c.insertText(QStringLiteral(" plain"), QTextCharFormat());
    QTextBlockFormat centred;
    centred.setAlignment(Qt::AlignHCenter);
    c.insertBlock(centred);
    c.insertText(QStringLiteral("centered"));
    c.insertBlock(QTextBlockFormat()); // blank paragraph
    c.insertBlock(QTextBlockFormat());
    c.insertText(QStringLiteral("a    b\tc  d"));
    c.insertBlock(QTextBlockFormat());
    c.insertText(QStringLiteral("   indented  "));
    c.insertBlock(QTextBlockFormat());
    c.insertText(QStringLiteral("line one") + QChar(QChar::LineSeparator) + QStringLiteral("  line two"));
    c.insertBlock(QTextBlockFormat());
    heading(c, 2, 18, QFont::Bold, QStringLiteral("Section"));
    c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    heading(c, 3, 14, QFont::DemiBold, QStringLiteral("Sub section"));
    c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    c.insertText(QStringLiteral("bullets:"));
    QTextListFormat disc;
    disc.setStyle(QTextListFormat::ListDisc);
    disc.setIndent(1);
    c.insertBlock();
    c.createList(disc);
    c.insertText(QStringLiteral("b1"));
    c.insertBlock();
    c.insertText(QStringLiteral("b2"));
    QTextListFormat alpha;
    alpha.setStyle(QTextListFormat::ListLowerAlpha);
    alpha.setIndent(2);
    c.insertBlock();
    c.createList(alpha);
    c.insertText(QStringLiteral("nested a"));
    c.insertBlock();
    c.insertText(QStringLiteral("nested b"));
    c.insertBlock(QTextBlockFormat());
    QTextListFormat decimal;
    decimal.setStyle(QTextListFormat::ListDecimal);
    decimal.setIndent(1);
    c.createList(decimal);
    c.insertText(QStringLiteral("n1"));
    c.insertBlock();
    c.insertText(QStringLiteral("n2"));
    QTextBlockFormat pageBreak;
    pageBreak.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    c.insertBlock(pageBreak);
    c.setBlockFormat(pageBreak); // leave the list
    c.insertText(QStringLiteral("after page break"));
    c.insertBlock(QTextBlockFormat());
    QTextTable *t = c.insertTable(3, 3);
    for (int r = 0; r < 3; ++r) {
        for (int col = 0; col < 3; ++col) {
            t->cellAt(r, col).firstCursorPosition().insertText(QStringLiteral("r%1c%2").arg(r).arg(col));
        }
    }
    t->mergeCells(0, 0, 1, 2);
    c.movePosition(QTextCursor::End);
    c.insertBlock(QTextBlockFormat()); // blank line after the table's own trailing block
    c.insertText(QStringLiteral("after table"));
}

bool writeLibreOfficeStyleOdt(const QString &path)
{
    QTemporaryDir dir;
    QDir(dir.path()).mkdir(QStringLiteral("META-INF"));
    auto put = [&](const QString &name, const QByteArray &data) {
        QFile f(dir.filePath(name));
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    };
    put(QStringLiteral("mimetype"), "application/vnd.oasis.opendocument.text");
    put(QStringLiteral("META-INF/manifest.xml"),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>");
    // Shaped like LibreOffice output: one list style with several levels, nested
    // lists without a style name, headings via text:h and Heading_20_N styles,
    // soft page breaks, a covered (merged) table cell, no pretty-printing.
    put(QStringLiteral("content.xml"),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
        "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" office:version=\"1.3\">"
        "<office:automatic-styles>"
        "<style:style style:name=\"P1\" style:family=\"paragraph\" style:parent-style-name=\"Standard\">"
        "<style:paragraph-properties fo:break-before=\"page\"/></style:style>"
        "<style:style style:name=\"P2\" style:family=\"paragraph\" style:parent-style-name=\"Heading_20_2\"/>"
        "<text:list-style style:name=\"L1\">"
        "<text:list-level-style-number text:level=\"1\" style:num-suffix=\".\" style:num-format=\"1\"/>"
        "<text:list-level-style-number text:level=\"2\" style:num-suffix=\".\" style:num-format=\"a\"/>"
        "<text:list-level-style-bullet text:level=\"3\" text:bullet-char=\"\xe2\x97\xa6\"/>"
        "</text:list-style>"
        "</office:automatic-styles><office:body><office:text>"
        "<text:h text:style-name=\"Heading_20_1\" text:outline-level=\"1\">LO heading</text:h>"
        "<text:p text:style-name=\"P2\">Styled heading two</text:p>"
        "<text:p text:style-name=\"Standard\">Intro  with <text:s text:c=\"2\"/>spaces<text:tab/>and tab</text:p>"
        "<text:list xml:id=\"list1\" text:style-name=\"L1\">"
        "<text:list-item><text:p>one</text:p>"
        "<text:list><text:list-item><text:p>one-a</text:p>"
        "<text:list><text:list-item><text:p>deep</text:p></text:list-item></text:list>"
        "</text:list-item><text:list-item><text:p>one-b</text:p></text:list-item></text:list>"
        "</text:list-item>"
        "<text:list-item><text:p>two</text:p></text:list-item>"
        "</text:list>"
        "<text:p text:style-name=\"Standard\">Before soft<text:soft-page-break/> after soft</text:p>"
        "<text:soft-page-break/>"
        "<text:p text:style-name=\"P1\">Hard break para</text:p>"
        "<table:table table:name=\"T1\"><table:table-column table:number-columns-repeated=\"2\"/>"
        "<table:table-row><table:table-cell table:number-columns-spanned=\"2\"><text:p>merged</text:p></table:table-cell>"
        "<table:covered-table-cell/></table:table-row>"
        "<table:table-row><table:table-cell><text:p>x</text:p></table:table-cell>"
        "<table:table-cell><text:p>y</text:p></table:table-cell></table:table-row>"
        "</table:table>"
        "<text:p text:style-name=\"Standard\">End</text:p>"
        "</office:text></office:body></office:document-content>");
    QFile::remove(path);
    QProcess z0;
    z0.setWorkingDirectory(dir.path());
    z0.start(QStringLiteral("zip"), {QStringLiteral("-X0q"), path, QStringLiteral("mimetype")});
    QProcess z1;
    z1.setWorkingDirectory(dir.path());
    if (!z0.waitForFinished(15000) || z0.exitCode() != 0) {
        return false;
    }
    z1.start(QStringLiteral("zip"), {QStringLiteral("-Xrq"), path, QStringLiteral("META-INF"), QStringLiteral("content.xml")});
    return z1.waitForFinished(15000) && z1.exitCode() == 0;
}

QString blockDump(const QTextDocument &doc, const QString &text)
{
    for (const QString &line : dump(doc).split(QLatin1Char('\n'))) {
        if (line.endsWith(QLatin1Char('|') + text)) {
            return line;
        }
    }
    return QString();
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir tmp;
    QString err;

    QTextDocument original;
    buildSample(&original);
    const QString expected = dump(original);

    const QString p1 = tmp.filePath(QStringLiteral("a.odt"));
    check(DocumentIo::save(&original, p1, DocumentIo::Format::Odt, &err), "save sample ODT");
    QTextDocument load1;
    check(DocumentIo::load(&load1, p1, &err), "open sample ODT");
    const QString got1 = dump(load1);
    check(got1 == expected, "reopened document matches the original structure/text");
    if (got1 != expected) {
        std::printf("--- expected\n%s--- got\n%s", qPrintable(expected), qPrintable(got1));
    }
    check(!load1.isUndoAvailable(), "no undo history after open");
    check(!load1.isModified(), "document clean after open");
    check(!anyExplicitForeground(load1), "no explicit text colour after open");
    bool wraps = true;
    for (QTextBlock b = load1.begin(); b.isValid(); b = b.next()) {
        wraps = wraps && !b.blockFormat().nonBreakableLines();
    }
    check(wraps, "reopened paragraphs still wrap (no non-breakable lines)");

    const QString p2 = tmp.filePath(QStringLiteral("b.odt"));
    DocumentIo::save(&load1, p2, DocumentIo::Format::Odt, &err);
    QTextDocument load2;
    DocumentIo::load(&load2, p2, &err);
    check(dump(load2) == got1, "second save/open cycle is identical");

    // Heading paragraphs are written as text:h.
    QProcess unzip;
    unzip.start(QStringLiteral("unzip"), {QStringLiteral("-p"), p1, QStringLiteral("content.xml")});
    unzip.waitForFinished(15000);
    const QByteArray xml = unzip.readAllStandardOutput();
    check(xml.contains("<text:h text:outline-level=\"1\"") && xml.contains("<text:h text:outline-level=\"3\""),
          "headings saved as text:h with outline level");

    // LibreOffice-shaped file.
    const QString lo = tmp.filePath(QStringLiteral("lo.odt"));
    check(writeLibreOfficeStyleOdt(lo), "build LibreOffice-style fixture");
    QTextDocument loDoc;
    check(DocumentIo::load(&loDoc, lo, &err), "open LibreOffice-style ODT");
    check(blockDump(loDoc, QStringLiteral("LO heading")).startsWith(QLatin1String("[h1][22pt w700]")),
          "LO text:h -> H1 (22 pt bold like Paragraph Style)");
    check(blockDump(loDoc, QStringLiteral("Styled heading two")).contains(QLatin1String("[h2]")),
          "LO Heading_20_2 paragraph style -> H2");
    check(blockDump(loDoc, QStringLiteral("Intro with   spaces<TAB>and tab")).startsWith(QLatin1Char('|')),
          "LO whitespace: run collapses, text:s count and text:tab kept");
    check(blockDump(loDoc, QStringLiteral("one")).startsWith(QStringLiteral("[list %1/1]").arg(int(QTextListFormat::ListDecimal))),
          "LO level 1 numbered");
    check(blockDump(loDoc, QStringLiteral("one-a")).startsWith(QStringLiteral("[list %1/2]").arg(int(QTextListFormat::ListLowerAlpha))),
          "LO nested list inherits style: level 2 lower-alpha");
    check(blockDump(loDoc, QStringLiteral("one-b")).startsWith(QStringLiteral("[list %1/2]").arg(int(QTextListFormat::ListLowerAlpha))),
          "LO nested list second item lower-alpha");
    check(blockDump(loDoc, QStringLiteral("deep")).startsWith(QStringLiteral("[list %1/3]").arg(int(QTextListFormat::ListCircle))),
          "LO level 3 circle bullets");
    check(blockDump(loDoc, QStringLiteral("two")).startsWith(QStringLiteral("[list %1/1]").arg(int(QTextListFormat::ListDecimal))),
          "LO back to level 1 numbered");
    check(!blockDump(loDoc, QStringLiteral("Before soft after soft")).isEmpty(), "soft page break ignored");
    check(blockDump(loDoc, QStringLiteral("Hard break para")).contains(QLatin1String("[break]")), "fo:break-before -> page break");
    check(blockDump(loDoc, QStringLiteral("merged")).contains(QLatin1String("1x2 of 2x2")), "merged cell");
    check(!anyExplicitForeground(loDoc), "LO: no explicit text colour");

    if (g_failures) {
        std::printf("--- LO dump\n%s", qPrintable(dump(loDoc)));
    }
    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
