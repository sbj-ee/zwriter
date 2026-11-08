#include "OutlinePane.hpp"

#include "OutlineEdit.hpp"

#include <QAction>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace {
constexpr int kIndexRole = Qt::UserRole + 1;

// The tree lets entries be dragged but never moves them itself: a drop is
// reported to the pane, which asks the window to move the section in the
// document; the tree is then rebuilt from the document.
class OutlineTree : public QTreeWidget
{
public:
    using CanDrop = std::function<bool(int from, int to)>;
    using Dropped = std::function<void(int from, int to, bool after)>;

    OutlineTree(CanDrop canDrop, Dropped dropped, QWidget *parent)
        : QTreeWidget(parent)
        , m_canDrop(std::move(canDrop))
        , m_dropped(std::move(dropped))
    {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::IgnoreAction);
    }

protected:
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        int from = 0;
        int to = 0;
        bool after = false;
        if (target(event->position().toPoint(), &from, &to, &after) && m_canDrop(from, to)) {
            QTreeWidget::dragMoveEvent(event);
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else {
            event->ignore();
        }
    }

    void dropEvent(QDropEvent *event) override
    {
        int from = 0;
        int to = 0;
        bool after = false;
        if (target(event->position().toPoint(), &from, &to, &after) && m_canDrop(from, to)) {
            m_dropped(from, to, after);
        }
        event->setDropAction(Qt::IgnoreAction); // the model is never changed by the drop itself
        event->ignore();
        viewport()->update();
    }

private:
    bool target(const QPoint &pos, int *from, int *to, bool *after) const
    {
        QTreeWidgetItem *src = currentItem();
        QTreeWidgetItem *dst = itemAt(pos);
        if (!src || !dst || src == dst) {
            return false;
        }
        *from = src->data(0, kIndexRole).toInt();
        *to = dst->data(0, kIndexRole).toInt();
        *after = pos.y() > visualItemRect(dst).center().y();
        return true;
    }

    CanDrop m_canDrop;
    Dropped m_dropped;
};
} // namespace

OutlinePane::OutlinePane(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("outlinePane"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_title = new QLabel(QStringLiteral("Outline"), this);
    m_title->setObjectName(QStringLiteral("outlineTitle"));
    layout->addWidget(m_title);

    m_tree = new OutlineTree(
        [this](int from, int to) { return canDrop(from, to); },
        [this](int from, int to, bool after) {
            if (from >= 0 && from < m_items.size() && to >= 0 && to < m_items.size()) {
                emit moveToRequested(m_items[from].position, m_items[to].position, after);
            }
        },
        this);
    m_tree->setObjectName(QStringLiteral("outlineTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setTextElideMode(Qt::ElideRight);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->installEventFilter(this);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &OutlinePane::showContextMenu);
    layout->addWidget(m_tree, 1);

    m_empty = new QLabel(this);
    m_empty->setObjectName(QStringLiteral("outlineEmpty"));
    m_empty->setWordWrap(true);
    m_empty->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_empty->setContentsMargins(7, 4, 7, 4);
    layout->addWidget(m_empty, 1);

    m_footer = new QLabel(this);
    m_footer->setObjectName(QStringLiteral("outlineFooter"));
    m_footer->setContentsMargins(7, 6, 7, 8);
    layout->addWidget(m_footer);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &OutlinePane::rebuildNow);

    // A click opens the heading; arrow keys only move through the list until
    // Enter (itemActivated) opens it.
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) { activate(item); });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) { activate(item); });

    setMarkdown(false); // sets the empty-state hint
    refreshFooter();
    m_tree->hide();
}

QSize OutlinePane::sizeHint() const
{
    return {240, 600};
}

void OutlinePane::attachEditor(QTextEdit *editor)
{
    if (m_editor == editor) {
        return;
    }
    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
    }
    m_editor = editor;
    if (m_editor) {
        connect(m_editor, &QTextEdit::textChanged, this, &OutlinePane::scheduleRebuild);
        connect(m_editor, &QTextEdit::cursorPositionChanged, this, &OutlinePane::followCaret);
    }
    scheduleRebuild();
}

void OutlinePane::setMarkdown(bool markdown)
{
    const bool changed = m_markdown != markdown;
    m_markdown = markdown;
    m_empty->setText(markdown
                         ? QStringLiteral("No headings yet.\n\nStart a line with # to make a heading.")
                         : QStringLiteral("No headings yet.\n\nMake a heading with Format → Heading 1–3 "
                                          "(Ctrl+Alt+1 … 3)."));
    if (changed) {
        scheduleRebuild();
    }
}

