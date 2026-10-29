// Subscript / superscript (1.13.0): H₂O and x² survive ODT and RTF save and
// reopen, stay in the book layout that PDF export paints, and come out of
// EPUB export as <sub>/<sup>.
// Run: QT_QPA_PLATFORM=offscreen ctest -R script --output-on-failure
#include "BookExport.hpp"
#include "BookManifest.hpp"
#include "DocumentIo.hpp"
#include "PrintLayout.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPageLayout>
#include <QPageSize>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <cstdio>

namespace {
int g_failures = 0;
void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", what, detail.isEmpty() ? "" : " — ", qPrintable(detail));
    if (!ok) {
        ++g_failures;
    }
}

void fill(QTextDocument *d)
{
    QTextCursor c(d);
    QTextCharFormat normal;
    QTextCharFormat sub;
    sub.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    QTextCharFormat sup;
    sup.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    c.insertText(QStringLiteral("Water is H"), normal);
    c.insertText(QStringLiteral("2"), sub);
    c.insertText(QStringLiteral("O and the area is x"), normal);
    c.insertText(QStringLiteral("2"), sup);
    c.insertText(QStringLiteral(" m."), normal);
}

// "H2O and x2": the vertical alignment of every character, as "n" / "b" (sub) / "p" (super).
QString marks(const QTextDocument &d)
{
    QString out;
    for (QTextBlock b = d.begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            const auto va = f.charFormat().verticalAlignment();
            const QChar m = va == QTextCharFormat::AlignSubScript ? QLatin1Char('b')
                : va == QTextCharFormat::AlignSuperScript         ? QLatin1Char('p')
                                                                  : QLatin1Char('n');
            out += QString(f.text().size(), m);
        }
    }
    return out;
}

QString expected()
{
    QTextDocument d;
    fill(&d);
    return marks(d);
}

PrintLayout::Page pageOf(const QPageLayout &l)
{
    PrintLayout::Page p;
    p.sheetMm = PrintLayout::sheetSizeMm(l);
    p.marginsMm = l.margins(QPageLayout::Millimeter);
    return p;
}

QString member(const QString &zip, const QString &name)
{
    QProcess p;
    p.start(QStringLiteral("unzip"), {QStringLiteral("-p"), zip, name});
    p.waitForFinished(10000);
    return QString::fromUtf8(p.readAllStandardOutput());
}

void roundTrip(const QString &dir, DocumentIo::Format fmt, const char *ext, const char *what)
{
    QTextDocument d;
    fill(&d);
    const QString path = dir + QStringLiteral("/script.") + QLatin1String(ext);
    QString err;
    check(DocumentIo::save(&d, path, fmt, &err), what, err);
    QTextDocument back;
    check(DocumentIo::load(&back, path, &err), "  reopened", err);
    check(back.toPlainText().contains(QStringLiteral("Water is H2O and the area is x2 m.")), "  text kept",
          back.toPlainText());
    check(marks(back) == expected(), "  sub/super kept", marks(back));
    // And once more: open, save, open.
    const QString again = dir + QStringLiteral("/script-again.") + QLatin1String(ext);
    check(DocumentIo::save(&back, again, fmt, &err), "  saved again", err);
    QTextDocument twice;
    check(DocumentIo::load(&twice, again, &err) && marks(twice) == expected(), "  second round trip", marks(twice));
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    roundTrip(tmp.path(), DocumentIo::Format::Odt, "odt", "ODT saved");
    roundTrip(tmp.path(), DocumentIo::Format::Rtf, "rtf", "RTF saved");

    // A one-chapter book for PDF / EPUB export.
    const QString dir = tmp.path() + QStringLiteral("/Chem");
    QDir().mkpath(dir);
    {
        QTextDocument d;
        fill(&d);
        QString err;
        check(DocumentIo::save(&d, dir + QStringLiteral("/01 Water.odt"), DocumentIo::Format::Odt, &err),
              "book chapter saved", err);
    }
    BookManifest book = BookManifest::fromFolder(dir);
    book.setTitle(QStringLiteral("Chem"));
    const QPageLayout layout(QPageSize(QPageSize::Letter), QPageLayout::Portrait, QMarginsF(25, 25, 25, 25),
                             QPageLayout::Millimeter);
    const QFont font(QStringLiteral("Sans Serif"), 12);
    BookExport::Options opt;
    opt.toc = opt.titlePage = false;

    // PDF export paints this document; its characters keep their vertical alignment.
    const BookExport::Result r = BookExport::build(book, opt, pageOf(layout), font);
    check(r.doc && marks(*r.doc).contains(QStringLiteral("nbnnnnnnnnnnnnnnnnnnnpnn")),
          "PDF layout keeps sub/super", r.doc ? marks(*r.doc) : QString());
    const QString pdf = tmp.path() + QStringLiteral("/chem.pdf");
    QString err;
    check(BookExport::writePdf(book, opt, layout, font, pdf, nullptr, &err), "PDF written", err);
    const QString epub = tmp.path() + QStringLiteral("/chem.epub");
    check(BookExport::writeEpub(book, opt, epub, &err), "EPUB written", err);
    const QString ch = member(epub, QStringLiteral("OEBPS/ch01.xhtml"));
    check(ch.contains(QLatin1String("H<sub>2</sub>O")), "EPUB has H<sub>2</sub>O", ch.right(400));
    check(ch.contains(QLatin1String("x<sup>2</sup>")), "EPUB has x<sup>2</sup>", ch.right(400));

    // "Defined as" symbols (1.13.0) in a real PDF and EPUB.
    {
        const QString ddir = tmp.path() + QStringLiteral("/Defs");
        QDir().mkpath(ddir);
        QTextDocument d;
        QTextCursor c(&d);
        c.insertText(QStringLiteral("Defs: a ≡ b, f ≜ g, x ≔ 1."));
        check(DocumentIo::save(&d, ddir + QStringLiteral("/01 Defs.odt"), DocumentIo::Format::Odt, &err), "defs chapter saved", err);
        BookManifest defs = BookManifest::fromFolder(ddir);
        defs.setTitle(QStringLiteral("Defs"));
        const QString dpdf = tmp.path() + QStringLiteral("/defs.pdf");
        check(BookExport::writePdf(defs, opt, layout, font, dpdf, nullptr, &err), "defs PDF written", err);
        QProcess pt;
        pt.start(QStringLiteral("pdftotext"), {dpdf, QStringLiteral("-")});
        if (pt.waitForStarted(3000) && pt.waitForFinished(10000) && pt.exitCode() == 0) {
            const QString txt = QString::fromUtf8(pt.readAllStandardOutput());
            check(txt.contains(QStringLiteral("≡")) && txt.contains(QStringLiteral("≜")) && txt.contains(QStringLiteral("≔")),
                  "PDF text has ≡ ≜ ≔ (pdftotext)", txt.simplified());
        } else {
            std::printf("skip  pdftotext not installed\n");
        }
        if (!qEnvironmentVariableIsEmpty("ZW_PDF_OUT")) {
            QFile::remove(qEnvironmentVariable("ZW_PDF_OUT"));
            QFile::copy(dpdf, qEnvironmentVariable("ZW_PDF_OUT"));
        }
        const QString depub = tmp.path() + QStringLiteral("/defs.epub");
        check(BookExport::writeEpub(defs, opt, depub, &err), "defs EPUB written", err);
        const QString x = member(depub, QStringLiteral("OEBPS/ch01.xhtml"));
        check(x.contains(QStringLiteral("a ≡ b, f ≜ g, x ≔ 1.")), "EPUB XHTML has ≡ ≜ ≔", x.right(200));
    }

    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
