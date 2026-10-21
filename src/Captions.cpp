#include "Captions.hpp"

#include "ParagraphStyles.hpp"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace Captions {
namespace {
const QRegularExpression &labelRe()
{
    static const QRegularExpression re(QStringLiteral(R"(^Figure \d+(?:\.\d+)?\.\s)"));
    return re;
}
} // namespace

bool isCaption(const QTextBlock &block)
{
    return ParagraphStyles::styleOf(block.blockFormat()) == QLatin1String("Caption");
}

QString label(int chapter, int n)
{
    return chapter > 0 ? QStringLiteral("Figure %1.%2. ").arg(chapter).arg(n) : QStringLiteral("Figure %1. ").arg(n);
}

int labelLength(const QTextBlock &block)
{
    const QRegularExpressionMatch m = labelRe().match(block.text());
    return m.hasMatch() ? int(m.capturedLength(0)) : 0;
}

int count(const QTextDocument *doc)
{
    int n = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        n += isCaption(b) ? 1 : 0;
    }
    return n;
}

int renumber(QTextDocument *doc, int chapter, bool joinPrevious)
{
    int n = 0;
    int changed = 0;
    QTextCursor edit(doc);
    bool open = false;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (!isCaption(b)) {
            continue;
        }
        ++n;
        const QString want = label(chapter, n);
        const int len = labelLength(b);
        if (b.text().left(len) == want) {
            continue;
        }
        if (!open) {
            if (joinPrevious) {
                edit.joinPreviousEditBlock();
            } else {
                edit.beginEditBlock();
            }
            open = true;
        }
        QTextCursor c(b);
        c.setPosition(b.position() + len, QTextCursor::KeepAnchor);
        // The label takes the look of the caption text after it.
        QTextCursor after(b);
        after.setPosition(b.position() + len);
        if (len < b.length() - 1) {
            after.movePosition(QTextCursor::NextCharacter);
        }
        const QTextCharFormat f = len > 0 ? c.charFormat() : after.charFormat();
        c.insertText(want, f);
        ++changed;
    }
    if (open) {
        edit.endEditBlock();
    }
    return changed;
}

} // namespace Captions
