#include "Symbols.hpp"

#include <QHash>
#include <QSettings>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QFontDatabase>
#include <QFontInfo>
#include <QTextDocument>

namespace Symbols {
namespace {

const char *const kGreekNames[] = {"alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta",
                                   "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi",
                                   "rho", "sigma", "tau", "upsilon", "phi", "chi", "psi", "omega"};

QList<Symbol> buildGreek()
{
    QList<Symbol> out;
    for (int upper = 0; upper < 2; ++upper) {
        for (int i = 0; i < 24; ++i) {
            QString name = QString::fromLatin1(kGreekNames[i]);
            // U+03B1..U+03C9 / U+0391..U+03A9, skipping final sigma (U+03C2) and the gap at U+03A2.
            const char16_t base = upper ? u'\u0391' : u'\u03B1';
            const char16_t ch = char16_t(base + i + (i >= 17 ? 1 : 0));
            const QString label = QStringLiteral("Greek %1 letter %2")
                                      .arg(upper ? QStringLiteral("capital") : QStringLiteral("small"), name);
            if (upper) {
                name[0] = name[0].toUpper();
            }
            out.append({name, QString(QChar(ch)), label});
        }
    }
    return out;
}

QList<Symbol> buildEngineering()
{
    return {
        {QStringLiteral("ohm"), QStringLiteral("Ω"), QStringLiteral("Ohm (capital omega)")},
        {QStringLiteral("micro"), QStringLiteral("µ"), QStringLiteral("Micro sign")},
        {QStringLiteral("deg"), QStringLiteral("°"), QStringLiteral("Degree sign")},
        {QStringLiteral("pm"), QStringLiteral("±"), QStringLiteral("Plus-minus sign")},
        {QStringLiteral("sqrt"), QStringLiteral("√"), QStringLiteral("Square root")},
        {QStringLiteral("pi"), QStringLiteral("π"), QStringLiteral("Pi")},
        {QStringLiteral("infty"), QStringLiteral("∞"), QStringLiteral("Infinity")},
        {QStringLiteral("approx"), QStringLiteral("≈"), QStringLiteral("Almost equal to")},
        {QStringLiteral("ne"), QStringLiteral("≠"), QStringLiteral("Not equal to")},
        {QStringLiteral("le"), QStringLiteral("≤"), QStringLiteral("Less-than or equal to")},
        {QStringLiteral("ge"), QStringLiteral("≥"), QStringLiteral("Greater-than or equal to")},
        {QStringLiteral("times"), QStringLiteral("×"), QStringLiteral("Multiplication sign")},
        {QStringLiteral("div"), QStringLiteral("÷"), QStringLiteral("Division sign")},
        {QStringLiteral("angle"), QStringLiteral("∠"), QStringLiteral("Angle (phasor)")},
        {QStringLiteral("Delta"), QStringLiteral("Δ"), QStringLiteral("Delta (change in)")},
        {QStringLiteral("sum"), QStringLiteral("∑"), QStringLiteral("N-ary summation")},
        {QStringLiteral("int"), QStringLiteral("∫"), QStringLiteral("Integral")},
        {QStringLiteral("cdot"), QStringLiteral("·"), QStringLiteral("Middle dot")},
    };
}

QList<Symbol> buildMath()
{
    return {
        {QStringLiteral("geq"), QStringLiteral("≥"), QStringLiteral("Greater-than or equal to")},
        {QStringLiteral("leq"), QStringLiteral("≤"), QStringLiteral("Less-than or equal to")},
        {QStringLiteral("neq"), QStringLiteral("≠"), QStringLiteral("Not equal to")},
        {QStringLiteral("approx"), QStringLiteral("≈"), QStringLiteral("Almost equal to")},
        {QStringLiteral("pm"), QStringLiteral("±"), QStringLiteral("Plus-minus sign")},
        {QStringLiteral("times"), QStringLiteral("×"), QStringLiteral("Multiplication sign")},
        {QStringLiteral("div"), QStringLiteral("÷"), QStringLiteral("Division sign")},
        {QStringLiteral("sqrt"), QStringLiteral("√"), QStringLiteral("Square root")},
        {QStringLiteral("infty"), QStringLiteral("∞"), QStringLiteral("Infinity")},
        {QStringLiteral("sum"), QStringLiteral("∑"), QStringLiteral("N-ary summation")},
        {QStringLiteral("int"), QStringLiteral("∫"), QStringLiteral("Integral")},
        {QStringLiteral("partial"), QStringLiteral("∂"), QStringLiteral("Partial differential")},
        {QStringLiteral("angle"), QStringLiteral("∠"), QStringLiteral("Angle (phasor)")},
        {QStringLiteral("Delta"), QStringLiteral("Δ"), QStringLiteral("Delta (change in)")},
        {QStringLiteral("to"), QStringLiteral("→"), QStringLiteral("Rightwards arrow")},
        {QStringLiteral("Rightarrow"), QStringLiteral("⇒"), QStringLiteral("Rightwards double arrow (implies)")},
    };
}

QList<Symbol> buildAll()
{
    QList<Symbol> out = greek();
    QHash<QString, bool> seen;
    for (const Symbol &s : out) {
        seen.insert(s.name, true);
    }
    QList<Symbol> rest = engineering();
    rest.append(math());
    for (const Symbol &s : rest) {
        if (!seen.contains(s.name)) {
            out.append(s);
            seen.insert(s.name, true);
        }
    }
    const QList<Symbol> aliases = {
        {QStringLiteral("degree"), QStringLiteral("°"), QStringLiteral("Degree sign")},
        {QStringLiteral("inf"), QStringLiteral("∞"), QStringLiteral("Infinity")},
        {QStringLiteral("Ohm"), QStringLiteral("Ω"), QStringLiteral("Ohm (capital omega)")},
        {QStringLiteral("rightarrow"), QStringLiteral("→"), QStringLiteral("Rightwards arrow")},
        {QStringLiteral("implies"), QStringLiteral("⇒"), QStringLiteral("Rightwards double arrow (implies)")},
        {QStringLiteral("varphi"), QStringLiteral("φ"), QStringLiteral("Greek small letter phi")},
    };
    out.append(aliases);
    return out;
}

const QHash<QString, QString> &nameTable()
{
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> t;
        for (const Symbol &s : all()) {
            t.insert(s.name, s.text);
        }
        return t;
    }();
    return table;
}

} // namespace

