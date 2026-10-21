#include <QDateTime>
#include <QHash>
#include "BookManifest.hpp"

#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextDocument>

#include "DocumentIo.hpp"

#include <algorithm>

namespace {

const QStringList kKnownChapterKeys = {
    QStringLiteral("path"),     QStringLiteral("title"), QStringLiteral("status"),
    QStringLiteral("synopsis"), QStringLiteral("notes"), QStringLiteral("pov"),
    QStringLiteral("tags"),     QStringLiteral("wordTarget")};
const QStringList kKnownBookKeys = {QStringLiteral("format"), QStringLiteral("version"),
                                    QStringLiteral("title"), QStringLiteral("chapters")};

QString cleanRel(const QString &p)
{
    QString s = p.trimmed();
    s.replace(QLatin1Char('\\'), QLatin1Char('/')); // a manifest written on Windows
    return s.isEmpty() ? s : QDir::cleanPath(s);
}

// (QCollator's numeric mode depends on ICU and the locale, so not used.)
bool naturalLessImpl(const QString &a, const QString &b)
{
    int i = 0;
    int j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            int ie = i;
            int je = j;
            while (ie < a.size() && a[ie].isDigit()) {
                ++ie;
            }
            while (je < b.size() && b[je].isDigit()) {
                ++je;
            }
            QString na = a.mid(i, ie - i);
            QString nb = b.mid(j, je - j);
            while (na.size() > 1 && na.startsWith(QLatin1Char('0'))) {
                na.remove(0, 1);
            }
            while (nb.size() > 1 && nb.startsWith(QLatin1Char('0'))) {
                nb.remove(0, 1);
            }
            if (na.size() != nb.size()) {
                return na.size() < nb.size();
            }
            if (na != nb) {
                return na < nb;
            }
            i = ie;
            j = je;
            continue;
        }
        const QChar ca = a[i].toCaseFolded();
        const QChar cb = b[j].toCaseFolded();
        if (ca != cb) {
            return ca < cb;
        }
        ++i;
        ++j;
    }
    if ((a.size() - i) != (b.size() - j)) {
        return (a.size() - i) < (b.size() - j);
    }
    return a < b;
}

} // namespace

bool BookChapter::operator==(const BookChapter &o) const
{
    return path == o.path && title == o.title && status == o.status && synopsis == o.synopsis
        && notes == o.notes && pov == o.pov && tags == o.tags && wordTarget == o.wordTarget
        && extra == o.extra;
}

QString BookManifest::manifestPath(const QString &bookDir)
{
    return QDir(bookDir).filePath(fileName());
}

bool BookManifest::isKnownStatus(const QString &status)
{
    return statuses().contains(status);
}

QStringList BookManifest::statuses()
{
    return {QStringLiteral("Idea"), QStringLiteral("Outline"), QStringLiteral("Draft"), QStringLiteral("Revised"),
            QStringLiteral("Final")};
}

bool BookManifest::exists(const QString &bookDir)
{
    return !bookDir.isEmpty() && QFileInfo(manifestPath(bookDir)).isFile();
}

void BookManifest::setBookDir(const QString &dir)
{
    m_dir = dir.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
}

bool BookManifest::load(const QString &bookDir, QString *error)
{
    *this = BookManifest();
    setBookDir(bookDir);
    QFile f(manifestPath(m_dir));
    if (!f.exists()) {
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = f.errorString();
        }
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = pe.error != QJsonParseError::NoError ? pe.errorString()
                                                          : QStringLiteral("not a JSON object");
        }
        return false;
    }
    *this = fromJson(doc.object(), m_dir);
    return true;
}

bool BookManifest::save(QString *error) const
{
    if (m_dir.isEmpty()) {
        if (error) {
            *error = QStringLiteral("no book folder");
        }
        return false;
    }
    QSaveFile f(manifestPath(m_dir));
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = f.errorString();
        }
        return false;
    }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) {
            *error = f.errorString();
        }
        return false;
    }
    return true;
}

QJsonObject BookManifest::toJson() const
{
    QJsonObject obj = m_extra;
    obj.insert(QStringLiteral("format"), QStringLiteral("zwriter-book"));
    obj.insert(QStringLiteral("version"), 1);
    obj.insert(QStringLiteral("title"), m_title);
    QJsonArray arr;
    for (const BookChapter &c : m_chapters) {
        QJsonObject o = c.extra;
        o.insert(QStringLiteral("path"), c.path);
        o.insert(QStringLiteral("title"), c.title);
        o.insert(QStringLiteral("status"), c.status);
        o.insert(QStringLiteral("synopsis"), c.synopsis);
        o.insert(QStringLiteral("notes"), c.notes);
        o.insert(QStringLiteral("pov"), c.pov);
        o.insert(QStringLiteral("tags"), QJsonArray::fromStringList(c.tags));
        o.insert(QStringLiteral("wordTarget"), c.wordTarget);
        arr.append(o);
    }
    obj.insert(QStringLiteral("chapters"), arr);
    return obj;
}

