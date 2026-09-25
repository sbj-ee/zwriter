// Paragraph alignment (ctest, headless):
//   * ODT round-trip of left / center / right / justify, including a
//     LibreOffice-style file (start/end, left/right, inherited and named styles)
//   * RTF export (\ql \qc \qr \qj) and import, including a Word-style file
//   * a multi-paragraph change is one undo step; a no-op changes nothing
//   * the checked toolbar/menu action follows the cursor, selection and undo
//   * print/PDF painting (PrintLayout) and pagination honour alignment, and
//     Justify leaves the last line of a paragraph ragged
// Needs `unzip` and `zip` on PATH. Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "Alignment.hpp"
#include "AlignmentActions.hpp"
#include "DocumentIo.hpp"
#include "PrintLayout.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>

#include <cstdio>

using Alignment::Kind;

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

const char *kindName(Kind k)
{
    switch (k) {
    case Kind::Left: return "left";
    case Kind::Center: return "center";
    case Kind::Right: return "right";
    case Kind::Justify: return "justify";
    }
    return "?";
}

QString kindsOf(const QTextDocument &doc)
{
    QStringList out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        out << QString::fromLatin1(kindName(Alignment::kindOf(b)));
    }
    return out.join(QLatin1Char(','));
}

QString textsOf(const QTextDocument &doc)
{
    QStringList out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        out << b.text();
    }
    return out.join(QLatin1Char('|'));
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

// Four paragraphs: Left, Center, Right, Justify (in that order).
void fillFour(QTextDocument *doc)
{
    doc->setPlainText(QStringLiteral("Left paragraph\nCentered title\nRight paragraph\nJustified paragraph"));
    const Kind kinds[] = {Kind::Left, Kind::Center, Kind::Right, Kind::Justify};
    int i = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next(), ++i) {
        Alignment::apply(QTextCursor(b), kinds[i]);
    }
}

const QString kFour = QStringLiteral("left,center,right,justify");

// ---- ODT -----------------------------------------------------------------
void testOdtRoundTrip(const QString &dir)
{
    QTextDocument doc;
    fillFour(&doc);
    // Mark the left paragraph explicitly too (it could also have no alignment).
    check(kindsOf(doc) == kFour, "setup: four alignments", kindsOf(doc));
    const QString path = dir + QStringLiteral("/align.odt");
    QString err;
    check(DocumentIo::save(&doc, path, DocumentIo::Format::Odt, &err), "ODT save", err);
    const QString content = QString::fromUtf8(unzipMember(path, QStringLiteral("content.xml")));
    check(content.contains(QLatin1String("fo:text-align=\"center\"")), "ODT writes fo:text-align=center");
    check(content.contains(QLatin1String("fo:text-align=\"justify\"")), "ODT writes fo:text-align=justify");
    check(content.contains(QLatin1String("fo:text-align=\"right\""))
              || content.contains(QLatin1String("fo:text-align=\"end\"")),
          "ODT writes fo:text-align=right/end");
    QTextDocument back;
    check(DocumentIo::load(&back, path, &err), "ODT reload", err);
    check(kindsOf(back) == kFour, "ODT round-trip keeps left/center/right/justify", kindsOf(back));
    check(textsOf(back) == textsOf(doc), "ODT round-trip keeps the text", textsOf(back));

    // Second cycle: stable.
    const QString path2 = dir + QStringLiteral("/align2.odt");
    check(DocumentIo::save(&back, path2, DocumentIo::Format::Odt, &err), "ODT save again", err);
    QTextDocument again;
    DocumentIo::load(&again, path2, &err);
    check(kindsOf(again) == kFour, "ODT second round-trip stable", kindsOf(again));
}

