#pragma once

#include "DocumentIo.hpp"
#include "DocumentMeta.hpp"

#include <QMainWindow>
#include <QString>

class QTextEdit;
class QLabel;
class QToolBar;
class QAction;
class QTimer;
class QMenu;
class QPrinter;
class TypewriterSounds;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void updateStats();
    void toggleChrome();
    void toggleFullscreen();
    void toggleKeySounds();
    void toggleBold();
    void toggleItalic();
    void applyHeading();
    void togglePageGuides();
    void fileOpen();
    void fileSave();
    void fileSaveAs();
    void exportPdf();
    void filePrint();
    void filePageSetup();
    void filePrintPreview();
    void fileProperties();
    void printPreview(QPrinter *printer);
    void hideAwayIdle();
    void syncFormatActions();
    void markDirty();

private:
    void applyDarkTheme();
    void setChromeVisible(bool visible);
    void loadWindowIcon();
    void updateKeySoundsLabel();
    void buildFormatToolbar();
    void buildFileMenu();
    void revealHideAway();
    void scheduleHideAway();
    void considerMouseHideAway(const QPoint &globalPos);
    QString defaultBaseName() const;
    void updateWindowTitle();
    bool maybeSave();
    bool saveToPath(const QString &path, DocumentIo::Format format);
    void setCurrentFile(const QString &path, DocumentIo::Format format);
    void doPrint(QPrinter *printer);
    void paintPageGuides();

    QTextEdit *m_editor = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_keysLabel = nullptr;
    QToolBar *m_formatBar = nullptr;
    QMenu *m_fileMenu = nullptr;
    QTimer *m_hideTimer = nullptr;
    QAction *m_boldAction = nullptr;
    QAction *m_italicAction = nullptr;
    QAction *m_h1Action = nullptr;
    QAction *m_h2Action = nullptr;
    QAction *m_h3Action = nullptr;
    QAction *m_paragraphAction = nullptr;
    QAction *m_pageGuidesAction = nullptr;
    TypewriterSounds *m_keySounds = nullptr;
    QPrinter *m_printer = nullptr;

    QString m_currentPath;
    DocumentIo::Format m_currentFormat = DocumentIo::Format::Odt;
    DocumentMeta m_meta;
    bool m_dirty = false;
    bool m_chromeVisible = false;
    bool m_hideAwayPinned = false;
    bool m_pageGuides = false;
};
