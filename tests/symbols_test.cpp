// Symbols (1.13.0): the \name -> symbol table, \name autocomplete in a
// QTextEdit, undo restoring the typed text, and the Recently used list.
// Run: QT_QPA_PLATFORM=offscreen ctest -R symbols --output-on-failure
#include "Symbols.hpp"

#include <QApplication>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextEdit>
#include <cstdio>

namespace {
int g_failures = 0;
void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", what, detail.isEmpty() ? "" : " — ",
                qPrintable(detail));
    if (!ok) {
        ++g_failures;
    }
}

void type(QTextEdit &ed, const QString &text)
{
    QTextCursor c = ed.textCursor();
    c.insertText(text);
    ed.setTextCursor(c);
}

void testTable()
{
    using namespace Symbols;
    check(greek().size() == 48, "48 Greek letters", QString::number(greek().size()));
    check(math().size() == 16, "16 Math symbols");
    check(engineering().size() == 18, "18 EE symbols", QString::number(engineering().size()));
    QString ee;
    for (const Symbol &s : engineering()) {
        ee += s.text;
    }
    check(ee == QStringLiteral("Ωµ°±√π∞≈≠≤≥×÷∠Δ∑∫·"), "EE row in order", ee);
    check(symbolForName(QStringLiteral("alpha")) == QStringLiteral("α"), "\\alpha");
    check(symbolForName(QStringLiteral("omega")) == QStringLiteral("ω"), "\\omega");
    check(symbolForName(QStringLiteral("Omega")) == QStringLiteral("Ω"), "\\Omega");
    check(symbolForName(QStringLiteral("sigma")) == QStringLiteral("σ"), "\\sigma (not final sigma)");
    check(symbolForName(QStringLiteral("Sigma")) == QStringLiteral("Σ"), "\\Sigma");
    check(symbolForName(QStringLiteral("mu")) == QStringLiteral("μ"), "\\mu is Greek mu");
    check(symbolForName(QStringLiteral("micro")) == QStringLiteral("µ"), "\\micro is the micro sign");
    check(symbolForName(QStringLiteral("deg")) == QStringLiteral("°"), "\\deg");
    check(symbolForName(QStringLiteral("pm")) == QStringLiteral("±"), "\\pm");
    check(symbolForName(QStringLiteral("neq")) == QStringLiteral("≠"), "\\neq alias");
    check(symbolForName(QStringLiteral("ohm")) == QStringLiteral("Ω"), "\\ohm");
    check(symbolForName(QStringLiteral("nosuch")).isEmpty(), "unknown name is empty");
    check(symbolForName(QStringLiteral("ALPHA")).isEmpty(), "names are case sensitive");
    QSet<QString> names;
    bool unique = true;
    for (const Symbol &s : all()) {
        unique = unique && !names.contains(s.name) && !s.text.isEmpty() && !s.label.isEmpty();
        names.insert(s.name);
    }
    check(unique, "names unique, every symbol has text and a label");
    check(describe(QStringLiteral("°")).contains(QStringLiteral("Degree")), "tooltip names the symbol",
          describe(QStringLiteral("°")));
}

void testAutocomplete()
{
    QTextEdit ed;
    type(ed, QStringLiteral("R = 10 k\\Omega"));
    check(Symbols::expandBeforeCursor(&ed, QStringLiteral(" ")), "\\Omega + Space expands");
    check(ed.toPlainText() == QStringLiteral("R = 10 kΩ "), "text is kΩ + space", ed.toPlainText());
    type(ed, QStringLiteral("at 25\\deg"));
    check(Symbols::expandBeforeCursor(&ed, QString()), "\\deg + Tab expands");
    check(ed.toPlainText().endsWith(QStringLiteral("25°")), "Tab adds nothing after the symbol",
          ed.toPlainText());

    // Undo: one step brings back exactly the typed \deg.
    ed.undo();
    check(ed.toPlainText().endsWith(QStringLiteral("at 25\\deg")), "undo restores \\deg", ed.toPlainText());
    ed.redo();
    check(ed.toPlainText().endsWith(QStringLiteral("25°")), "redo re-applies", ed.toPlainText());

    QTextEdit plain;
    type(plain, QStringLiteral("\\notasymbol"));
    check(!Symbols::expandBeforeCursor(&plain, QStringLiteral(" ")), "unknown name not expanded");
    plain.clear();
    type(plain, QStringLiteral("a\\\\alpha"));
    check(!Symbols::expandBeforeCursor(&plain, QStringLiteral(" ")), "escaped \\\\alpha left alone");
    plain.clear();
    type(plain, QStringLiteral("alpha"));
    check(!Symbols::expandBeforeCursor(&plain, QStringLiteral(" ")), "no backslash, no expansion");
    plain.clear();
    type(plain, QStringLiteral("x \\pm"));
    QTextCursor c = plain.textCursor();
    c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 3);
    plain.setTextCursor(c);
    check(!Symbols::expandBeforeCursor(&plain, QStringLiteral(" ")), "selection: no expansion");

    QTextEdit ins;
    type(ins, QStringLiteral("V"));
    Symbols::insertAtCursor(&ins, QStringLiteral("±"));
    check(ins.toPlainText() == QStringLiteral("V±"), "insertAtCursor");
    ins.undo();
    check(ins.toPlainText() == QStringLiteral("V"), "insert is one undo step", ins.toPlainText());
}

