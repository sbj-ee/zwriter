// Outline editing tests (ctest, headless):
//   R  rich text (ODT / RTF headings are blocks with a heading level): move a
//      section up / down among siblings with its subsections, move it to
//      another sibling (drag and drop), refusals, block formats (alignment,
//      indent, lists, tables, empty last paragraph) survive, one undo step
//   M  Markdown: the same on "#" lines, plus bytes: blank-line spacing stays
//      between the same sections and the file's final newline (or lack of one)
//      stays last
//   L  promote / demote: a heading and its subsections, limits, "#" added or
//      removed, the window's level setter is called per block, one undo step
// Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "Outline.hpp"
#include "OutlineEdit.hpp"

#include <QGuiApplication>
#include <QTextBlock>
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

// One rich block: heading level (0 = body), text, alignment, left margin.
struct Line {
    int level;
    QString text;
    Qt::Alignment align = Qt::AlignLeft;
    qreal margin = 0;
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
        QTextBlockFormat bf;
        bf.setHeadingLevel(l.level);
        bf.setAlignment(l.align);
        bf.setLeftMargin(l.margin);
        c.setBlockFormat(bf);
        c.insertText(l.text);
    }
    doc->clearUndoRedoStacks();
}

// "level:text/align/margin" per block: what must survive a move.
QStringList shape(const QTextDocument &doc)
{
    QStringList out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat f = b.blockFormat();
        out << QStringLiteral("%1:%2/%3/%4").arg(f.headingLevel()).arg(b.text()).arg(int(f.alignment())).arg(f.leftMargin());
    }
    return out;
}

QStringList texts(const QTextDocument &doc)
{
    QStringList out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        out << b.text();
    }
    return out;
}

QString join(const QStringList &l)
{
    return l.join(QStringLiteral(" | "));
}

int positionOf(const QTextDocument &doc, const QString &heading)
{
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (b.text() == heading) {
            return b.position();
        }
    }
    return -1;
}

// preamble; A (a1; A.1 (a11); A.2 (a21)); B (b1); C (c1); empty last paragraph
QList<Line> sampleLines()
{
    return {{0, QStringLiteral("preamble"), Qt::AlignLeft, 0},
            {1, QStringLiteral("A")},
            {0, QStringLiteral("a1"), Qt::AlignHCenter, 10},
            {2, QStringLiteral("A.1")},
            {0, QStringLiteral("a11"), Qt::AlignRight, 20},
            {2, QStringLiteral("A.2")},
            {0, QStringLiteral("a21"), Qt::AlignLeft, 30},
            {1, QStringLiteral("B")},
            {0, QStringLiteral("b1"), Qt::AlignJustify, 40},
            {1, QStringLiteral("C")},
            {0, QStringLiteral("c1"), Qt::AlignHCenter, 50},
            {0, QString()}};
}

