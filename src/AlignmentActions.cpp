#include "AlignmentActions.hpp"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

namespace Alignment {

Actions::Actions(QTextEdit *editor, QObject *parent)
    : QObject(parent)
    , m_editor(editor)
{
    m_group = new QActionGroup(this);
    m_group->setExclusive(true);
    struct Spec { Kind kind; const char *text; const char *tip; Qt::Key key; };
    // Ctrl is Cmd on macOS.
    static const Spec specs[] = {
        {Kind::Left, "Align &Left", "Align left", Qt::Key_L},
        {Kind::Center, "&Center", "Center", Qt::Key_E},
        {Kind::Right, "Align &Right", "Align right", Qt::Key_R},
        {Kind::Justify, "&Justify", "Justify", Qt::Key_J},
    };
    for (const Spec &s : specs) {
        auto *a = new QAction(QString::fromLatin1(s.text), this);
        const QKeySequence keys(Qt::CTRL | s.key);
        a->setShortcut(keys);
        a->setToolTip(QStringLiteral("%1 (%2)").arg(QString::fromLatin1(s.tip),
                                                    keys.toString(QKeySequence::NativeText)));
        a->setCheckable(true);
        a->setData(int(s.kind));
        a->setShortcutContext(Qt::WindowShortcut);
        // Menus show the radio check; the drawn icon is for the toolbar.
        a->setIconVisibleInMenu(false);
        m_group->addAction(a);
        m_actions[int(s.kind)] = a;
    }
    connect(m_group, &QActionGroup::triggered, this, [this](QAction *a) {
        const Kind kind = Kind(a->data().toInt());
        bool changed = false;
        if (m_editor) {
            changed = apply(m_editor->textCursor(), kind);
        }
        sync();
        emit applied(kind, changed);
    });
    if (m_editor) {
        connect(m_editor, &QTextEdit::cursorPositionChanged, this, &Actions::sync);
        connect(m_editor, &QTextEdit::selectionChanged, this, &Actions::sync);
        // Undo/redo and loading change alignment without moving the cursor.
        connect(m_editor, &QTextEdit::textChanged, this, &Actions::sync);
    }
    setIconColors(QColor(0x2a, 0x27, 0x22), QColor(0x16, 0x37, 0x4f), QColor(0x84, 0x7d, 0x70));
    sync();
}

QAction *Actions::action(Kind kind) const
{
    return m_actions[int(kind)];
}

QList<QAction *> Actions::actions() const
{
    return {m_actions[0], m_actions[1], m_actions[2], m_actions[3]};
}

void Actions::setIconColors(const QColor &ink, const QColor &checkedInk, const QColor &disabledInk)
{
    for (int i = 0; i < 4; ++i) {
        m_actions[i]->setIcon(icon(Kind(i), ink, checkedInk, disabledInk));
    }
}

void Actions::sync()
{
    if (!m_editor) {
        return;
    }
    const Kind kind = kindOf(m_editor->textCursor().block());
    for (int i = 0; i < 4; ++i) {
        const bool on = (i == int(kind));
        if (m_actions[i]->isChecked() != on) {
            m_actions[i]->setChecked(on); // setChecked does not emit triggered
        }
    }
}

void undoRedoKeepingCaret(QTextEdit *edit, bool redo)
{
    if (!edit) {
        return;
    }
    QTextDocument *doc = edit->document();
    const QTextCursor before = edit->textCursor();
    const QString text = doc->toPlainText();
    if (redo) {
        edit->redo();
    } else {
        edit->undo();
    }
    if (doc->toPlainText() != text) {
        return; // text changed: Qt's caret placement (at the edit) is right
    }
    const int last = qMax(0, doc->characterCount() - 1);
    QTextCursor keep(doc);
    keep.setPosition(qBound(0, before.anchor(), last));
    keep.setPosition(qBound(0, before.position(), last), QTextCursor::KeepAnchor);
    edit->setTextCursor(keep);
}

bool keepAlignmentOnEnter(QTextEdit *edit)
{
    if (!edit || edit->isReadOnly()) {
        return false;
    }
    QTextCursor c = edit->textCursor();
    if (c.hasSelection() || !c.block().text().isEmpty() || c.currentList()
        || c.blockFormat().headingLevel() > 0 || kindOf(c.block()) == Kind::Left) {
        return false;
    }
    c.insertBlock(c.blockFormat(), c.charFormat());
    edit->setTextCursor(c);
    return true;
}

} // namespace Alignment
