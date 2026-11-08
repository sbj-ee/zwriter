#pragma once

#include "Outline.hpp"
#include "Theme.hpp"

#include <QList>
#include <QWidget>

class QLabel;
class QTextEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

// The document's headings as a tree beside the editor (View > Outline,
// Ctrl+Alt+O), for ODT and Markdown documents alike (Outline.hpp says what a
// heading is). Click an entry, or press Enter on it, to jump to that heading;
// the entry for the section the caret is in follows it as you write.
//
// Like the Markdown preview the tree is rebuilt about 300 ms after the last
// edit, never on every keystroke, and only while the pane is visible: a hidden
// pane just remembers it is out of date and catches up when it is shown. When
// an edit leaves every heading's level and text as it was, the entries are
// kept (so the scroll position does not move) and only their positions and
// word counts are refreshed.
class OutlinePane : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kDebounceMs = 300;

    explicit OutlinePane(QWidget *parent = nullptr);

    QSize sizeHint() const override;

    // Scan this editor's document and follow its caret.
    void attachEditor(QTextEdit *editor);

    // Markdown documents are scanned for "#" lines, all others for heading
    // levels; the pane's empty-state hint follows.
    void setMarkdown(bool markdown);
    bool isMarkdown() const { return m_markdown; }

    void applyTheme(const ThemeColors &colors);

    // Debounced: (re)starts the timer if the pane is visible, otherwise marks
    // the outline stale for the next show.
    void scheduleRebuild();
    // Rescan right away (cancels a pending debounce).
    void rebuildNow();
    bool isRebuildPending() const;
    int debounceInterval() const;
    void setDebounceInterval(int ms); // tests only; the app keeps kDebounceMs
    int rebuildCount() const { return m_rebuildCount; }

    const QList<Outline::Item> &items() const { return m_items; }
    // The entry for the section the caret is in; -1 above the first heading.
    int currentIndex() const { return m_current; }
    // Puts the keyboard focus on the tree (the window does this when the pane
    // is shown from the menu or Ctrl+Alt+O).
    void focusTree();

    QTreeWidget *tree() const { return m_tree; }
    QLabel *emptyLabel() const { return m_empty; }
    QLabel *footerLabel() const { return m_footer; }

    // View > Outline: the user's choice, remembered in QSettings under
    // settingsKey() (default off).
    static QString settingsKey();
    static bool loadVisibleSetting();
    static void saveVisibleSetting(bool visible);

signals:
    // An entry was clicked or Enter was pressed on it: the document position
    // of its heading block. The window moves the caret there.
    void headingActivated(int position);
    // Esc in the tree: the window gives the focus back to the editor.
    void escapePressed();

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void fill();
    void refreshFooter();
    void followCaret();
    void activate(QTreeWidgetItem *item);
    void applyRowStyle(QTreeWidgetItem *row, const Outline::Item &item) const;

    QTreeWidget *m_tree = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_footer = nullptr;
    QTimer *m_debounce = nullptr;
    QTextEdit *m_editor = nullptr;
    QList<Outline::Item> m_items;
    QList<QTreeWidgetItem *> m_rows; // one per item, same order
    ThemeColors m_colors;
    bool m_markdown = false;
    bool m_stale = true;
    bool m_syncing = false; // the caret moved the selection, not the user
    int m_current = -1;
    int m_rebuildCount = 0;
};
