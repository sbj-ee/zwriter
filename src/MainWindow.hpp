#pragma once

#include "DocumentIo.hpp"
#include "DocumentMeta.hpp"

#include <QMainWindow>
#include <QString>
#include <QPair>
#include <QStringList>

class QTextEdit;
class PageTextEdit;
class PageCanvas;
class QLabel;
class QToolBar;
class QAction;
class QTimer;
class QKeyEvent;
class QMenu;
class QPrinter;
class QPageLayout;
class QFontComboBox;
class QSpinBox;
class QComboBox;
class QTextListFormat;
class QFrame;
class QScrollArea;
class QWidget;
class QGraphicsDropShadowEffect;
class TypewriterSounds;
class FindReplaceBar;
class QTextTable;
class QFileDialog;
class UpdateChecker;
class SpellChecker;
class SpellHighlighter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Open a file handed to us by the OS (file manager, command line, macOS open
    // event). Asks about unsaved changes first; unrecognised extensions open as
    // plain text and are saved back as plain text.
    void openExternalFile(const QString &path);

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
    void toggleUnderline();
    void editPastePlain();
    void clearFormatting();
    void fileNew();
    void applyHeading();
    void onFontFamilyChosen(const QFont &font);
    void onFontSizeChosen(int pointSize);
    void setThemePaper();
    void setThemeDark();
    void setThemeInverse();
    void togglePageGuides();
    void toggleFullPageView();
    void toggleTypewriterScroll();
    void toggleFocusMode();
    void setFocusScopeSentence();
    void setFocusScopeParagraph();
    void toggleSmartQuotes();
    void toggleSpellCheck();
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
    bool moveToAdjacentCell(bool forward);
    void editHeaderFooter();
    void showEditorContextMenu(const QPoint &pos);
    void tableInsertRow();
    void tableInsertColumn();
    void tableRemoveRow();
    void tableRemoveColumn();
    void helpAbout();
    void helpCheckUpdates();
    void onUpdateAvailable(const QString &tag, const QString &url);
    void onUpToDate();
    void onUpdateCheckFailed(const QString &reason);
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
    void setChromePinned(bool pinned);
    void loadWindowIcon();
    void buildFormatToolbar();
    void buildFileMenu();
    void buildEditMenu();
    void createFormatActions();
    void buildFormatMenu();
    void buildViewMenu();
    void buildHelpMenu();
    void applyList(int style, bool on);
    void rebuildRecentMenu();
    void addToRecentFiles(const QString &path);
    void loadSettings();
    void saveSettings() const;
    void loadPrinterSettings();
    void savePrinterSettings() const;
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
    void paintPageOverlays(const QRect &clip);
    void paintPageGuides(QPainter &painter, int firstPage, int lastPage, qreal pageH);
    void syncPageFrameHeight();
    void refreshPageCount();
    void updatePageLabel();
    void scheduleStatusUpdate();
    void setRootFrameMargins(qreal left, qreal top, qreal right, qreal bottom, qreal documentMargin);
    void togglePageNumbers();
    void forgetPageNumberState();
    QString *headerFooterBand(int index); // 0..5: header L/C/R, footer L/C/R
    void insertPageBreak();
    void handleEnterOnPageBreak(QKeyEvent *ke);
    bool pageNumbersOn() const;
    void paintHeaderFooter(QPainter *painter, const QRectF &pageRect,
                           int pageNumber, int pageCount,
                           const QPageLayout *printLayout = nullptr) const;
    QString expandHeaderFooterTokens(const QString &pattern, int pageNumber,
                                    int pageCount) const;
    int visiblePageNumber() const;
    int documentPageCount() const;
    void applyFullPageView();
    void updateFullPageGeometry();
    QSizeF printerPageSizePx() const;
    QMarginsF pageMarginsPx() const;
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
    void setTheme(const QString &id);

    PageTextEdit *m_editor = nullptr;
    PageCanvas *m_pageCanvas = nullptr;
    QLabel *m_pageLabel = nullptr;
    QAction *m_pageNumbersAction = nullptr;
    bool m_pageSyncQueued = false;
    int m_pageCount = 1;              // cached QTextDocument::pageCount()
    QTimer *m_statusTimer = nullptr;  // debounced word count / page label
    bool m_suppressDirty = false;     // layout-only document changes in progress
    // Format > Page Numbers: header/footer as it was when numbers were turned
    // off (restored when turned back on), and the band the toggle itself filled.
    struct PageNumbersSnapshot {
        bool valid = false;
        QString bands[6];
        int addedBand = -1;
        QString addedBandBefore;
    };
    PageNumbersSnapshot m_pageNumbersSnapshot;
    int m_pageNumberBand = -1;
    QString m_pageNumberBandBefore;
    bool m_mouseActive = false;       // pointer button held in the editor
    QWidget *m_desk = nullptr;
    QScrollArea *m_pageScroll = nullptr;
    QFrame *m_pageFrame = nullptr;
    FindReplaceBar *m_findBar = nullptr;
    QLabel *m_statsLabel = nullptr;
    QToolBar *m_formatBar = nullptr;
    QMenu *m_fileMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_recentMenu = nullptr;
    QTimer *m_hideTimer = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_formatMenu = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_cutAction = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_underlineAction = nullptr;
    QAction *m_alignLeftAction = nullptr;
    QAction *m_alignCenterAction = nullptr;
    QAction *m_alignRightAction = nullptr;
    QAction *m_alignJustifyAction = nullptr;
    QAction *m_bulletListAction = nullptr;
    QAction *m_numberListAction = nullptr;
    QAction *m_clearFormatAction = nullptr;
    QComboBox *m_styleCombo = nullptr;
    QAction *m_boldAction = nullptr;
    QAction *m_italicAction = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QSpinBox *m_fontSizeSpin = nullptr;
    QAction *m_themePaperAction = nullptr;
    QAction *m_themeDarkAction = nullptr;
    QAction *m_themeInverseAction = nullptr;
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
    QAction *m_spellCheckAction = nullptr;
    QAction *m_alwaysShowChromeAction = nullptr;
    QAction *m_keySoundsAction = nullptr;
    TypewriterSounds *m_keySounds = nullptr;
    UpdateChecker *m_updateChecker = nullptr;
    SpellChecker *m_spellChecker = nullptr;
    SpellHighlighter *m_spellHighlighter = nullptr;
    QAction *m_checkUpdatesAction = nullptr;
    QMenu *m_helpMenu = nullptr;
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
    bool m_dirty = false;
    bool m_chromeVisible = false;
    bool m_hideAwayPinned = true;      // default: toolbar/menus/status stay visible
    bool m_pageGuides = false;
    bool m_fullPageView = true;        // default ON — paper page frame
    bool m_typewriterScroll = true;   // default ON — distraction-free
    bool m_focusMode = false;         // default OFF
    bool m_focusSentence = false;     // false = paragraph scope
    bool m_smartQuotes = false;       // default OFF
    bool m_spellCheck = true;         // default ON
    bool m_centering = false;         // re-entrancy guard for scroll
    QString m_themeId = QStringLiteral("paper"); // paper (default) | dark | inverse
};
