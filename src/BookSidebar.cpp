#include "BookSidebar.hpp"

#include "BookChapterDialog.hpp"
#include "DocumentIo.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTextDocument>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QSet>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QAbstractItemDelegate>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int PathRole = Qt::UserRole + 1;
}

// The open chapter gets an accent bar at its left edge and bold text; the
// highlighted (selected) row is the plain highlight; a right-clicked row is
// outlined while its menu is open. The three can differ, and you can tell.
class ChapterDelegate : public QStyledItemDelegate
{
public:
    ChapterDelegate(const BookSidebar *side, QObject *parent) : QStyledItemDelegate(parent), m_side(side) {}
    void initStyleOption(QStyleOptionViewItem *opt, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(opt, index);
        if (index.data(BookSidebar::OpenRole).toBool()) {
            opt->font.setBold(true);
        }
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(p, option, index);
        const QColor accent = m_side->accentColor();
        p->save();
        if (index.data(BookSidebar::OpenRole).toBool()) {
            p->fillRect(QRect(option.rect.left(), option.rect.top(), 4, option.rect.height()), accent);
        }
        if (index.data(BookSidebar::ContextRole).toBool()) {
            p->setPen(QPen(accent, 1, Qt::DashLine));
            p->setBrush(Qt::NoBrush);
            p->drawRect(option.rect.adjusted(1, 1, -2, -2));
        }
        p->restore();
    }

private:
    const BookSidebar *m_side;
};