// A file shaped like LibreOffice's: automatic styles in content.xml with
// parents, named styles in styles.xml, start/end values.
void testOdtFromLibreOffice(const QString &dir)
{
    const QString pkg = dir + QStringLiteral("/lo");
    QDir().mkpath(pkg + QStringLiteral("/META-INF"));
    writeFile(pkg + QStringLiteral("/mimetype"), "application/vnd.oasis.opendocument.text");
    writeFile(pkg + QStringLiteral("/META-INF/manifest.xml"),
              R"(<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.3">
 <manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>
 <manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
 <manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
</manifest:manifest>
)");
    const char *ns = R"( xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3")";
    writeFile(pkg + QStringLiteral("/styles.xml"),
              QByteArray(R"(<?xml version="1.0" encoding="UTF-8"?><office:document-styles)") + ns + R"(>
<office:styles>
 <style:style style:name="Standard" style:family="paragraph"/>
 <style:style style:name="Title" style:family="paragraph" style:parent-style-name="Heading">
  <style:paragraph-properties fo:text-align="center"/>
 </style:style>
 <style:style style:name="Heading" style:family="paragraph" style:parent-style-name="Standard"/>
 <style:style style:name="Text_20_body" style:display-name="Text body" style:family="paragraph" style:parent-style-name="Standard">
  <style:paragraph-properties fo:text-align="justify"/>
 </style:style>
</office:styles>
<office:automatic-styles>
 <style:style style:name="P1" style:family="paragraph"><style:paragraph-properties fo:text-align="end"/></style:style>
</office:automatic-styles>
</office:document-styles>
)");
    writeFile(pkg + QStringLiteral("/content.xml"),
              QByteArray(R"(<?xml version="1.0" encoding="UTF-8"?><office:document-content)") + ns + R"(>
<office:automatic-styles>
 <style:style style:name="P1" style:family="paragraph" style:parent-style-name="Standard"><style:paragraph-properties fo:text-align="center"/></style:style>
 <style:style style:name="P2" style:family="paragraph" style:parent-style-name="Standard"><style:paragraph-properties fo:text-align="end"/></style:style>
 <style:style style:name="P3" style:family="paragraph" style:parent-style-name="Standard"><style:paragraph-properties fo:text-align="justify"/></style:style>
 <style:style style:name="P4" style:family="paragraph" style:parent-style-name="Title"><style:text-properties fo:font-weight="bold"/></style:style>
 <style:style style:name="P5" style:family="paragraph" style:parent-style-name="Title"><style:paragraph-properties fo:text-align="start"/></style:style>
 <style:style style:name="P6" style:family="paragraph" style:parent-style-name="Standard"><style:paragraph-properties fo:text-align="right"/></style:style>
 <style:style style:name="P7" style:family="paragraph" style:parent-style-name="Text_20_body"><style:paragraph-properties fo:text-align="left"/></style:style>
</office:automatic-styles>
<office:body><office:text>
 <text:p text:style-name="Standard">Plain</text:p>
 <text:p text:style-name="P1">Center</text:p>
 <text:p text:style-name="P2">End</text:p>
 <text:p text:style-name="P3">Justify</text:p>
 <text:p text:style-name="P4">Inherits Title</text:p>
 <text:p text:style-name="P5">Start overrides Title</text:p>
 <text:p text:style-name="P6">Right</text:p>
 <text:p text:style-name="P7">Left overrides Text body</text:p>
 <text:p text:style-name="Text_20_body">Named Text body</text:p>
 <text:p text:style-name="Title">Named Title</text:p>
 <text:h text:style-name="P1" text:outline-level="1">Centered heading</text:h>
</office:text></office:body></office:document-content>
)");
    const QString odt = dir + QStringLiteral("/lo.odt");
    QProcess zip;
    zip.setWorkingDirectory(pkg);
    zip.start(QStringLiteral("zip"), {QStringLiteral("-X"), QStringLiteral("-q"), QStringLiteral("-0"), odt,
                                      QStringLiteral("mimetype")});
    zip.waitForFinished(15000);
    zip.start(QStringLiteral("zip"), {QStringLiteral("-X"), QStringLiteral("-q"), QStringLiteral("-r"), odt,
                                      QStringLiteral("META-INF"), QStringLiteral("content.xml"),
                                      QStringLiteral("styles.xml")});
    zip.waitForFinished(15000);
    QTextDocument doc;
    QString err;
    check(DocumentIo::load(&doc, odt, &err), "LibreOffice-style ODT loads", err);
    const QString want = QStringLiteral(
        "left,center,right,justify,center,left,right,left,justify,center,center");
    check(kindsOf(doc) == want, "LibreOffice ODT: start/end/left/right/center/justify, inherited and named styles",
          kindsOf(doc));
    check(doc.lastBlock().blockFormat().headingLevel() == 1, "LibreOffice ODT: centered text:h stays a heading");
}