void OutlinePane::applyTheme(const ThemeColors &c)
{
    m_colors = c;
    setStyleSheet(QStringLiteral(
        "#outlinePane { background-color: %1; }"
        "#outlineTitle { background-color: %1; color: %2; font-size: 9pt; font-weight: 600;"
        " padding: 8px 7px 6px 7px; }"
        "#outlineEmpty { background-color: %1; color: %3; font-size: 9pt; }"
        "#outlineFooter { background-color: %1; color: %3; font-size: 9pt; }"
        "QTreeWidget#outlineTree { background-color: %1; color: %2; border: none; outline: 0;"
        " font-size: 10pt; selection-background-color: %5; selection-color: %6; }"
        "QTreeWidget#outlineTree::item { padding: 3px 2px; }"
        "QTreeWidget#outlineTree::item:hover { background-color: %4; }"
        "QTreeWidget#outlineTree::item:selected { background-color: %5; color: %6; }")
                      .arg(c.barBg.name(), c.fg.name(), c.muted.name(), c.hover.name(), c.accentSoft.name(),
                           c.accentFg.name()));
}

void OutlinePane::scheduleRebuild()
{
    m_stale = true;
    if (!isVisible()) {
        m_debounce->stop(); // caught up in showEvent()
        return;
    }
    m_debounce->start();
}

bool OutlinePane::isRebuildPending() const
{
    return m_debounce->isActive();
}

int OutlinePane::debounceInterval() const
{
    return m_debounce->interval();
}

void OutlinePane::setDebounceInterval(int ms)
{
    m_debounce->setInterval(ms);
}

void OutlinePane::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_stale) {
        rebuildNow();
    } else {
        followCaret();
    }
}

void OutlinePane::rebuildNow()
{
    m_debounce->stop();
    m_stale = false;
    ++m_rebuildCount;
    const QList<Outline::Item> fresh = Outline::scan(m_editor ? m_editor->document() : nullptr, m_markdown);

    bool sameHeadings = fresh.size() == m_items.size() && m_rows.size() == m_items.size();
    for (int i = 0; sameHeadings && i < fresh.size(); ++i) {
        sameHeadings = fresh[i].level == m_items[i].level && fresh[i].text == m_items[i].text;
    }
    m_items = fresh;
    if (sameHeadings) {
        // Typing in a section: positions and word counts moved, the tree did not.
        for (int i = 0; i < m_rows.size(); ++i) {
            applyRowStyle(m_rows[i], m_items[i]);
        }
    } else {
        fill();
    }
    followCaret();
    refreshFooter();
}

void OutlinePane::applyRowStyle(QTreeWidgetItem *row, const Outline::Item &item) const
{
    row->setToolTip(0, QStringLiteral("%1\n%2 words").arg(item.text).arg(item.words));
}

void OutlinePane::fill()
{
    const int scroll = m_tree->verticalScrollBar()->value();
    const QSignalBlocker block(m_tree);
    m_tree->clear();
    m_rows.clear();
    m_current = -1; // re-found by followCaret()

    // The parent of a heading is the nearest one before it with a lower level
    // (a Heading 3 straight under a Heading 1 hangs from the Heading 1).
    QList<QTreeWidgetItem *> stack;
    QList<int> levels;
    for (int i = 0; i < m_items.size(); ++i) {
        const Outline::Item &item = m_items[i];
        while (!levels.isEmpty() && levels.back() >= item.level) {
            levels.removeLast();
            stack.removeLast();
        }
        auto *row = stack.isEmpty() ? new QTreeWidgetItem(m_tree) : new QTreeWidgetItem(stack.back());
        row->setText(0, item.text);
        row->setData(0, kIndexRole, i);
        if (item.level == 1) {
            QFont f = row->font(0);
            f.setWeight(QFont::DemiBold);
            row->setFont(0, f);
        }
        applyRowStyle(row, item);
        m_rows.append(row);
        stack.append(row);
        levels.append(item.level);
    }
    m_tree->expandAll();
    m_tree->verticalScrollBar()->setValue(scroll);

    const bool any = !m_items.isEmpty();
    m_tree->setVisible(any);
    m_empty->setVisible(!any);
}