BookManifest BookManifest::fromJson(const QJsonObject &obj, const QString &bookDir)
{
    BookManifest b;
    b.setBookDir(bookDir);
    b.m_title = obj.value(QStringLiteral("title")).toString();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!kKnownBookKeys.contains(it.key())) {
            b.m_extra.insert(it.key(), it.value());
        }
    }
    const QJsonArray arr = obj.value(QStringLiteral("chapters")).toArray();
    for (const QJsonValue &v : arr) {
        QJsonObject o;
        if (v.isString()) {
            o.insert(QStringLiteral("path"), v.toString()); // shorthand: just a path
        } else if (v.isObject()) {
            o = v.toObject();
        } else {
            continue;
        }
        BookChapter c;
        c.path = cleanRel(o.value(QStringLiteral("path")).toString());
        if (c.path.isEmpty() || b.indexOf(c.path) >= 0) {
            continue; // no path, or a duplicate: nothing to point at
        }
        c.title = o.value(QStringLiteral("title")).toString();
        c.status = o.value(QStringLiteral("status")).toString();
        if (c.status.trimmed().isEmpty()) {
            c.status = QStringLiteral("Draft");
        }
        // An unknown status (e.g. a hand-edited "Done") is kept as written so
        // the next save doesn't overwrite it; the sidebar marks it as unknown.
        c.synopsis = o.value(QStringLiteral("synopsis")).toString();
        c.notes = o.value(QStringLiteral("notes")).toString();
        c.pov = o.value(QStringLiteral("pov")).toString();
        const QJsonValue tags = o.value(QStringLiteral("tags"));
        if (tags.isArray()) {
            for (const QJsonValue &t : tags.toArray()) {
                const QString s = t.toString().trimmed();
                if (!s.isEmpty()) {
                    c.tags << s;
                }
            }
        } else if (tags.isString()) {
            for (const QString &s : tags.toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                if (!s.trimmed().isEmpty()) {
                    c.tags << s.trimmed();
                }
            }
        }
        c.wordTarget = std::max(0, o.value(QStringLiteral("wordTarget")).toInt(0));
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (!kKnownChapterKeys.contains(it.key())) {
                c.extra.insert(it.key(), it.value());
            }
        }
        b.m_chapters.append(c);
    }
    return b;
}

void BookManifest::setChapter(int i, const BookChapter &c)
{
    if (i >= 0 && i < count()) {
        m_chapters[i] = c;
    }
}

QString BookManifest::relativePath(const QString &path) const
{
    if (QFileInfo(path).isAbsolute()) {
        return cleanRel(QDir(m_dir).relativeFilePath(path));
    }
    return cleanRel(path);
}

bool BookManifest::isInsideBook(const QString &rel)
{
    const QString p = cleanRel(rel);
    if (p.isEmpty() || p == QLatin1String(".") || p.startsWith(QLatin1Char('/'))
        || QFileInfo(p).isAbsolute() || (p.size() >= 2 && p.at(1) == QLatin1Char(':'))) {
        return false;
    }
    const QStringList parts = p.split(QLatin1Char('/'));
    return !parts.contains(QStringLiteral(".."));
}

bool BookManifest::isOutside(int i) const
{
    return i >= 0 && i < count() && !isInsideBook(m_chapters.at(i).path);
}

QString BookManifest::absolutePath(int i) const
{
    if (i < 0 || i >= count() || isOutside(i)) {
        return {};
    }
    return QDir::cleanPath(QDir(m_dir).absoluteFilePath(m_chapters.at(i).path));
}

bool BookManifest::isMissing(int i) const
{
    const QString abs = absolutePath(i);
    return abs.isEmpty() || !QFileInfo(abs).isFile();
}

int BookManifest::indexOf(const QString &path) const
{
    const QString rel = relativePath(path);
    for (int i = 0; i < count(); ++i) {
        if (m_chapters.at(i).path == rel) {
            return i;
        }
    }
    return -1;
}

QString BookManifest::displayTitle(int i) const
{
    const BookChapter &c = m_chapters.at(i);
    if (!c.title.trimmed().isEmpty()) {
        return c.title;
    }
    // "01 Introduction.md" shows as "Introduction": the sidebar numbers the
    // chapters itself, in book order.
    const QString base = QFileInfo(c.path).completeBaseName();
    static const QRegularExpression prefix(QStringLiteral(R"(^\d+[\s._-]+(?=\S))"));
    QString t = base;
    t.remove(prefix);
    return t.isEmpty() ? base : t;
}