// ---- RTF -----------------------------------------------------------------
void testRtf(const QString &dir)
{
    QTextDocument doc;
    fillFour(&doc);
    const QString path = dir + QStringLiteral("/align.rtf");
    QString err;
    check(DocumentIo::save(&doc, path, DocumentIo::Format::Rtf, &err), "RTF save", err);
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QString rtf = QString::fromLatin1(f.readAll());
    check(rtf.contains(QLatin1String("\\ql")) && rtf.contains(QLatin1String("\\qc"))
              && rtf.contains(QLatin1String("\\qr")) && rtf.contains(QLatin1String("\\qj")),
          "RTF export writes \\ql \\qc \\qr \\qj");
    QTextDocument back;
    check(DocumentIo::load(&back, path, &err), "RTF reload", err);
    check(kindsOf(back) == kFour, "RTF round-trip keeps left/center/right/justify", kindsOf(back));
    check(textsOf(back) == textsOf(doc), "RTF round-trip keeps the text", textsOf(back));

    // Word/TextEdit style: properties after \pard, reset by the next \pard,
    // a group-scoped paragraph, \qd.
    const QString word = dir + QStringLiteral("/word.rtf");
    writeFile(word, "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times;}}\n"
                    "\\pard\\qc\\f0\\fs24 Centered\\par\n"
                    "\\pard\\plain Plain after pard\\par\n"
                    "\\pard\\qr Right one\\par\n"
                    "Right two (same paragraph properties)\\par\n"
                    "\\pard\\qj Justified\\par\n"
                    "\\pard\\qd Distributed\\par\n"
                    "\\pard\\ql Left\\par\n"
                    "}\n");
    QTextDocument w;
    check(DocumentIo::load(&w, word, &err), "Word-style RTF loads", err);
    check(kindsOf(w) == QStringLiteral("center,left,right,right,justify,justify,left"),
          "Word-style RTF import reads \\qc \\qr \\qj \\qd \\ql and \\pard", kindsOf(w));
}

// ---- Undo ----------------------------------------------------------------
void testUndo()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("one\ntwo\nthree\nfour"));
    doc.clearUndoRedoStacks();
    doc.setModified(false);
    const int steps0 = doc.availableUndoSteps();

    // Select from the middle of "two" to the middle of "three".
    QTextCursor sel(&doc);
    sel.setPosition(doc.findBlockByNumber(1).position() + 1);
    sel.setPosition(doc.findBlockByNumber(2).position() + 2, QTextCursor::KeepAnchor);
    check(Alignment::touchedBlocks(sel).size() == 2, "selection touches two paragraphs");
    check(Alignment::apply(sel, Kind::Center), "multi-paragraph Center applies");
    check(kindsOf(doc) == QStringLiteral("left,center,center,left"), "only the touched paragraphs change",
          kindsOf(doc));
    check(doc.isModified(), "a real change marks the document modified");
    doc.undo();
    check(kindsOf(doc) == QStringLiteral("left,left,left,left"), "one undo restores every paragraph",
          kindsOf(doc));
    // (availableUndoSteps() counts Qt's internal commands, not edit blocks.)
    check(!doc.isUndoAvailable() && doc.availableUndoSteps() == steps0,
          "multi-paragraph Center was exactly one undo step");
    doc.redo();
    check(kindsOf(doc) == QStringLiteral("left,center,center,left"), "one redo re-applies it", kindsOf(doc));
    doc.undo();

    // Whole document to Justify, then undo in one step.
    QTextCursor all(&doc);
    all.select(QTextCursor::Document);
    Alignment::apply(all, Kind::Justify);
    check(kindsOf(doc) == QStringLiteral("justify,justify,justify,justify"), "select-all Justify");
    doc.undo();
    check(kindsOf(doc) == QStringLiteral("left,left,left,left"), "select-all Justify undoes in one step");

    // No-op: already aligned -> no undo step, not modified.
    doc.clearUndoRedoStacks();
    doc.setModified(false);
    QTextCursor caret(doc.findBlockByNumber(0));
    check(!Alignment::apply(caret, Kind::Left), "Left on a left paragraph is a no-op");
    check(!doc.isModified() && !doc.isUndoAvailable(), "no-op: not modified, no undo step");
    Alignment::apply(caret, Kind::Right);
    doc.clearUndoRedoStacks();
    doc.setModified(false);
    int changes = 0;
    QObject::connect(&doc, &QTextDocument::contentsChanged, [&changes]() { ++changes; });
    check(!Alignment::apply(caret, Kind::Right), "Right on a right paragraph is a no-op");
    check(!doc.isModified() && !doc.isUndoAvailable() && changes == 0,
          "no-op: no contentsChanged (the editor would mark it dirty)");
    // Mixed selection: only differing paragraphs change, still one step.
    QTextCursor mixed(&doc);
    mixed.select(QTextCursor::Document);
    check(Alignment::apply(mixed, Kind::Right), "mixed selection applies");
    check(kindsOf(doc) == QStringLiteral("right,right,right,right"), "mixed selection: all right", kindsOf(doc));
    doc.undo();
    check(!doc.isUndoAvailable() && kindsOf(doc) == QStringLiteral("right,left,left,left"),
          "mixed selection: one undo step", kindsOf(doc));
}

