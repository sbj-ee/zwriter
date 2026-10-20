#include "BookSidebar.hpp"

#include "BookChapterDialog.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int PathRole = Qt::UserRole + 1;
}

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
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(12, 8, 6, 8);
    hl->setSpacing(4);
    m_title = new QLabel(header);
    m_title->setObjectName(QStringLiteral("bookTitle"));
    m_title->setTextFormat(Qt::PlainText);
    m_title->setText(QStringLiteral("BOOK"));
    hl->addWidget(m_title);
    m_picker = new QComboBox(header);
    m_picker->setObjectName(QStringLiteral("bookPicker"));
    m_picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_picker->setMinimumContentsLength(8);
    connect(m_picker, &QComboBox::activated, this, [this](int i) {
        selectBook(m_picker->itemData(i).toString());
    });
    hl->addWidget(m_picker, 1);
    const auto tool = [header, hl](const QString &name, const QString &text, const QString &tip) {
        auto *b = new QToolButton(header);
        b->setObjectName(name);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        hl->addWidget(b);
        return b;
    };
    m_libraryButton = tool(QStringLiteral("bookToLibrary"), QStringLiteral("Library"),
                           QStringLiteral("Show the Library sidebar instead (Ctrl+Shift+L)"));
    connect(m_libraryButton, &QToolButton::clicked, this, &BookSidebar::switchToLibrary);
    m_addButton = tool(QStringLiteral("bookAdd"), QStringLiteral("+"),
                       QStringLiteral("Add chapters (.md / .txt inside the book folder)"));
    connect(m_addButton, &QToolButton::clicked, this, &BookSidebar::chooseChapterFiles);
    m_newButton = tool(QStringLiteral("bookNew"), QStringLiteral("New"),
                       QStringLiteral("New book: write book.json in a subfolder of the library"));
    connect(m_newButton, &QToolButton::clicked, this, &BookSidebar::chooseNewBook);
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
        "Write book.json in a subfolder, listing its .md / .txt files in name order"));
    connect(m_createButton, &QPushButton::clicked, this, &BookSidebar::chooseNewBook);
    hint->addWidget(m_createButton, 0, Qt::AlignLeft);
    m_chooseFolderButton = new QPushButton(QStringLiteral("Choose Library Folder…"), m_hintBox);
    m_chooseFolderButton->setObjectName(QStringLiteral("bookChooseFolder"));
    connect(m_chooseFolderButton, &QPushButton::clicked, this,
            &BookSidebar::chooseLibraryRequested);
    hint->addWidget(m_chooseFolderButton, 0, Qt::AlignLeft);
    hint->addStretch(1);
    layout->addWidget(m_hintBox, 1);

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
    m_view->setFocusPolicy(Qt::ClickFocus);
    m_view->setWordWrap(true);
    m_view->setTextElideMode(Qt::ElideRight);
    connect(m_view->model(), &QAbstractItemModel::rowsMoved, this, &BookSidebar::onRowsMoved);
    connect(m_view, &QListWidget::itemClicked, this, &BookSidebar::onItemClicked);
    connect(m_view, &QListWidget::itemActivated, this, &BookSidebar::onItemClicked);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_view->itemAt(pos);
        if (item) {
            m_view->setCurrentItem(item);
        }
        QMenu *menu = buildContextMenu(item ? m_view->row(item) : -1);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(m_view->viewport()->mapToGlobal(pos));
    });
    layout->addWidget(m_view, 1);

    m_footer = new QLabel(this);
    m_footer->setObjectName(QStringLiteral("bookFooter"));
    m_footer->setContentsMargins(12, 4, 12, 6);
    layout->addWidget(m_footer);

    auto *up = new QAction(QStringLiteral("Move Up"), this);
    up->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    up->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(up, &QAction::triggered, this, [this]() {
        moveChapter(currentRow(), currentRow() - 1);
    });
    auto *down = new QAction(QStringLiteral("Move Down"), this);
    down->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
    down->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(down, &QAction::triggered, this, [this]() {
        moveChapter(currentRow(), currentRow() + 1);
    });
    addAction(up);
    addAction(down);

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
    const QString title = QStringLiteral("%1. %2").arg(row + 1).arg(m_book.displayTitle(row));
    if (m_book.isOutside(row)) {
        return title + QStringLiteral("\nOutside the book folder: ") + c.path;
    }
    if (m_book.isMissing(row)) {
        return title + QStringLiteral("\nMissing: ") + c.path;
    }
    const QLocale loc;
    const int words = countWordsInFile(m_book.absolutePath(row));
    QString detail = c.status + QStringLiteral(" · ") + loc.toString(std::max(0, words));
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
    m_rebuilding = false;
    updateHeader();
}

