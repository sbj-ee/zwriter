#include "MarkdownSyntax.hpp"

#include <QString>
#include <QRegularExpression>

namespace MarkdownSyntax {
namespace {

constexpr int kFenceBacktick = 1;
constexpr int kFenceTilde = 2;

void mark(QList<quint16> &flags, int from, int to, quint16 bits)
{
    from = qMax(0, from);
    to = qMin(int(flags.size()), to);
    for (int i = from; i < to; ++i) {
        flags[i] |= bits;
    }
}

void protect(QList<bool> &mask, int from, int to)
{
    from = qMax(0, from);
    to = qMin(int(mask.size()), to);
    for (int i = from; i < to; ++i) {
        mask[i] = true;
    }
}

int countSpaces(const QString &s, int from)
{
    int i = from;
    while (i < s.size() && s.at(i) == QLatin1Char(' ')) {
        ++i;
    }
    return i - from;
}

int runLength(const QString &s, int from, int to, QChar ch)
{
    int i = from;
    while (i < to && s.at(i) == ch) {
        ++i;
    }
    return i - from;
}

bool isAsciiPunct(QChar c)
{
    return QLatin1String("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~").contains(c);
}

// A fence line: up to three spaces, then three or more ` or ~. A backtick
// fence's info string may not contain a backtick (that is inline code).
bool fenceAt(const QString &line, QChar *ch, int *len, int *after)
{
    const int indent = countSpaces(line, 0);
    if (indent > 3 || indent >= line.size()) {
        return false;
    }
    const QChar c = line.at(indent);
    if (c != QLatin1Char('`') && c != QLatin1Char('~')) {
        return false;
    }
    const int n = runLength(line, indent, int(line.size()), c);
    if (n < 3) {
        return false;
    }
    if (c == QLatin1Char('`') && line.indexOf(QLatin1Char('`'), indent + n) >= 0) {
        return false;
    }
    *ch = c;
    *len = n;
    *after = indent + n;
    return true;
}

int findClosingBracket(const QString &s, int open, int to)
{
    int depth = 0;
    for (int j = open + 1; j < to; ++j) {
        const QChar c = s.at(j);
        if (c == QLatin1Char('\\')) {
            ++j;
        } else if (c == QLatin1Char('[')) {
            ++depth;
        } else if (c == QLatin1Char(']')) {
            if (depth == 0) {
                return j;
            }
            --depth;
        }
    }
    return -1;
}

int findClosingParen(const QString &s, int open, int to)
{
    int depth = 0;
    for (int j = open + 1; j < to; ++j) {
        const QChar c = s.at(j);
        if (c == QLatin1Char('\\')) {
            ++j;
        } else if (c == QLatin1Char('(')) {
            ++depth;
        } else if (c == QLatin1Char(')')) {
            if (depth == 0) {
                return j;
            }
            --depth;
        }
    }
    return -1;
}

// Code spans, escapes, links and URLs. Their syntax characters are marked
// and "protected" so the emphasis pass never pairs a * or _ inside them.
void scanTokens(const QString &s, int from, int to, QList<quint16> &f, QList<bool> &prot)
{
    static const QRegularExpression autolink(
        QStringLiteral(R"(<(?:(?:https?|ftp)://[^\s<>]+|mailto:[^\s<>]+)>)"));
    static const QRegularExpression bareUrl(
        QStringLiteral(R"(https?://[^\s<>()\[\]]*[^\s<>()\[\].,;:!?'"*_])"));

    int i = from;
    while (i < to) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\') && i + 1 < to && isAsciiPunct(s.at(i + 1))) {
            mark(f, i, i + 1, Markup);
            protect(prot, i, i + 2);
            i += 2;
            continue;
        }
        if (c == QLatin1Char('`')) {
            const int n = runLength(s, i, to, c);
            int close = -1;
            for (int j = i + n; j < to;) {
                if (s.at(j) == QLatin1Char('`')) {
                    const int m = runLength(s, j, to, QLatin1Char('`'));
                    if (m == n) {
                        close = j;
                        break;
                    }
                    j += m;
                } else {
                    ++j;
                }
            }
            if (close >= 0) {
                mark(f, i, i + n, Markup | Code);
                mark(f, i + n, close, Code);
                mark(f, close, close + n, Markup | Code);
                protect(prot, i, close + n);
                i = close + n;
            } else {
                protect(prot, i, i + n); // an unmatched run is literal text
                i += n;
            }
            continue;
        }
        if (c == QLatin1Char('<')) {
            const QRegularExpressionMatch m = autolink.match(
                s, i, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
            if (m.hasMatch() && m.capturedEnd() <= to) {
                const int end = int(m.capturedEnd());
                mark(f, i, end, Url);
                mark(f, i, i + 1, Markup);
                mark(f, end - 1, end, Markup);
                protect(prot, i, end);
                i = end;
                continue;
            }
        }
        if (c == QLatin1Char('h') && (i == from || !s.at(i - 1).isLetterOrNumber())) {
            const QRegularExpressionMatch m = bareUrl.match(
                s, i, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
            if (m.hasMatch() && m.capturedEnd() <= to) {
                const int end = int(m.capturedEnd());
                mark(f, i, end, Url);
                protect(prot, i, end);
                i = end;
                continue;
            }
        }
        if (c == QLatin1Char('[')
            || (c == QLatin1Char('!') && i + 1 < to && s.at(i + 1) == QLatin1Char('['))) {
            const int open = c == QLatin1Char('!') ? i + 1 : i;
            const int close = findClosingBracket(s, open, to);
            if (close > open && close + 1 < to && s.at(close + 1) == QLatin1Char('(')) {
                const int paren = findClosingParen(s, close + 1, to);
                if (paren > 0) {
                    mark(f, i, open + 1, Markup);
                    mark(f, open + 1, close, LinkText);
                    mark(f, close, close + 2, Markup);
                    mark(f, close + 2, paren, Url);
                    mark(f, paren, paren + 1, Markup);
                    protect(prot, i, open + 1);
                    protect(prot, close, paren + 1);
                    scanTokens(s, open + 1, close, f, prot); // code / escapes inside the text
                    i = paren + 1;
                    continue;
                }
            }
        }
        ++i;
    }
}

struct DelimiterRun
{
    int pos = 0;
    int len = 0;
    QChar ch;
    bool canOpen = false;
    bool canClose = false;
    bool used = false;
};

// *emphasis*, **strong**, ***both*** (and the _ forms): runs pair with a run
// of the same character and the same length. A run opens when followed by
// non-space and closes when preceded by non-space; _ never opens or closes
// inside a word (snake_case_names stay plain).
void scanEmphasis(const QString &s, int from, int to, QList<quint16> &f, const QList<bool> &prot)
{
    QList<DelimiterRun> runs;
    for (int i = from; i < to;) {
        const QChar c = s.at(i);
        if ((c != QLatin1Char('*') && c != QLatin1Char('_')) || prot.at(i)) {
            ++i;
            continue;
        }
        int j = i;
        while (j < to && s.at(j) == c && !prot.at(j)) {
            ++j;
        }
        DelimiterRun r;
        r.pos = i;
        r.len = j - i;
        r.ch = c;
        const bool hasAfter = j < to;
        const bool hasBefore = i > from;
        r.canOpen = hasAfter && !s.at(j).isSpace();
        r.canClose = hasBefore && !s.at(i - 1).isSpace();
        if (c == QLatin1Char('_')) {
            if (i > 0 && s.at(i - 1).isLetterOrNumber()) {
                r.canOpen = false;
            }
            if (j < s.size() && s.at(j).isLetterOrNumber()) {
                r.canClose = false;
            }
        }
        runs.append(r);
        i = j;
    }

    for (int len = 3; len >= 1; --len) {
        const quint16 style = len == 3 ? quint16(Strong | Emphasis) : len == 2 ? quint16(Strong)
                                                                           : quint16(Emphasis);
        for (int a = 0; a < runs.size(); ++a) {
            DelimiterRun &open = runs[a];
            if (open.used || open.len != len || !open.canOpen) {
                continue;
            }
            for (int b = a + 1; b < runs.size(); ++b) {
                DelimiterRun &close = runs[b];
                if (close.used || close.ch != open.ch || close.len != len || !close.canClose
                    || close.pos < open.pos + len + 1) {
                    continue;
                }
                open.used = close.used = true;
                mark(f, open.pos, close.pos + len, style);
                mark(f, open.pos, open.pos + len, Markup);
                mark(f, close.pos, close.pos + len, Markup);
                break;
            }
        }
    }
}

void scanInline(const QString &s, int from, int to, QList<quint16> &f)
{
    if (from >= to) {
        return;
    }
    QList<bool> prot(s.size(), false);
    scanTokens(s, from, to, f, prot);
    scanEmphasis(s, from, to, f, prot);
}

} // namespace

BlockResult scan(const QString &line, int previousState)
{
    BlockResult r;
    const int n = int(line.size());
    r.flags = QList<quint16>(n, quint16(None));

    QChar fenceChar;
    int fenceLen = 0;
    int afterFence = 0;
    const bool isFence = fenceAt(line, &fenceChar, &fenceLen, &afterFence);

    // Inside a fenced code block: everything is code until the closing fence.
    if (previousState > kStateNormal) {
        const QChar want = (previousState & 3) == kFenceBacktick ? QLatin1Char('`') : QLatin1Char('~');
        const int wantLen = previousState >> 2;
        if (isFence && fenceChar == want && fenceLen >= wantLen
            && line.mid(afterFence).trimmed().isEmpty()) {
            mark(r.flags, 0, n, Markup | CodeBlock);
            r.state = kStateNormal;
        } else {
            mark(r.flags, 0, n, CodeBlock);
            r.state = previousState;
        }
        return r;
    }
    if (isFence) {
        mark(r.flags, 0, n, Markup | CodeBlock);
        r.state = (fenceLen << 2) | (fenceChar == QLatin1Char('`') ? kFenceBacktick : kFenceTilde);
        return r;
    }

    static const QRegularExpression rule(QStringLiteral(R"(^ {0,3}([-*_])(?:[ \t]*\1){2,}[ \t]*$)"));
    if (rule.match(line).hasMatch()) {
        mark(r.flags, 0, n, Markup | Rule);
        return r;
    }
    static const QRegularExpression setextUnderline(QStringLiteral(R"(^ {0,3}=+[ \t]*$)"));
    if (setextUnderline.match(line).hasMatch()) {
        mark(r.flags, 0, n, Markup);
        return r;
    }

    static const QRegularExpression heading(QStringLiteral(R"(^ {0,3}(#{1,6})(?:[ \t]+|$))"));
    const QRegularExpressionMatch h = heading.match(line);
    if (h.hasMatch()) {
        r.headingLevel = int(h.capturedLength(1));
        const int textStart = int(h.capturedEnd());
        int textEnd = n;
        static const QRegularExpression closing(QStringLiteral(R"((?:^|[ \t]+)#+[ \t]*$)"));
        const QRegularExpressionMatch c = closing.match(line, textStart);
        if (c.hasMatch()) {
            textEnd = int(c.capturedStart());
        }
        mark(r.flags, 0, n, Heading);
        mark(r.flags, 0, textStart, Markup);
        mark(r.flags, textEnd, n, Markup);
        scanInline(line, textStart, textEnd, r.flags);
        return r;
    }

    int pos = 0;
    bool quote = false;
    while (pos < n) {
        const int spaces = countSpaces(line, pos);
        if (spaces > 3 || pos + spaces >= n || line.at(pos + spaces) != QLatin1Char('>')) {
            break;
        }
        quote = true;
        mark(r.flags, pos, pos + spaces + 1, Markup);
        pos += spaces + 1;
        if (pos < n && line.at(pos) == QLatin1Char(' ')) {
            ++pos;
        }
    }
    if (quote) {
        mark(r.flags, 0, n, Quote);
    }

    static const QRegularExpression listMarker(QStringLiteral(R"([ \t]*([-*+]|\d{1,9}[.)])(?:[ \t]+|$))"));
    const QRegularExpressionMatch lm = listMarker.match(
        line, pos, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    if (lm.hasMatch()) {
        mark(r.flags, int(lm.capturedStart(1)), int(lm.capturedEnd(1)), Markup | ListMarker);
        pos = int(lm.capturedEnd());
        static const QRegularExpression task(QStringLiteral(R"(\[[ xX]\](?=[ \t]|$))"));
        const QRegularExpressionMatch tm = task.match(
            line, pos, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        if (tm.hasMatch()) {
            mark(r.flags, pos, int(tm.capturedEnd()), Markup | ListMarker);
            pos = int(tm.capturedEnd());
        }
    }

    scanInline(line, pos, n, r.flags);
    return r;
}

} // namespace MarkdownSyntax