// ---- Checked state -------------------------------------------------------
void testCheckedState()
{
    QTextEdit edit;
    edit.setPlainText(QStringLiteral("Title\nBody text\nMore"));
    Alignment::Actions actions(&edit);
    auto checked = [&]() -> QString {
        for (QAction *a : actions.actions()) {
            if (a->isChecked()) {
                return QString::fromLatin1(kindName(Kind(a->data().toInt())));
            }
        }
        return QStringLiteral("none");
    };
    check(checked() == QLatin1String("left"), "new text: Align Left checked", checked());
    int shortcutsOk = 0;
    const QKeySequence want[] = {QKeySequence(Qt::CTRL | Qt::Key_L), QKeySequence(Qt::CTRL | Qt::Key_E),
                                 QKeySequence(Qt::CTRL | Qt::Key_R), QKeySequence(Qt::CTRL | Qt::Key_J)};
    for (int i = 0; i < 4; ++i) {
        shortcutsOk += actions.action(Kind(i))->shortcut() == want[i] ? 1 : 0;
    }
    check(shortcutsOk == 4, "shortcuts Ctrl+L / E / R / J");
    check(actions.group()->isExclusive(), "alignment actions are one exclusive group");

    QTextCursor c = edit.textCursor();
    c.movePosition(QTextCursor::Start);
    edit.setTextCursor(c);
    actions.action(Kind::Center)->trigger();
    check(Alignment::kindOf(edit.document()->firstBlock()) == Kind::Center, "Center action centres the title");
    check(checked() == QLatin1String("center"), "Center checked in the title", checked());

    c.movePosition(QTextCursor::NextBlock);
    edit.setTextCursor(c);
    check(checked() == QLatin1String("left"), "cursor into body text: Left checked", checked());

    c.movePosition(QTextCursor::Start);
    edit.setTextCursor(c);
    check(checked() == QLatin1String("center"), "cursor back in the title: Center checked", checked());

    // Selection from the body back up into the title: follows the cursor end.
    c.setPosition(edit.document()->findBlockByNumber(1).position() + 3);
    c.setPosition(2, QTextCursor::KeepAnchor);
    edit.setTextCursor(c);
    check(checked() == QLatin1String("center"), "selection ending in the title: Center checked", checked());

    // Undo / redo change alignment; the caret stays in the title (zwriter's
    // undo keeps the caret on format-only steps) and the check follows.
    c.setPosition(2);
    edit.setTextCursor(c);
    Alignment::undoRedoKeepingCaret(&edit, false);
    check(edit.textCursor().position() == 2, "undo of an alignment keeps the caret in place",
          QStringLiteral("caret at %1").arg(edit.textCursor().position()));
    check(Alignment::kindOf(edit.document()->firstBlock()) == Kind::Left
              && checked() == QLatin1String("left"),
          "undo: title back to left, Left checked", checked());
    Alignment::undoRedoKeepingCaret(&edit, true);
    check(edit.textCursor().position() == 2 && checked() == QLatin1String("center"),
          "redo: title centred again, Center checked, caret kept", checked());
    // A text undo still puts the caret at the edit.
    QTextCursor typing(edit.document()->lastBlock());
    typing.movePosition(QTextCursor::EndOfBlock);
    edit.setTextCursor(typing);
    edit.insertPlainText(QStringLiteral("!"));
    c.setPosition(1);
    edit.setTextCursor(c);
    Alignment::undoRedoKeepingCaret(&edit, false);
    check(edit.textCursor().block() == edit.document()->lastBlock()
              && edit.document()->lastBlock().text() == QLatin1String("More"),
          "text undo: caret goes to the undone edit as usual");

    // Triggering the checked one again is a no-op: document stays unmodified.
    c.setPosition(1);
    edit.setTextCursor(c);
    edit.document()->setModified(false);
    actions.action(Kind::Center)->trigger();
    check(!edit.document()->isModified() && checked() == QLatin1String("center"),
          "re-triggering Center: not modified, still checked");

    // Enter in an empty centred paragraph adds another centred paragraph
    // (Qt alone would reset it to left and swallow the key).
    {
        QTextEdit e2;
        e2.setPlainText(QStringLiteral("Title\n"));
        Alignment::apply(QTextCursor(e2.document()), Kind::Center);
        QTextCursor all(e2.document());
        all.select(QTextCursor::Document);
        Alignment::apply(all, Kind::Center);
        QTextCursor end(e2.document());
        end.movePosition(QTextCursor::End);
        e2.setTextCursor(end); // in the empty second paragraph
        check(Alignment::keepAlignmentOnEnter(&e2), "Enter in an empty centred paragraph is handled");
        check(e2.document()->blockCount() == 3 && kindsOf(*e2.document()) == QStringLiteral("center,center,center")
                  && e2.textCursor().blockNumber() == 2,
              "...and adds a centred paragraph, caret in it", kindsOf(*e2.document()));
        // Left, headings and list items keep Qt's own Enter.
        QTextCursor left(e2.document()->lastBlock());
        Alignment::apply(left, Kind::Left);
        e2.setTextCursor(left);
        check(!Alignment::keepAlignmentOnEnter(&e2), "Enter in an empty left paragraph is Qt's");
        QTextCursor typed = e2.textCursor();
        typed.insertText(QStringLiteral("x"));
        Alignment::apply(typed, Kind::Center);
        e2.setTextCursor(typed);
        check(!Alignment::keepAlignmentOnEnter(&e2), "Enter in a non-empty paragraph is Qt's");
    }

    // Icons exist for every theme ink.
    const QIcon ic = Alignment::icon(Kind::Center, Qt::white, Qt::white, Qt::gray);
    check(!ic.pixmap(16, 16).isNull() && !ic.pixmap(16, 16, QIcon::Normal, QIcon::On).isNull(),
          "alignment icons draw (normal and checked)");
}