void testRichMoves()
{
    QTextDocument doc;
    fillRich(&doc, sampleLines());
    const QStringList original = shape(doc);

    // Down: A swaps with B, A.1 and A.2 go with A.
    OutlineEdit::Result r = OutlineEdit::moveSection(&doc, false, positionOf(doc, QStringLiteral("a1")), +1);
    check(r.ok, "R1 move a section down", r.why);
    check(texts(doc) == QStringList({"preamble", "B", "b1", "A", "a1", "A.1", "a11", "A.2", "a21", "C", "c1", ""}),
          "R2 A and its subsections follow B", join(texts(doc)));
    check(r.ok && r.position == positionOf(doc, QStringLiteral("A")), "R3 the result is the moved heading's position");

    // Formats travel with their blocks: the shape equals the original with the blocks permuted.
    QStringList expect = original;
    const QStringList a = original.mid(1, 6); // A .. a21
    const QStringList b = original.mid(7, 2); // B, b1
    expect = original.mid(0, 1) + b + a + original.mid(9);
    check(shape(doc) == expect, "R4 heading levels, alignment and margins stay on their own blocks",
          join(shape(doc)) + "  vs  " + join(expect));

    // One undo step puts everything back; redo moves again.
    doc.undo();
    check(shape(doc) == original, "R5 one undo restores the document");
    doc.redo();
    check(texts(doc).mid(1, 2) == QStringList({"B", "b1"}), "R6 redo moves it again");
    doc.undo();

    // Up: C moves above B; a first sibling cannot go up; a last one cannot go down.
    r = OutlineEdit::moveSection(&doc, false, positionOf(doc, QStringLiteral("c1")), -1);
    check(r.ok && texts(doc) == QStringList({"preamble", "A", "a1", "A.1", "a11", "A.2", "a21", "C", "c1", "B", "b1", ""}),
          "R7 move a section up", join(texts(doc)));
    doc.undo();
    r = OutlineEdit::moveSection(&doc, false, positionOf(doc, QStringLiteral("A")), -1);
    check(!r.ok && !r.why.isEmpty() && shape(doc) == original, "R8 the first section cannot move up (and nothing changed)", r.why);
    r = OutlineEdit::moveSection(&doc, false, positionOf(doc, QStringLiteral("C")), +1);
    check(!r.ok && shape(doc) == original, "R9 the last section cannot move down", r.why);
    r = OutlineEdit::moveSection(&doc, false, positionOf(doc, QStringLiteral("preamble")), +1);
    check(!r.ok && shape(doc) == original, "R10 text above the first heading is in no section", r.why);

    // Among subsections: A.2 goes above A.1; A.1 cannot leave A; A.2 cannot go down past A.
    r = OutlineEdit::moveSection(&doc, false, positionOf(doc, QStringLiteral("A.2")), -1);
    check(r.ok && texts(doc).mid(1, 6) == QStringList({"A", "a1", "A.2", "a21", "A.1", "a11"}),
          "R11 a subsection moves among its siblings", join(texts(doc)));
    doc.undo();
    r = OutlineEdit::moveSection(&doc, false, positionOf(doc, QStringLiteral("A.2")), +1);
    check(!r.ok, "R12 the last subsection does not move out of its parent", r.why);

    // Drag and drop: A to after C, C to before A, B to before B (no-op), levels must match.
    r = OutlineEdit::moveSectionTo(&doc, false, positionOf(doc, QStringLiteral("A")), positionOf(doc, QStringLiteral("C")), true);
    check(r.ok && texts(doc) == QStringList({"preamble", "B", "b1", "C", "c1", "A", "a1", "A.1", "a11", "A.2", "a21", ""}),
          "R13 drop a section after a sibling two places away", join(texts(doc)));
    check(!texts(doc).isEmpty() && texts(doc).last().isEmpty(), "R14 the empty last paragraph is still last");
    doc.undo();
    r = OutlineEdit::moveSectionTo(&doc, false, positionOf(doc, QStringLiteral("C")), positionOf(doc, QStringLiteral("A")), false);
    check(r.ok && texts(doc).mid(1, 4) == QStringList({"C", "c1", "A", "a1"}), "R15 drop a section before the first one", join(texts(doc)));
    doc.undo();
    r = OutlineEdit::moveSectionTo(&doc, false, positionOf(doc, QStringLiteral("B")), positionOf(doc, QStringLiteral("B")), false);
    check(!r.ok, "R16 dropping a section on itself does nothing");
    r = OutlineEdit::moveSectionTo(&doc, false, positionOf(doc, QStringLiteral("A.1")), positionOf(doc, QStringLiteral("B")), false);
    check(!r.ok && shape(doc) == original, "R17 sections of different levels do not swap", r.why);
    r = OutlineEdit::moveSectionTo(&doc, false, positionOf(doc, QStringLiteral("B")), positionOf(doc, QStringLiteral("A")), true);
    check(!r.ok && shape(doc) == original, "R18 dropping after the previous sibling is where it already is", r.why);
}

