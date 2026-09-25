// Regression tests for the v1.0.0 GUI test findings (ctest, headless):
//   H1  print/PDF body text is drawn at its real size (not ~1 pt)
//   H2  meta.xml / styles.xml are listed in META-INF/manifest.xml
//   L7  header/footer written as an ODF master page in styles.xml
//   H3  RTF import: no font/colour/style table leaks, Unicode, bold/italic, tables
//   L1  RTF export keeps bold/italic and the document font
//   M1  thin collapsed table grid on screen; 0.5 pt borders and 100 % width in ODT
//   M7  reopened ODT paragraphs/lists get no extra spacing; Courier keeps its fallbacks
// Needs `unzip` and `zip` on PATH. Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "DocumentIo.hpp"
#include "DocumentMeta.hpp"
#include "PrintLayout.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>

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

QByteArray unzipMember(const QString &zip, const QString &member)
{
    QProcess p;
    p.start(QStringLiteral("unzip"), {QStringLiteral("-p"), zip, member});
    p.waitForFinished(15000);
    return p.exitCode() == 0 ? p.readAllStandardOutput() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

QFont courier12()
{
    QFont f;
    f.setFamilies(DocumentIo::defaultFontFamilies());
    f.setStyleHint(QFont::TypeWriter);
    f.setPointSize(12);
    return f;
}

// ---- H1 -----------------------------------------------------------------
void testPrintScale()
{
    // Paint an A4 page at 300 dpi and measure the first line of 'H' glyphs.
    // 12 pt text at 300 dpi (50 px/em): cap height ~30 px, 20 monospace glyphs
    // advancing 0.6 em (30 px) each = ~600 px. The v1.0.0 bug drew it 12.5x
    // smaller (~2 px tall, ~48 px wide).
    const qreal dpi = 300.0;
    QTextDocument doc;
    doc.setDefaultFont(courier12());
    doc.setPlainText(QStringLiteral("HHHHHHHHHHHHHHHHHHHH"));
    PrintLayout::Page page;
    page.sheetMm = QSizeF(210, 297);
    page.marginsMm = QMarginsF(25.4, 25.4, 25.4, 25.4);
    QImage img(qRound(210 / 25.4 * dpi), qRound(297 / 25.4 * dpi), QImage::Format_RGB32);
    img.setDotsPerMeterX(qRound(dpi / 0.0254));
    img.setDotsPerMeterY(qRound(dpi / 0.0254));
    img.fill(Qt::white);
    int overlays = 0;
    {
        QPainter p(&img);
        const int pages = PrintLayout::paintDocument(
            doc, &p, page, dpi, dpi, []() { return true; },
            [&overlays](QPainter *, const QRectF &sheet, int, int) {
                if (sheet.width() > 2400) {
                    ++overlays; // header/footer callback gets device-pixel sheet
                }
            });
        check(pages == 1, "H1 one page painted");
    }
    int top = -1, bottom = -1, left = img.width(), right = -1;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        bool ink = false;
        for (int x = 0; x < img.width(); ++x) {
            if (qGray(row[x]) < 128) {
                ink = true;
                left = qMin(left, x);
                right = qMax(right, x);
            }
        }
        if (ink) {
            if (top < 0) {
                top = y;
            }
            bottom = y;
        }
    }
    const int capH = bottom - top + 1;
    const int width = right - left + 1;
    check(capH >= 22 && capH <= 40, "H1 12 pt glyphs are ~30 px tall at 300 dpi",
          QStringLiteral("ink height %1 px").arg(capH));
    check(width >= 480 && width <= 720, "H1 20 glyphs are ~600 px wide at 300 dpi",
          QStringLiteral("ink width %1 px").arg(width));
    check(qAbs(left - 300) <= 12 && top >= 300 && top <= 360, "H1 text starts at the 1 in margins",
          QStringLiteral("left %1 top %2").arg(left).arg(top));
    check(overlays == 1, "H1 header/footer overlay gets the device-size sheet");
}

