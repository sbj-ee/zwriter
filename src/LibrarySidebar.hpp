#pragma once

#include "Theme.hpp"

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QWidget>

class QFileSystemModel;
class QLabel;
class QModelIndex;
class QToolButton;
class QTreeView;

// Library sidebar: the Markdown and text files under one folder the user
// picks. Folders are always listed (so the tree can be walked); of the files
// only .md and .txt are shown (any case), everything else -- and hidden
// entries -- is left out. Clicking a file asks the window to open it; the
// window runs its normal unsaved-changes prompt first.
class LibrarySidebar : public QWidget
{
    Q_OBJECT

public:
    explicit LibrarySidebar(QWidget *parent = nullptr);

    // Empty or missing folder = nothing listed, with a hint to choose one.
    void setRootPath(const QString &path);
    QString rootPath() const { return m_rootPath; }

    // Highlight this file in the tree if it is in the library (e.g. after it
    // was opened some other way). Empty clears the selection.
    void setCurrentFile(const QString &path);

    void applyTheme(const ThemeColors &colors);

    // Asks for a folder (QFileDialog) and makes it the root; emits
    // rootPathChosen() when the user picked one.
    void chooseRootFolder();

    QSize sizeHint() const override;

    QTreeView *view() const { return m_view; }
    QFileSystemModel *model() const { return m_model; }

    static QStringList nameFilters(); // {"*.md", "*.txt"}
    static bool acceptsFile(const QString &fileName);

    // QSettings: library/rootPath (default empty) and library/visible
    // (default off).
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

private:
    void onClicked(const QModelIndex &index);
    void updateHeader();

    QFileSystemModel *m_model = nullptr;
    QTreeView *m_view = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_hint = nullptr;
    QToolButton *m_chooseButton = nullptr;
    QString m_rootPath;
    QString m_lastActivated;
    QElapsedTimer m_activateClock;
};
