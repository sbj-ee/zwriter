#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class SpellChecker;

class SpellHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit SpellHighlighter(QTextDocument *document, SpellChecker *checker);

    void setChecker(SpellChecker *checker);
    void refreshAll();

protected:
    void highlightBlock(const QString &text) override;

private:
    SpellChecker *m_checker = nullptr;
    QTextCharFormat m_misspelledFormat;
};