int BookManifest::add(const QString &path, int at)
{
    const QString rel = relativePath(path);
    const QString suffix = QFileInfo(rel).suffix().toLower();
    if (!isInsideBook(rel) || !isChapterSuffix(suffix)) {
        return -1;
    }
    const int existing = indexOf(rel);
    if (existing >= 0) {
        return existing;
    }
    BookChapter c;
    c.path = rel;
    c.status = QStringLiteral("Draft");
    if (at < 0 || at > count()) {
        at = count();
    }
    m_chapters.insert(at, c);
    return at;
}

bool BookManifest::remove(int i)
{
    if (i < 0 || i >= count()) {
        return false;
    }
    m_chapters.removeAt(i);
    return true;
}

bool BookManifest::move(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to) {
        return false;
    }
    m_chapters.move(from, to);
    return true;
}

void BookManifest::reorder(const QStringList &relativePaths)
{
    QVector<BookChapter> next;
    QVector<bool> used(count(), false);
    for (const QString &p : relativePaths) {
        const int i = indexOf(p);
        if (i >= 0 && !used[i]) {
            used[i] = true;
            next.append(m_chapters.at(i));
        }
    }
    for (int i = 0; i < count(); ++i) {
        if (!used[i]) {
            next.append(m_chapters.at(i));
        }
    }
    m_chapters = next;
}

BookManifest BookManifest::fromFolder(const QString &bookDir)
{
    BookManifest b;
    b.setBookDir(bookDir);
    b.m_title = QFileInfo(b.m_dir).fileName();
    QStringList files;
    QDirIterator it(b.m_dir, chapterNameFilters(),
                    QDir::Files); // top level only: images/, notes/ etc. are not chapters
    const QDir root(b.m_dir);
    while (it.hasNext()) {
        const QString abs = it.next();
        if (!it.fileInfo().isFile()) {
            continue;
        }
        const QString rel = root.relativeFilePath(abs);
        bool hidden = false;
        for (const QString &part : rel.split(QLatin1Char('/'))) {
            hidden = hidden || part.startsWith(QLatin1Char('.'));
        }
        if (!hidden) {
            files << rel;
        }
    }
    std::sort(files.begin(), files.end(), naturalLessImpl);
    for (const QString &f : files) {
        b.add(f);
    }
    return b;
}

int countWords(const QString &text)
{
    int n = 0;
    bool inWord = false;
    bool hasAlnum = false;
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            if (inWord && hasAlnum) {
                ++n;
            }
            inWord = false;
            hasAlnum = false;
        } else {
            inWord = true;
            hasAlnum = hasAlnum || ch.isLetterOrNumber();
        }
    }
    if (inWord && hasAlnum) {
        ++n;
    }
    return n;
}

QStringList BookManifest::chapterNameFilters()
{
    return {QStringLiteral("*.md"), QStringLiteral("*.txt"), QStringLiteral("*.odt")};
}

bool BookManifest::isChapterSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    return s == QLatin1String("md") || s == QLatin1String("txt") || s == QLatin1String("odt");
}

namespace {
// Word counts by file, reused while the file's size and time are unchanged:
// the sidebar recounts every chapter on each refresh, and loading an ODT
// (unzip, pictures) for each one made the whole window lag.
struct CachedCount {
    QDateTime modified;
    qint64 size = -1;
    int words = -1;
};
QHash<QString, CachedCount> &wordCache()
{
    static QHash<QString, CachedCount> cache;
    return cache;
}
int g_fileCounts = 0;
int countWordsUncached(const QString &path);
} // namespace

int wordCountFileReads()
{
    return g_fileCounts;
}

int countWordsInFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        wordCache().remove(path);
        return -1;
    }
    auto it = wordCache().constFind(path);
    if (it != wordCache().cend() && it->modified == info.lastModified() && it->size == info.size()) {
        return it->words;
    }
    const int words = countWordsUncached(path);
    wordCache().insert(path, {info.lastModified(), info.size(), words});
    return words;
}

namespace {
int countWordsUncached(const QString &path)
{
    ++g_fileCounts;
    if (QFileInfo(path).suffix().compare(QLatin1String("odt"), Qt::CaseInsensitive) == 0) {
        // The editor's own ODT load path (content.xml text), so the count
        // matches what opens.
        if (!QFileInfo(path).isFile()) {
            return -1;
        }
        QTextDocument doc;
        if (!DocumentIo::load(&doc, path)) {
            return -1;
        }
        return countWords(doc.toPlainText());
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return -1;
    }
    return countWords(QString::fromUtf8(f.readAll()));
}
} // namespace

bool BookManifest::naturalLess(const QString &a, const QString &b)
{
    return naturalLessImpl(a, b);
}