BookSidebar::BookSidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("bookSidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("bookHeader"));
    // Two rows: the book picker gets the full width (long titles stay
    // readable), the buttons go under it.
    auto *hv = new QVBoxLayout(header);
    hv->setContentsMargins(10, 8, 8, 6);
    hv->setSpacing(6);
    auto *hl = new QHBoxLayout;
    hl->setSpacing(6);
    auto *bl = new QHBoxLayout;
    bl->setSpacing(8);
    hv->addLayout(hl);
    hv->addLayout(bl);
    m_title = new QLabel(header);
    m_title->setObjectName(QStringLiteral("bookTitle"));
    m_title->setTextFormat(Qt::PlainText);
    m_title->setText(QStringLiteral("BOOK"));
    hl->addWidget(m_title);
    m_picker = new QComboBox(header);
    m_picker->setObjectName(QStringLiteral("bookPicker"));
    m_picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_picker->setMinimumContentsLength(8);
    m_picker->setToolTip(QStringLiteral("Choose a book"));
    connect(m_picker, &QComboBox::activated, this, [this](int i) {
        const QString key = m_picker->itemData(i).toString();
        if (key.isEmpty()) { // the "New Book…" entry
            const int cur = m_picker->findData(m_current);
            m_picker->setCurrentIndex(cur >= 0 ? cur : 0);
            chooseNewBook();
            return;
        }
        selectBook(key);
    });
    hl->addWidget(m_picker, 1);
    const auto tool = [header, bl](const QString &name, const QString &text, const QString &tip) {
        auto *b = new QToolButton(header);
        b->setObjectName(name);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        bl->addWidget(b);
        return b;
    };
    // "Files", not "Library": it shows every file in the library folder as a
    // tree (that sidebar's "Books" button comes back here).
    m_libraryButton = tool(QStringLiteral("bookToLibrary"), QStringLiteral("Files"),
                           QStringLiteral("Show all files in the library folder (Ctrl+Shift+L)"));
    connect(m_libraryButton, &QToolButton::clicked, this, &BookSidebar::switchToLibrary);
    // One clearly labelled button for chapters: a menu with New Chapter… and
    // Add Existing Chapters…. New Book is in the book picker, the right-click
    // menu and the File menu.
    m_addButton = tool(QStringLiteral("bookAdd"), QStringLiteral("+ Chapter"),
                       QStringLiteral("New chapter, or add existing .md / .txt / .odt files"));
    {
        auto *addMenu = new QMenu(m_addButton);
        addMenu->setObjectName(QStringLiteral("bookAddMenu"));
        addMenu->addAction(QStringLiteral("New Chapter…"), this, &BookSidebar::chooseNewChapter);
        addMenu->addAction(QStringLiteral("Add Existing Chapters…"), this, &BookSidebar::chooseChapterFiles);
        m_addButton->setMenu(addMenu);
        m_addButton->setPopupMode(QToolButton::InstantPopup);
    }
    bl->removeWidget(m_addButton);
    bl->insertWidget(0, m_addButton);
    bl->insertStretch(1, 1);
    // Kept for code that looks it up, but not shown: "+ Chapter" does its job.
    m_newButton = new QToolButton(header);
    m_newButton->setObjectName(QStringLiteral("bookNew"));
    m_newButton->hide();
    connect(m_newButton, &QToolButton::clicked, this, [this]() {
        if (m_hasBook) {
            chooseNewChapter();
        } else {
            chooseNewBook();
        }
    });
    layout->addWidget(header);

    m_hintBox = new QWidget(this);
    m_hintBox->setObjectName(QStringLiteral("bookHintBox"));
    auto *hint = new QVBoxLayout(m_hintBox);
    hint->setContentsMargins(12, 4, 12, 4);
    hint->setSpacing(8);
    m_hint = new QLabel(m_hintBox);
    m_hint->setObjectName(QStringLiteral("bookHint"));
    m_hint->setWordWrap(true);
    hint->addWidget(m_hint);
    m_createButton = new QPushButton(QStringLiteral("New Book…"), m_hintBox);
    m_createButton->setObjectName(QStringLiteral("bookCreate"));
    m_createButton->setToolTip(QStringLiteral(
        "Write book.json in a subfolder, listing its .md / .txt / .odt files in name order"));
    connect(m_createButton, &QPushButton::clicked, this, &BookSidebar::chooseNewBook);
    hint->addWidget(m_createButton, 0, Qt::AlignLeft);
    m_chooseFolderButton = new QPushButton(QStringLiteral("Choose Library Folder…"), m_hintBox);
    m_chooseFolderButton->setObjectName(QStringLiteral("bookChooseFolder"));
    connect(m_chooseFolderButton, &QPushButton::clicked, this,
            &BookSidebar::chooseLibraryRequested);
    hint->addWidget(m_chooseFolderButton, 0, Qt::AlignLeft);
    hint->addStretch(1);
    layout->addWidget(m_hintBox, 1);

    // A book with no chapters yet: say so and offer the two ways to add one.
    m_emptyBox = new QWidget(this);
    m_emptyBox->setObjectName(QStringLiteral("bookEmptyBox"));
    {
        auto *empty = new QVBoxLayout(m_emptyBox);
        empty->setContentsMargins(12, 8, 12, 8);
        empty->setSpacing(8);
        auto *label = new QLabel(QStringLiteral("This book has no chapters yet."), m_emptyBox);
        label->setObjectName(QStringLiteral("bookHint"));
        label->setWordWrap(true);
        empty->addWidget(label);
        auto *newChapter = new QPushButton(QStringLiteral("New Chapter…"), m_emptyBox);
        newChapter->setObjectName(QStringLiteral("bookEmptyNewChapter"));
        connect(newChapter, &QPushButton::clicked, this, &BookSidebar::chooseNewChapter);
        empty->addWidget(newChapter, 0, Qt::AlignLeft);
        auto *addExisting = new QPushButton(QStringLiteral("Add Existing Chapters…"), m_emptyBox);
        addExisting->setObjectName(QStringLiteral("bookEmptyAdd"));
        connect(addExisting, &QPushButton::clicked, this, &BookSidebar::chooseChapterFiles);
        empty->addWidget(addExisting, 0, Qt::AlignLeft);
        empty->addStretch(1);
    }
    m_emptyBox->hide();
    layout->addWidget(m_emptyBox, 1);

    m_view = new QListWidget(this);
    m_view->setObjectName(QStringLiteral("bookList"));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setDragDropMode(QAbstractItemView::InternalMove);
    m_view->setDefaultDropAction(Qt::MoveAction);
    m_view->setDragEnabled(true);
    m_view->setDropIndicatorShown(true);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setFocusPolicy(Qt::StrongFocus); // Tab reaches it: arrows, Enter, F2, Delete
    m_view->setItemDelegate(new ChapterDelegate(this, m_view));
    m_view->setDragDropOverwriteMode(false);
    m_view->installEventFilter(this);
    connect(m_view, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_rebuilding || m_renamingRow < 0) {
            return;
        }
        const int row = m_renamingRow;
        m_renamingRow = -1;
        if (row < m_book.count() && m_view->row(item) == row) {
            BookChapter c = m_book.at(row);
            const QString t = item->text().simplified();
            // An empty title goes back to the file name.
            c.title = t == QFileInfo(c.path).completeBaseName() ? QString() : t;
            const QString oldTitle = m_book.displayTitle(row);
            QTimer::singleShot(0, this, [this, row, c, oldTitle]() {
                setChapter(row, c);
                const int now = m_book.indexOf(m_book.absolutePath(row));
                if (now >= 0 && m_book.displayTitle(now) != oldTitle) {
                    emit chapterRenamed(m_book.absolutePath(now), oldTitle, m_book.displayTitle(now));
                }
            });
        }
    });
    connect(m_view->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this]() {
        if (m_renamingRow >= 0) { // Esc: nothing changed
            m_renamingRow = -1;
            QTimer::singleShot(0, this, [this]() { rebuild(m_view->currentRow()); });
        }
    });
    m_view->setWordWrap(true);
    m_view->setTextElideMode(Qt::ElideRight);
    connect(m_view->model(), &QAbstractItemModel::rowsMoved, this, &BookSidebar::onRowsMoved);
    connect(m_view, &QListWidget::itemClicked, this, &BookSidebar::onItemClicked);
    connect(m_view, &QListWidget::itemActivated, this, &BookSidebar::onItemClicked);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        // Right-click does not select or open the row; the menu names the chapter
        // it acts on, and the row is outlined while the menu is open.
        QListWidgetItem *item = m_view->itemAt(pos);
        QMenu *menu = buildContextMenu(item ? m_view->row(item) : -1);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        setContextRow(item ? m_view->row(item) : -1);
        connect(menu, &QMenu::aboutToHide, this, [this]() { setContextRow(-1); });
        menu->popup(m_view->viewport()->mapToGlobal(pos));
    });
    layout->addWidget(m_view, 1);

    m_footer = new QLabel(this);
    m_footer->setObjectName(QStringLiteral("bookFooter"));
    m_footer->setContentsMargins(12, 4, 12, 6);
    layout->addWidget(m_footer);

    auto *up = new QAction(QStringLiteral("Move Up"), this);
    up->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    // Window-wide, so Alt+Up/Down also move the open chapter while the editor
    // has focus. Qt only fires it while the sidebar is visible.
    up->setShortcutContext(Qt::WindowShortcut);
    // They move the chapter open in the editor (the one you are working
    // on), not whatever row happens to be selected in the list.
    connect(up, &QAction::triggered, this, [this]() {
        const int row = shortcutRow();
        moveChapter(row, row - 1);
    });
    auto *down = new QAction(QStringLiteral("Move Down"), this);
    down->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
    down->setShortcutContext(Qt::WindowShortcut);
    connect(down, &QAction::triggered, this, [this]() {
        const int row = shortcutRow();
        moveChapter(row, row + 1);
    });
    // F2 renames the current chapter from anywhere in the window (clicking a
    // chapter opens it and puts the focus in the editor), like Alt+Up/Down.
    auto *rename = new QAction(QStringLiteral("Rename Chapter"), this);
    rename->setShortcut(QKeySequence(Qt::Key_F2));
    rename->setShortcutContext(Qt::WindowShortcut);
    connect(rename, &QAction::triggered, this, [this]() {
        if (m_renamingRow < 0) {
            renameChapter(shortcutRow());
        }
    });
    addAction(up);
    addAction(down);
    addAction(rename);

    setRootPath(QString());
}

QSize BookSidebar::sizeHint() const
{
    return {240, 600};
}

QString BookSidebar::currentBookKey() { return QStringLiteral("book/current"); }
QString BookSidebar::visibleKey() { return QStringLiteral("book/visible"); }

BookSidebar::Settings BookSidebar::loadSettings()
{
    QSettings s;
    return {s.value(currentBookKey()).toString(), s.value(visibleKey(), false).toBool()};
}

void BookSidebar::saveSettings(const Settings &settings)
{
    QSettings s;
    s.setValue(currentBookKey(), settings.currentBook);
    s.setValue(visibleKey(), settings.visible);
}

int BookSidebar::currentRow() const
{
    return m_view->currentRow();
}

