#include "SpellChecker.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#ifdef ZWRITER_HAS_HUNSPELL
#  include <hunspell/hunspell.hxx>
#endif

namespace {

QStringList dictionarySearchPaths()
{
    QStringList paths;
#ifdef Q_OS_MACOS
    // zwriter.app ships en_US in Contents/Resources/hunspell; prefer it.
    paths << QDir(QCoreApplication::applicationDirPath())
                 .absoluteFilePath(QStringLiteral("../Resources/hunspell"));
#endif
    paths << QStringLiteral("/usr/share/hunspell")
          << QStringLiteral("/usr/share/myspell/dicts")
          << QStringLiteral("/usr/local/share/hunspell")
          << QStringLiteral("/opt/homebrew/share/hunspell")
          << QStringLiteral("/usr/local/share/myspell");
#ifdef Q_OS_MAC
    paths << QStringLiteral("/opt/homebrew/share/hunspell")
          << QStringLiteral("/usr/local/share/hunspell");
#endif
    return paths;
}

QString findDictBase(const QString &lang)
{
    for (const QString &dir : dictionarySearchPaths()) {
        const QString aff = dir + QLatin1Char('/') + lang + QStringLiteral(".aff");
        const QString dic = dir + QLatin1Char('/') + lang + QStringLiteral(".dic");
        if (QFileInfo::exists(aff) && QFileInfo::exists(dic)) {
            return dir + QLatin1Char('/') + lang;
        }
    }
    return {};
}

} // namespace

SpellChecker::SpellChecker(QObject *parent)
    : QObject(parent)
{
#ifdef ZWRITER_HAS_HUNSPELL
    m_dictId = QStringLiteral("en_US");
    const QString base = findDictBase(m_dictId);
    if (!base.isEmpty()) {
        m_hunspell = new Hunspell(QString(base + QStringLiteral(".aff")).toLocal8Bit().constData(),
                                  QString(base + QStringLiteral(".dic")).toLocal8Bit().constData());
        loadUserDictionary();
    }
#else
    m_dictId = QStringLiteral("en_US");
#endif
}

SpellChecker::~SpellChecker()
{
#ifdef ZWRITER_HAS_HUNSPELL
    delete m_hunspell;
    m_hunspell = nullptr;
#endif
}

bool SpellChecker::isAvailable() const
{
#ifdef ZWRITER_HAS_HUNSPELL
    return m_hunspell != nullptr;
#else
    return false;
#endif
}

QString SpellChecker::dictionaryId() const
{
    return m_dictId;
}

void SpellChecker::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

QString SpellChecker::normalizeWord(const QString &word)
{
    return word.trimmed();
}

bool SpellChecker::isCorrect(const QString &word) const
{
    if (!m_enabled) {
        return true;
    }
    const QString w = normalizeWord(word);
    if (w.isEmpty() || w.length() == 1) {
        return true;
    }
    // Skip pure digits / punctuation-only.
    bool hasLetter = false;
    for (const QChar &ch : w) {
        if (ch.isLetter()) {
            hasLetter = true;
            break;
        }
    }
    if (!hasLetter) {
        return true;
    }
    if (m_ignored.contains(w.toLower())) {
        return true;
    }
#ifdef ZWRITER_HAS_HUNSPELL
    if (!m_hunspell) {
        return true;
    }
    const QByteArray utf8 = w.toUtf8();
    return m_hunspell->spell(std::string(utf8.constData(), utf8.size()));
#else
    Q_UNUSED(w);
    return true;
#endif
}

QStringList SpellChecker::suggestions(const QString &word, int maxSuggestions) const
{
    QStringList out;
#ifdef ZWRITER_HAS_HUNSPELL
    if (!m_hunspell || !m_enabled) {
        return out;
    }
    const QByteArray utf8 = normalizeWord(word).toUtf8();
    const std::vector<std::string> raw =
        m_hunspell->suggest(std::string(utf8.constData(), utf8.size()));
    for (const std::string &s : raw) {
        out.append(QString::fromUtf8(s.data(), int(s.size())));
        if (out.size() >= maxSuggestions) {
            break;
        }
    }
#else
    Q_UNUSED(word);
    Q_UNUSED(maxSuggestions);
#endif
    return out;
}

void SpellChecker::ignoreWord(const QString &word)
{
    const QString w = normalizeWord(word);
    if (!w.isEmpty()) {
        m_ignored.insert(w.toLower());
    }
}

void SpellChecker::addToUserDictionary(const QString &word)
{
    const QString w = normalizeWord(word);
    if (w.isEmpty()) {
        return;
    }
#ifdef ZWRITER_HAS_HUNSPELL
    if (m_hunspell) {
        const QByteArray utf8 = w.toUtf8();
        m_hunspell->add(std::string(utf8.constData(), utf8.size()));
    }
#endif
    m_ignored.remove(w.toLower()); // no longer merely ignored
    appendUserDictionary(w);
}

QString SpellChecker::userDictionaryPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/user-dictionary.txt");
}

void SpellChecker::loadUserDictionary()
{
    QFile f(userDictionaryPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString w = normalizeWord(in.readLine());
        if (w.isEmpty()) {
            continue;
        }
#ifdef ZWRITER_HAS_HUNSPELL
        if (m_hunspell) {
            const QByteArray utf8 = w.toUtf8();
            m_hunspell->add(std::string(utf8.constData(), utf8.size()));
        }
#else
        Q_UNUSED(w);
#endif
    }
}

void SpellChecker::appendUserDictionary(const QString &word)
{
    QFile f(userDictionaryPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&f);
    out << word << QLatin1Char('\n');
}
