#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class Hunspell;

// Thin Hunspell wrapper (en_US default). No-op when Hunspell is unavailable.
class SpellChecker : public QObject
{
    Q_OBJECT

public:
    explicit SpellChecker(QObject *parent = nullptr);
    ~SpellChecker() override;

    bool isAvailable() const;
    QString dictionaryId() const; // e.g. "en_US"

    bool isCorrect(const QString &word) const;
    QStringList suggestions(const QString &word, int maxSuggestions = 8) const;

    void ignoreWord(const QString &word);          // session only
    void addToUserDictionary(const QString &word); // persists

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

private:
    void loadUserDictionary();
    void appendUserDictionary(const QString &word);
    QString userDictionaryPath() const;
    static QString normalizeWord(const QString &word);

    Hunspell *m_hunspell = nullptr;
    QString m_dictId;
    QSet<QString> m_ignored; // lower-cased
    bool m_enabled = true;
};