int BookSidebar::shortcutRow() const
{
    // The open chapter, always (the highlighted row may be one you moved to with
    // the arrow keys or right-clicked). With nothing open, the highlighted row.
    if (!m_currentFile.isEmpty()) {
        const int open = m_book.indexOf(QFileInfo(m_currentFile).absoluteFilePath());
        if (open >= 0) {
            return open;
        }
    }
    return m_view->currentRow();
}

void BookSidebar::setRootPath(const QString &path)
{
    const bool usable = !path.isEmpty() && QFileInfo(path).isDir();
    m_root = usable ? QDir::cleanPath(QFileInfo(path).absoluteFilePath()) : QString();
    scanBooks();
    if (!m_books.contains(m_current)) {
        m_current = m_books.isEmpty() ? QString() : m_books.first();
    }
    loadCurrent();
}

void BookSidebar::scanBooks()
{
    m_books.clear();
    if (!m_root.isEmpty()) {
        const QDir root(m_root);
        for (const QString &sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (BookManifest::exists(root.filePath(sub))) {
                m_books << sub;
            }
        }
        std::sort(m_books.begin(), m_books.end(), &BookManifest::naturalLess);
    }
    const QSignalBlocker block(m_picker);
    m_picker->clear();
    for (const QString &sub : m_books) {
        BookManifest b;
        b.load(QDir(m_root).filePath(sub));
        m_picker->addItem(b.title().trimmed().isEmpty() ? sub : b.title(), sub);
        m_picker->setItemData(m_picker->count() - 1, sub, Qt::ToolTipRole);
    }
    if (!m_books.isEmpty()) {
        m_picker->insertSeparator(m_picker->count());
        m_picker->addItem(QStringLiteral("New Book…"), QString());
    }
}

QStringList BookSidebar::candidateFolders() const
{
    QStringList out;
    if (m_root.isEmpty()) {
        return out;
    }
    for (const QString &sub : QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!m_books.contains(sub)) {
            out << sub;
        }
    }
    std::sort(out.begin(), out.end(), &BookManifest::naturalLess);
    return out;
}

bool BookSidebar::selectBook(const QString &subfolder)
{
    if (!m_books.contains(subfolder)) {
        return false;
    }
    if (subfolder != m_current) {
        m_current = subfolder;
        loadCurrent();
        emit currentBookChanged(m_current);
    }
    return true;
}

void BookSidebar::loadCurrent()
{
    m_lastError.clear();
    m_book = BookManifest();
    m_hasBook = false;
    if (!m_current.isEmpty()) {
        const QString dir = QDir(m_root).filePath(m_current);
        QString err;
        m_hasBook = m_book.load(dir, &err);
        if (!m_hasBook) {
            m_book.setBookDir(dir);
            m_lastError = err;
        }
    }
    {
        const QSignalBlocker block(m_picker);
        m_picker->setCurrentIndex(m_picker->findData(m_current));
    }
    rebuild();
    setCurrentFile(m_currentFile);
}

void BookSidebar::reload()
{
    const int row = currentRow();
    setRootPath(m_root);
    if (row >= 0 && row < m_view->count()) {
        m_view->setCurrentRow(row);
    }
    setCurrentFile(m_currentFile);
}

QString BookSidebar::itemText(int row) const
{
    const BookChapter &c = m_book.at(row);
    // The Contents chapter is not numbered; the chapters after it start at 1.
    const int number = m_book.chapterNumber(row);
    const QString title = number > 0 ? QStringLiteral("%1. %2").arg(number).arg(m_book.displayTitle(row))
                                     : m_book.displayTitle(row);
    if (m_book.isOutside(row)) {
        return title + QStringLiteral("\nOutside the book folder: ") + c.path;
    }
    if (m_book.isMissing(row)) {
        return title + QStringLiteral("\nMissing: ") + c.path;
    }
    const QLocale loc;
    const int words = countWordsInFile(m_book.absolutePath(row));
    QString detail = (BookManifest::isKnownStatus(c.status) ? c.status
                                                            : c.status + QStringLiteral(" (unknown status)"))
        + QStringLiteral(" · ") + loc.toString(std::max(0, words));
    detail += c.wordTarget > 0 ? QStringLiteral(" / %1 words").arg(loc.toString(c.wordTarget))
                               : QStringLiteral(" words");
    if (!c.pov.isEmpty()) {
        detail += QStringLiteral(" · ") + c.pov;
    }
    return title + QLatin1Char('\n') + detail;
}

void BookSidebar::rebuild(int selectRow)
{
    m_rebuilding = true;
    m_view->clear();
    for (int i = 0; i < m_book.count(); ++i) {
        auto *item = new QListWidgetItem(itemText(i), m_view);
        item->setData(PathRole, m_book.at(i).path);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        QString tip = m_book.at(i).path;
        if (!m_book.at(i).synopsis.isEmpty()) {
            tip += QStringLiteral("\n\n") + m_book.at(i).synopsis;
        }
        if (!m_book.at(i).tags.isEmpty()) {
            tip += QStringLiteral("\n\nTags: ") + m_book.at(i).tags.join(QStringLiteral(", "));
        }
        item->setToolTip(tip);
        if (m_book.isMissing(i)) {
            item->setData(Qt::UserRole + 2, true);
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
        }
    }
    if (selectRow >= 0 && selectRow < m_view->count()) {
        m_view->setCurrentRow(selectRow);
    }
    const int open = openRow();
    for (int r = 0; r < m_view->count(); ++r) {
        m_view->item(r)->setData(OpenRole, r == open);
    }
    m_rebuilding = false;
    updateHeader();
}

