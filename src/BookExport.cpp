#include "BookExport.hpp"

#include "BookManifest.hpp"
#include "Captions.hpp"
#include "DocumentIo.hpp"
#include "ParagraphStyles.hpp"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPrinter>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QTextOption>
#include <QTextTable>
#include <QUrl>
#include <QUuid>

namespace BookExport {

namespace {

QString chapterTitle(const BookManifest &book, int i)
{
    const BookChapter &c = book.at(i);
    return c.title.trimmed().isEmpty() ? QFileInfo(c.path).completeBaseName() : c.title.trimmed();
}

QString bookTitle(const BookManifest &book)
{
    return book.title().trimmed().isEmpty() ? QFileInfo(book.bookDir()).fileName() : book.title().trimmed();
}

void removeBlock(QTextDocument *d, const QTextBlock &b)
{
    QTextCursor c(d);
    if (b.next().isValid()) {
        c.setPosition(b.position());
        c.setPosition(b.next().position(), QTextCursor::KeepAnchor);
    } else if (b.previous().isValid()) {
        c.setPosition(b.position() - 1);
        c.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
    } else {
        c.setPosition(b.position());
        c.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(QTextBlockFormat());
        return;
    }
    c.removeSelectedText();
}

// A chapter file ready to go into the book: no ToC of its own, captions
// numbered for its place (or unlabelled), no first heading repeating the title.
bool loadChapter(const BookManifest &book, int i, const Options &o, const QFont &font, QTextDocument *d,
                 QString *title)
{
    const QString abs = book.absolutePath(i);
    if (abs.isEmpty() || book.isMissing(i)) {
        return false;
    }
    d->setDefaultFont(font);
    d->setUndoRedoEnabled(false);
    if (DocumentIo::formatFromPath(abs) == DocumentIo::Format::Markdown) {
        // The editor keeps Markdown as plain text; the book wants it rendered.
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) {
            return false;
        }
        d->setMarkdown(QString::fromUtf8(f.readAll()));
    } else if (!DocumentIo::load(d, abs, nullptr)) {
        return false;
    }
    for (QTextBlock b = d->lastBlock(); b.isValid();) {
        const QTextBlock prev = b.previous();
        const QTextBlockFormat f = b.blockFormat();
        if (f.hasProperty(DocumentIo::TocProperty) || f.hasProperty(DocumentIo::GeneratedTocProperty)
            || f.hasProperty(DocumentIo::GeneratedTocTitleProperty)) {
            removeBlock(d, b);
        }
        b = prev;
    }
    if (o.figureNumbers) {
        Captions::renumber(d, book.chapterNumber(i));
    } else {
        for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
            const int n = Captions::isCaption(b) ? Captions::labelLength(b) : 0;
            if (n > 0) {
                QTextCursor c(b);
                c.setPosition(b.position() + n, QTextCursor::KeepAnchor);
                c.removeSelectedText();
            }
        }
    }
    QTextBlock first = d->begin();
    while (first.isValid() && first.text().trimmed().isEmpty() && first.next().isValid()) {
        first = first.next();
    }
    // The chapter title is the book.json title; without one, the file's
    // first heading (else its name). That heading is not repeated below it.
    *title = chapterTitle(book, i);
    if (first.isValid() && first.blockFormat().headingLevel() > 0 && !first.text().trimmed().isEmpty()) {
        const QString h = first.text().trimmed();
        if (book.at(i).title.trimmed().isEmpty() || h.compare(*title, Qt::CaseInsensitive) == 0) {
            *title = h;
            removeBlock(d, first);
        }
    }
    return true;
}

QImage imageOf(const QVariant &v)
{
    if (v.canConvert<QImage>() && v.typeId() == QMetaType::QImage) {
        return v.value<QImage>();
    }
    if (v.typeId() == QMetaType::QPixmap) {
        return v.value<QPixmap>().toImage();
    }
    if (v.typeId() == QMetaType::QByteArray) {
        return QImage::fromData(v.toByteArray());
    }
    return QImage();
}

// Calls f(fragment) for every picture in the document.
template<typename F>
void forEachImage(const QTextDocument *d, F f)
{
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            if (it.fragment().isValid() && it.fragment().charFormat().isImageFormat()) {
                f(it.fragment());
            }
        }
    }
}

QTextCharFormat headingChar(qreal pt)
{
    QTextCharFormat cf;
    cf.setFontPointSize(pt);
    cf.setFontWeight(QFont::Bold);
    return cf;
}

