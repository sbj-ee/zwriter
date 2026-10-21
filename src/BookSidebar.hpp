#pragma once

#include "BookManifest.hpp"
#include "Theme.hpp"

#include <QPointer>
#include <QMessageBox>
#include <QWidget>

#include "BookChapterDialog.hpp"
class QComboBox;
#include <QInputDialog>
#include <QFileDialog>
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPushButton;
class QToolButton;

// Book sidebar: the chapters / pieces of one book, in book order, from the
// book folder's book.json (see BookManifest). Same place and look as the
// Library sidebar (View → Book Sidebar, Ctrl+Alt+B; the two take turns).
//
// The library folder can hold several books: each book is a subfolder of it
// with its own book.json. The picker at the top lists them; New Book… writes
// book.json in an existing or new subfolder. Chapters must be inside their
// book's folder.
//
// - Drag a chapter, or Move Up / Move Down (Alt+Up / Alt+Down), to reorder;
//   the order is saved to book.json at once.
// - Right-click → Edit Details… for title, status, POV, tags, word target,
//   synopsis and notes.
// - "+" adds .md / .txt / .odt files; Remove from Book drops the entry only, never
//   the file. A chapter whose file is gone stays listed, marked missing.
// - Click opens the chapter (the window's unsaved-changes prompt runs first).
class BookSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit BookSidebar(QWidget *parent = nullptr);

    // The library folder; its subfolders with a book.json are the books. The
    // previously picked book is kept when it is still there, else the first.
    void setRootPath(const QString &libraryPath);
    QString rootPath() const { return m_root; }
    // Book subfolder names (natural order), and the one shown ("" = none).
    QStringList books() const { return m_books; }
    QString currentBook() const { return m_current; }
    // The book to show once setRootPath() runs (saved setting); no signal.
    void selectBookOnLoad(const QString &subfolder) { m_current = subfolder; }
    bool selectBook(const QString &subfolder);
    // Subfolders that could still become a book (no book.json, not hidden).
    QStringList candidateFolders() const;
    bool hasBook() const { return m_hasBook; }
    const BookManifest &book() const { return m_book; }

    // Re-read book.json and the word counts (e.g. after a chapter was saved).
    void reload();
    void setCurrentFile(const QString &path);
    void applyTheme(const ThemeColors &colors);
    QSize sizeHint() const override;

    // Actions (also on the context menu); each saves book.json.
    // Writes <library>/<subfolder>/book.json (creating the folder if needed)
    // with the .md / .txt / .odt files already in it, then shows it.
    bool createBook(const QString &subfolder);
    void chooseNewBook();                   // asks for the subfolder (non-blocking)
    // Library → Add to Book: the file joins the book of the subfolder it is
    // in (that book is created first if needed). Returns an error, or "".
    QString addFileToBook(const QString &path);
    int addChapters(const QStringList &paths);
    bool removeChapter(int row);
    bool moveChapter(int from, int to);
    bool setChapter(int row, const BookChapter &chapter);
    void editChapter(int row);              // opens the details dialog (non-blocking)
    void chooseChapterFiles();
    // New Chapter: writes "<NN> <title>.<suffix>" (suffix odt / md / txt) in the
    // book folder with the title as its first heading, appends it to book.json
    // and opens it. Returns the new file's path, or "" (message() says why).
    QString createChapter(const QString &title, const QString &suffix);
    void chooseNewChapter();                // asks for title + format (non-blocking)
    static QString chapterFileName(int number, const QString &title, const QString &suffix);
    // F2: edit the chapter's title (book.json) in place in the list.
    void renameChapter(int row);
    // Delete: asks first. "Remove from Book" (the default) keeps the file;
    // "Delete File…" asks again before moving the file to the trash.
    void confirmRemoveChapter(int row);
    bool deleteChapterFile(int row); // no questions: moves the file to the trash, removes the row

    QListWidget *view() const { return m_view; }
    QPushButton *createButton() const { return m_createButton; }
    QComboBox *picker() const { return m_picker; }
    QInputDialog *newBookDialog() const { return m_newBookDialog.data(); }
    BookChapterDialog *chapterDialog() const { return m_dialog.data(); }
    QFileDialog *fileDialog() const { return m_fileDialog.data(); }
    QDialog *newChapterDialog() const { return m_newChapterDialog.data(); }
    QToolButton *addButton() const { return m_addButton; }
    QMessageBox *removeDialog() const { return m_removeDialog.data(); }
    QWidget *emptyBox() const { return m_emptyBox; }
    QMenu *buildContextMenu(int row); // caller owns
    QString lastError() const { return m_lastError; }
    int currentRow() const;

    static QString currentBookKey();
    static QString visibleKey();
    struct Settings {
        QString currentBook;
        bool visible = false;
    };
    static Settings loadSettings();
    static void saveSettings(const Settings &settings);

signals:
    void fileActivated(const QString &path);
    void chooseLibraryRequested(); // no library folder yet
    void currentBookChanged(const QString &subfolder);
    void message(const QString &text); // e.g. a file outside the book was refused
    void switchToLibrary();
    void bookChanged(); // after every successful save
    void tocRequested(); // "Insert/Update Table of Contents" from the menu
    // Export Book > PDF / EPUB and Print Book from the menu (1.10.0).
    void exportBookPdfRequested();
    void exportBookEpubRequested();
    void printBookRequested();

private:
    void loadCurrent();
    void scanBooks();
    void rebuild(int selectRow = -1);
    bool commit(int selectRow);
    void onRowsMoved();
    void onItemClicked(QListWidgetItem *item);
    void updateHeader();
    QString itemText(int row) const;

    QString m_root;
    QStringList m_books;
    QString m_current;
    QComboBox *m_picker = nullptr;
    QToolButton *m_newButton = nullptr;
    QPointer<QInputDialog> m_newBookDialog;
    BookManifest m_book;
    bool m_hasBook = false;
    bool m_rebuilding = false;
    QString m_currentFile;
    QString m_lastError;
    QLabel *m_title = nullptr;
    QToolButton *m_libraryButton = nullptr;
    QToolButton *m_addButton = nullptr;
    QWidget *m_hintBox = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_chooseFolderButton = nullptr;
    QListWidget *m_view = nullptr;
    QLabel *m_footer = nullptr;
    QPointer<BookChapterDialog> m_dialog;
    QPointer<QFileDialog> m_fileDialog;
    QPointer<QDialog> m_newChapterDialog;
    QPointer<QMessageBox> m_removeDialog;
    QWidget *m_emptyBox = nullptr;
    int m_renamingRow = -1;
    bool eventFilter(QObject *watched, QEvent *event) override;
};