void BookSidebar::updateHeader()
{
    const bool library = !m_root.isEmpty();
    m_picker->setVisible(!m_books.isEmpty());
    m_title->setVisible(m_books.isEmpty());
    m_picker->setToolTip(QStringLiteral("Choose a book (or New Book…)\n%1").arg(m_book.bookDir()));
    m_addButton->setEnabled(m_hasBook);
    m_newButton->setEnabled(library);
    m_addButton->setVisible(m_hasBook);
    m_emptyBox->setVisible(m_hasBook && m_book.count() == 0);
    m_view->setVisible(m_hasBook && m_book.count() > 0);
    m_footer->setVisible(m_hasBook);
    m_hintBox->setVisible(!m_hasBook);
    m_createButton->setVisible(library && m_lastError.isEmpty());
    m_chooseFolderButton->setVisible(!library);
    if (!library) {
        m_hint->setText(QStringLiteral("Choose a library folder first. Each book is a subfolder of "
                                       "it with its own book.json."));
    } else if (!m_lastError.isEmpty()) {
        m_hint->setText(QStringLiteral("%1/book.json could not be read (%2). It was left as it is; "
                                       "fix or remove it to continue.")
                            .arg(m_current, m_lastError));
    } else {
        m_hint->setText(QStringLiteral("No books in %1 yet. A book is a subfolder with its own "
                                       "book.json; New Book… writes one, listing the subfolder's "
                                       ".md, .txt and .odt files in name order.")
                            .arg(QFileInfo(m_root).fileName()));
    }
    if (m_hasBook) {
        int total = 0;
        int missing = 0;
        for (int i = 0; i < m_book.count(); ++i) {
            const int w = m_book.isMissing(i) ? -1 : countWordsInFile(m_book.absolutePath(i));
            if (w < 0) {
                ++missing;
            } else {
                total += w;
            }
        }
        const QLocale loc;
        QString t = QStringLiteral("%1 %2 · %3 words")
                        .arg(m_book.count())
                        .arg(m_book.count() == 1 ? QStringLiteral("chapter")
                                                 : QStringLiteral("chapters"))
                        .arg(loc.toString(total));
        if (missing > 0) {
            t += QStringLiteral(" · %1 missing").arg(missing);
        }
        m_footer->setText(t);
    }
}

bool BookSidebar::commit(int selectRow)
{
    QString err;
    if (!m_book.save(&err)) {
        m_lastError = err;
        return false;
    }
    m_hasBook = true;
    m_lastError.clear();
    rebuild(selectRow);
    setCurrentFile(m_currentFile);
    if (selectRow >= 0 && selectRow < m_view->count()) {
        m_view->setCurrentRow(selectRow);
    }
    emit bookChanged();
    return true;
}

bool BookSidebar::createBook(const QString &subfolder)
{
    const QString name = subfolder.trimmed();
    if (m_root.isEmpty() || name.isEmpty() || name.contains(QLatin1Char('/'))
        || name.contains(QLatin1Char('\\')) || name.startsWith(QLatin1Char('.'))) {
        emit message(QStringLiteral("A book needs a subfolder name (no slashes)."));
        return false;
    }
    const QString dir = QDir(m_root).filePath(name);
    if (!BookManifest::exists(dir)) {
        if (!QDir().mkpath(dir)) {
            emit message(QStringLiteral("Could not create %1").arg(dir));
            return false;
        }
        QString err;
        if (!BookManifest::fromFolder(dir).save(&err)) {
            emit message(QStringLiteral("Could not write %1/book.json: %2").arg(name, err));
            return false;
        }
    }
    scanBooks();
    m_current = name;
    loadCurrent();
    emit currentBookChanged(m_current);
    emit bookChanged();
    return m_hasBook;
}

void BookSidebar::chooseNewBook()
{
    if (m_root.isEmpty()) {
        emit chooseLibraryRequested();
        return;
    }
    if (m_newBookDialog) {
        m_newBookDialog->raise();
        return;
    }
    auto *dlg = new QInputDialog(window());
    dlg->setObjectName(QStringLiteral("bookNewDialog"));
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->setWindowTitle(QStringLiteral("New Book"));
    dlg->setLabelText(QStringLiteral("Subfolder of %1 for the book (pick one or type a new name):")
                          .arg(QFileInfo(m_root).fileName()));
    dlg->setComboBoxEditable(true);
    dlg->setComboBoxItems(candidateFolders());
    if (!candidateFolders().isEmpty()) {
        dlg->setTextValue(candidateFolders().first());
    }
    dlg->setWindowModality(Qt::WindowModal);
    connect(dlg, &QInputDialog::textValueSelected, this,
            [this](const QString &name) { createBook(name); });
    m_newBookDialog = dlg;
    dlg->open();
}

QString BookSidebar::addFileToBook(const QString &path)
{
    if (m_root.isEmpty()) {
        return QStringLiteral("No library folder");
    }
    const QString rel =
        QDir::cleanPath(QDir(m_root).relativeFilePath(QFileInfo(path).absoluteFilePath()));
    const QStringList parts = rel.split(QLatin1Char('/'));
    if (parts.size() < 2 || parts.first() == QLatin1String("..")) {
        return QStringLiteral("%1 is not in a book subfolder; each book is a subfolder of the "
                              "library.")
            .arg(QFileInfo(path).fileName());
    }
    const QString sub = parts.first();
    if (!m_books.contains(sub)) {
        if (!createBook(sub)) {
            return QStringLiteral("Could not create a book in %1").arg(sub);
        }
    } else {
        selectBook(sub);
    }
    if (!m_hasBook) {
        return QStringLiteral("%1/book.json could not be read").arg(sub);
    }
    addChapters({path});
    return {};
}

int BookSidebar::addChapters(const QStringList &paths)
{
    if (!m_hasBook) {
        return 0;
    }
    int added = 0;
    int last = -1;
    QStringList refused;
    for (const QString &p : paths) {
        const int before = m_book.count();
        const int at = m_book.add(p);
        if (at < 0) {
            refused << QFileInfo(p).fileName();
            continue;
        }
        last = at;
        if (m_book.count() > before) {
            ++added;
        }
    }
    if (!refused.isEmpty()) {
        emit message(QStringLiteral("Not added (chapters must be .md / .txt / .odt files inside %1): %2")
                         .arg(m_current, refused.join(QStringLiteral(", "))));
    }
    if (added > 0) {
        commit(last);
    } else if (last >= 0) {
        m_view->setCurrentRow(last);
    }
    return added;
}

bool BookSidebar::removeChapter(int row)
{
    if (!m_book.remove(row)) {
        return false;
    }
    return commit(std::min(row, m_book.count() - 1));
}

bool BookSidebar::moveChapter(int from, int to)
{
    if (!m_book.move(from, to)) {
        return false;
    }
    return commit(to);
}

bool BookSidebar::setChapter(int row, const BookChapter &chapter)
{
    if (row < 0 || row >= m_book.count()) {
        return false;
    }
    BookChapter c = chapter;
    c.path = m_book.at(row).path; // the file itself is not changed here
    m_book.setChapter(row, c);
    return commit(row);
}

