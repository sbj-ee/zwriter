#include "OutlineEdit.hpp"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QTextTable>

#include <algorithm>
#include <vector>

namespace OutlineEdit {

int previousSibling(const QList<Outline::Item> &items, int index)
{
    if (index < 0 || index >= items.size()) {
        return -1;
    }
    const int level = items[index].level;
    for (int j = index - 1; j >= 0; --j) {
        if (items[j].level < level) {
            return -1; // the parent: this is the first child
        }
        if (items[j].level == level) {
            return j;
        }
    }
    return -1;
}

int nextSibling(const QList<Outline::Item> &items, int index)
{
    if (index < 0 || index >= items.size()) {
        return -1;
    }
    const int level = items[index].level;
    for (int j = index + 1; j < items.size(); ++j) {
        if (items[j].level < level) {
            return -1; // the parent's next sibling begins: this was the last child
        }
        if (items[j].level == level) {
            return j;
        }
    }
    return -1;
}

bool canMove(const QList<Outline::Item> &items, int index, int delta)
{
    return delta < 0 ? previousSibling(items, index) >= 0 : nextSibling(items, index) >= 0;
}

namespace {

int maxLevel(bool markdown)
{
    return markdown ? kMaxMarkdownLevel : kMaxRichLevel;
}

// Blocks of the heading's whole section: [index, one past its last subsection).
int subtreeEnd(const QList<Outline::Item> &items, int index)
{
    int k = index + 1;
    while (k < items.size() && items[k].level > items[index].level) {
        ++k;
    }
    return k;
}

// The empty block a text file's final newline makes (or an empty last
// paragraph) stays last: it is outside every section.
int effectiveEnd(const QTextDocument *doc)
{
    const int n = doc->blockCount();
    return (n > 1 && doc->lastBlock().text().isEmpty()) ? n - 1 : n;
}

bool inTable(QTextDocument *doc, int blockNumber)
{
    const QTextBlock b = doc->findBlockByNumber(blockNumber);
    return b.isValid() && QTextCursor(b).currentTable() != nullptr;
}

Result fail(const QString &why)
{
    Result r;
    r.why = why;
    return r;
}

// One sibling section as blocks: text [x0, c) followed by a run of blank
// blocks [c, x1) (the spacing before the next section).
struct Unit {
    int x0 = 0;
    int c = 0;
    int x1 = 0;
};

// A blank block that is only spacing: no text, not a list item, not in a table.
bool isSpacer(const QTextBlock &b)
{
    return b.text().isEmpty() && !b.textList() && QTextCursor(b).currentTable() == nullptr;
}

Unit unitOf(QTextDocument *doc, const Outline::Item &item, int effEnd)
{
    Unit u;
    u.x0 = item.blockNumber;
    u.x1 = qMin(item.endBlock, effEnd);
    u.c = u.x1;
    while (u.c > u.x0 + 1 && isSpacer(doc->findBlockByNumber(u.c - 1))) {
        --u.c;
    }
    return u;
}

// Rebuilds the blocks the sibling `units` cover (they are consecutive) with
// their texts in `order` (indices into `units`). The blank runs do not move
// with the texts: each stays in the slot it was in, so the spacing between
// sections is what it was, and a section that was last (no blank line after
// it) still leaves the document ending the way it did. One undo step.
// Returns the position of the first block of the text of unit `moved`.
//
// Joining and splitting blocks leaves Qt free to give a block the format of
// its neighbour, so the format of every block that starts a piece, and of the
// blocks on both sides of the rebuilt range, is written back explicitly.
int reorderSiblings(QTextDocument *doc, const std::vector<Unit> &units, const std::vector<int> &order, int moved)
{
    const int w0 = units.front().x0;
    const int w1 = units.back().x1;
    const int n = doc->blockCount();
    const bool hasAfter = w1 < n;
    const bool hasBefore = w0 > 0;

    QTextCursor edit(doc);
    edit.beginEditBlock();

    std::vector<QTextDocumentFragment> text(units.size());
    std::vector<QTextBlockFormat> textFormat(units.size());
    std::vector<QList<QTextBlockFormat>> runFormats(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        const QTextBlock first = doc->findBlockByNumber(units[i].x0);
        const QTextBlock last = doc->findBlockByNumber(units[i].c - 1);
        QTextCursor copy(doc);
        copy.setPosition(first.position());
        copy.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor); // no closing separator
        text[i] = copy.selection();
        textFormat[i] = first.blockFormat();
        for (int b = units[i].c; b < units[i].x1; ++b) {
            runFormats[i].append(doc->findBlockByNumber(b).blockFormat());
        }
    }
    const QTextBlockFormat afterFormat = hasAfter ? doc->findBlockByNumber(w1).blockFormat() : QTextBlockFormat();
    const QTextBlockFormat beforeFormat = hasBefore ? doc->findBlockByNumber(w0 - 1).blockFormat() : QTextBlockFormat();

