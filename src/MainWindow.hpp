#pragma once

#include "BookExport.hpp"
#include "BookToc.hpp"
#include "DocumentIo.hpp"
#include "DocumentMeta.hpp"

#include "UpdateHost.hpp"

#include <QMainWindow>
#include <QTextCharFormat>
#include <QPageLayout>
#include <QPageSize>
#include <QPointer>
#include "ParagraphDialog.hpp"
#include <QString>
#include <QPair>
#include <QStringList>

class QTextEdit;
class PageTextEdit;
class PageCanvas;
class QLabel;
class QComboBox;
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
class QTextDocument;
class QFileDialog;
namespace Alignment { class Actions; }
class SpellChecker;
class WritingHighlighter;
class PreviewPane;
class LibrarySidebar;
class BookSidebar;
class QSplitter;

class MainWindow : public QMainWindow, public zwriter::UpdateHost
{
    Q_OBJECT

public:
    // The menu bar, toolbar and status bar are shown (not hidden away).
    bool chromeShown() const;
    // Ctrl+Tab: the editor to the chapter list and back.
    void swapEditorAndChapters();
    // The book's ToC entries with the whole-book PDF's page numbers.
    QList<BookToc::Entry> bookTocEntries() const;
    // After a rename in the Book sidebar: the chapter's title heading follows.
    void retitleChapterHeading(const QString &path, const QString &oldTitle, const QString &newTitle);
    explicit MainWindow(QWidget *parent = nullptr);

    // Paragraph formatting (1.7.0); public for tests.
    void showParagraphDialog();                 // Format → Paragraph… (non-blocking)
    class ParagraphDialog *paragraphDialog() const { return m_paragraphDialog.data(); }
    class Ruler *ruler() const { return m_ruler; }

    // Open a file handed to us by the OS (file manager, command line, macOS open
    // event). Asks about unsaved changes first; unrecognised extensions open as
    // plain text and are saved back as plain text.
    void openExternalFile(const QString &path);

    // zwriter::UpdateHost (the in-app updater's view of this window).
    bool hasUnsavedChanges() const override { return m_dirty; }
    QStringList restartArguments() const override;
    bool closeForRestart() override { return close(); }

    // Docs/CI helper: write a few PNGs then quit the app.
    void captureDemoScreenshots(const QString &dir);

    // Left/right page-guide x positions in document units (the editor's
    // cursorRect() space). The continuous view's guides mark the text column,
    // so text always starts on the left guide.
    QPair<qreal, qreal> pageGuideColumn() const;