// ---- Print / PDF / pagination -------------------------------------------
struct Ink { int left = -1, right = -1; };

Ink inkInRows(const QImage &img, int y0, int y1)
{
    Ink ink;
    ink.left = img.width();
    for (int y = qMax(0, y0); y < qMin(img.height(), y1); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qGray(row[x]) < 128) {
                ink.left = qMin(ink.left, x);
                ink.right = qMax(ink.right, x);
            }
        }
    }
    if (ink.right < 0) {
        ink.left = -1;
    }
    return ink;
}

// Rows occupied by each text line, in device pixels, from the painted image.
QList<QPair<int, int>> lineBands(const QImage &img)
{
    QList<QPair<int, int>> bands;
    int start = -1;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        bool ink = false;
        for (int x = 0; x < img.width() && !ink; ++x) {
            ink = qGray(row[x]) < 128;
        }
        if (ink && start < 0) {
            start = y;
        } else if (!ink && start >= 0) {
            bands.append({start, y});
            start = -1;
        }
    }
    if (start >= 0) {
        bands.append({start, img.height()});
    }
    return bands;
}

void testPrintAlignment()
{
    const qreal dpi = 150.0;
    QTextDocument doc;
    QFont f;
    f.setFamilies(DocumentIo::defaultFontFamilies());
    f.setStyleHint(QFont::TypeWriter);
    f.setPointSize(12);
    doc.setDefaultFont(f);
    const QString longText = QStringLiteral(
        "Justified text runs from margin to margin on every line except the last one of the "
        "paragraph, which stays ragged like ordinary left aligned text does in any word "
        "processor, so the final line here is short.");
    doc.setPlainText(QStringLiteral("CENTERED TITLE\nRight side\n") + longText
                     + QStringLiteral("\nPAGE TWO CENTER"));
    const Kind kinds[] = {Kind::Center, Kind::Right, Kind::Justify, Kind::Center};
    int i = 0;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next(), ++i) {
        Alignment::apply(QTextCursor(b), kinds[i]);
    }
    // Last paragraph on its own page: pagination keeps alignment.
    QTextCursor pb(doc.lastBlock());
    QTextBlockFormat bf = pb.blockFormat();
    bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    pb.setBlockFormat(bf);

    PrintLayout::Page page;
    page.sheetMm = QSizeF(210, 297);
    page.marginsMm = QMarginsF(25.4, 25.4, 25.4, 25.4);
    const int w = qRound(210 / 25.4 * dpi);
    const int h = qRound(297 / 25.4 * dpi);
    // One painter for the whole run, as with QPrinter: paintDocument keeps
    // the painter it was given across newPage(). Each finished page is
    // copied out and the canvas cleared.
    QList<QImage> pages;
    QImage canvas(w, h, QImage::Format_RGB32);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    const int n = PrintLayout::paintDocument(doc, &painter, page, dpi, dpi, [&]() {
        pages.append(canvas.copy());
        painter.save();
        painter.resetTransform();
        painter.setClipping(false);
        painter.fillRect(QRect(0, 0, w, h), Qt::white);
        painter.restore();
        return true;
    });
    painter.end();
    pages.append(canvas.copy());
    check(n == 2 && pages.size() == 2, "print: two pages (page break)", QStringLiteral("%1 pages").arg(n));

    const qreal marginPx = dpi; // 25.4 mm = 1 in
    const qreal textLeft = marginPx, textRight = w - marginPx, mid = w / 2.0;
    const qreal tol = dpi * 0.12; // ~3 mm: glyph side bearings

    const QList<QPair<int, int>> bands = lineBands(pages.first());
    check(bands.size() >= 5, "print: title, right line and a multi-line paragraph",
          QStringLiteral("%1 lines").arg(bands.size()));
    if (bands.size() >= 5) {
        const Ink title = inkInRows(pages.first(), bands[0].first, bands[0].second);
        const qreal titleMid = (title.left + title.right) / 2.0;
        check(qAbs(titleMid - mid) <= tol, "print/PDF: centered title is centred on the text area",
              QStringLiteral("ink %1..%2, mid %3 vs %4").arg(title.left).arg(title.right).arg(titleMid).arg(mid));
        const Ink right = inkInRows(pages.first(), bands[1].first, bands[1].second);
        check(qAbs(right.right - textRight) <= tol && right.left > mid,
              "print/PDF: right-aligned line ends at the right margin",
              QStringLiteral("ink %1..%2 vs margin %3").arg(right.left).arg(right.right).arg(textRight));
        int fullLines = 0;
        for (int l = 2; l + 1 < bands.size(); ++l) {
            const Ink j = inkInRows(pages.first(), bands[l].first, bands[l].second);
            if (qAbs(j.left - textLeft) <= tol && qAbs(j.right - textRight) <= tol) {
                ++fullLines;
            }
        }
        check(fullLines == bands.size() - 3, "print/PDF: justified lines run margin to margin",
              QStringLiteral("%1 of %2").arg(fullLines).arg(bands.size() - 3));
        const Ink last = inkInRows(pages.first(), bands.last().first, bands.last().second);
        check(qAbs(last.left - textLeft) <= tol && last.right < textRight - dpi,
              "print/PDF: last line of a justified paragraph stays ragged",
              QStringLiteral("ink %1..%2").arg(last.left).arg(last.right));
    }
    if (pages.size() == 2) {
        const Ink p2 = inkInRows(pages.at(1), 0, h);
        const qreal p2Mid = (p2.left + p2.right) / 2.0;
        check(p2.right > 0 && qAbs(p2Mid - mid) <= tol, "pagination: centered paragraph on page 2 stays centred",
              QStringLiteral("mid %1 vs %2").arg(p2Mid).arg(mid));
    }
}

} // namespace

int main(int argc, char **argv)
{
    // Unbuffered, so a crash still shows how far the checks got.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Headless on macOS the default native style needs a Cocoa window server
    // and crashes under the offscreen platform; Fusion works everywhere.
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("FAIL  temp dir\n");
        return 1;
    }
    testOdtRoundTrip(dir.path());
    testOdtFromLibreOffice(dir.path());
    testRtf(dir.path());
    testUndo();
    testCheckedState();
    testPrintAlignment();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
