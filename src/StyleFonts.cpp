#include "StyleFonts.hpp"

#include "ParagraphStyles.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <cmath>

namespace StyleFonts {

const QStringList &keys()
{
    static const QStringList k = [] {
        QStringList out{QStringLiteral("Body"), QStringLiteral("Heading1"), QStringLiteral("Heading2"),
                        QStringLiteral("Heading3")};
        for (const ParagraphStyles::Style &s : ParagraphStyles::named()) {
            out << s.name;
        }
        return out;
    }();
    return k;
}

QString displayName(const QString &key)
{
    if (key == QLatin1String("Body")) {
        return QStringLiteral("Body Text");
    }
    if (key.startsWith(QLatin1String("Heading"))) {
        return QStringLiteral("Heading ") + key.mid(7);
    }
    if (const ParagraphStyles::Style *s = ParagraphStyles::find(key)) {
        return s->display;
    }
    return key;
}

QString keyFor(int headingLevel, const QString &namedStyle)
{
    if (!namedStyle.isEmpty() && ParagraphStyles::find(namedStyle)) {
        return ParagraphStyles::find(namedStyle)->name;
    }
    if (headingLevel > 0) {
        return QStringLiteral("Heading%1").arg(qMin(headingLevel, 3));
    }
    return QStringLiteral("Body");
}

Map builtinDefaults()
{
    Map m;
    m.insert(QStringLiteral("Body"), {QString(), 12});
    m.insert(QStringLiteral("Heading1"), {QString(), 22});
    m.insert(QStringLiteral("Heading2"), {QString(), 17});
    m.insert(QStringLiteral("Heading3"), {QString(), 13});
    for (const ParagraphStyles::Style &s : ParagraphStyles::named()) {
        m.insert(s.name, {QString(), s.pointSize});
    }
    return m;
}

Entry entry(const Map &map, const QString &key)
{
    auto it = map.constFind(key);
    if (it != map.constEnd()) {
        return it.value();
    }
    return builtinDefaults().value(key, Entry{});
}

Map appDefaults(QSettings &settings)
{
    Map m = builtinDefaults();
    for (const QString &k : keys()) {
        const QString base = QStringLiteral("styleFonts/") + k + QLatin1Char('/');
        Entry e = m.value(k);
        if (settings.contains(base + QStringLiteral("family"))) {
            e.family = settings.value(base + QStringLiteral("family")).toString();
        }
        const qreal sz = settings.value(base + QStringLiteral("size"), e.pointSize).toReal();
        if (sz >= 4 && sz <= 200) {
            e.pointSize = sz;
        }
        m.insert(k, e);
    }
    return m;
}

void saveAppDefaults(QSettings &settings, const Map &map)
{
    for (const QString &k : keys()) {
        const Entry e = entry(map, k);
        const QString base = QStringLiteral("styleFonts/") + k + QLatin1Char('/');
        settings.setValue(base + QStringLiteral("family"), e.family);
        settings.setValue(base + QStringLiteral("size"), e.pointSize);
    }
}

QString serialize(const Map &map)
{
    QJsonObject root;
    for (const QString &k : keys()) {
        const Entry e = entry(map, k);
        QJsonObject o;
        o.insert(QStringLiteral("family"), e.family);
        o.insert(QStringLiteral("size"), e.pointSize);
        root.insert(k, o);
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool parse(const QString &json, Map *map)
{
    if (json.trimmed().isEmpty()) {
        return false;
    }
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject()) {
        return false;
    }
    Map m = builtinDefaults();
    const QJsonObject root = d.object();
    for (const QString &k : keys()) { // unknown keys are ignored
        const QJsonObject o = root.value(k).toObject();
        if (o.isEmpty()) {
            continue;
        }
        Entry e = m.value(k);
        e.family = o.value(QStringLiteral("family")).toString();
        const qreal sz = o.value(QStringLiteral("size")).toDouble(e.pointSize);
        if (sz >= 4 && sz <= 200) {
            e.pointSize = sz;
        }
        m.insert(k, e);
    }
    *map = m;
    return true;
}

static Map &activeMap()
{
    static Map m = builtinDefaults();
    return m;
}

const Map &active() { return activeMap(); }
void setActive(const Map &map) { activeMap() = map; }

QStringList families(const Map &map, const QString &key, const QStringList &bodyChain,
                     const QStringList &headingChain)
{
    const Entry e = entry(map, key);
    QStringList chain;
    if (key == QLatin1String("Body")) {
        chain = bodyChain;
    } else if (key.startsWith(QLatin1String("Heading"))) {
        chain = headingChain;
    }
    if (e.family.isEmpty()) {
        return chain;
    }
    QStringList out{e.family};
    out += chain;
    out.removeDuplicates();
    return out;
}

static QString cssFamily(const QString &f)
{
    static const QStringList generic{QStringLiteral("serif"), QStringLiteral("sans-serif"),
                                     QStringLiteral("monospace")};
    if (generic.contains(f)) {
        return f;
    }
    QString q = f;
    q.remove(QLatin1Char('\'')).remove(QLatin1Char('"')).remove(QLatin1Char(';')).remove(QLatin1Char('}'));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

QString css(const Map &map, const QString &key)
{
    const Entry e = entry(map, key);
    const Entry b = builtinDefaults().value(key);
    QString out;
    if (!e.family.isEmpty()) {
        const bool mono = key == QLatin1String("Body");
        out += QStringLiteral("font-family:%1, %2;").arg(cssFamily(e.family),
                                                           mono ? QStringLiteral("serif") : QStringLiteral("sans-serif"));
    }
    if (!qFuzzyCompare(e.pointSize, b.pointSize)) {
        out += QStringLiteral("font-size:%1pt;").arg(QString::number(e.pointSize, 'g', 4));
    }
    return out;
}

static QString firstFamily(const QTextCharFormat &f)
{
    const QStringList fams = f.fontFamilies().toStringList();
    if (!fams.isEmpty()) {
        return fams.first();
    }
    return f.hasProperty(QTextFormat::FontFamily) ? f.fontFamily() : QString();
}

int restyle(QTextDocument *doc, const Map &from, const Map &to, const QStringList &bodyChain,
            const QStringList &headingChain)
{
    if (!doc) {
        return 0;
    }
    int touched = 0;
    QTextCursor batch(doc);
    batch.beginEditBlock();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString key = keyFor(b.blockFormat().headingLevel(), ParagraphStyles::styleOf(b.blockFormat()));
        const Entry oldE = entry(from, key);
        const Entry newE = entry(to, key);
        if (oldE == newE) {
            continue;
        }
        const QStringList oldFams = families(from, key, bodyChain, headingChain);
        const QStringList newFams = families(to, key, bodyChain, headingChain);
        const QString oldFirst = oldFams.value(0);
        const bool familyChanged = oldFams != newFams;
        const bool sizeChanged = !qFuzzyCompare(oldE.pointSize, newE.pointSize);
        auto adjust = [&](const QTextCharFormat &cur, bool emptyBlock) {
            QTextCharFormat f;
            const QString fam = firstFamily(cur);
            // Named styles without a face never set one: any face counts as theirs.
            const bool famIsStyle = fam.isEmpty() || fam == oldFirst || oldFirst.isEmpty() || emptyBlock;
            const qreal pt = cur.fontPointSize();
            const bool sizeIsStyle = pt <= 0 || std::abs(pt - oldE.pointSize) < 0.01 || emptyBlock;
            if (familyChanged && famIsStyle) {
                if (!newFams.isEmpty()) { // back to "no face" leaves the face as it is
                    f.setFontFamilies(newFams);
                }
            }
            if (sizeChanged && sizeIsStyle) {
                f.setFontPointSize(newE.pointSize);
            }
            return f;
        };
        bool any = false;
        struct Run { int pos, len; QTextCharFormat f; };
        QList<Run> runs; // collected first: merging formats can merge fragments
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) {
                continue;
            }
            const QTextCharFormat f = adjust(frag.charFormat(), false);
            if (f.propertyCount() > 0) {
                runs.append({frag.position(), frag.length(), f});
            }
        }
        for (const Run &r : runs) {
            QTextCursor c(doc);
            c.setPosition(r.pos);
            c.setPosition(r.pos + r.len, QTextCursor::KeepAnchor);
            c.mergeCharFormat(r.f);
            any = true;
        }
        const QTextCharFormat bcf = adjust(b.charFormat(), b.length() <= 1);
        if (bcf.propertyCount() > 0) {
            QTextCursor c(b);
            c.mergeBlockCharFormat(bcf);
            any = true;
        }
        touched += any ? 1 : 0;
    }
    batch.endEditBlock();
    return touched;
}

} // namespace StyleFonts