void BookSidebar::onRowsMoved()
{
    if (m_rebuilding) {
        return;
    }
    QStringList order;
    for (int i = 0; i < m_view->count(); ++i) {
        order << m_view->item(i)->data(PathRole).toString();
    }
    const QString moved = m_view->currentItem() ? m_view->currentItem()->data(PathRole).toString()
                                                : QString();
    m_book.reorder(order);
    // Deferred: the view is still finishing the drop when rowsMoved fires.
    QMetaObject::invokeMethod(this, [this, moved]() {
        const int at = m_book.indexOf(moved);
        if (commit(at) && at >= 0) {
            emit message(QStringLiteral("Moved %1 to position %2").arg(m_book.displayTitle(at)).arg(at + 1));
        }
    }, Qt::QueuedConnection);
}

void BookSidebar::onItemClicked(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    const int row = m_view->row(item);
    if (row >= 0 && !m_book.isMissing(row)) {
        emit fileActivated(m_book.absolutePath(row));
    }
}

int BookSidebar::openRow() const
{
    return m_currentFile.isEmpty() ? -1 : m_book.indexOf(QFileInfo(m_currentFile).absoluteFilePath());
}

void BookSidebar::setContextRow(int row)
{
    m_contextRow = row;
    for (int r = 0; r < m_view->count(); ++r) {
        m_view->item(r)->setData(ContextRole, r == row);
    }
}

void BookSidebar::setCurrentFile(const QString &path)
{
    m_currentFile = path;
    const int i = path.isEmpty() ? -1 : m_book.indexOf(QFileInfo(path).absoluteFilePath());
    for (int r = 0; r < m_view->count(); ++r) {
        m_view->item(r)->setData(OpenRole, r == i);
    }
    if (i >= 0) {
        m_view->setCurrentRow(i);
    } else {
        m_view->clearSelection();
    }
}

QMenu *BookSidebar::buildContextMenu(int row)
{
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("bookContextMenu"));
    const bool valid = row >= 0 && row < m_book.count();
    if (valid) {
        QAction *title = menu->addAction(m_book.displayTitle(row));
        title->setObjectName(QStringLiteral("bookContextTitle"));
        title->setEnabled(false);
        menu->addSeparator();
        QAction *open = menu->addAction(QStringLiteral("Open"), this, [this, row]() {
            emit fileActivated(m_book.absolutePath(row));
        });
        open->setEnabled(!m_book.isMissing(row));
        menu->addAction(QStringLiteral("Edit Details…"), this, [this, row]() { editChapter(row); });
        QAction *rename = menu->addAction(QStringLiteral("Rename"), this, [this, row]() { renameChapter(row); });
        menu->addSeparator();
        QAction *up = menu->addAction(QStringLiteral("Move Up"), this,
                                      [this, row]() { moveChapter(row, row - 1); });
        up->setEnabled(row > 0);
        QAction *down = menu->addAction(QStringLiteral("Move Down"), this,
                                        [this, row]() { moveChapter(row, row + 1); });
        down->setEnabled(row + 1 < m_book.count());
        menu->addSeparator();
        QAction *remove = menu->addAction(QStringLiteral("Remove from Book…"), this,
                                          [this, row]() { confirmRemoveChapter(row); });
        remove->setToolTip(QStringLiteral("Asks first; the file is kept unless you choose to delete it"));
        remove->setShortcut(QKeySequence(QKeySequence::Delete));
        menu->addSeparator();
    }
    menu->addAction(QStringLiteral("New Chapter…"), this, &BookSidebar::chooseNewChapter)
        ->setEnabled(m_hasBook);
    menu->addAction(QStringLiteral("Add Existing Chapters…"), this, &BookSidebar::chooseChapterFiles)
        ->setEnabled(m_hasBook);
    menu->addSeparator();
    menu->addAction(QStringLiteral("New Book…"), this, &BookSidebar::chooseNewBook)
        ->setEnabled(!m_root.isEmpty());
    menu->addAction(QStringLiteral("Insert/Update Table of Contents"), this, &BookSidebar::tocRequested)
        ->setEnabled(m_hasBook);
    QAction *renumber = menu->addAction(QStringLiteral("Renumber Files…"), this, [this]() { renumberFiles(); });
    renumber->setObjectName(QStringLiteral("bookRenumber"));
    renumber->setToolTip(QStringLiteral("Rename the chapter files so their numbers follow the book order (asks first)"));
    renumber->setEnabled(m_hasBook && m_book.count() > 0); // says so when nothing needs renaming
    QMenu *exportMenu = menu->addMenu(QStringLiteral("Export Book"));
    exportMenu->setEnabled(m_hasBook && m_book.count() > 0);
    exportMenu->addAction(QStringLiteral("PDF…"), this, &BookSidebar::exportBookPdfRequested);
    exportMenu->addAction(QStringLiteral("EPUB…"), this, &BookSidebar::exportBookEpubRequested);
    menu->addAction(QStringLiteral("Print Book…"), this, &BookSidebar::printBookRequested)
        ->setEnabled(m_hasBook && m_book.count() > 0);
    menu->addAction(QStringLiteral("Change Library Folder…"), this, &BookSidebar::chooseLibraryRequested)
        ->setToolTip(QStringLiteral("The folder your books are in (each book is a subfolder)"));
    return menu;
}

void BookSidebar::editChapter(int row)
{
    if (row < 0 || row >= m_book.count()) {
        return;
    }
    if (m_dialog) {
        m_dialog->close();
    }
    auto *dlg = new BookChapterDialog(m_book.at(row), countWordsInFile(m_book.absolutePath(row)),
                                      window());
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->setWindowModality(Qt::WindowModal);
    const QString path = m_book.at(row).path;
    connect(dlg, &QDialog::accepted, this, [this, dlg, path]() {
        const int i = m_book.indexOf(path); // the row may have moved meanwhile
        if (i >= 0) {
            setChapter(i, dlg->chapter());
        }
    });
    m_dialog = dlg;
    dlg->open();
}