QTextCharFormat tocChar(const QFont &font, int level)
{
    QTextCharFormat cf;
    cf.setFontPointSize(font.pointSizeF() > 0 ? font.pointSizeF() : 12);
    cf.setFontWeight(level == 1 ? QFont::Bold : QFont::Normal);
    cf.setFontItalic(false);
    return cf;
}

} // namespace

Result build(const BookManifest &book, const Options &options, const PrintLayout::Page &page,
             const QFont &defaultFont)
{
    Result r;
    r.doc = std::make_unique<QTextDocument>();
    QTextDocument *doc = r.doc.get();
    doc->setDefaultFont(defaultFont);
    doc->setUndoRedoEnabled(false);
    QTextCursor c(doc);
    bool empty = true;
    auto newBlock = [&](const QTextBlockFormat &bf, const QTextCharFormat &cf) {
        if (empty) {
            c.setBlockFormat(bf);
            c.setBlockCharFormat(cf);
            c.setCharFormat(cf);
            empty = false;
        } else {
            c.insertBlock(bf, cf);
        }
    };
    const qreal pageHeightPx = page.sheetMm.height() * PrintLayout::layoutDpi() / 25.4;

    if (options.titlePage) {
        // A fixed-height empty line pushes the title down a third of the page
        // (a top margin on the first block of a page is dropped).
        QTextBlockFormat spacer;
        spacer.setLineHeight(pageHeightPx / 3, QTextBlockFormat::FixedHeight);
        spacer.setProperty(TitlePageProperty, true);
        newBlock(spacer, QTextCharFormat());
        QTextBlockFormat bf;
        bf.setAlignment(Qt::AlignHCenter);
        bf.setProperty(TitlePageProperty, true);
        const QTextCharFormat cf = headingChar(28);
        newBlock(bf, cf);
        c.insertText(bookTitle(book), cf);
    }
    int tocPos = -1;
    // A book with a Contents chapter gets its table there, not a second one here.
    const int contentsAt = book.contentsIndex();
    if (options.toc && contentsAt < 0) {
        QTextBlockFormat bf;
        if (!empty) {
            bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        }
        bf.setBottomMargin(12);
        bf.setProperty(TocEntryProperty, 0);
        const QTextCharFormat cf = headingChar(20);
        newBlock(bf, cf);
        c.insertText(QStringLiteral("Contents"), cf);
        tocPos = c.position();
    }

    for (int i = 0; i < book.count(); ++i) {
        if (i == contentsAt) {
            // The Contents chapter: its saved table is replaced by the one built
            // below (page numbers for this layout); left out without the ToC.
            if (!options.toc) {
                continue;
            }
            QTextBlockFormat bf;
            if (!empty) {
                bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            }
            bf.setBottomMargin(12);
            bf.setProperty(TocEntryProperty, 0);
            const QTextCharFormat cf = headingChar(20);
            newBlock(bf, cf);
            c.insertText(book.displayTitle(i), cf);
            tocPos = c.position();
            ++r.chapters;
            continue;
        }
        QTextDocument ch;
        QString title;
        if (!loadChapter(book, i, options, defaultFont, &ch, &title)) {
            r.skipped << book.at(i).path;
            continue;
        }
        ++r.chapters;
        // Pictures get book-wide names so two chapters' "image1.png" don't clash.
        QList<QPair<int, int>> ranges;
        QList<QTextImageFormat> formats;
        forEachImage(&ch, [&](const QTextFragment &f) {
            QTextImageFormat imf = f.charFormat().toImageFormat();
            const QString newName = QStringLiteral("zwbook-ch%1-%2").arg(i + 1).arg(formats.size() + 1);
            const QVariant v = ch.resource(QTextDocument::ImageResource, QUrl(imf.name()));
            if (v.isValid()) {
                doc->addResource(QTextDocument::ImageResource, QUrl(newName), v);
                imf.setName(newName);
            }
            ranges.append({f.position(), f.length()});
            formats.append(imf);
        });
        for (int k = 0; k < ranges.size(); ++k) {
            QTextCursor ic(&ch);
            ic.setPosition(ranges[k].first);
            ic.setPosition(ranges[k].first + ranges[k].second, QTextCursor::KeepAnchor);
            ic.setCharFormat(formats[k]);
        }

        QTextBlockFormat hb;
        hb.setHeadingLevel(1);
        if (!empty) {
            hb.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        }
        hb.setBottomMargin(18);
        hb.setProperty(ChapterStartProperty, i + 1);
        const QTextCharFormat hcf = headingChar(20);
        newBlock(hb, hcf);
        c.insertText(title, hcf);
        if (!ch.isEmpty()) {
            // The fragment's first paragraph merges into this block, so give
            // it that paragraph's format (a heading stays a heading).
            c.insertBlock(ch.begin().blockFormat(), ch.begin().charFormat());
            c.insertFragment(QTextDocumentFragment(&ch));
        }
    }

    if (tocPos < 0 && !options.pagesOnly) {
        PrintLayout::preparePages(doc, page);
        return r;
    }

    // ToC targets: chapter titles and the headings (levels 1-2) inside them.
    QList<TocLine> lines;
    bool inChapter = false;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat f = b.blockFormat();
        TocLine line;
        if (f.hasProperty(ChapterStartProperty)) {
            inChapter = true;
            line.level = 1;
            line.chapter = f.intProperty(ChapterStartProperty) - 1;
            line.text = QStringLiteral("%1. %2").arg(book.chapterNumber(line.chapter)).arg(b.text());
        } else if (inChapter && f.headingLevel() > 0 && f.headingLevel() <= 2 && !b.text().trimmed().isEmpty()) {
            line.level = 2;
            line.text = b.text().trimmed();
            line.chapter = lines.isEmpty() ? -1 : lines.last().chapter;
        } else {
            continue;
        }
        QTextBlockFormat mark;
        mark.setProperty(AnchorProperty, int(lines.size()) + 1);
        QTextCursor(b).mergeBlockFormat(mark);
        lines.append(line);
    }

    // Placeholder lines (one line each, so filling in numbers keeps the pages).
    const qreal width = PrintLayout::textWidth(page);
    if (tocPos >= 0) {
        c.setPosition(tocPos);
    }
    for (const TocLine &line : lines) {
        if (tocPos < 0) {
            break; // pages only: no ToC in this book
        }
        QTextBlockFormat bf;
        bf.setProperty(TocEntryProperty, line.level);
        bf.setLeftMargin(line.level > 1 ? 24 : 0);
        bf.setTopMargin(line.level == 1 ? 6 : 0);
        bf.setTabPositions({QTextOption::Tab(width - bf.leftMargin() - 1, QTextOption::RightTab)});
        c.insertBlock(bf, tocChar(defaultFont, line.level));
        c.insertText(QStringLiteral("0"), tocChar(defaultFont, line.level));
    }

    PrintLayout::preparePages(doc, page);
    QHash<int, int> pageOfAnchor;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().hasProperty(AnchorProperty)) {
            pageOfAnchor.insert(b.blockFormat().intProperty(AnchorProperty), PrintLayout::pageOf(doc, b.position()));
        }
    }

    if (tocPos < 0) {
        for (int n = 0; n < lines.size(); ++n) {
            lines[n].page = pageOfAnchor.value(n + 1);
        }
        r.toc = lines;
        return r;
    }
    // One edit block for all the numbers: the layout is redone once at the end,
    // not after every line (that took ~10 s for a 275-page book).
    QTextCursor batch(doc);
    batch.beginEditBlock();
    int k = 0;
    for (QTextBlock b = doc->begin(); b.isValid() && k < lines.size(); b = b.next()) {
        const QTextBlockFormat f = b.blockFormat();
        if (!f.hasProperty(TocEntryProperty) || f.intProperty(TocEntryProperty) == 0) {
            continue;
        }
        TocLine &line = lines[k];
        line.page = pageOfAnchor.value(k + 1);
        ++k;
        const QTextCharFormat cf = tocChar(defaultFont, line.level);
        QFont font = defaultFont;
        font.setBold(line.level == 1);
        const QFontMetricsF fm(font);
        const QString num = QString::number(line.page);
        const qreal avail = width - f.leftMargin() - 2;
        const QString title = fm.elidedText(line.text, Qt::ElideRight, avail * 0.8);
        const qreal dotW = fm.horizontalAdvance(QStringLiteral(". "));
        const qreal room = avail - fm.horizontalAdvance(title + QLatin1Char(' ')) - fm.horizontalAdvance(num)
                           - fm.horizontalAdvance(QStringLiteral("  "));
        const int dots = qMax(2, int(room / dotW));
        QTextCursor bc(b);
        bc.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        bc.insertText(title + QLatin1Char(' ') + QStringLiteral(". ").repeated(dots).trimmed() + QLatin1Char('\t') + num,
                      cf);
    }
    batch.endEditBlock();
    r.toc = lines;
    return r;
}

