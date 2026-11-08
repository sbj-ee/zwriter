#include "Outline.hpp"

#include "DocumentIo.hpp"
#include "MarkdownSyntax.hpp"

#include <QTextBlock>
#include <QTextDocument>

#include <vector>

namespace Outline {

int wordCount(QStringView text)
{
    int words = 0;
    bool counted = false; // this whitespace-separated run already counted
    for (const QChar c : text) {
        if (c.isSpace()) {
            counted = false;
            continue;
        }
        if (!counted && c.isLetterOrNumber()) {
            counted = true;
            ++words;
        }
    }
    return words;
}

namespace {

bool isGeneratedToc(const QTextBlock &b)
{
    const QTextBlockFormat f = b.blockFormat();
    return f.hasProperty(DocumentIo::GeneratedTocProperty) || f.hasProperty(DocumentIo::GeneratedTocTitleProperty);
}

} // namespace

QList<Item> scan(const QTextDocument *doc, bool markdown)
{
    QList<Item> items;
    if (!doc) {
        return items;
    }
    // Words per block (heading lines and the generated ToC count for nothing),
    // as running totals so a section's words are one subtraction.
    std::vector<int> prefix;
    prefix.reserve(size_t(doc->blockCount()) + 1);
    prefix.push_back(0);

    int mdState = MarkdownSyntax::kStateNormal;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString text = b.text();
        int level = 0;
        QString heading;
        int words = 0;
        if (markdown) {
            const MarkdownSyntax::BlockResult r = MarkdownSyntax::scan(text, mdState);
            mdState = r.state;
            if (r.headingLevel > 0) {
                level = r.headingLevel;
                heading = text.mid(r.headingTextStart, r.headingTextEnd - r.headingTextStart).simplified();
            } else {
                words = wordCount(text);
            }
        } else if (isGeneratedToc(b)) {
            words = 0;
        } else {
            level = b.blockFormat().headingLevel();
            if (level > 0) {
                heading = text.simplified();
            } else {
                words = wordCount(text);
            }
        }
        if (level > 0 && !heading.isEmpty()) {
            Item item;
            item.level = level;
            item.text = heading;
            item.blockNumber = b.blockNumber();
            item.position = b.position();
            items.append(item);
            words = 0;
        } else if (level > 0) {
            words = 0; // an empty heading: neither an entry nor body text
        }
        prefix.push_back(prefix.back() + words);
    }

    // Section ends: a heading ends every open section of the same or a deeper
    // level. What is left open runs to the end of the document.
    const int blockCount = int(prefix.size()) - 1;
    std::vector<int> open; // indices into items, levels strictly increasing
    for (int i = 0; i < items.size(); ++i) {
        while (!open.empty() && items[open.back()].level >= items[i].level) {
            items[open.back()].endBlock = items[i].blockNumber;
            open.pop_back();
        }
        open.push_back(i);
    }
    for (const int i : open) {
        items[i].endBlock = blockCount;
    }
    for (Item &item : items) {
        item.words = prefix[size_t(item.endBlock)] - prefix[size_t(item.blockNumber) + 1];
    }
    return items;
}

int indexAt(const QList<Item> &items, int position)
{
    int lo = 0;
    int hi = int(items.size()); // first item that starts after position
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (items[mid].position <= position) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo - 1;
}

} // namespace Outline