// ---- H2 / L7 / M1 / M7 ---------------------------------------------------
void testOdtPackage(const QString &dir)
{
    QTextDocument doc;
    doc.setDefaultFont(courier12());
    QTextCursor c(&doc);
    c.insertText(QStringLiteral("Intro paragraph"));
    c.insertBlock();
    c.insertText(QStringLiteral("Second paragraph"));
    c.insertBlock();
    QTextListFormat lf;
    lf.setStyle(QTextListFormat::ListDisc);
    c.createList(lf);
    c.insertText(QStringLiteral("item one"));
    c.insertBlock();
    c.insertText(QStringLiteral("item two"));
    c.insertBlock();
    QTextBlockFormat plain;
    c.setBlockFormat(plain);
    if (QTextList *l = c.currentList()) {
        l->remove(c.block());
    }
    QTextTable *table = c.insertTable(2, 2);
    DocumentIo::styleTable(table);
    table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("A1"));
    table->cellAt(1, 1).firstCursorPosition().insertText(QStringLiteral("B2"));

    // M1 on screen: collapsed borders, 1 px everywhere.
    const QTextTableFormat tf = table->format();
    const QTextTableCellFormat cf = table->cellAt(1, 1).format().toTableCellFormat();
    check(tf.borderCollapse() && qFuzzyCompare(tf.border(), 1.0) && qFuzzyCompare(cf.leftBorder(), 1.0)
              && qFuzzyCompare(cf.topBorder(), 1.0) && tf.cellSpacing() == 0,
          "M1 table uses a single collapsed 1 px grid");

    const QString path = dir + QStringLiteral("/pkg.odt");
    QString err;
    check(DocumentIo::save(&doc, path, DocumentIo::Format::Odt, &err), "ODT saved", err);
    DocumentMeta meta;
    meta.ensureDefaults();
    meta.headerLeft = QStringLiteral("HDR");
    meta.footerRight = QStringLiteral("Page {page} of {pages}");
    OdtPageStyle ps;
    check(OdtMeta::writeToOdt(path, meta, &err, ps), "ODT metadata written", err);

    const QString manifest = QString::fromUtf8(unzipMember(path, QStringLiteral("META-INF/manifest.xml")));
    check(manifest.contains(QLatin1String("manifest:full-path=\"meta.xml\"")),
          "H2 manifest lists meta.xml");
    check(manifest.contains(QLatin1String("manifest:full-path=\"styles.xml\"")),
          "H2 manifest lists styles.xml");
    check(manifest.contains(QLatin1String("manifest:full-path=\"content.xml\"")),
          "H2 manifest still lists content.xml");
    QProcess list;
    list.start(QStringLiteral("unzip"), {QStringLiteral("-Z1"), path});
    list.waitForFinished(15000);
    const QStringList members = QString::fromUtf8(list.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    check(!members.isEmpty() && members.first() == QLatin1String("mimetype"), "H2 mimetype is the first member");
    bool allListed = true;
    for (const QString &m : members) {
        if (m == QLatin1String("mimetype") || m.endsWith(QLatin1Char('/')) || m == QLatin1String("META-INF/manifest.xml")) {
            continue;
        }
        if (!manifest.contains(QStringLiteral("manifest:full-path=\"%1\"").arg(m))) {
            allListed = false;
            std::printf("      not in manifest: %s\n", qPrintable(m));
        }
    }
    check(allListed, "H2 every package member is in the manifest");

    const QString styles = QString::fromUtf8(unzipMember(path, QStringLiteral("styles.xml")));
    check(styles.contains(QLatin1String("<style:master-page style:name=\"Standard\""))
              && styles.contains(QLatin1String("<style:header>"))
              && styles.contains(QLatin1String("HDR")),
          "L7 header written to the ODF master page");
    check(styles.contains(QLatin1String("<text:page-number")) && styles.contains(QLatin1String("<text:page-count")),
          "L7 {page}/{pages} become ODF page fields");
    check(styles.contains(QLatin1String("fo:page-width=\"210.00mm\"")), "L7 page size written");

    const QString content = QString::fromUtf8(unzipMember(path, QStringLiteral("content.xml")));
    check(content.contains(QLatin1String("fo:border=\"0.5pt solid #b8b8b8\"")), "M1 ODT cell borders are 0.5 pt");
    check(!content.contains(QLatin1String("1.125pt")), "M1 no 1.125 pt borders left");
    check(content.contains(QLatin1String("style:rel-width=\"100%\"")) && !content.contains(QLatin1String("style:width=\"100pt\"")),
          "M1 ODT table width is 100 % (rel-width), not 100 pt");

    // M7: reopen -> no paragraph/list spacing, Courier keeps fallbacks.
    QTextDocument back;
    check(DocumentIo::load(&back, path, &err), "ODT reopened", err);
    bool noSpacing = true;
    for (QTextBlock b = back.begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().topMargin() > 0.01 || b.blockFormat().bottomMargin() > 0.01) {
            noSpacing = false;
            std::printf("      block '%s' margins %.1f/%.1f\n", qPrintable(b.text()),
                        b.blockFormat().topMargin(), b.blockFormat().bottomMargin());
        }
    }
    check(noSpacing, "M7 reopened paragraphs and list items have no extra spacing");
    const QTextCharFormat first = back.begin().begin().fragment().charFormat();
    const QStringList fams = first.fontFamilies().toStringList();
    check(fams.size() > 1 && fams.contains(QLatin1String("Courier")), "M7 reopened Courier text keeps the fallback chain",
          fams.join(QLatin1Char(',')));
    bool tableBack = false;
    for (QTextFrame *f : back.rootFrame()->childFrames()) {
        if (auto *t = qobject_cast<QTextTable *>(f)) {
            tableBack = t->format().borderCollapse()
                && qFuzzyCompare(t->cellAt(0, 1).format().toTableCellFormat().leftBorder(), 1.0);
        }
    }
    check(tableBack, "M1 reopened table gets the thin grid");
}