int paint(const QTextDocument &doc, QPrinter *printer, const Options &options)
{
    if (!printer) {
        return 0;
    }
    const bool savedFullPage = printer->fullPage();
    printer->setFullPage(true);
    const QPageLayout layout = printer->pageLayout();
    PrintLayout::Page page;
    page.sheetMm = PrintLayout::sheetSizeMm(layout);
    page.marginsMm = layout.margins(QPageLayout::Millimeter);
    QPainter painter(printer);
    int pages = 0;
    if (painter.isActive()) {
        const qreal bottomDev = page.marginsMm.bottom() * printer->logicalDpiY() / 25.4;
        pages = PrintLayout::paintDocument(
            doc, &painter, page, printer->logicalDpiX(), printer->logicalDpiY(),
            [printer]() { return printer->newPage(); },
            [&](QPainter *p, const QRectF &sheet, int no, int) {
                if (options.titlePage && no == 1) {
                    return; // no number on the title page
                }
                p->save();
                QFont f(doc.defaultFont(), printer);
                f.setPointSizeF(10);
                p->setFont(f);
                p->setPen(Qt::black);
                p->drawText(QRectF(0, sheet.height() - bottomDev, sheet.width(), bottomDev), Qt::AlignCenter,
                            QString::number(no));
                p->restore();
            });
        painter.end();
    }
    printer->setFullPage(savedFullPage);
    return pages;
}

