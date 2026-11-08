// Outline tests (ctest, headless):
//   W  wordCount: words are runs with a letter or digit; Markdown markers do not count
//   O  scan, block heading levels (ODT / RTF): levels, text, positions, section
//      ends and word counts (subsections belong to their parent), generated ToC
//      and empty headings left out
//   M  scan, Markdown: "#" lines 1-6, closing #s dropped, fenced code skipped,
//      "#" with no text skipped, setext headings not recognised, markers not counted
//   I  indexAt: the section a position is in (-1 above the first heading)
//   P  OutlinePane: one rebuild per edit burst, none while hidden (caught up on
//      show), kept rows when only text under a heading changes, rebuilt rows when
//      a heading changes, nesting under the nearest lower level, caret follow
//      (which does not jump), click / Enter emit the heading position, arrow keys
//      do not, Esc, empty-state hint per mode
//   S  QSettings: outline/visible
// Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "DocumentIo.hpp"
#include "Outline.hpp"
#include "OutlinePane.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTreeWidget>

#include <cstdio>
#include <functional>

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

void spin(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

bool spinUntil(const std::function<bool()> &cond, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return cond();
}

// A rich document: one block per entry; level > 0 makes it a heading block.
struct Line {
    int level;
    QString text;
};

void fillRich(QTextDocument *doc, const QList<Line> &lines)
{
    doc->clear();
    QTextCursor c(doc);
    bool first = true;
    for (const Line &l : lines) {
        if (!first) {
            c.insertBlock();
        }
        first = false;
        QTextBlockFormat bf = c.blockFormat();
        bf.setHeadingLevel(l.level);
        c.setBlockFormat(bf);
        c.insertText(l.text);
    }
}

QString summary(const QList<Outline::Item> &items)
{
    QStringList parts;
    for (const Outline::Item &i : items) {
        parts << QStringLiteral("%1:%2").arg(i.level).arg(i.text);
    }
    return parts.join(QStringLiteral(" | "));
}

void testWordCount()
{
    check(Outline::wordCount(u"") == 0, "W1 empty text has no words");
    check(Outline::wordCount(u"One two  three\tfour") == 4, "W2 words split on any whitespace");
    check(Outline::wordCount(u"- ## --- a b3 ***") == 2, "W3 Markdown markers are not words");
    check(Outline::wordCount(u"don't re-read it") == 3, "W4 apostrophes and hyphens stay inside a word");
}

void testScanRich()
{
    QTextDocument doc;
    fillRich(&doc, {{0, QStringLiteral("front matter words")},       // 0
                    {1, QStringLiteral("Title")},                     // 1
                    {0, QStringLiteral("one two three")},             // 2
                    {2, QStringLiteral("Alpha")},                     // 3
                    {0, QStringLiteral("four five")},                 // 4
                    {3, QStringLiteral("Alpha one")},                 // 5
                    {0, QStringLiteral("six")},                       // 6
                    {2, QStringLiteral("Beta")},                      // 7
                    {0, QStringLiteral("seven eight nine ten")},      // 8
                    {1, QStringLiteral("Next")},                      // 9
                    {0, QStringLiteral("eleven")}});                  // 10
    const QList<Outline::Item> items = Outline::scan(&doc, false);
    check(items.size() == 5, "O1 five headings found", summary(items));
    check(summary(items) == QStringLiteral("1:Title | 2:Alpha | 3:Alpha one | 2:Beta | 1:Next"),
          "O2 levels and text in document order", summary(items));
    if (items.size() != 5) {
        return;
    }
    check(items[0].blockNumber == 1 && items[0].position == doc.findBlockByNumber(1).position(),
          "O3 position is the start of the heading block");
    check(items[0].endBlock == 9 && items[1].endBlock == 7 && items[2].endBlock == 7
              && items[3].endBlock == 9 && items[4].endBlock == 11,
          "O4 a section ends at the next heading of the same or a higher level",
          QStringLiteral("%1 %2 %3 %4 %5")
              .arg(items[0].endBlock).arg(items[1].endBlock).arg(items[2].endBlock)
              .arg(items[3].endBlock).arg(items[4].endBlock));
    check(items[0].words == 3 + 2 + 1 + 4, "O5 a parent's words include its subsections", QString::number(items[0].words));
    check(items[1].words == 2 + 1, "O6 a middle section counts its own subsection", QString::number(items[1].words));
    check(items[2].words == 1 && items[3].words == 4 && items[4].words == 1,
          "O7 leaf sections count their own body",
          QStringLiteral("%1 %2 %3").arg(items[2].words).arg(items[3].words).arg(items[4].words));

    // Generated table of contents and empty headings are not outline entries.
    QTextDocument toc;
    fillRich(&toc, {{1, QStringLiteral("Contents")}, {1, QStringLiteral("Real")}, {1, QStringLiteral("   ")},
                    {0, QStringLiteral("body")}});
    QTextCursor c(&toc);
    c.setPosition(toc.firstBlock().position());
    QTextBlockFormat bf = c.blockFormat();
    bf.setProperty(DocumentIo::GeneratedTocTitleProperty, true);
    c.setBlockFormat(bf);
    const QList<Outline::Item> t = Outline::scan(&toc, false);
    check(t.size() == 1 && t[0].text == QStringLiteral("Real"), "O8 generated ToC and empty headings are left out",
          summary(t));
    check(t.size() == 1 && t[0].words == 1, "O9 an empty heading adds no words to the section above",
          t.isEmpty() ? QString() : QString::number(t[0].words));

    check(Outline::scan(nullptr, false).isEmpty(), "O10 no document, no outline");
    QTextDocument none;
    none.setPlainText(QStringLiteral("just text\nand more"));
    check(Outline::scan(&none, false).isEmpty(), "O11 a document without headings has an empty outline");
}

void testScanMarkdown()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral(
        "preamble words\n"            // 0
        "# One\n"                     // 1
        "\n"                          // 2
        "text words here\n"           // 3
        "## Two ##\n"                 // 4
        "```\n"                       // 5
        "# not a heading\n"           // 6
        "```\n"                       // 7
        "- a list item\n"             // 8
        "   ### Three\n"              // 9
        "#\n"                         // 10  no text: not an entry
        "####### seven\n"             // 11  seven #s: not a heading
        "#hashtag\n"                  // 12  no space: not a heading
        "Setext\n"                    // 13
        "======\n"                    // 14  setext headings are not recognised
        "# Last  one\n"));            // 15
    const QList<Outline::Item> items = Outline::scan(&doc, true);
    check(summary(items) == QStringLiteral("1:One | 2:Two | 3:Three | 1:Last one"),
          "M1 ATX headings, closing #s dropped, fences / bare # / 7 #s / #tag / setext skipped",
          summary(items));
    if (items.size() != 4) {
        return;
    }
    check(items[0].blockNumber == 1 && items[0].endBlock == 15, "M2 level 1 runs to the next level 1",
          QStringLiteral("%1..%2").arg(items[0].blockNumber).arg(items[0].endBlock));
    check(items[1].endBlock == 15 && items[2].endBlock == 15, "M3 nested sections end with their parent",
          QStringLiteral("%1 %2").arg(items[1].endBlock).arg(items[2].endBlock));
    check(items[0].words > 0 && items[3].words == 0, "M4 body words counted, a heading with no body has none");

    // A fence that never closes swallows the rest, as the editor's highlighting does.
    QTextDocument open;
    open.setPlainText(QStringLiteral("# A\n```\n# B\n"));
    check(Outline::scan(&open, true).size() == 1, "M5 an unclosed fence hides the headings after it");

    // The same text is not an outline when the document is not Markdown.
    check(Outline::scan(&doc, false).isEmpty(), "M6 plain blocks have no heading levels outside Markdown mode");
}