const QList<Symbol> &greek()
{
    static const QList<Symbol> g = buildGreek();
    return g;
}

const QList<Symbol> &engineering()
{
    static const QList<Symbol> e = buildEngineering();
    return e;
}

const QList<Symbol> &math()
{
    static const QList<Symbol> m = buildMath();
    return m;
}

const QList<Symbol> &all()
{
    static const QList<Symbol> a = buildAll();
    return a;
}

QString symbolForName(const QString &name)
{
    return nameTable().value(name);
}

QString describe(const QString &text)
{
    for (const QList<Symbol> *list : {&engineering(), &math(), &greek()}) {
        for (const Symbol &s : *list) {
            if (s.text == text) {
                return QStringLiteral("%1 (\\%2)").arg(s.label, s.name);
            }
        }
    }
    return text;
}

bool expandBeforeCursor(QTextEdit *editor, const QString &trailing)
{
    if (!editor) {
        return false;
    }
    QTextCursor cur = editor->textCursor();
    if (cur.hasSelection()) {
        return false;
    }
    const QString line = cur.block().text().left(cur.positionInBlock());
    int i = line.size();
    while (i > 0 && line.at(i - 1).isLetter() && line.at(i - 1).unicode() < 128) {
        --i;
    }
    if (i == line.size() || i == 0 || line.at(i - 1) != QLatin1Char('\\')) {
        return false;
    }
    if (i >= 2 && line.at(i - 2) == QLatin1Char('\\')) {
        return false; // "\\alpha" is an escaped backslash, leave it alone
    }
    const QString name = line.mid(i);
    const QString sym = symbolForName(name);
    if (sym.isEmpty()) {
        return false;
    }
    cur.beginEditBlock();
    cur.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, int(name.size()) + 1);
    cur.insertText(sym + trailing);
    cur.endEditBlock();
    editor->setTextCursor(cur);
    return true;
}