    // Take the range out with one separator: the one after it, or (when it ends
    // the document) the one before it.
    QTextCursor del(doc);
    if (hasAfter) {
        del.setPosition(doc->findBlockByNumber(w0).position());
        del.setPosition(doc->findBlockByNumber(w1).position(), QTextCursor::KeepAnchor);
    } else {
        const QTextBlock last = doc->lastBlock();
        del.setPosition(hasBefore ? doc->findBlockByNumber(w0).position() - 1 : 0);
        del.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    }
    del.removeSelectedText();
    if (hasBefore) {
        QTextCursor(doc->findBlockByNumber(w0 - 1)).setBlockFormat(beforeFormat);
    }

    // Put the pieces back: each a new empty block at its place, then its text.
    QTextCursor ins(doc);
    int next = w0;             // block number where the next piece begins
    bool reuse = !hasBefore && !hasAfter; // the document's one empty block takes the first piece
    int movedPos = 0;
    const auto place = [&]() {
        if (reuse) {
            reuse = false;
            ins.setPosition(0);
        } else if (next < doc->blockCount()) {
            const int pos = doc->findBlockByNumber(next).position();
            ins.setPosition(pos);
            ins.insertBlock(); // empty block before the one that followed the range
            ins.setPosition(pos);
        } else {
            const QTextBlock tail = doc->lastBlock();
            ins.setPosition(tail.position() + tail.length() - 1);
            ins.insertBlock(); // empty block after the last
        }
    };
    for (size_t k = 0; k < order.size(); ++k) {
        const size_t u = size_t(order[k]);
        place();
        ins.insertFragment(text[u]);
        QTextCursor(doc->findBlockByNumber(next)).setBlockFormat(textFormat[u]);
        if (int(u) == moved) {
            movedPos = doc->findBlockByNumber(next).position();
        }
        next += units[u].c - units[u].x0;
        // The blank run of the slot (the k-th unit's), not of this text.
        for (const QTextBlockFormat &f : std::as_const(runFormats[k])) {
            place();
            QTextCursor(doc->findBlockByNumber(next)).setBlockFormat(f);
            next += 1;
        }
    }
    if (hasAfter) {
        QTextCursor(doc->findBlockByNumber(next)).setBlockFormat(afterFormat);
    }
    edit.endEditBlock();
    return movedPos;
}

} // namespace