void BookSidebar::chooseChapterFiles()
{
    if (!m_hasBook) {
        return;
    }
    if (m_fileDialog) {
        m_fileDialog->raise();
        m_fileDialog->activateWindow();
        return;
    }
    // Qt dialog, parented to the main window: see LibrarySidebar::createFolderDialog.
    auto *dlg = new QFileDialog(window(), QStringLiteral("Add Chapters"), m_book.bookDir(),
                                QStringLiteral("Chapters (*.md *.txt *.odt)"));
    dlg->setObjectName(QStringLiteral("bookAddDialog"));
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->setFileMode(QFileDialog::ExistingFiles);
    dlg->setOption(QFileDialog::DontUseNativeDialog, true);
    dlg->setWindowModality(Qt::WindowModal);
    connect(dlg, &QFileDialog::filesSelected, this,
            [this](const QStringList &files) { addChapters(files); });
    m_fileDialog = dlg;
    dlg->open();
}

void BookSidebar::applyTheme(const ThemeColors &c)
{
    m_accent = c.accent;
    m_view->viewport()->update();
    setStyleSheet(QStringLiteral(
        "#bookSidebar, #bookEmptyBox { background-color: %1; }"
        "#bookHeader { background-color: %1; }"
        "#bookTitle { color: %2; font-size: 9pt; font-weight: 600; }"
        "#bookHint, #bookFooter { color: %3; font-size: 9pt; }"
        "#bookFooter { padding: 4px 12px 6px 12px; }"
        "#bookCreate, #bookChooseFolder, #bookEmptyNewChapter, #bookEmptyAdd { color: %2; background-color: %4; border: none;"
        " border-radius: 4px; padding: 4px 12px; }"
        "#bookToLibrary, #bookAdd, #bookNew { color: %2; border: none; border-radius: 4px;"
        " padding: 3px 8px; }"
        // "+ Chapter" opens a menu: its arrow gets its own space at the right
        // (without this the macOS style drew it over the text).
        "#bookAdd { padding-right: 20px; }"
        "#bookAdd::menu-indicator { subcontrol-origin: padding; subcontrol-position: center right;"
        " right: 6px; width: 8px; height: 8px; }"
        "#bookToLibrary:hover, #bookAdd:hover, #bookNew:hover { background-color: %4; }"
        "QComboBox#bookPicker { color: %2; background-color: %1; border: none; font-size: 9pt;"
        " font-weight: 600; padding: 3px 22px 3px 4px; }"
        // The drop-down arrow sits in its own 18 px area inside the box.
        "QComboBox#bookPicker::drop-down { subcontrol-origin: padding; subcontrol-position: center right;"
        " width: 18px; border: none; }"
        "QComboBox#bookPicker::down-arrow { image: url(:/ui/arrow-down-%9.png); width: 10px; height: 6px; }"
        "QComboBox#bookPicker:hover { background-color: %4; }"
        "QListWidget#bookList { background-color: %1; color: %2; border: 2px solid transparent; outline: 0;"
        " font-size: 10pt; }"
        // Focus ring: the list shows when it has the keyboard (arrows, Enter, F2, Delete).
        "QListWidget#bookList:focus { border: 2px solid %8; }"
        "QListWidget#bookList::item { padding: 6px 10px; border-bottom: 1px solid %7; }"
        "QListWidget#bookList::item:hover { background-color: %4; }"
        "QListWidget#bookList::item:selected { background-color: %5; color: %6; }")
                      .arg(c.barBg.name(), c.fg.name(), c.muted.name(), c.hover.name(),
                           c.accentSoft.name(), c.accentFg.name(), c.border.name(), c.accent.name(), c.id == QLatin1String("paper") ? QStringLiteral("paper") : QStringLiteral("dark")));
}

static void fitButtons(QMessageBox *box);

QList<QPair<QString, QString>> BookSidebar::renumberPlan() const
{
    QList<QPair<QString, QString>> plan;
    static const QRegularExpression prefix(QStringLiteral(R"(^\d+[\s._-]+(?=\S))"));
    const int width = m_book.count() >= 100 ? 3 : 2;
    for (int i = 0; i < m_book.count(); ++i) {
        const QString rel = m_book.at(i).path;
        if (m_book.isOutside(i) || m_book.isMissing(i)) {
            continue;
        }
        const QFileInfo fi(rel);
        QString name = fi.completeBaseName();
        name.remove(prefix);
        const QString dir = fi.path() == QLatin1String(".") ? QString() : fi.path() + QLatin1Char('/');
        // Numbers follow the chapter numbers; the Contents chapter is 00.
        QString wanted = dir + QStringLiteral("%1 %2").arg(m_book.chapterNumber(i), width, 10, QLatin1Char('0')).arg(name);
        if (!fi.suffix().isEmpty()) {
            wanted += QLatin1Char('.') + fi.suffix();
        }
        if (wanted != rel) {
            plan.append({rel, wanted});
        }
    }
    return plan;
}