QString operatorFor(QChar first, QChar second)
{
    if (second == QLatin1Char('=')) {
        if (first == QLatin1Char('>')) return QStringLiteral("≥");
        if (first == QLatin1Char('<')) return QStringLiteral("≤");
        if (first == QLatin1Char('!')) return QStringLiteral("≠");
    } else if (second == QLatin1Char('>')) {
        if (first == QLatin1Char('-')) return QStringLiteral("→");
        if (first == QLatin1Char('=')) return QStringLiteral("⇒");
    }
    return QString();
}

namespace {
bool isMonospace(const QString &family)
{
    if (family.isEmpty()) {
        return false;
    }
    const QString f = family.toLower();
    if (f.contains(QLatin1String("mono")) || f.contains(QLatin1String("consol")) || f.contains(QLatin1String("menlo"))
        || f.contains(QLatin1String("code"))) {
        return true;
    }
    return QFontDatabase::isFixedPitch(family);
}
} // namespace

bool isCodeContext(const QTextEdit *editor, const QTextCursor &cursor)
{
    if (cursor.blockFormat().nonBreakableLines()) {
        return true; // <pre> / code block
    }
    const QTextCharFormat cf = cursor.charFormat();
    const QStringList fams = cf.fontFamilies().toStringList();
    const QString family = fams.isEmpty() ? QString() : fams.first();
    if (family.isEmpty()) {
        return cf.fontFixedPitch(); // the document default font, unless marked fixed pitch
    }
    // zwriter's body font is a Courier typewriter chain ("Courier New, Courier,
    // … monospace"); that is prose, not code. Code is a run explicitly set in
    // another monospace face (DejaVu Sans Mono, Consolas, Menlo …).
    const QString base = editor ? editor->document()->defaultFont().family() : QString();
    if (family.compare(base, Qt::CaseInsensitive) == 0 || family.startsWith(QLatin1String("Courier"), Qt::CaseInsensitive)) {
        return false;
    }
    return isMonospace(family);
}

bool typeOperator(QTextEdit *editor, QChar typed)
{
    if (!editor) {
        return false;
    }
    QTextCursor cur = editor->textCursor();
    if (cur.hasSelection() || cur.positionInBlock() == 0) {
        return false;
    }
    const QString line = cur.block().text();
    const QChar prev = line.at(cur.positionInBlock() - 1);
    const QString sym = operatorFor(prev, typed);
    if (sym.isEmpty() || isCodeContext(editor, cur)) {
        return false;
    }
    // Step 1: the typed character, as typing would.
    cur.beginEditBlock();
    cur.insertText(QString(typed));
    cur.endEditBlock();
    // Step 2: the pair becomes the symbol.
    cur.beginEditBlock();
    cur.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 2);
    cur.insertText(sym);
    cur.endEditBlock();
    editor->setTextCursor(cur);
    return true;
}

bool autoReplaceEnabled()
{
    return QSettings().value(QStringLiteral("symbols/autoReplace"), true).toBool();
}

void setAutoReplaceEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("symbols/autoReplace"), on);
}

void insertAtCursor(QTextEdit *editor, const QString &text)
{
    if (!editor || text.isEmpty()) {
        return;
    }
    QTextCursor cur = editor->textCursor();
    cur.beginEditBlock();
    cur.insertText(text);
    cur.endEditBlock();
    editor->setTextCursor(cur);
}

bool autocompleteEnabled()
{
    return QSettings().value(QStringLiteral("symbols/autocomplete"), true).toBool();
}

void setAutocompleteEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("symbols/autocomplete"), on);
}

QStringList recent()
{
    QStringList r = QSettings().value(QStringLiteral("symbols/recent")).toStringList();
    while (r.size() > kMaxRecent) {
        r.removeLast();
    }
    return r;
}

void addRecent(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    QStringList r = recent();
    r.removeAll(text);
    r.prepend(text);
    while (r.size() > kMaxRecent) {
        r.removeLast();
    }
    QSettings().setValue(QStringLiteral("symbols/recent"), r);
}

} // namespace Symbols