void OutlinePane::followCaret()
{
    if (!m_editor || m_stale) {
        return; // a pending rebuild ends in a call to this
    }
    const int idx = Outline::indexAt(m_items, m_editor->textCursor().position());
    if (idx == m_current) {
        return;
    }
    m_current = idx;
    m_syncing = true;
    {
        const QSignalBlocker block(m_tree);
        if (idx >= 0 && idx < m_rows.size()) {
            m_tree->setCurrentItem(m_rows[idx]);
            m_tree->scrollToItem(m_rows[idx]);
        } else {
            m_tree->setCurrentItem(nullptr);
            m_tree->clearSelection();
        }
    }
    m_syncing = false;
    refreshFooter();
}

void OutlinePane::refreshFooter()
{
    if (m_items.isEmpty()) {
        m_footer->clear();
        m_footer->hide();
        return;
    }
    QString text = m_items.size() == 1 ? QStringLiteral("1 heading")
                                       : QStringLiteral("%1 headings").arg(m_items.size());
    if (m_current >= 0 && m_current < m_items.size()) {
        text += QStringLiteral(" · this section %1 words").arg(m_items[m_current].words);
    }
    m_footer->setText(text);
    m_footer->show();
}

void OutlinePane::activate(QTreeWidgetItem *item)
{
    if (m_syncing || !item) {
        return;
    }
    bool ok = false;
    const int idx = item->data(0, kIndexRole).toInt(&ok);
    if (ok && idx >= 0 && idx < m_items.size()) {
        emit headingActivated(m_items[idx].position);
    }
}

int OutlinePane::rowIndex(const QTreeWidgetItem *row) const
{
    bool ok = false;
    const int idx = row ? row->data(0, kIndexRole).toInt(&ok) : -1;
    return ok && idx >= 0 && idx < m_items.size() ? idx : -1;
}

bool OutlinePane::treeHasFocus() const
{
    return m_tree->hasFocus();
}

int OutlinePane::currentHeadingPosition() const
{
    const int idx = rowIndex(m_tree->currentItem());
    return idx < 0 ? -1 : m_items[idx].position;
}

bool OutlinePane::canDrop(int from, int to) const
{
    if (from < 0 || to < 0 || from >= m_items.size() || to >= m_items.size() || from == to) {
        return false;
    }
    // Only among siblings: walk from one to the other.
    for (int i = OutlineEdit::nextSibling(m_items, from); i >= 0; i = OutlineEdit::nextSibling(m_items, i)) {
        if (i == to) {
            return true;
        }
    }
    for (int i = OutlineEdit::previousSibling(m_items, from); i >= 0; i = OutlineEdit::previousSibling(m_items, i)) {
        if (i == to) {
            return true;
        }
    }
    return false;
}

void OutlinePane::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *row = m_tree->itemAt(pos);
    const int idx = rowIndex(row);
    if (idx < 0) {
        return;
    }
    m_tree->setCurrentItem(row);
    const int where = m_items[idx].position;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("outlineMenu"));
    auto add = [&](const QString &text, const QString &name, bool enabled, const std::function<void()> &run) {
        QAction *a = menu.addAction(text);
        a->setObjectName(name);
        a->setEnabled(enabled);
        connect(a, &QAction::triggered, this, run);
    };
    add(QStringLiteral("Move Section &Up"), QStringLiteral("outlineMoveUp"), OutlineEdit::canMove(m_items, idx, -1),
        [this, where] { emit moveRequested(where, -1); });
    add(QStringLiteral("Move Section &Down"), QStringLiteral("outlineMoveDown"), OutlineEdit::canMove(m_items, idx, +1),
        [this, where] { emit moveRequested(where, +1); });
    menu.addSeparator();
    add(QStringLiteral("&Promote"), QStringLiteral("outlinePromote"),
        OutlineEdit::canChangeLevel(m_items, idx, -1, m_markdown), [this, where] { emit levelRequested(where, -1); });
    add(QStringLiteral("De&mote"), QStringLiteral("outlineDemote"),
        OutlineEdit::canChangeLevel(m_items, idx, +1, m_markdown), [this, where] { emit levelRequested(where, +1); });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void OutlinePane::focusTree()
{
    if (m_tree->isVisibleTo(this)) {
        m_tree->setFocus();
    }
}

bool OutlinePane::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tree && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        emit escapePressed();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

QString OutlinePane::settingsKey()
{
    return QStringLiteral("outline/visible");
}

bool OutlinePane::loadVisibleSetting()
{
    return QSettings().value(settingsKey(), false).toBool();
}

void OutlinePane::saveVisibleSetting(bool visible)
{
    QSettings().setValue(settingsKey(), visible);
}