bool BookSidebar::renumberFiles(bool ask, QString *error)
{
    const auto plan = renumberPlan();
    if (plan.isEmpty()) {
        emit message(QStringLiteral("The chapter files already follow the book order; nothing to rename."));
        return false;
    }
    const QDir dir(m_book.bookDir());
    // Never overwrite a file that is not part of the plan.
    QSet<QString> moving;
    for (const auto &p : plan) {
        moving.insert(p.first);
    }
    for (const auto &p : plan) {
        if (!moving.contains(p.second) && QFileInfo::exists(dir.filePath(p.second))) {
            const QString msg = QStringLiteral("%1 already exists; nothing was renamed.").arg(p.second);
            if (error) {
                *error = msg;
            }
            emit message(msg);
            return false;
        }
    }
    if (ask) {
        QStringList lines;
        for (const auto &p : plan) {
            lines << QStringLiteral("%1  \u2192  %2").arg(p.first, p.second);
        }
        QMessageBox box(QMessageBox::Question, QStringLiteral("Renumber Files"),
                        plan.size() == 1 ? QStringLiteral("Rename 1 chapter file to follow the book order?")
                                         : QStringLiteral("Rename %1 chapter files to follow the book order?").arg(plan.size()),
                        QMessageBox::NoButton, window());
        box.setObjectName(QStringLiteral("bookRenumberDialog"));
        box.setInformativeText(QStringLiteral("Titles, order and details stay the same. Links to these files from "
                                              "outside the book will not follow."));
        box.setDetailedText(lines.join(QLatin1Char('\n')));
        QPushButton *yes = box.addButton(QStringLiteral("Rename Files"), QMessageBox::AcceptRole);
        QPushButton *no = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(no);
        box.setEscapeButton(no);
        fitButtons(&box);
        box.exec();
        if (box.clickedButton() != yes) {
            return false;
        }
    }
    // Two steps (through temporary names), so 01<->02 swaps cannot collide.
    QList<QPair<QString, QString>> done;
    auto rollback = [&]() {
        for (auto it = done.crbegin(); it != done.crend(); ++it) {
            QFile::rename(dir.filePath(it->second), dir.filePath(it->first));
        }
    };
    QList<QString> temps;
    for (int k = 0; k < plan.size(); ++k) {
        const QString tmp = plan.at(k).first + QStringLiteral(".zw-renumber-%1").arg(k);
        if (!QFile::rename(dir.filePath(plan.at(k).first), dir.filePath(tmp))) {
            rollback();
            const QString msg = QStringLiteral("Could not rename %1; nothing was renamed.").arg(plan.at(k).first);
            if (error) {
                *error = msg;
            }
            emit message(msg);
            return false;
        }
        done.append({plan.at(k).first, tmp});
        temps << tmp;
    }
    for (int k = 0; k < plan.size(); ++k) {
        if (!QFile::rename(dir.filePath(temps.at(k)), dir.filePath(plan.at(k).second))) {
            rollback();
            const QString msg = QStringLiteral("Could not rename %1; nothing was renamed.").arg(plan.at(k).first);
            if (error) {
                *error = msg;
            }
            emit message(msg);
            return false;
        }
        done[k] = {plan.at(k).first, plan.at(k).second};
    }
    for (const auto &p : plan) {
        const int i = m_book.indexOf(p.first);
        if (i >= 0) {
            BookChapter c = m_book.at(i);
            c.path = p.second;
            m_book.setChapter(i, c);
        }
    }
    QString err;
    if (!m_book.save(&err)) {
        emit message(QStringLiteral("Files renamed, but the book could not be saved: %1").arg(err));
    }
    for (const auto &p : plan) {
        const QString oldAbs = QFileInfo(dir.filePath(p.first)).absoluteFilePath();
        const QString newAbs = QFileInfo(dir.filePath(p.second)).absoluteFilePath();
        if (!m_currentFile.isEmpty() && QFileInfo(m_currentFile).absoluteFilePath() == oldAbs) {
            m_currentFile = newAbs;
        }
        emit chapterFileMoved(oldAbs, newAbs);
    }
    rebuild(m_view->currentRow());
    emit bookChanged();
    emit message(QStringLiteral("Renumbered %1 chapter file(s)").arg(plan.size()));
    return true;
}

QString BookSidebar::chapterFileName(int number, const QString &title, const QString &suffix)
{
    static const QRegularExpression bad(QStringLiteral(R"([/\\:*?"<>|\x00-\x1f])"));
    QString t = title;
    t.replace(bad, QStringLiteral(" "));
    t = t.simplified();
    while (t.startsWith(QLatin1Char('.'))) {
        t.remove(0, 1);
    }
    if (t.isEmpty()) {
        t = QStringLiteral("Chapter %1").arg(number);
    }
    return QStringLiteral("%1 %2.%3").arg(number, 2, 10, QLatin1Char('0')).arg(t, suffix);
}

QString BookSidebar::createChapter(const QString &title, const QString &suffixIn)
{
    if (!m_hasBook) {
        emit message(QStringLiteral("Open or create a book first"));
        return {};
    }
    const QString suffix = suffixIn.toLower();
    if (!BookManifest::isChapterSuffix(suffix)) {
        emit message(QStringLiteral("Chapters are .odt, .md or .txt files"));
        return {};
    }
    const QString heading = title.simplified().isEmpty()
        ? QStringLiteral("Chapter %1").arg(m_book.count() + 1)
        : title.simplified();
    const QDir dir(m_book.bookDir());
    QString name = chapterFileName(m_book.count() + 1, heading, suffix);
    for (int n = 2; QFileInfo::exists(dir.filePath(name)); ++n) {
        name = chapterFileName(m_book.count() + 1, heading + QStringLiteral(" %1").arg(n), suffix);
    }
    const QString path = dir.filePath(name);
    bool ok = false;
    if (suffix == QLatin1String("odt")) {
        QTextDocument doc;
        doc.setHtml(QStringLiteral("<h1>%1</h1><p></p>").arg(heading.toHtmlEscaped()));
        ok = DocumentIo::save(&doc, path, DocumentIo::Format::Odt);
    } else {
        QFile f(path);
        const QByteArray text = suffix == QLatin1String("md")
            ? (QStringLiteral("# ") + heading + QStringLiteral("\n\n")).toUtf8()
            : (heading + QStringLiteral("\n\n")).toUtf8();
        ok = f.open(QIODevice::WriteOnly | QIODevice::NewOnly) && f.write(text) == text.size();
    }
    if (!ok) {
        emit message(QStringLiteral("Could not create %1").arg(name));
        return {};
    }
    const int at = m_book.add(path);
    if (at < 0 || !commit(at)) {
        emit message(QStringLiteral("Created %1, but book.json could not be saved").arg(name));
        return path;
    }
    emit message(QStringLiteral("New chapter %1").arg(name));
    emit fileActivated(QDir::cleanPath(path));
    return path;
}

int BookSidebar::contentsRow() const
{
    return m_book.contentsIndex();
}

QString BookSidebar::createContentsChapter()
{
    if (!m_hasBook) {
        return {};
    }
    const QDir dir(m_book.bookDir());
    QString name = QStringLiteral("00 Contents.odt");
    for (int n = 2; QFileInfo::exists(dir.filePath(name)); ++n) {
        name = QStringLiteral("00 Contents %1.odt").arg(n);
    }
    const QString path = dir.filePath(name);
    QTextDocument doc;
    if (!DocumentIo::save(&doc, path, DocumentIo::Format::Odt)) {
        emit message(QStringLiteral("Could not create %1").arg(name));
        return {};
    }
    BookChapter c;
    const int at = m_book.add(path, 0);
    if (at < 0) {
        return {};
    }
    c = m_book.at(at);
    c.title = QStringLiteral("Contents");
    m_book.setChapter(at, c);
    if (!commit(at)) {
        emit message(QStringLiteral("Created %1, but book.json could not be saved").arg(name));
    }
    return QFileInfo(path).absoluteFilePath();
}