void testRichStructures()
{
    // A section with a bulleted list and a table: both travel intact.
    QTextDocument doc;
    fillRich(&doc, {{1, QStringLiteral("One")}, {0, QStringLiteral("intro")}, {1, QStringLiteral("Two")}, {0, QStringLiteral("tail")}});
    {
        QTextCursor c(&doc);
        c.setPosition(doc.findBlockByNumber(1).position());
        c.movePosition(QTextCursor::EndOfBlock);
        c.insertBlock();
        QTextListFormat lf;
        lf.setStyle(QTextListFormat::ListDisc);
        QTextList *list = c.createList(lf);
        Q_UNUSED(list)
        c.insertText(QStringLiteral("item one"));
        c.insertBlock();
        c.insertText(QStringLiteral("item two"));
        c.insertBlock();
        QTextBlockFormat plain;
        c.setBlockFormat(plain);
        c.insertText(QStringLiteral("after list"));
        c.insertTable(2, 2);
        doc.clearUndoRedoStacks();
    }
    int tablesBefore = 0;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        Q_UNUSED(b)
    }
    for (QTextFrame *f : doc.rootFrame()->childFrames()) {
        tablesBefore += qobject_cast<QTextTable *>(f) ? 1 : 0;
    }
    check(tablesBefore == 1, "S0 fixture has one table");

    const OutlineEdit::Result r = OutlineEdit::moveSection(&doc, false, positionOf(doc, QStringLiteral("One")), +1);
    check(r.ok, "S1 move a section that holds a list and a table", r.why);
    const QStringList t = texts(doc);
    check(t.size() > 2 && t[0] == QStringLiteral("Two") && t.contains(QStringLiteral("item one")), "S2 order changed, content kept", join(t));

    int tablesAfter = 0;
    for (QTextFrame *f : doc.rootFrame()->childFrames()) {
        tablesAfter += qobject_cast<QTextTable *>(f) ? 1 : 0;
    }
    check(tablesAfter == 1, "S3 the table is still one table");
    bool listOk = true;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (b.text() == QStringLiteral("item one") || b.text() == QStringLiteral("item two")) {
            listOk = listOk && b.textList() != nullptr && b.textList()->format().style() == QTextListFormat::ListDisc;
        }
    }
    check(listOk, "S4 the list items are still bullets");
    bool levels = true;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (b.text() == QStringLiteral("One") || b.text() == QStringLiteral("Two")) {
            levels = levels && b.blockFormat().headingLevel() == 1;
        }
        if (b.text() == QStringLiteral("intro") || b.text() == QStringLiteral("tail")) {
            levels = levels && b.blockFormat().headingLevel() == 0;
        }
    }
    check(levels, "S5 headings are still headings and body text is still body text");
    doc.undo();
    check(texts(doc).first() == QStringLiteral("One"), "S6 one undo puts it back");

    // A heading inside a table cell cannot be moved.
    QTextDocument cell;
    fillRich(&cell, {{1, QStringLiteral("H1")}, {0, QStringLiteral("x")}, {1, QStringLiteral("H2")}});
    QTextCursor c(&cell);
    c.setPosition(cell.findBlockByNumber(1).position());
    c.movePosition(QTextCursor::EndOfBlock);
    QTextTable *table = c.insertTable(1, 1);
    QTextCursor inCell = table->cellAt(0, 0).firstCursorPosition();
    QTextBlockFormat bf = inCell.blockFormat();
    bf.setHeadingLevel(1);
    inCell.setBlockFormat(bf);
    inCell.insertText(QStringLiteral("in a cell"));
    const QStringList before = texts(cell);
    const OutlineEdit::Result rc = OutlineEdit::moveSection(&cell, false, positionOf(cell, QStringLiteral("H1")), +1);
    check(!rc.ok || texts(cell).size() == before.size(), "S7 a heading in a table cell never breaks the table", rc.why);
}

