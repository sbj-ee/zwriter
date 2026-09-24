#pragma once

#include "DocumentIo.hpp"
#include "DocumentMeta.hpp"

#include <QMainWindow>
#include <QString>
#include <QPair>
#include <QStringList>

class QTextEdit;
class QLabel;
class QToolBar;
class QAction;
class QTimer;
class QMenu;
class QPrinter;
class QFontComboBox;
class QSpinBox;
class QFrame;
class QWidget;
class QGraphicsDropShadowEffect;
class TypewriterSounds;
class FindReplaceBar;
class QTextTable;
class QFileDialog;
class UpdateChecker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Docs/CI helper: write a few PNGs then quit the app.
    void captureDemoScreenshots(const QString &dir);

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
    void onFontFamilyChosen(const QFont &font);
    void onFontSizeChosen(int pointSize);
    void setThemePaper();
    void setThemeDark();
    void togglePageGuides();
    void toggleFullPageView();
    void toggleTypewriterScroll();
    void toggleFocusMode();
    void setFocusScopeSentence();
    void setFocusScopeParagraph();
    void toggleSmartQuotes();
    void showFind();
    void showReplace();
    void findNext();
    void findPrev();
    void replaceOne();
    void replaceAll();
    void hideFindBar();
    void fileOpen();
    void fileSave();
    void fileSaveAs();
    void exportPdf();
    void filePrint();
    void filePageSetup();
    void filePrintPreview();
    void fileProperties();
    void openRecentFile();
    void clearRecentFiles();
    void insertTable();
    void tableInsertRow();
    void tableInsertColumn();
    void tableRemoveRow();
    void tableRemoveColumn();
    void helpAbout();
    void helpCheckUpdates();
    void onUpdateAvailable(const QString &tag, const QString &url);
    void onUpToDate();
    void onUpdateCheckFailed();
    void printPreview(QPrinter *printer);
    void hideAwayIdle();
    void syncFormatActions();
    void markDirty();
    void onCursorMoved();

private:
    void applyTheme();
    void applyDocumentDefaults();
    QFont defaultDocumentFont() const;
    void setChromeVisible(bool visible);
    void loadWindowIcon();
    void updateKeySoundsLabel();
    void buildFormatToolbar();
    void buildFileMenu();
    void buildViewMenu();
    void buildHelpMenu();
    void buildInsertMenu();
    void rebuildRecentMenu();
    void addToRecentFiles(const QString &path);
    void loadSettings();
    void saveSettings() const;
    void revealHideAway();
    void scheduleHideAway();
    void considerMouseHideAway(const QPoint &globalPos);
    QString defaultBaseName() const;
    void updateWindowTitle();
    bool maybeSave();
    bool saveToPath(const QString &path, DocumentIo::Format format);
    void setCurrentFile(const QString &path, DocumentIo::Format format);
    bool openPath(const QString &path);
    QString documentsStartDir() const;
    void rememberDocDir(const QString &path);
    void setupNativeFileDialog(QFileDialog &dlg) const;
    QString runSaveDocumentDialog(DocumentIo::Format *outFormat);
    QString runOpenDocumentDialog();
    QString runExportPdfDialog();
    static QString suffixForFilter(const QString &filter);
    static void syncSaveNameToFilter(QFileDialog &dlg, const QString &filter);
    void doPrint(QPrinter *printer);
    void paintPageGuides();
    void applyFullPageView();
    void updateFullPageGeometry();
    QSizeF printerPageSizePx() const;
    void applyDocumentPageMetrics(const QSize &pagePx);
    void clearDocumentPageMetrics();
    void centerCaret();
    void updateFocusHighlight();
    QPair<int, int> focusRange() const; // start, end positions in document
    bool trySmartTypography(QKeyEvent *event);
    bool findInDoc(bool forward);
    void syncViewActions();
    QTextTable *currentTable() const;
    void updateTableActions();
    bool isPaperTheme() const;

    QTextEdit *m_editor = nullptr;
    QWidget *m_desk = nullptr;
    QFrame *m_pageFrame = nullptr;
    QGraphicsDropShadowEffect *m_pageShadow = nullptr;
    FindReplaceBar *m_findBar = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_keysLabel = nullptr;
    QToolBar *m_formatBar = nullptr;
    QMenu *m_fileMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_recentMenu = nullptr;
    QTimer *m_hideTimer = nullptr;
    QAction *m_boldAction = nullptr;
    QAction *m_italicAction = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QSpinBox *m_fontSizeSpin = nullptr;
    QAction *m_themePaperAction = nullptr;
    QAction *m_themeDarkAction = nullptr;
    QAction *m_h1Action = nullptr;
    QAction *m_h2Action = nullptr;
    QAction *m_h3Action = nullptr;
    QAction *m_paragraphAction = nullptr;
    QAction *m_pageGuidesAction = nullptr;
    QAction *m_fullPageViewAction = nullptr;
    QAction *m_typewriterScrollAction = nullptr;
    QAction *m_focusModeAction = nullptr;
    QAction *m_focusSentenceAction = nullptr;
    QAction *m_focusParagraphAction = nullptr;
    QAction *m_smartQuotesAction = nullptr;
    TypewriterSounds *m_keySounds = nullptr;
    UpdateChecker *m_updateChecker = nullptr;
    QAction *m_checkUpdatesAction = nullptr;
    QMenu *m_helpMenu = nullptr;
    QMenu *m_insertMenu = nullptr;
    QAction *m_tableInsertRowAction = nullptr;
    QAction *m_tableInsertColAction = nullptr;
    QAction *m_tableRemoveRowAction = nullptr;
    QAction *m_tableRemoveColAction = nullptr;
    QPrinter *m_printer = nullptr;

    QString m_currentPath;
    DocumentIo::Format m_currentFormat = DocumentIo::Format::Odt;
    DocumentMeta m_meta;
    QStringList m_recentFiles;
    QString m_lastDocDir;
    QString m_lastSaveFilter;
    bool m_dirty = false;
    bool m_chromeVisible = false;
    bool m_hideAwayPinned = false;
    bool m_pageGuides = false;
    bool m_fullPageView = true;        // default ON — paper page frame
    bool m_typewriterScroll = true;   // default ON — distraction-free
    bool m_focusMode = false;         // default OFF
    bool m_focusSentence = false;     // false = paragraph scope
    bool m_smartQuotes = false;       // default OFF
    bool m_centering = false;         // re-entrancy guard for scroll
    QString m_themeId = QStringLiteral("paper"); // paper (default) | dark
};