bool writePdf(const BookManifest &book, const Options &options, const QPageLayout &layout,
              const QFont &defaultFont, const QString &path, int *pages, QString *error)
{
    PrintLayout::Page page;
    page.sheetMm = PrintLayout::sheetSizeMm(layout);
    page.marginsMm = layout.margins(QPageLayout::Millimeter);
    const Result r = build(book, options, page, defaultFont);
    if (r.chapters == 0) {
        if (error) {
            *error = QStringLiteral("No chapter of the book could be read.");
        }
        return false;
    }
    QPrinter pdf(QPrinter::HighResolution);
    pdf.setOutputFormat(QPrinter::PdfFormat);
    pdf.setOutputFileName(path);
    pdf.setPageLayout(layout);
    pdf.setDocName(bookTitle(book));
    const int n = paint(*r.doc, &pdf, options);
    if (pages) {
        *pages = n;
    }
    if (n == 0 || QFileInfo(path).size() == 0) {
        if (error) {
            *error = QStringLiteral("Could not write %1.").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- EPUB

namespace {

QString esc(const QString &s)
{
    QString o;
    o.reserve(s.size());
    for (const QChar ch : s) {
        switch (ch.unicode()) {
        case '&': o += QStringLiteral("&amp;"); break;
        case '<': o += QStringLiteral("&lt;"); break;
        case '>': o += QStringLiteral("&gt;"); break;
        case '"': o += QStringLiteral("&quot;"); break;
        case 0x2028: o += QStringLiteral("<br/>"); break;
        case 0xFFFC: break;
        case '\t': o += QLatin1Char(' '); break;
        default:
            if (ch.unicode() >= 0x20 || ch == QLatin1Char('\n')) {
                o += ch;
            }
        }
    }
    return o;
}

QString page(const QString &title, const QString &body, bool epubNs = false)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n"
                          "<html xmlns=\"http://www.w3.org/1999/xhtml\"%1 xml:lang=\"en\" lang=\"en\">\n"
                          "<head>\n<meta charset=\"utf-8\"/>\n<title>%2</title>\n"
                          "<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/>\n</head>\n"
                          "<body>\n%3</body>\n</html>\n")
        .arg(epubNs ? QStringLiteral(" xmlns:epub=\"http://www.idpf.org/2007/ops\"") : QString(), esc(title), body);
}

// Paragraph CSS -> class name, shared by all chapters (one stylesheet).
struct Css {
    StyleFonts::Map fonts = StyleFonts::builtinDefaults();
    QHash<QString, QString> classOf;
    QStringList rules;
    QString classFor(const QTextBlock &b)
    {
        QString css = ParagraphStyles::blockCss(b.blockFormat());
        const QString named = ParagraphStyles::styleOf(b.blockFormat());
        if (const ParagraphStyles::Style *s = named.isEmpty() ? nullptr : ParagraphStyles::find(named)) {
            if (s->italic) {
                css += QStringLiteral("font-style:italic;");
            }
            if (s->weight >= 600) {
                css += QStringLiteral("font-weight:bold;");
            }
            const QString styleCss = StyleFonts::css(fonts, s->name);
            if (!styleCss.isEmpty()) {
                css += styleCss; // Format > Styles… (1.15.0)
            } else if (s->pointSize > 0 && s->pointSize != 12) {
                css += QStringLiteral("font-size:%1em;").arg(QString::number(s->pointSize / 12.0, 'f', 2));
            }
        }
        if (css.isEmpty()) {
            return QString();
        }
        auto it = classOf.constFind(css);
        if (it != classOf.constEnd()) {
            return it.value();
        }
        const QString name = QStringLiteral("p%1").arg(classOf.size() + 1);
        classOf.insert(css, name);
        rules << QStringLiteral(".%1 { %2 }").arg(name, css);
        return name;
    }
};

struct EpubImage {
    QString file;
    QByteArray png;
};

QString inlineXhtml(const QTextDocument &d, const QTextBlock &b, int chapter, QList<EpubImage> *images)
{
    QString out;
    for (auto it = b.begin(); !it.atEnd(); ++it) {
        const QTextFragment f = it.fragment();
        if (!f.isValid()) {
            continue;
        }
        const QTextCharFormat cf = f.charFormat();
        if (cf.isImageFormat()) {
            const QImage img = imageOf(d.resource(QTextDocument::ImageResource, QUrl(cf.toImageFormat().name())));
            if (img.isNull()) {
                continue;
            }
            EpubImage ei;
            ei.file = QStringLiteral("images/c%1-%2.png").arg(chapter).arg(images->size() + 1);
            QBuffer buf(&ei.png);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
            images->append(ei);
            out += QStringLiteral("<img src=\"%1\" alt=\"\"/>").arg(ei.file);
            continue;
        }
        QStringList open, close;
        const QString href = cf.anchorHref();
        if (cf.isAnchor() && (href.startsWith(QLatin1String("http://")) || href.startsWith(QLatin1String("https://")))) {
            open << QStringLiteral("<a href=\"%1\">").arg(esc(href));
            close.prepend(QStringLiteral("</a>"));
        }
        if (cf.fontWeight() >= QFont::DemiBold) {
            open << QStringLiteral("<b>");
            close.prepend(QStringLiteral("</b>"));
        }
        if (cf.fontItalic()) {
            open << QStringLiteral("<i>");
            close.prepend(QStringLiteral("</i>"));
        }
        if (cf.fontUnderline() && !cf.isAnchor()) {
            open << QStringLiteral("<u>");
            close.prepend(QStringLiteral("</u>"));
        }
        if (cf.fontStrikeOut()) {
            open << QStringLiteral("<s>");
            close.prepend(QStringLiteral("</s>"));
        }
        if (cf.verticalAlignment() == QTextCharFormat::AlignSuperScript) {
            open << QStringLiteral("<sup>");
            close.prepend(QStringLiteral("</sup>"));
        } else if (cf.verticalAlignment() == QTextCharFormat::AlignSubScript) {
            open << QStringLiteral("<sub>");
            close.prepend(QStringLiteral("</sub>"));
        }
        out += open.join(QString()) + esc(f.text()) + close.join(QString());
    }
    return out;
}

bool isOrdered(QTextListFormat::Style s)
{
    return s == QTextListFormat::ListDecimal || s == QTextListFormat::ListLowerAlpha
           || s == QTextListFormat::ListUpperAlpha || s == QTextListFormat::ListLowerRoman
           || s == QTextListFormat::ListUpperRoman;
}

struct NavHeading {
    QString id, text;
};

// The outermost table a block is in (nullptr outside tables).
QTextTable *outerTable(const QTextBlock &b)
{
    QTextTable *found = nullptr;
    for (QTextFrame *f = QTextCursor(b).currentFrame(); f; f = f->parentFrame()) {
        if (auto *t = qobject_cast<QTextTable *>(f)) {
            found = t;
        }
    }
    return found;
}

} // namespace

// A table as XHTML (1.16.0): <colgroup> with the column widths in percent, the
// header rows (Table > Repeat Header Row) as <thead> with <th> cells, merged
// cells as colspan/rowspan. Each paragraph of a cell is a <p>; a table nested
// in a cell comes out as its paragraphs.
QString tableXhtml(const QTextTable *t, const std::function<QString(const QTextBlock &)> &paragraph)
{
    QString out = QStringLiteral("<table>\n");
    const QList<QTextLength> widths = t->format().columnWidthConstraints();
    bool percent = widths.size() == t->columns();
    for (const QTextLength &l : widths) {
        percent = percent && l.type() == QTextLength::PercentageLength;
    }
    if (percent) {
        out += QStringLiteral("<colgroup>");
        for (const QTextLength &l : widths) {
            out += QStringLiteral("<col style=\"width:%1%\"/>").arg(QString::number(l.rawValue(), 'f', 2));
        }
        out += QStringLiteral("</colgroup>\n");
    }
    const int header = qBound(0, t->format().headerRowCount(), t->rows());
    for (int r = 0; r < t->rows(); ++r) {
        if (r == 0 && header > 0) {
            out += QStringLiteral("<thead>\n");
        }
        if (r == header) {
            out += QStringLiteral("<tbody>\n");
        }
        out += QStringLiteral("<tr>");
        const QString tag = r < header ? QStringLiteral("th") : QStringLiteral("td");
        for (int c = 0; c < t->columns(); ++c) {
            const QTextTableCell cell = t->cellAt(r, c);
            if (!cell.isValid() || cell.row() != r || cell.column() != c) {
                continue; // covered by a merged cell
            }
            out += QLatin1Char('<') + tag;
            if (cell.columnSpan() > 1) {
                out += QStringLiteral(" colspan=\"%1\"").arg(cell.columnSpan());
            }
            if (cell.rowSpan() > 1) {
                out += QStringLiteral(" rowspan=\"%1\"").arg(cell.rowSpan());
            }
            out += QLatin1Char('>');
            const QTextBlock last = cell.lastCursorPosition().block();
            for (QTextBlock b = cell.firstCursorPosition().block(); b.isValid(); b = b.next()) {
                out += paragraph(b);
                if (b == last) {
                    break;
                }
            }
            out += QStringLiteral("</") + tag + QLatin1Char('>');
        }
        out += QStringLiteral("</tr>\n");
        if (r == header - 1) {
            out += QStringLiteral("</thead>\n");
        }
    }
    if (t->rows() > header) {
        out += QStringLiteral("</tbody>\n");
    }
    out += QStringLiteral("</table>\n");
    return out;
}

namespace {

QString chapterXhtml(const QTextDocument &d, const QString &title, int chapter, Css *css,
                     QList<EpubImage> *images, QList<NavHeading> *headings)
{
    QString body = QStringLiteral("<section epub:type=\"chapter\" role=\"doc-chapter\">\n<h1>%1</h1>\n").arg(esc(title));
    QTextList *list = nullptr;
    auto closeList = [&]() {
        if (list) {
            body += isOrdered(list->format().style()) ? QStringLiteral("</ol>\n") : QStringLiteral("</ul>\n");
            list = nullptr;
        }
    };
    for (QTextBlock b = d.begin(); b.isValid(); b = b.next()) {
        if (QTextTable *t = outerTable(b)) {
            closeList();
            body += tableXhtml(t, [&](const QTextBlock &cb) {
                const QString pc = css->classFor(cb);
                const QString content = inlineXhtml(d, cb, chapter, images);
                return pc.isEmpty() ? QStringLiteral("<p>%1</p>").arg(content)
                                    : QStringLiteral("<p class=\"%1\">%2</p>").arg(pc, content);
            });
            b = d.findBlock(t->lastPosition());
            continue;
        }
        const QString content = inlineXhtml(d, b, chapter, images);
        QTextList *bl = b.textList();
        if (bl != list) {
            closeList();
            if (bl) {
                list = bl;
                body += isOrdered(bl->format().style()) ? QStringLiteral("<ol>\n") : QStringLiteral("<ul>\n");
            }
        }
        if (list) {
            body += QStringLiteral("<li>%1</li>\n").arg(content);
            continue;
        }
        const int level = b.blockFormat().headingLevel();
        if (level > 0) {
            const QString id = QStringLiteral("s%1").arg(headings->size() + 1);
            if (level <= 2 && !b.text().trimmed().isEmpty()) {
                headings->append({id, b.text().trimmed()});
            }
            const int h = qMin(6, level + 1);
            body += QStringLiteral("<h%1 id=\"%2\">%3</h%1>\n").arg(h).arg(id, content);
            if (level > 2 || b.text().trimmed().isEmpty()) {
                headings->append({QString(), QString()}); // keep ids unique
            }
            continue;
        }
        QString cls = Captions::isCaption(b) ? QStringLiteral("caption") : QString();
        const QString pc = css->classFor(b);
        if (!pc.isEmpty()) {
            cls = cls.isEmpty() ? pc : cls + QLatin1Char(' ') + pc;
        }
        body += cls.isEmpty() ? QStringLiteral("<p>%1</p>\n").arg(content)
                              : QStringLiteral("<p class=\"%1\">%2</p>\n").arg(cls, content);
    }
    closeList();
    body += QStringLiteral("</section>\n");
    return page(title, body, true);
}

bool writeFile(const QString &root, const QString &rel, const QByteArray &bytes)
{
    const QString path = root + QLatin1Char('/') + rel;
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

bool runZip(const QString &dir, const QStringList &args)
{
    QProcess zip;
    zip.setWorkingDirectory(dir);
    zip.start(QStringLiteral("zip"), args);
    return zip.waitForFinished(60000) && zip.exitStatus() == QProcess::NormalExit && zip.exitCode() == 0;
}

} // namespace

QString epubStyleFontCss(const StyleFonts::Map &fonts)
{
    // Body Text -> body; Heading 1 -> the chapter title (h1) and h2, Heading 2
    // -> h3, Heading 3 -> h4 (chapter headings sit one level below the title).
    QString out;
    const struct { const char *key; const char *sel; } rules[] = {
        {"Body", "body"}, {"Heading1", "h1, h2"}, {"Heading2", "h3"}, {"Heading3", "h4, h5, h6"}};
    for (const auto &r : rules) {
        const QString c = StyleFonts::css(fonts, QString::fromLatin1(r.key));
        if (!c.isEmpty()) {
            out += QStringLiteral("%1 { %2 }\n").arg(QString::fromLatin1(r.sel), c);
        }
    }
    return out.isEmpty() ? out : QStringLiteral("/* Style fonts (Format > Styles…) */\n") + out;
}

bool writeEpub(const BookManifest &book, const Options &options, const QString &path, QString *error,
               QStringList *skipped)
{
    auto fail = [error](const QString &m) {
        if (error) {
            *error = m;
        }
        return false;
    };
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        return fail(QStringLiteral("No temporary folder for the EPUB."));
    }
    const QString root = tmp.path();
    const QString title = bookTitle(book);
    Css css;
    css.fonts = options.styleFonts;
    QStringList manifest, spine, nav;
    QList<EpubImage> images;

    if (options.titlePage) {
        writeFile(root, QStringLiteral("OEBPS/title.xhtml"),
                  page(title, QStringLiteral("<section epub:type=\"titlepage\" class=\"titlepage\">\n"
                                             "<h1 class=\"title\">%1</h1>\n</section>\n")
                                  .arg(esc(title)),
                       true)
                      .toUtf8());
        manifest << QStringLiteral("<item id=\"title\" href=\"title.xhtml\" media-type=\"application/xhtml+xml\"/>");
        spine << QStringLiteral("<itemref idref=\"title\"/>");
    }
    if (options.toc) {
        spine << QStringLiteral("<itemref idref=\"nav\"/>");
    }
    int written = 0;
    for (int i = 0; i < book.count(); ++i) {
        if (book.isContents(i)) {
            continue; // the EPUB's own navigation is the table of contents
        }
        QTextDocument ch;
        QString ct;
        if (!loadChapter(book, i, options, QFont(), &ch, &ct)) {
            if (skipped) {
                *skipped << book.at(i).path;
            }
            continue;
        }
        ++written;
        const QString file = QStringLiteral("ch%1.xhtml").arg(i + 1, 2, 10, QLatin1Char('0'));
        QList<NavHeading> heads;
        writeFile(root, QStringLiteral("OEBPS/") + file, chapterXhtml(ch, ct, book.chapterNumber(i), &css, &images, &heads).toUtf8());
        const QString id = QStringLiteral("ch%1").arg(i + 1);
        manifest << QStringLiteral("<item id=\"%1\" href=\"%2\" media-type=\"application/xhtml+xml\"/>").arg(id, file);
        spine << QStringLiteral("<itemref idref=\"%1\"/>").arg(id);
        QString li = QStringLiteral("<li><a href=\"%1\">%2. %3</a>").arg(file).arg(book.chapterNumber(i)).arg(esc(ct));
        QStringList sub;
        for (const NavHeading &h : heads) {
            if (!h.id.isEmpty()) {
                sub << QStringLiteral("<li><a href=\"%1#%2\">%3</a></li>").arg(file, h.id, esc(h.text));
            }
        }
        if (!sub.isEmpty()) {
            li += QStringLiteral("\n<ol>\n") + sub.join(QLatin1Char('\n')) + QStringLiteral("\n</ol>\n");
        }
        nav << li + QStringLiteral("</li>");
    }
    if (written == 0) {
        return fail(QStringLiteral("No chapter of the book could be read."));
    }
    for (const EpubImage &im : images) {
        writeFile(root, QStringLiteral("OEBPS/") + im.file, im.png);
        const QString id = QFileInfo(im.file).completeBaseName();
        manifest << QStringLiteral("<item id=\"img-%1\" href=\"%2\" media-type=\"image/png\"/>").arg(id, im.file);
    }

    writeFile(root, QStringLiteral("OEBPS/nav.xhtml"),
              page(QStringLiteral("Contents"),
                   QStringLiteral("<nav epub:type=\"toc\" role=\"doc-toc\" id=\"toc\">\n<h1>Contents</h1>\n<ol>\n%1\n</ol>\n</nav>\n")
                       .arg(nav.join(QLatin1Char('\n'))),
                   true)
                  .toUtf8());
    manifest.prepend(QStringLiteral("<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"));
    manifest.prepend(QStringLiteral("<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>"));

    const QString baseCss = QStringLiteral(
        "body { font-family: serif; line-height: 1.4; }\n"
        "h1 { font-size: 1.6em; margin: 1em 0 0.8em 0; }\n"
        "h2 { font-size: 1.3em; } h3 { font-size: 1.15em; } h4, h5, h6 { font-size: 1em; }\n"
        "p { margin: 0 0 0.5em 0; }\n"
        "img { max-width: 100%; }\n"
        "table { border-collapse: collapse; width: 100%; margin: 0 0 0.8em 0; }\n"
        "td, th { border: 1px solid #b8b8b8; padding: 0.25em 0.4em; vertical-align: top; }\n"
        "th { font-weight: bold; text-align: left; }\n"
        "td p, th p { margin: 0; }\n"
        ".caption { text-align: center; font-style: italic; }\n"
        ".titlepage { text-align: center; margin-top: 30%; }\n"
        ".title { font-size: 2.2em; }\n"
        "nav ol { list-style: none; padding-left: 1em; }\n"
        "/* Paragraph styles and indents from the chapters */\n");
    writeFile(root, QStringLiteral("OEBPS/style.css"),
              (baseCss + epubStyleFontCss(options.styleFonts) + css.rules.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());

    const QString uuid = QUuid::createUuidV5(QUuid(QStringLiteral("{6ba7b811-9dad-11d1-80b4-00c04fd430c8}")),
                                             QDir(book.bookDir()).absolutePath())
                             .toString(QUuid::WithoutBraces);
    const QString opf = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"bookid\" xml:lang=\"en\">\n"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
        "<dc:identifier id=\"bookid\">urn:uuid:%1</dc:identifier>\n"
        "<dc:title>%2</dc:title>\n<dc:language>en</dc:language>\n"
        "<meta property=\"dcterms:modified\">%3</meta>\n"
        "</metadata>\n<manifest>\n%4\n</manifest>\n<spine>\n%5\n</spine>\n</package>\n")
                            .arg(uuid, esc(title),
                                 QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ")),
                                 manifest.join(QLatin1Char('\n')), spine.join(QLatin1Char('\n')));
    writeFile(root, QStringLiteral("OEBPS/content.opf"), opf.toUtf8());
    writeFile(root, QStringLiteral("META-INF/container.xml"),
              QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
                                "<rootfiles>\n<rootfile full-path=\"OEBPS/content.opf\" "
                                "media-type=\"application/oebps-package+xml\"/>\n</rootfiles>\n</container>\n"));
    writeFile(root, QStringLiteral("mimetype"), QByteArrayLiteral("application/epub+zip"));

    const QString out = QFileInfo(path).absoluteFilePath();
    QFile::remove(out);
    // mimetype first and stored (not compressed), as EPUB requires.
    if (!runZip(root, {QStringLiteral("-X0q"), out, QStringLiteral("mimetype")})
        || !runZip(root, {QStringLiteral("-Xr9Dq"), out, QStringLiteral("META-INF"), QStringLiteral("OEBPS")})) {
        return fail(QStringLiteral("Could not write the EPUB (is zip available?)."));
    }
    return true;
}

} // namespace BookExport
