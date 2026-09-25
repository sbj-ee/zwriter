#pragma once

#include "Alignment.hpp"

#include <QColor>
#include <QList>
#include <QObject>

class QAction;
class QActionGroup;
class QTextEdit;

namespace Alignment {

// Format > Align actions (Align Left / Center / Align Right / Justify) as one
// exclusive group bound to an editor. The checked action follows the
// paragraph at the cursor: it is updated on cursor and selection moves and on
// document changes (undo/redo, loading a file).
class Actions : public QObject
{
    Q_OBJECT

public:
    explicit Actions(QTextEdit *editor, QObject *parent = nullptr);

    QAction *action(Kind kind) const;
    QList<QAction *> actions() const;
    QActionGroup *group() const { return m_group; }

    // Re-draw the icons in the theme's colours.
    void setIconColors(const QColor &ink, const QColor &checkedInk, const QColor &disabledInk);

public slots:
    // Check the action matching the paragraph at the cursor.
    void sync();

signals:
    // An alignment was chosen (menu, toolbar or shortcut) and applied.
    void applied(Alignment::Kind kind, bool changed);

private:
    QTextEdit *m_editor = nullptr;
    QActionGroup *m_group = nullptr;
    QAction *m_actions[4] = {nullptr, nullptr, nullptr, nullptr};
};

// Undo / redo on `edit`, except that a step which only changed formatting
// (alignment, bold, ...) leaves the caret and selection where they were. Qt
// otherwise moves the caret to the end of the reformatted range, so undoing
// Center on a title would drop the caret into the next paragraph.
void undoRedoKeepingCaret(QTextEdit *edit, bool redo);

// Enter in an empty centred / right-aligned / justified paragraph (not a list
// item or heading): add a paragraph with the same alignment and return true.
// Qt would instead reset the empty paragraph to the default format and
// swallow the key, so a blank line under a centred title lost its centring
// and a Return went missing. Returns false (Qt handles Enter) otherwise.
bool keepAlignmentOnEnter(QTextEdit *edit);

} // namespace Alignment
