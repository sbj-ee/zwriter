#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

// Markdown scanner for the editor's inline styling (Markdown mode). The markup
// stays in the text; this only classifies each character of one block (one
// line) so the highlighter can style it. No widgets, no document: the scanner
// is a pure function of the line and the state the previous line left behind
// (fenced code blocks span lines), which keeps it unit-testable.
//
// Covers: ATX headings (# .. ######), **strong** / __strong__, *emphasis* /
// _emphasis_, ***both***, `inline code`, fenced code (``` and ~~~), [links](url),
// ![images](url), <autolinks>, bare http(s) URLs, - * + and 1. / 1) list
// markers (with [ ] / [x] task boxes), > blockquotes, thematic breaks, and
// backslash escapes. Not covered: setext headings (the underline is greyed,
// the title line above is not styled), indented code blocks, reference-style
// links, HTML blocks, tables.
namespace MarkdownSyntax {

enum Flag : quint16 {
    None = 0,
    Markup = 1 << 0,     // syntax characters: #, *, _, `, [, ](, ), >, list bullets, fences
    Heading = 1 << 1,    // the whole heading line
    Strong = 1 << 2,
    Emphasis = 1 << 3,
    Code = 1 << 4,       // inline code span
    CodeBlock = 1 << 5,  // fenced code block (fence lines and contents)
    LinkText = 1 << 6,   // [this part] of a link / image
    Url = 1 << 7,        // (this part), <autolink>, bare URL
    Quote = 1 << 8,      // blockquote line
    ListMarker = 1 << 9, // "-", "1.", "[ ]" at the start of a list item
    Rule = 1 << 10,      // thematic break line (---, ***, ___)
};

// Block state carried from line to line (QSyntaxHighlighter::currentBlockState).
// 0 (or -1, Qt's "no state yet") = normal text; otherwise inside a fenced code
// block: fence character in the low bits, fence length above them.
constexpr int kStateNormal = 0;

struct BlockResult
{
    QList<quint16> flags; // one entry per character of the line
    int state = kStateNormal;
    int headingLevel = 0; // 1..6 on an ATX heading line, else 0
};

BlockResult scan(const QString &line, int previousState);

// Words touching these flags are not spell-checked (code, URLs, markup).
inline bool isSpellCheckable(quint16 flags)
{
    return (flags & (Markup | Code | CodeBlock | Url)) == 0;
}

} // namespace MarkdownSyntax
