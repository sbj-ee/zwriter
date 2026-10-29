#pragma once
// Greek letters and EE symbols (1.13.0): the table behind Insert > Symbol…
// and the \name autocomplete (\alpha + Space -> α).
#include <QList>
#include <QString>
#include <QStringList>

class QTextEdit;
class QTextCursor;

namespace Symbols {

struct Symbol {
    QString name;  // autocomplete name, without the backslash ("alpha", "Omega", "deg")
    QString text;  // what gets inserted ("α")
    QString label; // tooltip ("Greek small letter alpha")
};

// Lower then upper case Greek alphabet, 48 entries, in alphabet order.
const QList<Symbol> &greek();
// The EE row: Ω µ ° ± √ π ∞ ≈ ≠ ≤ ≥ × ÷ ∠ Δ ∑ ∫ ·
const QList<Symbol> &engineering();
// The Math row: ≥ ≤ ≠ ≈ ± × ÷ √ ∞ ∑ ∫ ∂ ∠ Δ → ⇒
const QList<Symbol> &math();
// Every name autocomplete knows, including aliases (\neq, \leq, \inf, \ohm …).
const QList<Symbol> &all();

// "alpha" -> "α"; names are case sensitive (\omega ω, \Omega Ω). Empty if unknown.
QString symbolForName(const QString &name);
// Tooltip text for an inserted symbol ("Greek small letter alpha (\alpha)").
QString describe(const QString &text);

// If the text just before the caret is \name with a known name (and not an
// escaped \\name), replace it with the symbol followed by `trailing`, as ONE
// undo step, and return true. Undo brings back the typed \name.
bool expandBeforeCursor(QTextEdit *editor, const QString &trailing);

// Typed operators (1.13.0): >= ≥, <= ≤, != ≠, -> →, => ⇒. Returns the
// symbol for the two characters, or empty.
QString operatorFor(QChar first, QChar second);
// True where text should stay literal: a code / preformatted block (Qt's
// non-breakable-lines <pre> blocks) or an inline run explicitly set in a
// monospace font other than the document's own default font.
bool isCodeContext(const QTextEdit *editor, const QTextCursor &cursor);
// Called for a typed character `typed` before it is inserted. If it completes
// an operator, inserts it (one undo step), then replaces the pair with the
// symbol (a second undo step), so one Ctrl+Z brings back ">=". Returns true
// when it handled the key.
bool typeOperator(QTextEdit *editor, QChar typed);

// Insert `text` at the caret (replacing a selection) as one undo step.
void insertAtCursor(QTextEdit *editor, const QString &text);

// Settings: "symbols/autocomplete" (default on), "symbols/recent" (newest first).
bool autocompleteEnabled();
void setAutocompleteEnabled(bool on);
// "symbols/autoReplace" (default on): the typed-operator replacement.
bool autoReplaceEnabled();
void setAutoReplaceEnabled(bool on);
constexpr int kMaxRecent = 12;
QStringList recent();
void addRecent(const QString &text);

} // namespace Symbols