    // Insert → Image… without the file dialog (also used by tests): insert
    // the picture at the caret. False (with a status message) if it isn't one.
    bool insertImageFile(const QString &path);
    // The picture under a document position for Image Properties (position of
    // the image character, or -1).
    int imagePositionAt(const QPoint &documentPos) const;
    // Right-click → Image Properties… for the picture at `position`.
    void showImageProperties(int position);
    // A caption paragraph right under the picture at `position`: centred,
    // italic, 2 pt smaller than body text, "Caption" selected for typing.
    bool addCaption(int position);
    // File > Insert/Update Table of Contents (needs an open book); and the
    // automatic update of an existing one when the book changes.
    bool insertBookToc();
    bool refreshBookToc();
    // Figure <chapter>.<n> labels on Caption paragraphs; chapter = place of
    // this file in the open book (0 = not in one).
    int renumberCaptions(bool joinPrevious = false);
    int chapterNumber() const;
    // Folder the Save dialog opens in: the file's own folder, else the open
    // book's folder (bookDir), else the last-used / Documents folder.
    QString saveStartDir(const QString &bookDir) const;
    void setCurrentPathForTest(const QString &path) { m_currentPath = path; }
    bool openPathForTest(const QString &path) { return openPath(path); }
    // The open document's sheet (1.12.0); public for tests.
    QPageLayout documentPageLayout() const;
    void setDocumentPageSizeForTest(const QPageSize &size) { setDocumentPageSize(size, false); applyFullPageView(); }
    int alignKeptTocTabs();
    QString bookExportStartDir() const;
    // Folder the Open dialog starts in (1.11.3): the open book's folder, else
    // the open file's folder, else the last-used / Documents folder.
    QString openStartDir() const;
    // Insert > Symbol… and \name autocomplete (1.13.0); public for tests.
    QAction *symbolAction() const { return m_symbolAction; }
    QAction *symbolAutocompleteAction() const { return m_symbolAutocompleteAction; }
    QAction *mathReplaceAction() const { return m_mathReplaceAction; }
    QMenu *insertMenu() const { return m_insertMenu; }
    class SymbolDialog *symbolDialog() const;
    void showSymbolDialog();
    QAction *subscriptAction() const { return m_subscriptAction; }
    QAction *superscriptAction() const { return m_superscriptAction; }
    // View zoom (1.14.0), display only.
    QAction *zoomInAction() const { return m_zoomInAction; }
    QAction *zoomOutAction() const { return m_zoomOutAction; }
    QAction *zoomResetAction() const { return m_zoomResetAction; }
    QAction *fullPageViewAction() const { return m_fullPageViewAction; }
    QComboBox *zoomCombo() const { return m_zoomCombo; }
    int viewZoomPercent() const { return m_viewZoomPercent; }
    void setViewZoomPercent(int percent);
    bool saveToPathForTest(const QString &path) { return saveToPath(path, DocumentIo::Format::Odt); }
    // File → Export as PDF without the dialog (also used by tests).
    bool exportPdfTo(const QString &path);
    // Whole book (1.10.0), without the dialogs (also used by tests). Print
    // Book and Export Book > PDF share the layout: BookExport::build + paint.
    bool exportBookPdfTo(const QString &path, const BookExport::Options &options, int *pages = nullptr);
    bool exportBookEpubTo(const QString &path, const BookExport::Options &options);
    int printBookTo(QPrinter *printer, const BookExport::Options &options);
    QMenu *fileMenu() const { return m_fileMenu; }
    // Markdown mode: copy the picture into "<name>.assets/" and type a link.
    bool insertMarkdownPicture(const QByteArray &data);

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
    void setVerticalAlign(QTextCharFormat::VerticalAlignment align);
    void editPastePlain();
    void clearFormatting();
    void fileNew();
    void insertImageFromFile();
    void insertCaptionHere();
    void fileNewMarkdown();
    void applyHeading();
    void onFontFamilyChosen(const QFont &font);
    void onFontSizeChosen(int pointSize);
    void setThemePaper();
    void setThemeDark();
    void setThemeInverse();
    void togglePageGuides();
    void toggleFullPageView();
    void toggleFitPageWidth();
    void toggleTypewriterScroll();
    void toggleFocusMode();
    void setFocusScopeSentence();
    void setFocusScopeParagraph();
    void toggleSmartQuotes();
    void toggleSpellCheck();
    void toggleMarkdownPreview();
    void toggleLibrary();
    void toggleBook();
    void chooseLibraryFolder();
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
    void exportBookPdf();
    void exportBookEpub();
    void printBook();
    void filePrint();
    void filePageSetup();
    void filePageSizeAndUnits();
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
    void printPreview(QPrinter *printer);
    void hideAwayIdle();
    void syncFormatActions();
    void markDirty();
    void onCursorMoved();

private:
    void applyTheme();
    void applyDocumentDefaults();
    QFont defaultDocumentFont() const; // the current mode's body font
    QFont richDocumentFont() const;    // ODT/TXT/RTF: Courier-class 12 pt
    QFont markdownFont() const;        // Markdown mode: iA Writer Duo
    // Markdown mode (.md / .markdown): plain-text editing with inline styling,
    // iA-style view defaults; rich formatting, tables, page breaks, headers/
    // footers and Full Page are off. Decided per document when it is opened
    // or created.
    void setMarkdownMode(bool on);
    void applyMarkdownBlockFormat();
    void syncRichActions();
    void updateColumnCap();
    void startNewDocument(bool markdown);
    void setChromeVisible(bool visible);
    void setChromePinned(bool pinned);
    void loadWindowIcon();
    void buildFormatToolbar();
    void buildFileMenu();
    void buildEditMenu();
    void createFormatActions();
    void buildFormatMenu();
    void buildInsertMenu();
    void buildViewMenu();
    void buildHelpMenu();
    void applyList(int style, bool on);
    void rebuildRecentMenu();
    void addToRecentFiles(const QString &path);
    void loadSettings();
    void saveSettings() const;
    void loadPrinterSettings();
    // The open document's sheet (1.12.0: per document; see PageSetup.hpp).
    void setDocumentPageSize(const QPageSize &size, bool landscape);
    void savePrinterSettings() const;
    void revealHideAway();
    void scheduleHideAway();
    void considerMouseHideAway(const QPoint &globalPos);
    QString defaultBaseName() const;
    void updateWindowTitle();
    bool maybeSave();
    // TXT/RTF/Markdown can't hold images: ask before saving without them.
    bool confirmImagesDropped(const QTextDocument *doc, DocumentIo::Format format);
    // Markdown saved: offer to delete pictures zwriter put in <name>.assets/
    // that the text no longer links to (default: keep). Returns files deleted.
    int offerAssetCleanup(const QString &markdownPath, const QString &text);
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
    qreal fitPageZoom() const;
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
    void saveLibrarySettings() const;
    void saveBookSettings() const;
    // What the document looked like when it was opened / last saved; a
    // "modification" that leaves it identical is not an unsaved change.
    void captureCleanBaseline();
    bool contentDiffersFromClean() const;
    void showSidebar(bool book); // Library and Book take turns in the same slot
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
    QLabel *m_styleLabel = nullptr;
    QAction *m_tocAction = nullptr;
    QMenu *m_exportBookMenu = nullptr;
    QAction *m_printBookAction = nullptr;
    BookExport::Options m_bookExportOptions;
    bool bookReadyForExport();
    void rememberBookExportDir(const QString &exportedFile);
    QString runBookExportPath(const QString &title, const QString &filter, const QString &suffix);
    class QTimer *m_captionTimer = nullptr;
    bool m_renumbering = false;
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
    QAction *m_subscriptAction = nullptr;
    QAction *m_superscriptAction = nullptr;
    Alignment::Actions *m_alignActions = nullptr;
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
    QList<QAction *> m_namedStyleActions; // Title, Subtitle, Block Quote, Caption, Bibliography
    QAction *m_paragraphDialogAction = nullptr;
    QAction *m_indentAction = nullptr;
    QAction *m_outdentAction = nullptr;
    QAction *m_rulerAction = nullptr;
    QMenu *m_insertMenu = nullptr;
    QAction *m_symbolAction = nullptr;
    QAction *m_symbolAutocompleteAction = nullptr;
    QAction *m_mathReplaceAction = nullptr;
    QPointer<class SymbolDialog> m_symbolDialog;
    class Ruler *m_ruler = nullptr;
    QPointer<class ParagraphDialog> m_paragraphDialog;
    void applyNamedStyle(const QString &name);
    void syncStyleControls();
    QAction *m_paragraphAction = nullptr;
    QAction *m_pageGuidesAction = nullptr;
    QAction *m_fullPageViewAction = nullptr;
    QAction *m_fitPageWidthAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_zoomResetAction = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    int m_viewZoomPercent = 100;
    void syncZoomControls();
    int shownZoomPercent() const;
    QAction *m_typewriterScrollAction = nullptr;
    QAction *m_focusModeAction = nullptr;
    QAction *m_focusSentenceAction = nullptr;
    QAction *m_focusParagraphAction = nullptr;
    QAction *m_smartQuotesAction = nullptr;
    QAction *m_spellCheckAction = nullptr;
    QAction *m_alwaysShowChromeAction = nullptr;
    QAction *m_keySoundsAction = nullptr;
    TypewriterSounds *m_keySounds = nullptr;
    SpellChecker *m_spellChecker = nullptr;
    WritingHighlighter *m_highlighter = nullptr; // spell check + Markdown styling
    QAction *m_checkUpdatesAction = nullptr;
    QAction *m_checkUpdatesAtStartupAction = nullptr;
    QAction *m_newMarkdownAction = nullptr;
    QAction *m_insertTableAction = nullptr;
    QAction *m_insertImageAction = nullptr;
    QAction *m_insertCaptionAction = nullptr;
    QAction *m_pageBreakAction = nullptr;
    QAction *m_headerFooterAction = nullptr;
    QMenu *m_helpMenu = nullptr;
    QAction *m_tableInsertRowAction = nullptr;
    QAction *m_tableInsertColAction = nullptr;
    QAction *m_tableRemoveRowAction = nullptr;
    QAction *m_tableRemoveColAction = nullptr;
    QPrinter *m_printer = nullptr;
    // Side panes (PreviewPane.cpp / LibrarySidebar.cpp): library | desk | preview.
    QSplitter *m_splitter = nullptr;
    LibrarySidebar *m_library = nullptr;
    BookSidebar *m_book = nullptr;
    QString m_cleanText;
    QString m_cleanHtml;
    bool m_cleanRich = false;
    bool m_metaDirty = false; // header/footer or properties edited (not in the text)
    PreviewPane *m_preview = nullptr;
    QAction *m_previewAction = nullptr;
    QAction *m_libraryAction = nullptr;
    QAction *m_chooseLibraryAction = nullptr;
    QAction *m_bookAction = nullptr;
    bool m_previewWanted = false; // markdown/previewVisible; shown in Markdown mode only

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
    bool m_fitPageWidth = false;       // default OFF — page shown at true size
    bool m_typewriterScroll = true;   // default ON — distraction-free
    bool m_focusMode = false;         // default OFF
    bool m_focusSentence = false;     // false = paragraph scope
    bool m_smartQuotes = false;       // default OFF
    bool m_spellCheck = true;         // default ON
    bool m_centering = false;         // re-entrancy guard for scroll
    bool m_markdownMode = false;      // current document is Markdown
    // While in Markdown mode m_fullPageView / m_hideAwayPinned hold Markdown's
    // view (continuous, hide-away chrome); the ODT-mode preferences wait here
    // and are what view/fullPageView and view/chromePinned keep storing.
    bool m_odtFullPageView = true;
    bool m_odtChromePinned = true;
    bool m_markdownChromePinned = false; // markdown/chromePinned, default hide-away
    QString m_themeId = QStringLiteral("paper"); // paper (default) | dark | inverse
};