void testSettings()
{
    check(Symbols::autocompleteEnabled(), "autocomplete on by default");
    Symbols::setAutocompleteEnabled(false);
    check(!Symbols::autocompleteEnabled(), "autocomplete off is saved");
    Symbols::setAutocompleteEnabled(true);
    check(Symbols::recent().isEmpty(), "no recent symbols at first");
    Symbols::addRecent(QStringLiteral("α"));
    Symbols::addRecent(QStringLiteral("Ω"));
    Symbols::addRecent(QStringLiteral("α"));
    check(Symbols::recent() == QStringList({QStringLiteral("α"), QStringLiteral("Ω")}),
          "recent is newest first, no duplicates", Symbols::recent().join(QLatin1Char(',')));
    for (const Symbols::Symbol &s : Symbols::greek()) {
        Symbols::addRecent(s.text);
    }
    check(Symbols::recent().size() == Symbols::kMaxRecent, "recent is capped");
}

void testMath()
{
    using namespace Symbols;
    QString row;
    for (const Symbol &s : math()) {
        row += s.text;
    }
    check(row == QStringLiteral("≥≤≠≈±×÷√∞∑∫∂∠Δ→⇒"), "Math row in order", row);
    const struct { const char *name; const char *sym; } names[] = {
        {"geq", "≥"}, {"leq", "≤"}, {"neq", "≠"}, {"approx", "≈"}, {"pm", "±"}, {"times", "×"},
        {"div", "÷"}, {"sqrt", "√"}, {"infty", "∞"}, {"sum", "∑"}, {"int", "∫"}, {"partial", "∂"},
        {"angle", "∠"}, {"Delta", "Δ"}, {"to", "→"}, {"Rightarrow", "⇒"}};
    for (const auto &n : names) {
        check(symbolForName(QLatin1String(n.name)) == QString::fromUtf8(n.sym), "math \\name", QLatin1String(n.name));
        QTextEdit ed;
        type(ed, QStringLiteral("a \\") + QLatin1String(n.name));
        check(expandBeforeCursor(&ed, QStringLiteral(" "))
                  && ed.toPlainText() == QStringLiteral("a ") + QString::fromUtf8(n.sym) + QLatin1Char(' '),
              "math autocomplete", ed.toPlainText());
    }
    check(operatorFor(QLatin1Char('>'), QLatin1Char('=')) == QStringLiteral("≥"), ">= -> ≥");
    check(operatorFor(QLatin1Char('<'), QLatin1Char('=')) == QStringLiteral("≤"), "<= -> ≤");
    check(operatorFor(QLatin1Char('!'), QLatin1Char('=')) == QStringLiteral("≠"), "!= -> ≠");
    check(operatorFor(QLatin1Char('-'), QLatin1Char('>')) == QStringLiteral("→"), "-> -> →");
    check(operatorFor(QLatin1Char('='), QLatin1Char('>')) == QStringLiteral("⇒"), "=> -> ⇒");
    check(operatorFor(QLatin1Char('='), QLatin1Char('=')).isEmpty(), "== left alone");

    // Typing: one Ctrl+Z (undo) brings back the typed pair.
    const struct { const char *pre; char key; const char *out; const char *undone; } typed[] = {
        {"V >", '=', "V ≥", "V >="}, {"V <", '=', "V ≤", "V <="}, {"a !", '=', "a ≠", "a !="},
        {"in -", '>', "in →", "in ->"}, {"p =", '>', "p ⇒", "p =>"}};
    for (const auto &t : typed) {
        QTextEdit ed;
        type(ed, QString::fromUtf8(t.pre));
        ed.document()->clearUndoRedoStacks();
        check(typeOperator(&ed, QLatin1Char(t.key)) && ed.toPlainText() == QString::fromUtf8(t.out),
              "operator replaced", ed.toPlainText());
        ed.undo();
        check(ed.toPlainText() == QString::fromUtf8(t.undone), "one undo restores the pair", ed.toPlainText());
    }
    {
        QTextEdit ed;
        type(ed, QStringLiteral("x"));
        check(!typeOperator(&ed, QLatin1Char('=')) && ed.toPlainText() == QStringLiteral("x"), "x= not handled");
    }
    // Not in code: a <pre> block, or an explicit monospace run.
    {
        QTextEdit ed;
        ed.setHtml(QStringLiteral("<pre>if (a &gt;</pre>"));
        QTextCursor c = ed.textCursor();
        c.movePosition(QTextCursor::End);
        ed.setTextCursor(c);
        check(isCodeContext(&ed, c), "pre block is code");
        check(!typeOperator(&ed, QLatin1Char('=')), "no replacement in a code block");
    }
    {
        QTextEdit ed;
        ed.document()->setDefaultFont(QFont(QStringLiteral("Sans Serif"), 12));
        QTextCharFormat mono;
        mono.setFontFamilies({QStringLiteral("DejaVu Sans Mono")});
        QTextCursor c = ed.textCursor();
        c.insertText(QStringLiteral("x -"), mono);
        ed.setTextCursor(c);
        check(!typeOperator(&ed, QLatin1Char('>')), "no replacement in monospace text");
        QTextCharFormat fixed;
        fixed.setFontFixedPitch(true);
        c.insertText(QStringLiteral(" y >"), fixed);
        ed.setTextCursor(c);
        check(!typeOperator(&ed, QLatin1Char('=')), "no replacement in fixed-pitch text");
    }
    {
        // zwriter's own default font (Courier) is not "code".
        QTextEdit ed;
        ed.document()->setDefaultFont(QFont(QStringLiteral("Courier"), 12));
        type(ed, QStringLiteral("V >"));
        check(typeOperator(&ed, QLatin1Char('=')), "document default monospace font still replaces");
    }
    check(autoReplaceEnabled(), "auto-replace on by default");
    setAutoReplaceEnabled(false);
    check(!autoReplaceEnabled(), "auto-replace off is saved");
    setAutoReplaceEnabled(true);
}
} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("sbj-ee-test"));
    QApplication::setApplicationName(QStringLiteral("zwriter-symbols-test"));
    testTable();
    testAutocomplete();
    testSettings();
    testMath();
    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
