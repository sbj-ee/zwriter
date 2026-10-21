#include "BookToc.hpp"

#include "BookManifest.hpp"
#include "DocumentIo.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace BookToc {
namespace {

struct Cached {
    QDateTime modified;
    qint64 size = -1;
    QList<QPair<int, QString>> headings;
};

QList<QPair<int, QString>> readHeadings(const QString &path)
{
    QList<QPair<int, QString>> out;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("odt")) {
        QTextDocument doc;
        if (!DocumentIo::load(&doc, path)) {
            return out;
        }
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
            const int level = b.blockFormat().headingLevel();
            const QString text = b.text().simplified();
            if (level > 0 && !text.isEmpty()) {
                out.append({level, text});
            }
        }
    } else if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return out;
        }
        static const QRegularExpression re(QStringLiteral(R"(^(#{1,6})\s+(.*?)\s*#*\s*$)"));
        bool fence = false;
        for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
            if (line.trimmed().startsWith(QLatin1String("```")) || line.trimmed().startsWith(QLatin1String("~~~"))) {
                fence = !fence;
                continue;
            }
            if (fence) {
                continue;
            }
            const QRegularExpressionMatch m = re.match(line);
            if (m.hasMatch() && !m.captured(2).isEmpty()) {
                out.append({int(m.captured(1).size()), m.captured(2)});
            }
        }
    }
    return out;
}

bool isTocBlock(const QTextBlock &b)
{
    return b.blockFormat().hasProperty(GeneratedProperty) || b.blockFormat().hasProperty(TitleProperty);
}

} // namespace

QList<QPair<int, QString>> headingsOf(const QString &path)
{
    static QHash<QString, Cached> cache;
    const QFileInfo info(path);
    if (!info.isFile()) {
        return {};
    }
    auto it = cache.constFind(path);
    if (it != cache.cend() && it->modified == info.lastModified() && it->size == info.size()) {
        return it->headings;
    }
    Cached c{info.lastModified(), info.size(), readHeadings(path)};
    cache.insert(path, c);
    return c.headings;
}

QList<Entry> entries(const BookManifest &book, int maxLevel, const QString &skipPath)
{
    QList<Entry> out;
    const QString skip = skipPath.isEmpty() ? QString() : QFileInfo(skipPath).absoluteFilePath();
    int number = 0;
    for (int i = 0; i < book.count(); ++i) {
        const QString path = book.absolutePath(i);
        if (!skip.isEmpty() && QFileInfo(path).absoluteFilePath() == skip) {
            continue;
        }
        if (book.isMissing(i)) {
            continue;
        }
        ++number;
        const QString title = book.displayTitle(i);
        out.append({1, QStringLiteral("%1. %2").arg(number).arg(title), i});
        if (maxLevel < 2) {
            continue;
        }
        QList<QPair<int, QString>> hs = headingsOf(path);
        if (!hs.isEmpty() && hs.first().second.compare(title, Qt::CaseInsensitive) == 0) {
            hs.removeFirst();
        }
        // The chapter's top level of heading becomes level 2.
        int top = 99;
        for (const auto &h : std::as_const(hs)) {
            top = qMin(top, h.first);
        }
        for (const auto &h : std::as_const(hs)) {
            const int level = 2 + (h.first - top);
            if (level <= maxLevel) {
                out.append({level, h.second, i});
            }
        }
    }
    return out;
}

bool hasToc(const QTextDocument *doc)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (isTocBlock(b)) {
            return true;
        }
    }
    return false;
}

QString plainText(const QList<Entry> &entries)
{
    QString s = QStringLiteral("Contents\n");
    for (const Entry &e : entries) {
        s += QString((e.level - 1) * 4, QLatin1Char(' ')) + e.text + QLatin1Char('\n');
    }
    return s;
}

bool insertOrUpdate(QTextDocument *doc, QTextCursor *at, const QList<Entry> &list)
{
    // The current ToC (first run of marked paragraphs).
    QTextBlock first;
    QTextBlock last;
    QList<QPair<int, QString>> current;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (isTocBlock(b)) {
            if (!first.isValid()) {
                first = b;
            }
            last = b;
            if (b.blockFormat().hasProperty(GeneratedProperty)) {
                current.append({b.blockFormat().intProperty(GeneratedProperty), b.text()});
            }
        } else if (first.isValid()) {
            break;
        }
    }
    QList<QPair<int, QString>> wanted;
    for (const Entry &e : list) {
        wanted.append({e.level, e.text});
    }
    if (first.isValid() && current == wanted) {
        return false;
    }
    QTextCursor c(doc);
    c.beginEditBlock();
    if (first.isValid()) {
        c.setPosition(first.position());
        c.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    } else {
        c = at ? *at : QTextCursor(doc);
        c.movePosition(QTextCursor::StartOfBlock);
        if (!c.block().text().isEmpty()) {
            c.insertBlock();
            c.movePosition(QTextCursor::PreviousBlock);
        }
    }
    // Title, then one paragraph per entry; the paragraph after the ToC is
    // left as it was.
    QTextBlockFormat tf;
    tf.setProperty(TitleProperty, true);
    tf.setHeadingLevel(0);
    tf.setTopMargin(0);
    tf.setBottomMargin(10);
    QTextCharFormat tc;
    tc.setFontPointSize(16);
    tc.setFontWeight(QFont::Bold);
    c.setBlockFormat(tf);
    c.setBlockCharFormat(tc);
    c.insertText(QStringLiteral("Contents"), tc);
    for (const Entry &e : list) {
        QTextBlockFormat bf;
        bf.setProperty(GeneratedProperty, e.level);
        bf.setLeftMargin((e.level - 1) * 24.0);
        bf.setTopMargin(e.level == 1 ? 6 : 0);
        bf.setBottomMargin(2);
        QTextCharFormat cf;
        cf.setFontWeight(e.level == 1 ? QFont::DemiBold : QFont::Normal);
        cf.setFontPointSize(e.level == 1 ? 12 : 11);
        c.insertBlock(bf, cf);
        c.insertText(e.text, cf);
    }
    c.endEditBlock();
    if (at) {
        *at = c;
    }
    return true;
}

} // namespace BookToc