void testMarkdown()
{
    const QString src = QStringLiteral("intro\n\n# A\n\na text\n\n## A1\n\nx\n\n# B\n\nb text\n");
    QTextDocument doc;
    doc.setPlainText(src);
    doc.clearUndoRedoStacks();

    OutlineEdit::Result r = OutlineEdit::moveSection(&doc, true, positionOf(doc, QStringLiteral("b text")), -1);
    check(r.ok, "M1 move a Markdown section up", r.why);
    check(doc.toPlainText() == QStringLiteral("intro\n\n# B\n\nb text\n\n# A\n\na text\n\n## A1\n\nx\n"),
          "M2 blank lines stay between the sections and the file still ends in one newline",
          QString(doc.toPlainText()).replace('\n', "\\n"));
    doc.undo();
    check(doc.toPlainText() == src, "M3 one undo gives the original text back byte for byte");

    // Down: A moves below B; no final newline stays no final newline.
    const QString noEol = QStringLiteral("# A\n\na\n\n# B\n\nb");
    doc.setPlainText(noEol);
    doc.clearUndoRedoStacks();
    r = OutlineEdit::moveSection(&doc, true, 0, +1);
    check(r.ok && doc.toPlainText() == QStringLiteral("# B\n\nb\n\n# A\n\na"),
          "M4 a file without a final newline gets none, and none is added between", QString(doc.toPlainText()).replace('\n', "\\n"));
    r = OutlineEdit::moveSection(&doc, true, 0, +1);
    check(r.ok && doc.toPlainText() == noEol, "M5 moving it back gives the original bytes", QString(doc.toPlainText()).replace('\n', "\\n"));

    // Three siblings, drop the first after the last.
    const QString three = QStringLiteral("# A\n\na\n\n# B\n\nb\n\n# C\n\nc\n");
    doc.setPlainText(three);
    r = OutlineEdit::moveSectionTo(&doc, true, 0, positionOf(doc, QStringLiteral("# C")), true);
    check(r.ok && doc.toPlainText() == QStringLiteral("# B\n\nb\n\n# C\n\nc\n\n# A\n\na\n"),
          "M6 drop the first of three after the last", QString(doc.toPlainText()).replace('\n', "\\n"));
    doc.setPlainText(three);
    r = OutlineEdit::moveSectionTo(&doc, true, positionOf(doc, QStringLiteral("# C")), 0, false);
    check(r.ok && doc.toPlainText() == QStringLiteral("# C\n\nc\n\n# A\n\na\n\n# B\n\nb\n"),
          "M7 drop the last of three before the first", QString(doc.toPlainText()).replace('\n', "\\n"));

    // Fenced code with "#" lines stays inside its section and is not a heading.
    const QString fenced = QStringLiteral("# A\n\n```\n# not\n```\n\n# B\n\nb\n");
    doc.setPlainText(fenced);
    r = OutlineEdit::moveSection(&doc, true, 0, +1);
    check(r.ok && doc.toPlainText() == QStringLiteral("# B\n\nb\n\n# A\n\n```\n# not\n```\n"),
          "M8 a fenced block moves with its section", QString(doc.toPlainText()).replace('\n', "\\n"));

    // Windows line endings are not in the text (blocks), so nothing to keep here; two sections only.
    doc.setPlainText(QStringLiteral("# A\n# B\n"));
    r = OutlineEdit::moveSection(&doc, true, 0, +1);
    check(r.ok && doc.toPlainText() == QStringLiteral("# B\n# A\n"), "M9 sections with no blank lines between them", QString(doc.toPlainText()).replace('\n', "\\n"));
}

