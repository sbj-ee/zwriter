#include "SpellHighlighter.hpp"
#include "SpellChecker.hpp"

#include <QColor>
#include <QTextDocument>

SpellHighlighter::SpellHighlighter(QTextDocument *document, SpellChecker *checker)
    : QSyntaxHighlighter(document)
    , m_checker(checker)
{
    m_misspelledFormat.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    m_misspelledFormat.setUnderlineColor(QColor(200, 40, 40));
}

void SpellHighlighter::setChecker(SpellChecker *checker)
{
    m_checker = checker;
    rehighlight();
}

void SpellHighlighter::refreshAll()
{
    rehighlight();
}

void SpellHighlighter::highlightBlock(const QString &text)
{
    if (!m_checker || !m_checker->isEnabled() || !m_checker->isAvailable()) {
        return;
    }

    int i = 0;
    const int n = text.size();
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
        const QString word = text.mid(i, j - i);
        // Strip leading/trailing apostrophes for check.
        QString core = word;
        while (core.startsWith(QLatin1Char('\''))) {
            core.remove(0, 1);
        }
        while (core.endsWith(QLatin1Char('\''))) {
            core.chop(1);
        }
        if (!core.isEmpty() && !m_checker->isCorrect(core)) {
            setFormat(i, j - i, m_misspelledFormat);
        }
        i = j;
    }
}