// ---- H3 / L1 -------------------------------------------------------------
void testRtf(const QString &dir)
{
    // zwriter 1.0's own RTF (from the GUI test report).
    const QByteArray own =
        "{\\rtf1\\ansi\\deff0\n{\\fonttbl{\\f0 Times New Roman;}}\n\\f0\\fs24\n"
        "Plain start bold words and italic words end. Caf\\u233?.\\par\nSecond RTF paragraph.\n}\n";
    // LibreOffice-style header, formatting, \'hh escapes and a 2x2 table.
    const QByteArray lo =
        "{\\rtf1\\ansi\\ansicpg1252\\deff3\\adeflang1025\n"
        "{\\fonttbl{\\f0\\froman\\fprq2\\fcharset0 Times New Roman;}{\\f3\\froman\\fprq2\\fcharset0 Liberation Serif{\\*\\falt Times New Roman};}}\n"
        "{\\colortbl;\\red0\\green0\\blue0;\\red0\\green0\\blue255;}\n"
        "{\\stylesheet{\\s0\\snext0 Normal;}{\\s1\\sbasedon22\\snext19 heading 1;}{\\*\\cs15\\snext15 Endnote Characters;}}\n"
        "{\\*\\generator LibreOffice/25.2}{\\info{\\creatim\\yr2026\\mo9\\dy24}}\n"
        "\\pard\\plain \\s1\\fs48\\b LO Heading\n\\par \\pard\\plain {\\loch Normal }{\\loch\\b bold}{\\loch  }{\\loch\\i italic}"
        "{\\loch  caf\\'e9 \\'93q\\'94 na\\u239\\'69ve.}\n\\par "
        "\\trowd\\cellx414\\cellx845\\pard\\plain \\intbl{\\loch A1}\\cell\\pard\\plain \\intbl{\\loch B1}\\cell\\row\n"
        "\\trowd\\cellx414\\cellx845\\pard\\plain \\intbl{\\loch A2}\\cell\\pard\\plain \\intbl{\\loch B2}\\cell\\row\n"
        "\\pard\\plain After table.\\par }\n";
    const QString ownPath = dir + QStringLiteral("/own.rtf");
    const QString loPath = dir + QStringLiteral("/lo.rtf");
    writeFile(ownPath, own);
    writeFile(loPath, lo);

    QTextDocument a;
    QString err;
    check(DocumentIo::load(&a, ownPath, &err), "H3 own RTF opens", err);
    check(a.toPlainText() == QStringLiteral("Plain start bold words and italic words end. Caf\u00e9.\nSecond RTF paragraph."),
          "H3 own RTF: no font-table leak, \\u233? is e-acute", a.toPlainText());

    QTextDocument b;
    check(DocumentIo::load(&b, loPath, &err), "H3 LibreOffice RTF opens", err);
    const QString text = b.toPlainText();
    check(!text.contains(QLatin1String("Times New Roman")) && !text.contains(QLatin1String("Normal;"))
              && !text.contains(QLatin1String("LibreOffice")) && !text.contains(QLatin1Char(';')),
          "H3 font/colour/style tables and info do not leak", text.left(120));
    check(text.contains(QStringLiteral("caf\u00e9 \u201cq\u201d na\u00efve.")), "H3 \\'hh (cp1252) and \\uN decode",
          text);
    QTextTable *table = nullptr;
    for (QTextFrame *f : b.rootFrame()->childFrames()) {
        table = qobject_cast<QTextTable *>(f) ? qobject_cast<QTextTable *>(f) : table;
    }
    check(table && table->rows() == 2 && table->columns() == 2, "H3 RTF table becomes a 2x2 table");
    if (table) {
        auto cellText = [table](int r, int col) {
            QTextCursor c = table->cellAt(r, col).firstCursorPosition();
            c.setPosition(table->cellAt(r, col).lastCursorPosition().position(), QTextCursor::KeepAnchor);
            return c.selectedText();
        };
        check(cellText(0, 0) == QLatin1String("A1") && cellText(0, 1) == QLatin1String("B1")
                  && cellText(1, 0) == QLatin1String("A2") && cellText(1, 1) == QLatin1String("B2"),
              "H3 RTF cells keep their text");
    }
    bool sawBold = false, sawItalic = false;
    for (QTextBlock blk = b.begin(); blk.isValid(); blk = blk.next()) {
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (fr.text() == QLatin1String("bold") && fr.charFormat().fontWeight() >= QFont::Bold) {
                sawBold = true;
            }
            if (fr.text() == QLatin1String("italic") && fr.charFormat().fontItalic()) {
                sawItalic = true;
            }
        }
    }
    check(sawBold && sawItalic, "H3 RTF bold and italic survive import");

    // L1: export keeps formatting and font, and reads back.
    QTextDocument src;
    src.setDefaultFont(courier12());
    QTextCursor c(&src);
    c.insertText(QStringLiteral("plain "));
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    c.insertText(QStringLiteral("strong"), bold);
    QTextCharFormat ital;
    ital.setFontItalic(true);
    c.insertText(QStringLiteral(" slanted"), ital);
    c.insertText(QStringLiteral(" \u00e9t\u00e9 \u2014 done"), QTextCharFormat());
    c.insertBlock();
    c.insertText(QStringLiteral("item"));
    c.createList(QTextListFormat::ListDisc);
    const QString outPath = dir + QStringLiteral("/out.rtf");
    check(DocumentIo::save(&src, outPath, DocumentIo::Format::Rtf, &err), "L1 RTF saved", err);
    QFile f(outPath);
    f.open(QIODevice::ReadOnly);
    const QByteArray rtf = f.readAll();
    check(rtf.contains("\\b ") && rtf.contains("\\i ") && rtf.contains("Courier New")
              && !rtf.contains("Times New Roman"),
          "L1 RTF export has \\b, \\i and the document font");
    check(rtf.contains("{\\bullet\\tab }item"), "L1 RTF export writes list bullets as \\bullet");
    QTextDocument round;
    DocumentIo::load(&round, outPath, &err);
    bool rb = false, ri = false;
    for (auto it = round.begin().begin(); !it.atEnd(); ++it) {
        rb = rb || (it.fragment().text() == QLatin1String("strong") && it.fragment().charFormat().fontWeight() >= QFont::Bold);
        ri = ri || (it.fragment().text() == QLatin1String(" slanted") && it.fragment().charFormat().fontItalic());
    }
    // Lists are exported as literal markers, so the bullet comes back as text.
    const QString expected = QStringLiteral("plain strong slanted \u00e9t\u00e9 \u2014 done\n\u2022\titem");
    check(round.toPlainText() == expected && rb && ri, "L1 RTF round-trip keeps text, bold and italic",
          round.toPlainText());
}

