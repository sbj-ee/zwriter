#include "WritingHighlighter.hpp"
#include "MarkdownSyntax.hpp"

#include <QColor>
#include <QFont>
#include <QTextDocument>

WritingHighlighter::WritingHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    setColors(Theme::colors(QStringLiteral("paper")));
}

void WritingHighlighter::rehighlightKeepingModified()
{
    // QSyntaxHighlighter::rehighlight() runs inside an edit block, so Qt flips
    // the document's modified flag (and emits textChanged) although only
    // formats changed. Restyling is not an edit: put the flag back, or an
    // untouched document asks "Save changes?" on close after e.g. a spell
    // check toggle.
    QTextDocument *doc = document();
    const bool wasModified = doc && doc->isModified();
    m_rehighlighting = true;
    rehighlight();
    m_rehighlighting = false;
    if (doc && doc->isModified() != wasModified) {
        doc->setModified(wasModified);
    }
}

void WritingHighlighter::setMisspelledFunction(MisspelledFn fn)
{
    m_misspelled = std::move(fn);
    rehighlightKeepingModified();
}

void WritingHighlighter::setMarkdownEnabled(bool on)
{
    if (m_markdown == on) {
        return;
    }
    m_markdown = on;
    rehighlightKeepingModified();
}

void WritingHighlighter::setColors(const ThemeColors &colors)
{
    if (m_markup == colors.markup && m_code == colors.code && m_link == colors.link
        && m_quote == colors.quote) {
        return;
    }
    m_markup = colors.markup;
    m_code = colors.code;
    m_link = colors.link;
    m_quote = colors.quote;
    if (m_markdown) {
        rehighlightKeepingModified();
    }
}

void WritingHighlighter::refreshAll()
{
    rehighlightKeepingModified();
}

void WritingHighlighter::highlightBlock(const QString &text)
{
    if (!m_markdown) {
        setCurrentBlockState(MarkdownSyntax::kStateNormal);
        highlightSpelling(text, nullptr);
        return;
    }
    QList<quint16> flags;
    highlightMarkdown(text, &flags);
    highlightSpelling(text, &flags);
}

void WritingHighlighter::highlightMarkdown(const QString &text, QList<quint16> *flagsOut)
{
    using namespace MarkdownSyntax;
    const int previous = previousBlockState() < 0 ? kStateNormal : previousBlockState();
    const BlockResult r = scan(text, previous);
    setCurrentBlockState(r.state);
    *flagsOut = r.flags;

    // One setFormat() per run of identical flags.
    const int n = int(text.size());
    int i = 0;
    while (i < n) {
        const quint16 f = r.flags.at(i);
        int j = i + 1;
        while (j < n && r.flags.at(j) == f) {
            ++j;
        }
        if (f != None) {
            QTextCharFormat fmt;
            if (f & (Heading | Strong)) {
                fmt.setFontWeight(QFont::Bold);
            }
            if (f & Emphasis) {
                fmt.setFontItalic(true);
            }
            if (f & Quote) {
                fmt.setForeground(m_quote);
            }
            if (f & LinkText) {
                fmt.setForeground(m_link);
            }
            if (f & (Code | CodeBlock)) {
                fmt.setForeground(m_code);
            }
            // Markup and URLs recede: visible, but quieter than the prose.
            if (f & (Markup | Url)) {
                fmt.setForeground(m_markup);
            }
            setFormat(i, j - i, fmt);
        }
        i = j;
    }
}

void WritingHighlighter::highlightSpelling(const QString &text, const QList<quint16> *flags)
{
    if (!m_misspelled) {
        return;
    }
    const int n = int(text.size());
    int i = 0;
    while (i < n) {
        while (i < n && !text.at(i).isLetter() && text.at(i) != QLatin1Char('\'')) {
            ++i;
        }
        if (i >= n) {
            break;
        }
        int j = i;
        while (j < n && (text.at(j).isLetter() || text.at(j) == QLatin1Char('\''))) {
            ++j;
        }
        bool checkable = true;
        if (flags) {
            for (int k = i; k < j && checkable; ++k) {
                checkable = MarkdownSyntax::isSpellCheckable(flags->at(k));
            }
        }
        // Strip leading/trailing apostrophes for the check.
        int a = i;
        int b = j;
        while (a < b && text.at(a) == QLatin1Char('\'')) {
            ++a;
        }
        while (b > a && text.at(b - 1) == QLatin1Char('\'')) {
            --b;
        }
        if (checkable && a < b && m_misspelled(text.mid(a, b - a))) {
            // Keep the Markdown styling of the word; add the underline on top.
            for (int k = i; k < j; ++k) {
                QTextCharFormat fmt = format(k);
                fmt.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
                fmt.setUnderlineColor(QColor(200, 40, 40));
                setFormat(k, 1, fmt);
            }
        }
        i = j;
    }
}
