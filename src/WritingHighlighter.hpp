#pragma once

#include "Theme.hpp"

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <functional>

// The editor's one QSyntaxHighlighter. A document can only usefully carry one
// (a second highlighter's formats replace the first's), so spell-check
// underlines and Markdown inline styling live in the same class:
//   - ODT / TXT / RTF documents: spell check only (as before).
//   - Markdown mode: headings, **strong**, *emphasis*, `code`, fenced code,
//     links, lists and blockquotes are styled in place while the markup stays
//     in the text, drawn in the theme's quieter "markup" colour. Misspellings
//     are underlined except inside code, URLs and markup.
class WritingHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    // Returns true when a word should get the misspelling underline. An
    // empty function turns spell check off.
    using MisspelledFn = std::function<bool(const QString &word)>;

    explicit WritingHighlighter(QTextDocument *document);

    void setMisspelledFunction(MisspelledFn fn);
    void setMarkdownEnabled(bool on);
    bool markdownEnabled() const { return m_markdown; }
    void setColors(const ThemeColors &colors);
    void refreshAll();

protected:
    void highlightBlock(const QString &text) override;

private:
    void highlightMarkdown(const QString &text, QList<quint16> *flags);
    void highlightSpelling(const QString &text, const QList<quint16> *flags);

    MisspelledFn m_misspelled;
    bool m_markdown = false;
    QColor m_markup;
    QColor m_code;
    QColor m_link;
    QColor m_quote;
};