Result moveSectionTo(QTextDocument *doc, bool markdown, int position, int targetPosition, bool after)
{
    if (!doc) {
        return fail(QStringLiteral("No document."));
    }
    const QList<Outline::Item> items = Outline::scan(doc, markdown);
    const int s = Outline::indexAt(items, position);
    const int t = Outline::indexAt(items, targetPosition);
    if (s < 0 || t < 0) {
        return fail(QStringLiteral("Put the cursor in a section first."));
    }
    if (s == t) {
        return fail(QStringLiteral("That section is already there."));
    }
    const int level = items[s].level;
    if (items[t].level != level) {
        return fail(QStringLiteral("Sections move among sections of the same level."));
    }
    for (int k = qMin(s, t) + 1; k < qMax(s, t); ++k) {
        if (items[k].level < level) {
            return fail(QStringLiteral("Sections move within their parent section."));
        }
    }
    if (inTable(doc, items[s].blockNumber) || inTable(doc, items[t].blockNumber)) {
        return fail(QStringLiteral("A heading inside a table cannot be moved."));
    }

    // The siblings from the first to the last of the two, and the new order.
    const int eff = effectiveEnd(doc);
    std::vector<Unit> units;
    std::vector<int> order;
    int movedUnit = -1;
    int targetUnit = -1;
    for (int k = qMin(s, t); k <= qMax(s, t); ++k) {
        if (items[k].level != level) {
            continue; // a subsection: it travels inside its parent's unit
        }
        if (k == s) {
            movedUnit = int(units.size());
        }
        if (k == t) {
            targetUnit = int(units.size());
        }
        units.push_back(unitOf(doc, items[k], eff));
    }
    for (int i = 0; i < int(units.size()); ++i) {
        if (i != movedUnit) {
            order.push_back(i);
        }
    }
    const int at = int(std::find(order.begin(), order.end(), targetUnit) - order.begin());
    order.insert(order.begin() + (after ? at + 1 : at), movedUnit);
    bool same = true;
    for (int i = 0; i < int(order.size()); ++i) {
        same = same && order[size_t(i)] == i;
    }
    if (same) {
        return fail(QStringLiteral("That section is already there."));
    }
    Result r;
    r.position = reorderSiblings(doc, units, order, movedUnit);
    r.ok = true;
    return r;
}

Result moveSection(QTextDocument *doc, bool markdown, int position, int delta)
{
    if (!doc) {
        return fail(QStringLiteral("No document."));
    }
    const QList<Outline::Item> items = Outline::scan(doc, markdown);
    const int s = Outline::indexAt(items, position);
    if (s < 0) {
        return fail(QStringLiteral("Put the cursor in a section first."));
    }
    const int t = delta < 0 ? previousSibling(items, s) : nextSibling(items, s);
    if (t < 0) {
        return fail(delta < 0 ? QStringLiteral("It is already the first section here.")
                              : QStringLiteral("It is already the last section here."));
    }
    return moveSectionTo(doc, markdown, items[s].position, items[t].position, delta > 0);
}

bool canChangeLevel(const QList<Outline::Item> &items, int index, int delta, bool markdown)
{
    if (index < 0 || index >= items.size() || delta == 0) {
        return false;
    }
    const int end = subtreeEnd(items, index);
    for (int k = index; k < end; ++k) {
        const int level = items[k].level + (delta < 0 ? -1 : 1);
        if (level < 1 || level > maxLevel(markdown)) {
            return false;
        }
    }
    return true;
}

Result changeLevel(QTextDocument *doc, bool markdown, int position, int delta, const LevelSetter &setLevel)
{
    if (!doc) {
        return fail(QStringLiteral("No document."));
    }
    const QList<Outline::Item> items = Outline::scan(doc, markdown);
    const int s = Outline::indexAt(items, position);
    if (s < 0) {
        return fail(QStringLiteral("Put the cursor in a section first."));
    }
    const int step = delta < 0 ? -1 : 1;
    const int end = subtreeEnd(items, s);
    for (int k = s; k < end; ++k) {
        const int level = items[k].level + step;
        if (level < 1) {
            return fail(QStringLiteral("It is already a top-level heading."));
        }
        if (level > maxLevel(markdown)) {
            return fail(QStringLiteral("Headings go down to level %1.").arg(maxLevel(markdown)));
        }
    }

    QTextCursor edit(doc);
    edit.beginEditBlock();
    for (int k = s; k < end; ++k) {
        const int level = items[k].level + step;
        const QTextBlock b = doc->findBlockByNumber(items[k].blockNumber);
        if (markdown) {
            const int hash = b.text().indexOf(QLatin1Char('#'));
            QTextCursor c(doc);
            c.setPosition(b.position() + qMax(0, hash));
            if (step > 0) {
                c.insertText(QStringLiteral("#"));
            } else {
                c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                c.removeSelectedText();
            }
        } else if (setLevel) {
            setLevel(items[k].blockNumber, level);
        } else {
            QTextCursor c(b);
            QTextBlockFormat f = c.blockFormat();
            f.setHeadingLevel(level);
            c.setBlockFormat(f);
        }
    }
    edit.endEditBlock();

    Result r;
    r.ok = true;
    r.position = items[s].position;
    return r;
}

} // namespace OutlineEdit