void testLevels()
{
    QTextDocument doc;
    fillRich(&doc, {{1, QStringLiteral("A")}, {0, QStringLiteral("a")}, {2, QStringLiteral("A.1")}, {0, QStringLiteral("x")},
                    {1, QStringLiteral("B")}});
    // Demote A: A -> 2, A.1 -> 3. The setter is told each block and its new level.
    QList<QPair<int, int>> calls;
    OutlineEdit::Result r = OutlineEdit::changeLevel(&doc, false, positionOf(doc, QStringLiteral("a")), +1,
                                                     [&](int block, int level) {
                                                         calls.append({block, level});
                                                         QTextCursor c(doc.findBlockByNumber(block));
                                                         QTextBlockFormat f = c.blockFormat();
                                                         f.setHeadingLevel(level);
                                                         c.setBlockFormat(f);
                                                     });
    check(r.ok && calls == QList<QPair<int, int>>({{0, 2}, {2, 3}}), "L1 demote changes the heading and its subsections through the setter");
    check(doc.findBlockByNumber(0).blockFormat().headingLevel() == 2 && doc.findBlockByNumber(2).blockFormat().headingLevel() == 3
              && doc.findBlockByNumber(4).blockFormat().headingLevel() == 1,
          "L2 only that section changed");
    r = OutlineEdit::changeLevel(&doc, false, positionOf(doc, QStringLiteral("a")), +1);
    check(!r.ok && !r.why.isEmpty(), "L3 Heading 3 is the deepest in a rich document, so demoting A again is refused", r.why);
    doc.undo();
    doc.undo();
    // Promote with no setter: block heading levels change.
    r = OutlineEdit::changeLevel(&doc, false, positionOf(doc, QStringLiteral("A.1")), -1);
    check(r.ok && doc.findBlockByNumber(2).blockFormat().headingLevel() == 1, "L4 promote without a setter sets the block's level");
    r = OutlineEdit::changeLevel(&doc, false, positionOf(doc, QStringLiteral("B")), -1);
    check(!r.ok, "L5 a top-level heading cannot be promoted", r.why);
    doc.undo();
    check(doc.findBlockByNumber(2).blockFormat().headingLevel() == 2, "L6 one undo for the whole promote");

    // Markdown.
    QTextDocument md;
    md.setPlainText(QStringLiteral("# A\n\n## A1 ##\n\n###### deep\n\n# B\n"));
    md.clearUndoRedoStacks();
    r = OutlineEdit::changeLevel(&md, true, 0, +1);
    check(!r.ok, "L7 a Markdown heading with a level-6 subsection cannot be demoted", r.why);
    r = OutlineEdit::changeLevel(&md, true, positionOf(md, QStringLiteral("## A1 ##")), -1);
    check(r.ok && md.toPlainText().contains(QStringLiteral("\n# A1 ##\n")), "L8 promoting takes one # off");
    check(md.toPlainText() == QStringLiteral("# A\n\n# A1 ##\n\n##### deep\n\n# B\n"),
          "L9 the heading and the level-6 subsection under it both move one level", QString(md.toPlainText()).replace('\n', "\\n"));
    md.undo();
    check(md.toPlainText() == QStringLiteral("# A\n\n## A1 ##\n\n###### deep\n\n# B\n"), "L10 one undo for the whole promote");
    md.setPlainText(QStringLiteral("# A\n\nbody\n\n## S\n\ntext\n"));
    r = OutlineEdit::changeLevel(&md, true, 0, +1);
    check(r.ok && md.toPlainText() == QStringLiteral("## A\n\nbody\n\n### S\n\ntext\n"), "L11 demoting a Markdown section adds a # to each heading",
          QString(md.toPlainText()).replace('\n', "\\n"));
    md.setPlainText(QStringLiteral("   ## indented\n"));
    r = OutlineEdit::changeLevel(&md, true, 0, -1);
    check(r.ok && md.toPlainText() == QStringLiteral("   # indented\n"), "L12 leading spaces are left alone", QString(md.toPlainText()).replace('\n', "\\n"));

    const QList<Outline::Item> items = Outline::scan(&doc, false);
    check(OutlineEdit::canChangeLevel(items, 0, +1, false) && !OutlineEdit::canChangeLevel(items, 0, -1, false)
              && OutlineEdit::canMove(items, 0, +1) && !OutlineEdit::canMove(items, 0, -1),
          "L13 canMove / canChangeLevel agree with what the operations do");
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    testRichMoves();
    testRichStructures();
    testMarkdown();
    testLevels();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