void BookSidebar::updateHeader()
{
    const bool library = !m_root.isEmpty();
    m_picker->setVisible(!m_books.isEmpty());
    m_title->setVisible(m_books.isEmpty());
    m_picker->setToolTip(m_book.bookDir());
    m_addButton->setEnabled(m_hasBook);
    m_newButton->setEnabled(library);
    m_view->setVisible(m_hasBook);
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
                                       ".md and .txt files in name order.")
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
        emit message(QStringLiteral("Not added (chapters must be .md / .txt files inside %1): %2")
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
    QMetaObject::invokeMethod(this, [this, moved]() { commit(m_book.indexOf(moved)); },
                              Qt::QueuedConnection);
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

void BookSidebar::setCurrentFile(const QString &path)
{
    m_currentFile = path;
    const int i = path.isEmpty() ? -1 : m_book.indexOf(QFileInfo(path).absoluteFilePath());
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
        QAction *open = menu->addAction(QStringLiteral("Open"), this, [this, row]() {
            emit fileActivated(m_book.absolutePath(row));
        });
        open->setEnabled(!m_book.isMissing(row));
        menu->addAction(QStringLiteral("Edit Details…"), this, [this, row]() { editChapter(row); });
        menu->addSeparator();
        QAction *up = menu->addAction(QStringLiteral("Move Up"), this,
                                      [this, row]() { moveChapter(row, row - 1); });
        up->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
        up->setEnabled(row > 0);
        QAction *down = menu->addAction(QStringLiteral("Move Down"), this,
                                        [this, row]() { moveChapter(row, row + 1); });
        down->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
        down->setEnabled(row + 1 < m_book.count());
        menu->addSeparator();
        menu->addAction(QStringLiteral("Remove from Book"), this,
                        [this, row]() { removeChapter(row); })
            ->setToolTip(QStringLiteral("The file itself is kept"));
        menu->addSeparator();
    }
    menu->addAction(QStringLiteral("Add Chapters…"), this, &BookSidebar::chooseChapterFiles)
        ->setEnabled(m_hasBook);
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
                                QStringLiteral("Markdown and text (*.md *.txt)"));
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
    setStyleSheet(QStringLiteral(
        "#bookSidebar { background-color: %1; }"
        "#bookHeader { background-color: %1; }"
        "#bookTitle { color: %2; font-size: 9pt; font-weight: 600; }"
        "#bookHint, #bookFooter { color: %3; font-size: 9pt; }"
        "#bookCreate, #bookChooseFolder { color: %2; background-color: %4; border: none;"
        " border-radius: 4px; padding: 4px 12px; }"
        "#bookToLibrary, #bookAdd, #bookNew { color: %2; border: none; border-radius: 4px;"
        " padding: 1px 6px; }"
        "#bookToLibrary:hover, #bookAdd:hover, #bookNew:hover { background-color: %4; }"
        "QComboBox#bookPicker { color: %2; background-color: %1; border: none; font-size: 9pt;"
        " font-weight: 600; padding: 1px 4px; }"
        "QComboBox#bookPicker:hover { background-color: %4; }"
        "QListWidget#bookList { background-color: %1; color: %2; border: none; outline: 0;"
        " font-size: 10pt; }"
        "QListWidget#bookList::item { padding: 4px 8px; border-bottom: 1px solid %7; }"
        "QListWidget#bookList::item:hover { background-color: %4; }"
        "QListWidget#bookList::item:selected { background-color: %5; color: %6; }")
                      .arg(c.barBg.name(), c.fg.name(), c.muted.name(), c.hover.name(),
                           c.accentSoft.name(), c.accentFg.name(), c.border.name()));
}
