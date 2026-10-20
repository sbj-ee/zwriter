#pragma once

#include "BookManifest.hpp"
#include "Theme.hpp"

#include <QPointer>
#include <QWidget>

class BookChapterDialog;
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
// - Drag a chapter, or Move Up / Move Down (Alt+Up / Alt+Down), to reorder;
//   the order is saved to book.json at once.
// - Right-click → Edit Details… for title, status, POV, tags, word target,
//   synopsis and notes.
// - "+" adds .md / .txt files; Remove from Book drops the entry only, never
//   the file. A chapter whose file is gone stays listed, marked missing.
// - Click opens the chapter (the window's unsaved-changes prompt runs first).
class BookSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit BookSidebar(QWidget *parent = nullptr);

    // Loads dir/book.json. No manifest: a hint with a "Create Book" button.
    void setRootPath(const QString &path);
    QString rootPath() const { return m_book.bookDir(); }
    bool hasBook() const { return m_hasBook; }
    const BookManifest &book() const { return m_book; }

    // Re-read book.json and the word counts (e.g. after a chapter was saved).
    void reload();
    void setCurrentFile(const QString &path);
    void applyTheme(const ThemeColors &colors);
    QSize sizeHint() const override;

    // Actions (also on the context menu); each saves book.json.
    bool createBook();                      // from the folder's top-level files
    int addChapters(const QStringList &paths);
    bool removeChapter(int row);
    bool moveChapter(int from, int to);
    bool setChapter(int row, const BookChapter &chapter);
    void editChapter(int row);              // opens the details dialog (non-blocking)
    void chooseRootFolder();
    void chooseChapterFiles();

    QListWidget *view() const { return m_view; }
    QPushButton *createButton() const { return m_createButton; }
    BookChapterDialog *chapterDialog() const { return m_dialog.data(); }
    QFileDialog *fileDialog() const { return m_fileDialog.data(); }
    QMenu *buildContextMenu(int row); // caller owns
    QString lastError() const { return m_lastError; }
    int currentRow() const;

    static QString rootPathKey();
    static QString visibleKey();
    struct Settings {
        QString rootPath;
        bool visible = false;
    };
    static Settings loadSettings();
    static void saveSettings(const Settings &settings);

signals:
    void fileActivated(const QString &path);
    void rootPathChosen(const QString &path);
    void switchToLibrary();
    void bookChanged(); // after every successful save

private:
    void rebuild(int selectRow = -1);
    bool commit(int selectRow);
    void onRowsMoved();
    void onItemClicked(QListWidgetItem *item);
    void updateHeader();
    QString itemText(int row) const;

    BookManifest m_book;
    bool m_hasBook = false;
    bool m_rebuilding = false;
    QString m_currentFile;
    QString m_lastError;
    QLabel *m_title = nullptr;
    QToolButton *m_libraryButton = nullptr;
    QToolButton *m_addButton = nullptr;
    QToolButton *m_chooseButton = nullptr;
    QWidget *m_hintBox = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_chooseFolderButton = nullptr;
    QListWidget *m_view = nullptr;
    QLabel *m_footer = nullptr;
    QPointer<BookChapterDialog> m_dialog;
    QPointer<QFileDialog> m_fileDialog;
    QPointer<QFileDialog> m_folderDialog;
};
