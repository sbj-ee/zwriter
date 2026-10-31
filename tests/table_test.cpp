// Tables (1.16.0): column widths, merged cells and header rows survive ODT
// save/open (also LibreOffice-style files with only absolute or repeated
// columns), go into EPUB as <colgroup>/<thead>/colspan, header rows repeat on
// every page, and the drag-resize geometry finds the right borders.
// Needs `unzip` and `zip` on PATH. Run headless (QT_QPA_PLATFORM=offscreen).
#include <QXmlStreamReader>
#include <functional>
#include "BookExport.hpp"
#include "BookManifest.hpp"
#include "DocumentIo.hpp"
#include "TableColumns.hpp"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

#include <cmath>
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
    return QString::fromUtf8(p.readAllStandardOutput());
}
QList<QTextTable *> tablesOf(QTextDocument *d)
{
    QList<QTextTable *> out;
    std::function<void(QTextFrame *)> walk = [&](QTextFrame *f) {
        for (QTextFrame *c : f->childFrames()) {
            if (auto *t = qobject_cast<QTextTable *>(c)) {
                out << t;
            }
            walk(c);
        }
    };
    walk(d->rootFrame());
    return out;
}
QString list(const QList<double> &p)
{
    QStringList s;
    for (double v : p) {
        s << QString::number(v, 'f', 2);
    }
    return s.join(QLatin1Char(' '));
}
bool near(const QList<double> &a, const QList<double> &b, double tol = 0.02)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (int i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) > tol) {
            return false;
        }
    }
    return true;
}
QTextTable *buildTable(QTextDocument *d, int rows = 4)
{
    QTextCursor c(d);
    c.insertText(QStringLiteral("Before the table."));
    c.insertBlock();
    QTextTable *t = c.insertTable(rows, 3);
    DocumentIo::styleTable(t);
    const char *head[] = {"Part", "Qty", "Notes"};
    for (int col = 0; col < 3; ++col) {
        t->cellAt(0, col).firstCursorPosition().insertText(QString::fromLatin1(head[col]));
    }
    for (int r = 1; r < rows; ++r) {
        for (int col = 0; col < 3; ++col) {
            t->cellAt(r, col).firstCursorPosition().insertText(QStringLiteral("r%1c%2").arg(r).arg(col));
        }
    }
    TableColumns::setPercents(t, {20, 30, 50});
    QTextTableFormat f = t->format();
    f.setHeaderRowCount(1);
    t->setFormat(f);
    t->mergeCells(1, 0, 1, 2);
    QTextCursor after(d);
    after.movePosition(QTextCursor::End);
    after.insertBlock();
    after.insertText(QStringLiteral("After the table."));
    return t;
}
bool zipOdt(const QString &dir, const QString &out)
{
    QProcess z;
    z.setWorkingDirectory(dir);
    z.start(QStringLiteral("zip"), {QStringLiteral("-X0q"), out, QStringLiteral("mimetype")});
    z.waitForFinished(15000);
    QProcess z2;
    z2.setWorkingDirectory(dir);
    z2.start(QStringLiteral("zip"), {QStringLiteral("-Xrq"), out, QStringLiteral("content.xml"), QStringLiteral("META-INF")});
    z2.waitForFinished(15000);
    return z.exitCode() == 0 && z2.exitCode() == 0;
}
void writeFile(const QString &path, const QByteArray &b)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qFatal("cannot open %s", qPrintable(f.fileName()));
    }
    f.write(b);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;

    // ------------------------------------------------ pure width helpers
    check(near(TableColumns::normalized({1, 1, 2}), {25, 25, 50}), "normalized scales to 100");
    check(near(TableColumns::dragged({20, 30, 50}, 1, 10), {30, 20, 50}), "drag moves only the two neighbours");
    check(near(TableColumns::dragged({20, 30, 50}, 2, -40), {20, 3, 77}), "drag keeps the minimum width",
          list(TableColumns::dragged({20, 30, 50}, 2, -40)));
    check(near(TableColumns::afterInsert({20, 30, 50}, 1, 1), TableColumns::normalized({20, 100.0 / 3, 30, 50})),
          "insert column: average width, rescaled", list(TableColumns::afterInsert({20, 30, 50}, 1, 1)));
    check(near(TableColumns::afterRemove({20, 30, 50}, 0, 1), {37.5, 62.5}), "remove column rescales");

    // ------------------------------------------------ ODT round trip
    const QString odt = tmp.path() + QStringLiteral("/t.odt");
    {
        QTextDocument d;
        buildTable(&d);
        QString err;
        check(DocumentIo::save(&d, odt, DocumentIo::Format::Odt, &err), "save ODT", err);
    }
    const QString content = member(odt, QStringLiteral("content.xml"));
    check(content.contains(QLatin1String("style:rel-column-width=\"13107*\""))
              && content.contains(QLatin1String("style:rel-column-width=\"32768*\"")),
          "ODT: rel-column-width per column (LibreOffice style)");
    check(content.contains(QLatin1String("style:column-width=\"3.302cm\"")), "ODT: absolute column-width too");
    check(content.count(QLatin1String("<table:table-column ")) == 3, "ODT: three table-column elements");
    check(!content.contains(QLatin1String("column-width=\"20%\"")), "ODT: Qt's invalid % column styles dropped");
    check(content.count(QLatin1String("<table:table-header-rows>")) == 1
              && content.indexOf(QLatin1String("</table:table-header-rows>")) < content.indexOf(QLatin1String("r1c0"))
              && content.indexOf(QLatin1String("Notes")) < content.indexOf(QLatin1String("</table:table-header-rows>")),
          "ODT: first row inside table:table-header-rows");
    check(content.contains(QLatin1String("table:number-columns-spanned=\"2\"")), "ODT: merged cell spans 2 columns");
    {
        QTextDocument d;
        QString err;
        check(DocumentIo::load(&d, odt, &err), "load ODT", err);
        const QList<QTextTable *> ts = tablesOf(&d);
        check(ts.size() == 1, "one table back");
        if (!ts.isEmpty()) {
            QTextTable *t = ts.first();
            check(near(TableColumns::storedPercents(t), {20, 30, 50}), "widths survive save/open",
                  list(TableColumns::storedPercents(t)));
            check(t->format().headerRowCount() == 1, "header row survives save/open");
            check(t->cellAt(1, 0).columnSpan() == 2 && t->cellAt(1, 1) == t->cellAt(1, 0), "merged cell survives");
            check(t->format().border() == DocumentIo::kTableBorderPx, "zwriter's grid styling still applied");
            // Split, save, open: the span is gone, widths and header stay.
            QTextCursor c(&d);
            c.beginEditBlock();
            t->splitCell(1, 0, 1, 1);
            c.endEditBlock();
            const QString odt2 = tmp.path() + QStringLiteral("/t2.odt");
            DocumentIo::save(&d, odt2, DocumentIo::Format::Odt, &err);
            QTextDocument d2;
            DocumentIo::load(&d2, odt2, &err);
            const QList<QTextTable *> t2 = tablesOf(&d2);
            check(t2.size() == 1 && t2.first()->cellAt(1, 0).columnSpan() == 1 && t2.first()->cellAt(1, 1).columnSpan() == 1
                      && t2.first()->columns() == 3,
                  "split cell round-trips");
            check(t2.size() == 1 && near(TableColumns::storedPercents(t2.first()), {20, 30, 50})
                      && t2.first()->format().headerRowCount() == 1,
                  "second round trip keeps widths and header row");
            check(member(odt2, QStringLiteral("content.xml")) .count(QLatin1String("<table:table-header-rows>")) == 1,
                  "second save: still one header-rows block");
        }
    }
    {
        // A table with no stored widths and no header: written as before, opens as before.
        QTextDocument d;
        QTextCursor c(&d);
        QTextTable *t = c.insertTable(2, 2);
        DocumentIo::styleTable(t);
        const QString plain = tmp.path() + QStringLiteral("/plain.odt");
        QString err;
        DocumentIo::save(&d, plain, DocumentIo::Format::Odt, &err);
        const QString x = member(plain, QStringLiteral("content.xml"));
        check(!x.contains(QLatin1String("rel-column-width")) && !x.contains(QLatin1String("header-rows")),
              "table without widths: no column widths or header rows written");
        QTextDocument back;
        DocumentIo::load(&back, plain, &err);
        const QList<QTextTable *> ts = tablesOf(&back);
        check(ts.size() == 1 && TableColumns::storedPercents(ts.first()).isEmpty()
                  && ts.first()->format().headerRowCount() == 0,
              "... and opens without widths or header");
    }

    // ------------------------------------------------ LibreOffice-style file
    {
        const QString dir = tmp.path() + QStringLiteral("/lo");
        writeFile(dir + QStringLiteral("/mimetype"), "application/vnd.oasis.opendocument.text");
        writeFile(dir + QStringLiteral("/META-INF/manifest.xml"),
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
                  "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
                  "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>\n");
        writeFile(dir + QStringLiteral("/content.xml"), R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3">
<office:automatic-styles>
<style:style style:name="Tabelle1" style:family="table"><style:table-properties style:width="17cm" table:align="margins"/></style:style>
<style:style style:name="Tabelle1.A" style:family="table-column"><style:table-column-properties style:column-width="3.4cm"/></style:style>
<style:style style:name="Tabelle1.B" style:family="table-column"><style:table-column-properties style:column-width="6.8cm"/></style:style>
<style:style style:name="Tabelle2.A" style:family="table-column"><style:table-column-properties style:column-width="2cm" style:rel-column-width="1000*"/></style:style>
<style:style style:name="Tabelle2.B" style:family="table-column"><style:table-column-properties style:column-width="5cm" style:rel-column-width="3000*"/></style:style>
</office:automatic-styles>
<office:body><office:text>
<text:p>Intro</text:p>
<table:table table:name="Tabelle1" table:style-name="Tabelle1">
<table:table-column table:style-name="Tabelle1.A"/>
<table:table-column table:style-name="Tabelle1.B" table:number-columns-repeated="2"/>
<table:table-header-rows><table:table-row>
<table:table-cell office:value-type="string"><text:p>H1</text:p></table:table-cell>
<table:table-cell office:value-type="string"><text:p>H2</text:p></table:table-cell>
<table:table-cell office:value-type="string"><text:p>H3</text:p></table:table-cell>
</table:table-row></table:table-header-rows>
<table:table-row>
<table:table-cell office:value-type="string"><text:p>a</text:p></table:table-cell>
<table:table-cell table:number-columns-spanned="2" office:value-type="string"><text:p>bc</text:p></table:table-cell>
<table:covered-table-cell/>
</table:table-row>
</table:table>
<text:p>Middle</text:p>
<table:table table:name="Tabelle2">
<table:table-column table:style-name="Tabelle2.A"/>
<table:table-column table:style-name="Tabelle2.B"/>
<table:table-row><table:table-cell><text:p>x</text:p></table:table-cell><table:table-cell><text:p>y</text:p></table:table-cell></table:table-row>
</table:table>
<text:p>End</text:p>
</office:text></office:body></office:document-content>
)");
        const QString lo = tmp.path() + QStringLiteral("/lo.odt");
        check(zipOdt(dir, lo), "setup: LibreOffice-style ODT zipped");
        QTextDocument d;
        QString err;
        check(DocumentIo::load(&d, lo, &err), "load LibreOffice-style ODT", err);
        const QList<QTextTable *> ts = tablesOf(&d);
        check(ts.size() == 2, "two tables", QString::number(ts.size()));
        if (ts.size() == 2) {
            check(near(TableColumns::storedPercents(ts[0]), {20, 40, 40}),
                  "absolute widths with number-columns-repeated -> 20/40/40 %", list(TableColumns::storedPercents(ts[0])));
            check(ts[0]->format().headerRowCount() == 1, "table:table-header-rows -> header row");
            check(ts[0]->cellAt(1, 1).columnSpan() == 2, "LibreOffice span read");
            check(near(TableColumns::storedPercents(ts[1]), {25, 75}),
                  "rel-column-width wins over absolute -> 25/75 %", list(TableColumns::storedPercents(ts[1])));
            check(ts[1]->format().headerRowCount() == 0, "no header rows when the file has none");
        }
    }

    // ------------------------------------------------ EPUB
    {
        const QString dir = tmp.path() + QStringLiteral("/Book");
        QDir().mkpath(dir);
        {
            QTextDocument d;
            buildTable(&d);
            QString err;
            DocumentIo::save(&d, dir + QStringLiteral("/01 Parts.odt"), DocumentIo::Format::Odt, &err);
        }
        BookManifest book = BookManifest::fromFolder(dir);
        book.setTitle(QStringLiteral("Parts"));
        BookExport::Options opt;
        opt.titlePage = false;
        const QString epub = tmp.path() + QStringLiteral("/parts.epub");
        QString err;
        QStringList skipped;
        check(BookExport::writeEpub(book, opt, epub, &err, &skipped), "EPUB written", err);
        const QString ch = member(epub, QStringLiteral("OEBPS/ch01.xhtml"));
        check(ch.contains(QLatin1String("<colgroup><col style=\"width:20.00%\"/><col style=\"width:30.00%\"/><col style=\"width:50.00%\"/></colgroup>")),
              "EPUB: <colgroup> with the column widths");
        check(ch.contains(QLatin1String("<thead>")) && ch.contains(QLatin1String("<th><p>Part</p></th>")),
              "EPUB: header row as <thead>/<th>");
        check(ch.contains(QLatin1String("<td colspan=\"2\"><p>r1c0 r1c1</p></td>"))
                  || ch.contains(QLatin1String("<td colspan=\"2\"><p>r1c0</p><p>r1c1</p></td>")),
              "EPUB: merged cell as colspan");
        check(ch.count(QLatin1String("r2c2")) == 1 && ch.contains(QLatin1String("<p>After the table.</p>"))
                  && ch.indexOf(QLatin1String("</table>")) < ch.indexOf(QLatin1String("After the table.")),
              "EPUB: cell text once, inside the table; text after it follows");
        const QString css = member(epub, QStringLiteral("OEBPS/style.css"));
        check(css.contains(QLatin1String("border-collapse")), "EPUB: table CSS");
        // Well-formed XHTML.
        QXmlStreamReader x(ch);
        while (!x.atEnd()) {
            x.readNext();
        }
        check(!x.hasError(), "EPUB chapter is well-formed XML", x.errorString());
    }

    // ------------------------------------------------ layout: header repeats, resize geometry
    {
        auto firstRowOnPage2 = [](bool header) {
            QTextDocument d;
            d.setPageSize(QSizeF(500, 300));
            QTextTable *t = buildTable(&d, 30);
            t->splitCell(1, 0, 1, 1);
            QTextTableFormat f = t->format();
            f.setHeaderRowCount(header ? 1 : 0);
            t->setFormat(f);
            d.documentLayout()->documentSize();
            for (int r = 1; r < t->rows(); ++r) {
                const QRectF br = d.documentLayout()->blockBoundingRect(t->cellAt(r, 0).firstCursorPosition().block());
                if (br.top() >= 300) {
                    return br.top() - 300;
                }
            }
            return -1.0;
        };
        const double without = firstRowOnPage2(false);
        const double with = firstRowOnPage2(true);
        check(without >= 0 && with > without + 10, "header row repeats at the top of page 2 (body row pushed down)",
              QStringLiteral("%1 vs %2").arg(without).arg(with));

        QTextDocument d;
        d.setTextWidth(600);
        QTextTable *t = buildTable(&d);
        d.documentLayout()->documentSize();
        const QList<double> e = TableColumns::edges(t);
        check(e.size() == 4, "edges: n + 1");
        if (e.size() == 4) {
            const double w = e[3] - e[0];
            check(std::abs((e[1] - e[0]) / w - 0.2) < 0.03 && std::abs((e[2] - e[0]) / w - 0.5) < 0.03,
                  "edges follow 20/30/50", QStringLiteral("%1 %2 %3 %4").arg(e[0]).arg(e[1]).arg(e[2]).arg(e[3]));
            const auto rowY = [&](int r) {
                return d.documentLayout()->blockBoundingRect(t->cellAt(r, 2).firstCursorPosition().block()).center().y();
            };
            check(TableColumns::tableAt(&d, QPointF(e[1], rowY(0))) == t, "tableAt finds the table");
            check(TableColumns::borderAt(t, QPointF(e[1] + 2, rowY(0)), 4) == 1, "border 1 grabbed in a normal row");
            check(TableColumns::borderAt(t, QPointF(e[2] - 2, rowY(2)), 4) == 2, "border 2 grabbed");
            check(TableColumns::borderAt(t, QPointF(e[1], rowY(1)), 4) == -1,
                  "border hidden inside a merged cell is not grabbed");
            check(TableColumns::borderAt(t, QPointF(e[2], rowY(1)), 4) == 2, "merged cell's own right border grabbed");
            check(TableColumns::borderAt(t, QPointF((e[1] + e[2]) / 2, rowY(0)), 4) == -1, "mid-cell: no border");
            // A drag as the editor applies it: one undo step.
            const int undoBefore = d.availableUndoSteps();
            QTextCursor edit(&d);
            edit.beginEditBlock();
            TableColumns::setPercents(t, TableColumns::dragged(TableColumns::effectivePercents(t), 1, 15));
            edit.endEditBlock();
            check(near(TableColumns::storedPercents(t), {35, 15, 50}), "drag result stored",
                  list(TableColumns::storedPercents(t)));
            check(d.availableUndoSteps() == undoBefore + 1, "resize is one undo step");
            d.undo();
            check(near(TableColumns::storedPercents(t), {20, 30, 50}), "undo restores the widths");
        }
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