// M1 rendered: the grid between two cells is one thin line (<= 2 px), not a
// double/thick bevelled border. Checks the Qt version CI builds against.
void testTableGridRender()
{
    QTextDocument d;
    d.setDocumentMargin(10);
    d.setTextWidth(300);
    QTextCursor c(&d);
    QTextTable *t = c.insertTable(2, 2);
    DocumentIo::styleTable(t);
    QImage img(320, 120, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    d.drawContents(&p);
    p.end();
    // Scan across the first row's middle and down the middle of the table.
    auto runs = [&](bool horizontal, int at) {
        QList<int> widths;
        int run = 0;
        const int n = horizontal ? img.width() : img.height();
        for (int i = 0; i < n; ++i) {
            const QRgb v = horizontal ? img.pixel(i, at) : img.pixel(at, i);
            if (qGray(v) < 245) {
                ++run;
            } else if (run) {
                widths << run;
                run = 0;
            }
        }
        if (run) {
            widths << run;
        }
        return widths;
    };
    const qreal rowH = t->cellAt(0, 0).firstCursorPosition().block().layout()->boundingRect().height();
    const QList<int> across = runs(true, int(10 + 8 + rowH / 2));
    const QList<int> down = runs(false, 10 + 75);
    bool thin = across.size() == 3 && down.size() == 3;
    for (int w : across + down) {
        thin = thin && w <= 2;
    }
    QStringList desc;
    for (int w : across) desc << QString::number(w);
    desc << QStringLiteral("|");
    for (int w : down) desc << QString::number(w);
    check(thin, "M1 table grid renders as single lines of <= 2 px", desc.join(QLatin1Char(' ')));
    // Light grey, not black.
    bool grey = false;
    for (int x = 0; x < img.width(); ++x) {
        const int g = qGray(img.pixel(x, int(10 + 8 + rowH / 2)));
        grey = grey || (g > 150 && g < 245);
        if (g < 100) {
            grey = false;
            break;
        }
    }
    check(grey, "M1 table grid is light grey");
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("FAIL  temp dir\n");
        return 1;
    }
    testPrintScale();
    testOdtPackage(dir.path());
    testRtf(dir.path());
    testTableGridRender();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
