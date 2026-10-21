// Whole-book export (1.10.0).
//   B the joined book: order, title page, ToC with page numbers and dot
//     leaders, figure labels per chapter, options off, skipped chapters
//   P PDF via QPrinter (the path Print Book uses), continuous pages
//   E EPUB 3.3: mimetype first and stored, files, nav, paragraph CSS,
//     well-formed XHTML (EPUBCheck runs on it in CI: ZW_EPUB_OUT)

#include "BookExport.hpp"
#include "BookManifest.hpp"
#include "Captions.hpp"
#include "DocumentIo.hpp"
#include "ParagraphStyles.hpp"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
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
QByteArray readAll(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}
QString member(const QString &zip, const QString &name)
{
    QProcess p;
    p.start(QStringLiteral("unzip"), {QStringLiteral("-p"), zip, name});
    p.waitForFinished(15000);
    return QString::fromUtf8(p.readAllStandardOutput());
}
QStringList members(const QString &zip)
{
    QProcess p;
    p.start(QStringLiteral("unzip"), {QStringLiteral("-Z1"), zip});
    p.waitForFinished(15000);
    return QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}
int pdfPages(const QString &path)
{
    static const QRegularExpression re(QStringLiteral("/Type\\s*/Page[^s]"));
    const QString s = QString::fromLatin1(readAll(path));
    return int(s.count(re));
}
QPageLayout a5()
{
    return QPageLayout(QPageSize(QPageSize::A5), QPageLayout::Portrait, QMarginsF(15, 15, 15, 20),
                       QPageLayout::Millimeter);
}
PrintLayout::Page pageOf(const QPageLayout &l)
{
    PrintLayout::Page p;
    p.sheetMm = PrintLayout::sheetSizeMm(l);
    p.marginsMm = l.margins(QPageLayout::Millimeter);
    return p;
}
QStringList tocOf(const BookExport::Result &r)
{
    QStringList l;
    for (const auto &t : r.toc) {
        l << QStringLiteral("%1:%2@%3").arg(t.level).arg(t.text).arg(t.page);
    }
    return l;
}
QTextBlock findText(const QTextDocument &d, const QString &prefix)
{
    for (QTextBlock b = d.begin(); b.isValid(); b = b.next()) {
        if (b.text().startsWith(prefix)) {
            return b;
        }
    }
    return QTextBlock();
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString dir = tmp.path() + QStringLiteral("/The Harbor");

    QByteArray harbor = "# Harbor\n\nThe tide came in.\n\n";
    for (int i = 0; i < 40; ++i) {
        harbor += "Gulls wheeled over the breakwater while the boats came home one by one, low in the water.\n\n";
    }
    harbor += "## At night\n\nLights on the water.\n";
    harbor = "# Harbor\n\n## Morning\n\nThe tide came in.\n\n" + harbor.mid(QByteArray("# Harbor\n\nThe tide came in.\n\n").size());
    write(dir + QStringLiteral("/01 Harbor.md"), harbor);

    {
        QTextDocument d;
        QTextCursor c(&d);
        c.insertText(QStringLiteral("Birds on the shore."));
        QTextBlockFormat indented;
        indented.setLeftMargin(48);
        indented.setTextIndent(30);
        c.insertBlock(indented);
        c.insertText(QStringLiteral("An indented paragraph."));
        c.insertBlock(QTextBlockFormat());
        QImage img(40, 30, QImage::Format_RGB32);
        img.fill(Qt::red);
        d.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("pic.png")), img);
        QTextImageFormat imf;
        imf.setName(QStringLiteral("pic.png"));
        imf.setWidth(40);
        imf.setHeight(30);
        c.insertImage(imf);
        c.insertBlock(QTextBlockFormat());
        c.insertText(QStringLiteral("Swans"));
        ParagraphStyles::apply(c, QStringLiteral("Caption"));
        Captions::renumber(&d, 7); // a stale number; the export renumbers
        QString err;
        const bool saved = DocumentIo::save(&d, dir + QStringLiteral("/02 Birds.odt"), DocumentIo::Format::Odt, &err);
        check(saved, "setup: ODT chapter saved", err);
    }
    write(dir + QStringLiteral("/03 Gone.md"), "# Gone\n");

    BookManifest book = BookManifest::fromFolder(dir);
    book.setTitle(QStringLiteral("The Harbor"));
    check(book.count() == 3, "setup: three chapters");
    BookChapter birds = book.at(1);
    birds.title = QStringLiteral("Birds"); // chapter 1 takes its title from its first heading
    book.setChapter(1, birds);
    QFile::remove(dir + QStringLiteral("/03 Gone.md"));

    const QFont font(QStringLiteral("Sans Serif"), 12);
    const QPageLayout layout = a5();

    // ------------------------------------------------------------ B
    BookExport::Options opt;
    BookExport::Result r = BookExport::build(book, opt, pageOf(layout), font);
    check(r.chapters == 2, "B two chapters joined", QString::number(r.chapters));
    check(r.skipped == QStringList{QStringLiteral("03 Gone.md")}, "B the missing chapter is skipped",
          r.skipped.join(QLatin1Char(',')));
    const QStringList toc = tocOf(r);
    // "Morning" comes right after the chapter's own title heading (which is
    // dropped): it must stay a heading.
    check(toc.size() == 4 && toc.value(0) == QLatin1String("1:1. Harbor@3") && toc.value(1) == QLatin1String("2:Morning@3")
              && toc.value(2).startsWith(QLatin1String("2:At night@")) && toc.value(3).startsWith(QLatin1String("1:2. Birds@")),
          "B ToC: chapters and headings with page numbers (title page 1, contents 2)", toc.join(QStringLiteral(" | ")));
    const int nightPage = r.toc.value(2).page;
    const int birdsPage = r.toc.value(3).page;
    check(nightPage > 3 && birdsPage == nightPage + 1, "B the long chapter runs over pages; Birds starts on a new one",
          QStringLiteral("%1 %2").arg(nightPage).arg(birdsPage));
    check(r.doc->pageCount() == birdsPage, "B page count", QString::number(r.doc->pageCount()));
    const QTextBlock tocLine = findText(*r.doc, QStringLiteral("1. Harbor"));
    check(tocLine.isValid() && tocLine.text().contains(QLatin1String(". . . .")) && tocLine.text().endsWith(QLatin1String("\t3")),
          "B ToC line has dot leaders and the page after a right tab", QString(tocLine.text()).replace(QLatin1Char('\t'), QLatin1String("<TAB>")));
    check(PrintLayout::pageOf(r.doc.get(), findText(*r.doc, QStringLiteral("Contents")).position()) == 2,
          "B Contents on page 2");
    {
        const QTextBlock t = findText(*r.doc, QStringLiteral("The Harbor"));
        const qreal y = r.doc->documentLayout()->blockBoundingRect(t).top();
        check(t.isValid() && y > r.doc->pageSize().height() / 5, "B the title sits lower on the title page", QString::number(y));
        QScopedPointer<QTextDocument> cl(PrintLayout::copyForPrint(*r.doc));
        PrintLayout::preparePages(cl.data(), pageOf(layout));
        const qreal yc = cl->documentLayout()->blockBoundingRect(findText(*cl, QStringLiteral("The Harbor"))).top();
        check(qAbs(yc - y) < 1, "B ... also in the copy that is printed", QString::number(yc));
    }
    check(findText(*r.doc, QStringLiteral("Figure 2.1. Swans")).isValid(), "B caption numbered for chapter 2");
    int harborTitles = 0;
    for (QTextBlock b = r.doc->begin(); b.isValid(); b = b.next()) {
        harborTitles += b.text() == QLatin1String("Harbor") ? 1 : 0;
    }
    check(harborTitles == 1, "B a first heading repeating the chapter title is not doubled", QString::number(harborTitles));
    bool hasImage = false;
    for (QTextBlock b = r.doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat()) {
                const QString n = it.fragment().charFormat().toImageFormat().name();
                hasImage = !r.doc->resource(QTextDocument::ImageResource, QUrl(n)).isNull();
            }
        }
    }
    check(hasImage, "B the picture comes along");

    BookExport::Options off;
    off.toc = off.titlePage = off.figureNumbers = false;
    BookExport::Result r2 = BookExport::build(book, off, pageOf(layout), font);
    check(r2.toc.isEmpty() && r2.doc->begin().text() == QLatin1String("Harbor")
              && r2.doc->begin().blockFormat().pageBreakPolicy() == QTextFormat::PageBreak_Auto,
          "B options off: no title page or contents, starts with chapter 1", r2.doc->begin().text());
    const QTextBlock bare = findText(*r2.doc, QStringLiteral("Swans"));
    check(bare.isValid() && bare.text() == QLatin1String("Swans"), "B figure numbering off removes the labels", bare.text());

    // ------------------------------------------------------------ P
    const QString pdf = tmp.path() + QStringLiteral("/book.pdf");
    int pages = 0;
    QString err;
    check(BookExport::writePdf(book, opt, layout, font, pdf, &pages, &err), "P book PDF written", err);
    check(readAll(pdf).startsWith("%PDF") && pages == birdsPage && pdfPages(pdf) == pages,
          "P one PDF, pages numbered through the book", QStringLiteral("%1 %2").arg(pages).arg(pdfPages(pdf)));
    {
        // Print Book paints the same document onto the chosen printer.
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        const QString printed = tmp.path() + QStringLiteral("/printed.pdf");
        printer.setOutputFileName(printed);
        printer.setPageLayout(layout);
        const int n = BookExport::paint(*r.doc, &printer, opt);
        check(n == pages && pdfPages(printed) == pages, "P printing uses the same layout", QString::number(n));
    }
    if (!qEnvironmentVariableIsEmpty("ZW_PDF_OUT")) {
        QFile::remove(qEnvironmentVariable("ZW_PDF_OUT"));
        QFile::copy(pdf, qEnvironmentVariable("ZW_PDF_OUT"));
    }
    check(!BookExport::writePdf(BookManifest(), opt, layout, font, tmp.path() + QStringLiteral("/none.pdf"), nullptr, &err)
              && !err.isEmpty(),
          "P an empty book is an error", err);

    // ------------------------------------------------------------ E
    const QString epub = tmp.path() + QStringLiteral("/book.epub");
    QStringList skipped;
    check(BookExport::writeEpub(book, opt, epub, &err, &skipped), "E EPUB written", err);
    const QByteArray zip = readAll(epub);
    check(zip.mid(30, 8) == "mimetype" && zip.at(8) == 0 && zip.at(9) == 0 && zip.mid(38, 20) == "application/epub+zip",
          "E mimetype is the first member, stored");
    const QStringList files = members(epub);
    for (const char *f : {"META-INF/container.xml", "OEBPS/content.opf", "OEBPS/nav.xhtml", "OEBPS/style.css",
                          "OEBPS/title.xhtml", "OEBPS/ch01.xhtml", "OEBPS/ch02.xhtml", "OEBPS/images/c2-1.png"}) {
        check(files.contains(QLatin1String(f)), "E member present", QLatin1String(f));
    }
    check(!files.contains(QStringLiteral("OEBPS/ch03.xhtml")) && skipped.size() == 1, "E missing chapter left out");
    const QString css = member(epub, QStringLiteral("OEBPS/style.css"));
    check(css.contains(QLatin1String("margin-left:48.0px;text-indent:30.0px;")), "E paragraph indents in the CSS", css);
    check(css.contains(QLatin1String("font-style:italic")), "E named style (Caption) in the CSS");
    const QString ch2 = member(epub, QStringLiteral("OEBPS/ch02.xhtml"));
    check(ch2.contains(QLatin1String("Figure 2.1. Swans")) && ch2.contains(QLatin1String("class=\"caption"))
              && ch2.contains(QLatin1String("<img src=\"images/c2-1.png\"")) && ch2.contains(QLatin1String("<h1>Birds</h1>")),
          "E chapter 2: title, picture, numbered caption", ch2.left(600));
    const QString nav = member(epub, QStringLiteral("OEBPS/nav.xhtml"));
    check(nav.contains(QLatin1String("epub:type=\"toc\"")) && nav.contains(QLatin1String("1. Harbor"))
              && nav.contains(QLatin1String("ch01.xhtml#s1\">Morning")) && nav.contains(QLatin1String("ch01.xhtml#s2\">At night")) && !nav.contains(QLatin1String("01 Harbor")),
          "E nav lists chapters and headings", nav);
    const QString opf = member(epub, QStringLiteral("OEBPS/content.opf"));
    check(opf.contains(QLatin1String("version=\"3.0\"")) && opf.contains(QLatin1String("dcterms:modified"))
              && opf.contains(QLatin1String("<itemref idref=\"nav\"/>")),
          "E package: EPUB 3, modified date, contents in the reading order");
    for (const QString &f : files) {
        if (f.endsWith(QLatin1String(".xhtml")) || f.endsWith(QLatin1String(".opf")) || f.endsWith(QLatin1String(".xml"))) {
            QXmlStreamReader x(member(epub, f));
            while (!x.atEnd()) {
                x.readNext();
            }
            check(!x.hasError(), "E well-formed XML", f + QLatin1Char(' ') + x.errorString());
        }
    }
    if (!qEnvironmentVariableIsEmpty("ZW_EPUB_OUT")) {
        QFile::remove(qEnvironmentVariable("ZW_EPUB_OUT"));
        QFile::copy(epub, qEnvironmentVariable("ZW_EPUB_OUT"));
    }
    const QString epub2 = tmp.path() + QStringLiteral("/bare.epub");
    check(BookExport::writeEpub(book, off, epub2, &err), "E options off written", err);
    const QString opf2 = member(epub2, QStringLiteral("OEBPS/content.opf"));
    check(!members(epub2).contains(QStringLiteral("OEBPS/title.xhtml")) && !opf2.contains(QLatin1String("<itemref idref=\"nav\"/>"))
              && member(epub2, QStringLiteral("OEBPS/ch02.xhtml")).contains(QLatin1String(">Swans<"))
              && !member(epub2, QStringLiteral("OEBPS/ch02.xhtml")).contains(QLatin1String("Figure")),
          "E options off: no title page, contents not in the reading order, no labels");
    if (!qEnvironmentVariableIsEmpty("ZW_EPUB_OUT")) {
        const QString o2 = qEnvironmentVariable("ZW_EPUB_OUT").replace(QLatin1String(".epub"), QLatin1String("-bare.epub"));
        QFile::remove(o2);
        QFile::copy(epub2, o2);
    }

    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