void testIndexAt()
{
    QTextDocument doc;
    fillRich(&doc, {{0, QStringLiteral("above")}, {1, QStringLiteral("A")}, {0, QStringLiteral("body")},
                    {1, QStringLiteral("B")}, {0, QStringLiteral("end")}});
    const QList<Outline::Item> items = Outline::scan(&doc, false);
    const int a = doc.findBlockByNumber(1).position();
    const int b = doc.findBlockByNumber(3).position();
    check(Outline::indexAt(items, 0) == -1, "I1 above the first heading is -1");
    check(Outline::indexAt(items, a) == 0, "I2 the first character of a heading is in it");
    check(Outline::indexAt(items, b - 1) == 0, "I3 the end of the section above is still in it");
    check(Outline::indexAt(items, b) == 1 && Outline::indexAt(items, doc.characterCount() - 1) == 1,
          "I4 later positions are in the last heading's section");
    check(Outline::indexAt({}, 5) == -1, "I5 no headings, no section");
}

QTreeWidgetItem *rowAt(OutlinePane &pane, int i)
{
    // Rows are listed depth first, which is document order.
    int n = 0;
    std::function<QTreeWidgetItem *(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) -> QTreeWidgetItem * {
        if (n++ == i) {
            return it;
        }
        for (int k = 0; k < it->childCount(); ++k) {
            if (QTreeWidgetItem *r = walk(it->child(k))) {
                return r;
            }
        }
        return nullptr;
    };
    for (int k = 0; k < pane.tree()->topLevelItemCount(); ++k) {
        if (QTreeWidgetItem *r = walk(pane.tree()->topLevelItem(k))) {
            return r;
        }
    }
    return nullptr;
}