void BookSidebar::chooseNewChapter()
{
    if (!m_hasBook) {
        chooseNewBook();
        return;
    }
    if (m_newChapterDialog) {
        m_newChapterDialog->raise();
        return;
    }
    auto *dlg = new QDialog(window());
    dlg->setObjectName(QStringLiteral("bookNewChapterDialog"));
    dlg->setWindowTitle(QStringLiteral("New Chapter"));
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->setWindowModality(Qt::WindowModal);
    auto *form = new QFormLayout(dlg);
    auto *title = new QLineEdit(dlg);
    title->setObjectName(QStringLiteral("chapterTitle"));
    title->setPlaceholderText(QStringLiteral("Chapter %1").arg(m_book.count() + 1));
    form->addRow(QStringLiteral("&Title:"), title);
    auto *format = new QComboBox(dlg);
    format->setObjectName(QStringLiteral("chapterFormat"));
    format->addItem(QStringLiteral("ODT document (.odt)"), QStringLiteral("odt"));
    format->addItem(QStringLiteral("Markdown (.md)"), QStringLiteral("md"));
    format->addItem(QStringLiteral("Plain text (.txt)"), QStringLiteral("txt"));
    // Default to the format most chapters already use.
    QMap<QString, int> uses;
    for (int i = 0; i < m_book.count(); ++i) {
        ++uses[QFileInfo(m_book.at(i).path).suffix().toLower()];
    }
    int best = 0;
    for (int i = 0; i < format->count(); ++i) {
        if (uses.value(format->itemData(i).toString()) > uses.value(format->itemData(best).toString())) {
            best = i;
        }
    }
    format->setCurrentIndex(best);
    form->addRow(QStringLiteral("&Format:"), format);
    auto *where = new QLabel(QStringLiteral("Saved in %1 and added at the end of the book.")
                                 .arg(QDir::toNativeSeparators(m_book.bookDir())), dlg);
    where->setWordWrap(true);
    form->addRow(where);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    connect(dlg, &QDialog::accepted, this, [this, title, format]() {
        createChapter(title->text(), format->currentData().toString());
    });
    m_newChapterDialog = dlg;
    dlg->open();
}

bool BookSidebar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view && event->type() == QEvent::KeyPress && m_renamingRow < 0) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const int row = m_view->currentRow();
        if (row >= 0 && ke->modifiers() == Qt::NoModifier) {
            if (ke->key() == Qt::Key_F2) {
                renameChapter(row);
                return true;
            }
            if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
                confirmRemoveChapter(row);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BookSidebar::renameChapter(int row)
{
    if (row < 0 || row >= m_book.count() || row >= m_view->count()) {
        return;
    }
    QListWidgetItem *item = m_view->item(row);
    m_rebuilding = true;
    item->setText(m_book.displayTitle(row));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    m_rebuilding = false;
    m_renamingRow = row;
    m_view->setCurrentItem(item);
    m_view->editItem(item);
}

// The theme gives dialog buttons generous padding; QMessageBox sizes them
// before that style is applied, which cut "Remove from Book" to "nove from B…".
static void fitButtons(QMessageBox *box)
{
    box->ensurePolished();
    for (QPushButton *b : box->findChildren<QPushButton *>()) {
        b->ensurePolished();
        b->setMinimumWidth(b->sizeHint().width());
    }
}

void BookSidebar::confirmRemoveChapter(int row)
{
    if (row < 0 || row >= m_book.count() || m_removeDialog) {
        return;
    }
    const QString name = m_book.displayTitle(row);
    const QString path = m_book.at(row).path;
    auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("Remove Chapter"),
                                QStringLiteral("Remove \"%1\" from the book?").arg(name),
                                QMessageBox::NoButton, window());
    box->setObjectName(QStringLiteral("bookRemoveDialog"));
    box->setInformativeText(QStringLiteral("The file %1 stays in the book folder.").arg(path));
    box->setAttribute(Qt::WA_DeleteOnClose, true);
    box->setWindowModality(Qt::WindowModal);
    QPushButton *remove = box->addButton(QStringLiteral("Remove from Book"), QMessageBox::AcceptRole);
    QPushButton *del = box->addButton(QStringLiteral("Delete File…"), QMessageBox::DestructiveRole);
    box->addButton(QMessageBox::Cancel);
    box->setDefaultButton(remove);
    fitButtons(box);
    connect(box, &QMessageBox::buttonClicked, this, [this, box, remove, del, path](QAbstractButton *b) {
        const int i = m_book.indexOf(path);
        if (i < 0) {
            return;
        }
        if (b == remove) {
            removeChapter(i);
            emit message(QStringLiteral("Removed %1 from the book (file kept)").arg(path));
        } else if (b == del) {
            // Second, explicit question; Cancel is the default.
            auto *sure = new QMessageBox(QMessageBox::Warning, QStringLiteral("Delete File"),
                                         QStringLiteral("Move %1 to the trash?").arg(path),
                                         QMessageBox::NoButton, window());
            sure->setObjectName(QStringLiteral("bookDeleteDialog"));
            sure->setInformativeText(QStringLiteral("It is also removed from the book."));
            sure->setAttribute(Qt::WA_DeleteOnClose, true);
            sure->setWindowModality(Qt::WindowModal);
            QPushButton *yes = sure->addButton(QStringLiteral("Delete File"), QMessageBox::DestructiveRole);
            QPushButton *no = sure->addButton(QMessageBox::Cancel);
            sure->setDefaultButton(no);
            sure->setEscapeButton(no);
            fitButtons(sure);
            connect(sure, &QMessageBox::buttonClicked, this, [this, yes, path](QAbstractButton *c) {
                const int j = m_book.indexOf(path);
                if (c == yes && j >= 0) {
                    deleteChapterFile(j);
                }
            });
            m_removeDialog = sure;
            sure->open();
        }
    });
    m_removeDialog = box;
    box->open();
}

bool BookSidebar::deleteChapterFile(int row)
{
    if (row < 0 || row >= m_book.count() || m_book.isOutside(row)) {
        return false;
    }
    const QString abs = m_book.absolutePath(row);
    const QString rel = m_book.at(row).path;
    if (QFileInfo::exists(abs) && !QFile::moveToTrash(abs) && !QFile::remove(abs)) {
        emit message(QStringLiteral("Could not delete %1").arg(rel));
        return false;
    }
    removeChapter(row);
    emit message(QStringLiteral("Deleted %1").arg(rel));
    return true;
}