void testPane()
{
    QTextEdit editor;
    editor.setAcceptRichText(true);
    fillRich(editor.document(), {{1, QStringLiteral("Title")}, {0, QStringLiteral("one two")},
                                 {2, QStringLiteral("Alpha")}, {0, QStringLiteral("three")},
                                 {3, QStringLiteral("Deep")}, {0, QStringLiteral("four")},
                                 {1, QStringLiteral("Next")}, {0, QStringLiteral("five")}});
    OutlinePane pane;
    pane.setDebounceInterval(40);
    pane.attachEditor(&editor);

    // D: hidden panes do no work.
    editor.insertPlainText(QStringLiteral(" "));
    spin(120);
    check(pane.rebuildCount() == 0 && !pane.isRebuildPending(), "P1 a hidden pane does not rebuild or schedule");
    pane.show();
    check(pane.rebuildCount() == 1, "P2 showing a stale pane catches up at once", QString::number(pane.rebuildCount()));
    check(pane.items().size() == 4, "P3 four headings", QString::number(pane.items().size()));

    // Tree shape: Deep hangs from Alpha, which hangs from Title.
    QTreeWidgetItem *title = pane.tree()->topLevelItem(0);
    check(pane.tree()->topLevelItemCount() == 2 && title && title->childCount() == 1
              && title->child(0)->childCount() == 1 && title->child(0)->child(0)->text(0) == QStringLiteral("Deep"),
          "P4 headings nest under the nearest lower level");
    check(pane.tree()->isVisibleTo(&pane) && !pane.emptyLabel()->isVisibleTo(&pane),
          "P5 the tree shows when there are headings");

    // R: one rebuild per burst, after the pause.
    const int before = pane.rebuildCount();
    QTreeWidgetItem *rowBefore = rowAt(pane, 1);
    QTextCursor c(editor.document());
    c.setPosition(editor.document()->findBlockByNumber(1).position());
    for (int i = 0; i < 5; ++i) {
        c.insertText(QStringLiteral("x"));
        spin(5);
    }
    check(pane.isRebuildPending() && pane.rebuildCount() == before, "P6 typing only schedules a rebuild");
    check(spinUntil([&] { return pane.rebuildCount() == before + 1; }, 1000), "P7 one rebuild after the burst");
    spin(120);
    check(pane.rebuildCount() == before + 1, "P8 and no more", QString::number(pane.rebuildCount() - before));
    check(rowAt(pane, 1) == rowBefore, "P9 editing body text keeps the rows (the list does not jump)");
    check(rowAt(pane, 0)->toolTip(0).contains(QStringLiteral("words")), "P10 rows say how long their section is");

    // A changed heading rebuilds.
    c.setPosition(editor.document()->findBlockByNumber(2).position());
    c.insertText(QStringLiteral("More "));
    check(spinUntil([&] { return !pane.isRebuildPending(); }, 1000), "P11 rebuild after a heading edit");
    check(rowAt(pane, 1)->text(0) == QStringLiteral("More Alpha"), "P12 the entry shows the new heading text");

    // F: the entry follows the caret and following never jumps.
    QList<int> activated; // positions the pane asked the window to go to
    QObject::connect(&pane, &OutlinePane::headingActivated, &pane, [&](int pos) { activated << pos; });
    c.setPosition(editor.document()->findBlockByNumber(5).position()); // body under "Deep"
    editor.setTextCursor(c);
    check(pane.currentIndex() == 2 && pane.tree()->currentItem() == rowAt(pane, 2),
          "P13 the caret in a section selects its entry", QString::number(pane.currentIndex()));
    c.setPosition(0);
    editor.setTextCursor(c);
    check(pane.currentIndex() == 0, "P14 the caret on the first heading selects it");
    check(activated.isEmpty(), "P15 following the caret does not emit headingActivated");
    check(pane.footerLabel()->text().contains(QStringLiteral("4 headings"))
              && pane.footerLabel()->text().contains(QStringLiteral("words")),
          "P16 the footer counts headings and the current section", pane.footerLabel()->text());

    // C: click and Enter open a heading; arrows only move.
    QTreeWidgetItem *next = rowAt(pane, 3);
    pane.tree()->itemClicked(next, 0);
    check(activated.size() == 1 && activated.last() == pane.items()[3].position,
          "P17 clicking an entry emits its heading position");
    pane.tree()->itemActivated(rowAt(pane, 1), 0);
    check(activated.size() == 2 && activated.last() == pane.items()[1].position,
          "P18 Enter on an entry emits its heading position");
    pane.tree()->setCurrentItem(rowAt(pane, 2));
    check(activated.size() == 2, "P19 moving through the list with the keys does not jump");

    int escaped = 0;
    QObject::connect(&pane, &OutlinePane::escapePressed, &pane, [&]() { ++escaped; });
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(pane.tree(), &esc);
    check(escaped == 1, "P20 Esc in the list asks for the editor back");

    // E: no headings -> hint, per mode.
    editor.setPlainText(QStringLiteral("no headings here"));
    check(spinUntil([&] { return pane.items().isEmpty() && !pane.isRebuildPending(); }, 1000),
          "P21 an edit that removes every heading empties the outline");
    check(pane.emptyLabel()->isVisibleTo(&pane) && !pane.tree()->isVisibleTo(&pane),
          "P22 the empty hint replaces the tree");
    check(pane.emptyLabel()->text().contains(QStringLiteral("Ctrl+Alt+1")), "P23 the hint for ODT mentions Heading 1-3");
    pane.setMarkdown(true);
    check(pane.emptyLabel()->text().contains(QStringLiteral("# ")), "P24 the hint for Markdown mentions #");
    editor.setPlainText(QStringLiteral("# Md one\ntext\n## Md two\n"));
    check(spinUntil([&] { return pane.items().size() == 2 && !pane.isRebuildPending(); }, 1000),
          "P25 in Markdown mode the pane scans # lines");
    pane.setMarkdown(false);
    check(spinUntil([&] { return pane.items().isEmpty() && !pane.isRebuildPending(); }, 1000),
          "P26 switching mode rescans the same text");
}

void testSettings()
{
    QSettings().remove(OutlinePane::settingsKey());
    check(!OutlinePane::loadVisibleSetting(), "S1 the outline is off by default");
    OutlinePane::saveVisibleSetting(true);
    QSettings().sync();
    check(OutlinePane::loadVisibleSetting() && QSettings().value(QStringLiteral("outline/visible")).toBool(),
          "S2 on is remembered under outline/visible");
    OutlinePane::saveVisibleSetting(false);
    check(!OutlinePane::loadVisibleSetting(), "S3 and off again");
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        std::printf("FAIL  temp dir\n");
        return 1;
    }
    // Never touch the real zwriter settings.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("sbj-ee-test"));
    QApplication::setApplicationName(QStringLiteral("zwriter-outline-test"));

    testWordCount();
    testScanRich();
    testScanMarkdown();
    testIndexAt();
    testPane();
    testSettings();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
