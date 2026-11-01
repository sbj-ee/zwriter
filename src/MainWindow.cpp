#include "MainWindow.hpp"
#include "TableColumns.hpp"
#include "ParagraphDialog.hpp"
#include "ParagraphStyles.hpp"
#include "StylesDialog.hpp"
#include "BookToc.hpp"
#include "BookExportDialog.hpp"
#include "Captions.hpp"
#include "PageSettingsDialog.hpp"
#include "PageSetup.hpp"
#include "Ruler.hpp"
#include "Images.hpp"
#include "ImagePropertiesDialog.hpp"
#include "AlignmentActions.hpp"
#include "DocumentIo.hpp"
#include "DocumentMeta.hpp"
#include "FindReplaceBar.hpp"
#include "FocusRange.hpp"
#include "HeaderFooterDialog.hpp"
#include "SpellChecker.hpp"
#include "WritingHighlighter.hpp"
#include "InsertTableDialog.hpp"
#include "LibrarySidebar.hpp"
#include "BookSidebar.hpp"
#include "BookChapterDialog.hpp"
#include "BookManifest.hpp"
#include "PageWidgets.hpp"
#include "PreviewPane.hpp"
#include "PrintLayout.hpp"
#include "PropertiesDialog.hpp"
#include "TypewriterSounds.hpp"
#include "UpdateManager.hpp"
#include "version.hpp"

#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QComboBox>
#include <QLineEdit>
#include <QWheelEvent>
#include "ViewZoom.hpp"
#include <QActionGroup>
#include <QCloseEvent>
#include <QShowEvent>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFont>
#include <QFontComboBox>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QIcon>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QPaintEvent>
#include <QKeyEvent>
#include <QPushButton>
#include "Theme.hpp"
#include <QTextListFormat>
#include <QTextList>
#include <QGuiApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QInputDialog>
#include <QClipboard>
#include <QMimeData>
#include <QTextDocumentFragment>
#include <QKeyEvent>
#include <QKeySequence>
#include <QToolButton>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPageLayout>
#include <QPageSetupDialog>
#include <QPageSize>
#include <QPaintDevice>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QColor>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include "SymbolDialog.hpp"
#include "Symbols.hpp"
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QTextTableFormat>
#include <QTextLength>
#include <QTextEdit>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QTextFrame>
#include <QTextFrameFormat>
#include <QTextLayout>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "MainWindowInternal.hpp"

namespace {
// Where the caret sits visually: (block number, wrapped line within block).
// Used to hear the carriage return when typing soft-wraps onto a new line.
QPair<int, int> caretVisualLine(QTextEdit *editor)
{
    const QTextCursor c = editor->textCursor();
    const QTextBlock block = c.block();
    // Force layout of this block so the line info is current.
    editor->document()->documentLayout()->blockBoundingRect(block);
    const QTextLayout *layout = block.layout();
    const QTextLine line = layout ? layout->lineForTextPosition(c.positionInBlock()) : QTextLine();
    return {block.blockNumber(), line.isValid() ? line.lineNumber() : 0};
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("zwriter"));
    loadWindowIcon();
    loadSettings();
    if (!restoreGeometry(QSettings().value(QStringLiteral("window/geometry")).toByteArray())) {
        resize(1040, 760);
    }

    m_meta.ensureDefaults();
    {
        QSettings s;
        adoptStyleFonts(StyleFonts::appDefaults(s)); // new documents start from the app defaults
    }

    m_printer = new QPrinter(QPrinter::HighResolution);
    m_printer->setPageMargins(QMarginsF(25.4, 25.4, 25.4, 25.4), QPageLayout::Millimeter);
    loadPrinterSettings();
    // The untitled document at startup is a new document: US Letter unless
    // the user picked another size for new documents (1.12.0).
    setDocumentPageSize(QPageSize(PageSetup::newDocumentSizeId()), false);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_findBar = new FindReplaceBar(central);
    layout->addWidget(m_findBar);

    // Desk + scrollable centered page frame (full page view) or expanding strip (continuous).
    // Document page metrics always use true physical size (mm → DIPs via logicalDpi);
    // the scroll area lets small windows pan a real A4 instead of shrinking metrics
    // (which made pt fonts look huge — classic Qt pageSize trap).
    m_desk = new QWidget(central);
    m_desk->setObjectName(QStringLiteral("desk"));
    auto *deskLayout = new QVBoxLayout(m_desk);
    deskLayout->setContentsMargins(0, 0, 0, 0);
    deskLayout->setSpacing(0);

    m_pageScroll = new QScrollArea(m_desk);
    m_pageScroll->setObjectName(QStringLiteral("pageScroll"));
    m_pageScroll->setFrameShape(QFrame::NoFrame);
    m_pageScroll->setWidgetResizable(true);
    m_pageScroll->setAlignment(Qt::AlignCenter);
    m_pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pageScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_pageFrame = new QFrame;
    m_pageFrame->setObjectName(QStringLiteral("pageFrame"));
    m_pageFrame->setFrameShape(QFrame::NoFrame);
    auto *pageLayout = new QVBoxLayout(m_pageFrame);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    m_editor = new PageTextEdit(m_pageFrame);
    m_editor->setAcceptRichText(true);
    m_editor->setFrameShape(QFrame::NoFrame);
    // Clear vertical bar caret. Qt draws it by inverting the pixels under it:
    // near-black on the paper page, light on the dark theme.
    m_editor->setCursorWidth(2);
    pageLayout->addWidget(m_editor);

    m_pageCanvas = new PageCanvas(m_pageFrame);
    m_pageScroll->setWidget(m_pageCanvas);
    // QScrollArea auto-fills its widget; let the desk colour show through instead.
    m_pageCanvas->setAutoFillBackground(false);
    m_pageScroll->viewport()->setAutoFillBackground(false);
    m_ruler = new Ruler(m_editor, m_desk);
    m_ruler->setVisible(QSettings().value(QStringLiteral("view/ruler"), false).toBool());
    deskLayout->addWidget(m_ruler);
    deskLayout->addWidget(m_pageScroll, 1);

    // Pages appear as the document grows: keep the paper exactly N pages tall.
    // (pageCountChanged does not fire while typing; documentSizeChanged does.
    // Resizing from inside a layout callback would re-enter the layout, so the
    // work is deferred and coalesced.)
    connect(m_editor->document()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged, this, [this](const QSizeF &) {
                if (m_pageSyncQueued) {
                    return;
                }
                m_pageSyncQueued = true;
                QTimer::singleShot(0, this, [this]() {
                    m_pageSyncQueued = false;
                    syncPageFrameHeight(); // also refreshes the cached page count
                    scheduleStatusUpdate();
                });
            });

    // Library sidebar | page desk | Markdown preview. Both side panes start
    // hidden, so a window without them is laid out exactly as before.
    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setObjectName(QStringLiteral("workspaceSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    m_library = new LibrarySidebar(m_splitter);
    m_library->hide();
    m_splitter->addWidget(m_library);
    // Book sidebar: same slot as the library (only one of the two is shown).
    m_book = new BookSidebar(m_splitter);
    m_book->hide();
    m_splitter->addWidget(m_book);
    m_splitter->addWidget(m_desk);
    m_preview = new PreviewPane(m_splitter);
    m_preview->hide();
    m_splitter->addWidget(m_preview);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 3);
    m_splitter->setStretchFactor(3, 2);
    m_preview->attachEditor(m_editor);
    // Focus moves from the editor straight to the chapter list (Ctrl+Tab;
    // Tab itself types a tab in the document), not to the sidebar header.
    QWidget::setTabOrder(m_editor, m_book->view());
    // Ctrl+Tab and Ctrl+Shift+Tab switch between the editor and the chapter list
    // (both ways; the list's own key handling ignores Ctrl+Tab, so the earlier
    // editor-only key filter could never come back). On macOS the physical Ctrl
    // key is Qt's Meta; Cmd+Tab belongs to the system.
    {
        auto *swap = new QAction(QStringLiteral("Switch Between Editor and Chapters"), this);
        swap->setObjectName(QStringLiteral("focusSwap"));
#ifdef Q_OS_MACOS
        const Qt::Modifier ctrl = Qt::META;
#else
        const Qt::Modifier ctrl = Qt::CTRL;
#endif
        swap->setShortcuts({QKeySequence(ctrl | Qt::Key_Tab), QKeySequence(ctrl | Qt::SHIFT | Qt::Key_Backtab),
                            QKeySequence(ctrl | Qt::SHIFT | Qt::Key_Tab)});
        swap->setShortcutContext(Qt::WindowShortcut);
        connect(swap, &QAction::triggered, this, &MainWindow::swapEditorAndChapters);
        addAction(swap);
    }
    // Tabbing into a hidden-away toolbar shows it.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (now && !m_chromeVisible && m_formatBar && m_formatBar->isAncestorOf(now)) {
            revealHideAway();
        }
    });
    m_previewWanted = PreviewPane::loadVisibleSetting();
    {
        const LibrarySidebar::Settings lib = LibrarySidebar::loadSettings();
        m_library->setRootPath(lib.rootPath);
        m_library->setVisible(lib.visible);
        const BookSidebar::Settings book = BookSidebar::loadSettings();
        m_book->selectBookOnLoad(book.currentBook);
        m_book->setRootPath(lib.rootPath); // books are subfolders of the library
        m_book->setVisible(book.visible && !lib.visible);
    }
    connect(m_library, &LibrarySidebar::switchToBook, this, [this]() { showSidebar(true); });
    connect(m_book, &BookSidebar::switchToLibrary, this, [this]() { showSidebar(false); });
    connect(m_library, &LibrarySidebar::addToBookRequested, this, [this](const QString &path) {
        const QString err = m_book->addFileToBook(path);
        if (!err.isEmpty()) {
            statusBar()->showMessage(err, 6000);
            return;
        }
        showSidebar(true);
    });
    connect(m_book, &BookSidebar::currentBookChanged, this, [this]() { saveBookSettings(); });
    // A generated table of contents follows the book: reorders, renames,
    // chapters added or removed.
    connect(m_book, &BookSidebar::bookChanged, this, [this]() {
        refreshBookToc();
        renumberCaptions(); // the chapter may have moved
    });
    connect(m_book, &BookSidebar::chapterRenamed, this, &MainWindow::retitleChapterHeading);
    connect(m_book, &BookSidebar::chapterFileMoved, this, [this](const QString &from, const QString &to) {
        if (!m_currentPath.isEmpty() && QFileInfo(m_currentPath).absoluteFilePath() == from) {
            m_currentPath = to; // unsaved edits stay; Save writes to the new name
            updateWindowTitle();
            m_book->setCurrentFile(to);
        }
    });
    connect(m_book, &BookSidebar::tocRequested, this, [this]() { insertBookToc(); });
    connect(m_book, &BookSidebar::exportBookPdfRequested, this, &MainWindow::exportBookPdf);
    connect(m_book, &BookSidebar::exportBookEpubRequested, this, &MainWindow::exportBookEpub);
    connect(m_book, &BookSidebar::printBookRequested, this, &MainWindow::printBook);
    connect(m_book, &BookSidebar::chooseLibraryRequested, this, &MainWindow::chooseLibraryFolder);
    connect(m_book, &BookSidebar::message, this,
            [this](const QString &text) { statusBar()->showMessage(text, 6000); });
    connect(m_book, &BookSidebar::fileActivated, this, [this](const QString &path) {
        if (m_currentPath.isEmpty() || QFileInfo(path) != QFileInfo(m_currentPath)) {
            openExternalFile(path);
        }
        m_book->setCurrentFile(m_currentPath);
        m_editor->setFocus(Qt::OtherFocusReason);
    });
    connect(m_library, &LibrarySidebar::rootPathChosen, this, [this]() {
        if (!m_library->isVisibleTo(this)) {
            m_library->setVisible(true);
        }
        m_library->setCurrentFile(m_currentPath);
        m_book->setRootPath(m_library->rootPath());
        saveLibrarySettings();
        syncViewActions();
    });
    connect(m_library, &LibrarySidebar::fileActivated, this, [this](const QString &path) {
        if (m_currentPath.isEmpty() || QFileInfo(path) != QFileInfo(m_currentPath)) {
            openExternalFile(path); // unsaved-changes prompt, then open
        }
        m_library->setCurrentFile(m_currentPath); // e.g. the prompt was cancelled
        m_editor->setFocus(Qt::OtherFocusReason);
    });

    layout->addWidget(m_splitter, 1);
    setCentralWidget(central);
    m_desk->installEventFilter(this);
    // Hide-away: in Full Page view the pointer at the top edge is over the desk
    // / scroll area, not the editor, so those need move events too.
    for (QWidget *w : {static_cast<QWidget *>(m_desk), static_cast<QWidget *>(m_pageScroll),
                       m_pageScroll->viewport(), static_cast<QWidget *>(m_pageCanvas),
                       static_cast<QWidget *>(m_pageFrame)}) {
        w->setMouseTracking(true);
        if (w != m_desk) {
            w->installEventFilter(this);
        }
    }

    m_keySounds = new TypewriterSounds(this);
    m_keySounds->setEnabled(
        QSettings().value(QStringLiteral("view/keySounds"), false).toBool());
    m_spellChecker = new SpellChecker(this);
    m_spellChecker->setEnabled(m_spellCheck);
    m_highlighter = new WritingHighlighter(m_editor->document());
    m_highlighter->setMisspelledFunction([this](const QString &word) {
        return m_spellChecker && m_spellChecker->isEnabled() && m_spellChecker->isAvailable()
            && !m_spellChecker->isCorrect(word);
    });
    m_editor->setPlainImageHandler([this](const QByteArray &data) { return insertMarkdownPicture(data); });
    connect(m_editor, &PageTextEdit::imageNote, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 5000); });
    m_editor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_editor, &QWidget::customContextMenuRequested,
            this, &MainWindow::showEditorContextMenu);
    m_editor->setTableResizeEnabled(true);
    connect(m_editor, &PageTextEdit::tableColumnsResized, this, [this]() {
        markDirty();
        statusBar()->showMessage(QStringLiteral("Column widths changed"), 2000);
    });

    // The paragraph style at the caret, always visible (the toolbar may be hidden).
    m_styleLabel = new QLabel(QStringLiteral("Body Text"), this);
    m_styleLabel->setObjectName(QStringLiteral("statusStyle"));
    m_styleLabel->setToolTip(QStringLiteral("Paragraph style at the cursor"));
    statusBar()->addPermanentWidget(m_styleLabel);
    m_pageLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_pageLabel);
    m_statsLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statsLabel);
    // View zoom % (1.14.0): pick a step or type a value from 50 to 300.
    m_zoomCombo = new QComboBox(this);
    m_zoomCombo->setObjectName(QStringLiteral("statusZoom"));
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setInsertPolicy(QComboBox::NoInsert);
    m_zoomCombo->setFocusPolicy(Qt::ClickFocus);
    for (int p : ViewZoom::steps()) {
        m_zoomCombo->addItem(QStringLiteral("%1%").arg(p), p);
    }
    m_zoomCombo->setToolTip(QStringLiteral(
        "View zoom (Ctrl++ / Ctrl+- / Ctrl+0, or Ctrl+scroll). Display only: "
        "printing and exports are unchanged"));
    auto applyTyped = [this]() {
        QString t = m_zoomCombo->currentText().trimmed();
        t.remove(QLatin1Char('%'));
        bool ok = false;
        const int p = qRound(t.toDouble(&ok));
        if (ok) {
            setViewZoomPercent(p);
        }
        syncZoomControls();
        if (m_editor) {
            m_editor->setFocus();
        }
    };
    connect(m_zoomCombo, &QComboBox::activated, this, [applyTyped](int) { applyTyped(); });
    connect(m_zoomCombo->lineEdit(), &QLineEdit::editingFinished, this, applyTyped);
    statusBar()->addPermanentWidget(m_zoomCombo);
    statusBar()->setSizeGripEnabled(false);

#ifndef Q_OS_MACOS
    // Keep the menu bar inside the window on Linux. Desktops running a global
    // menu (appmenu / Fildem register com.canonical.AppMenu.Registrar) make Qt
    // export the bar over D-Bus, which leaves zwriter with no visible menu when
    // the panel applet is absent -- and silently breaks hide-away, which reveals
    // chrome from menuBar()->geometry() (see considerMouseHideAway).
    menuBar()->setNativeMenuBar(false);
#endif

    createFormatActions();
    buildFileMenu();
    buildEditMenu();
    buildFormatMenu();
    buildInsertMenu();
    buildViewMenu();
    buildHelpMenu();
    buildFormatToolbar();
    // 1.17.0: restore the toolbar/dock layout once every bar exists (each has
    // an objectName, which QMainWindow::restoreState keys on).
    restoreState(QSettings().value(QStringLiteral("window/state")).toByteArray());
    if (m_pageGuidesAction) {
        const QSignalBlocker b(m_pageGuidesAction);
        m_pageGuidesAction->setChecked(m_pageGuides);
    }

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(1200);
    connect(m_hideTimer, &QTimer::timeout, this, &MainWindow::hideAwayIdle);

    // Word count and "Page N of M" are refreshed shortly after typing pauses,
    // not synchronously on every keystroke (both walk the document).
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(150);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() {
        updateStats();
        updatePageLabel();
    });
    connect(m_editor, &QTextEdit::textChanged, this, &MainWindow::scheduleStatusUpdate);
    // "Unsaved changes" follows the document's modified flag, NOT textChanged:
    // QSyntaxHighlighter::rehighlight() (spell check on/off, a word added to
    // the dictionary, colours) emits textChanged without touching the text, and
    // that used to make an untouched document ask "Save changes?" on close.
    //
    // And the flag alone is not trusted either (1.4.1): Qt also sets it for
    // changes that leave the text and its formatting exactly as loaded (an
    // input-method event that replaces text with the same text, an empty edit
    // block, ...). Such a "modification" is compared with what was loaded and
    // dropped; only a real difference makes the window dirty.
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, [this](bool modified) {
        if (!modified || (m_highlighter && m_highlighter->isRehighlighting()) || m_suppressDirty) {
            return;
        }
        if (!contentDiffersFromClean()) {
            m_editor->document()->setModified(false); // nothing to save
            return;
        }
        markDirty();
    });
    // Key sounds fire from the editor keyPress eventFilter (typing + Return),
    // not textChanged — avoids clicks on paste/programmatic edits/nav side-effects.
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, &MainWindow::onCursorMoved);
    // Figures moved, cut or pasted: renumber a moment after the edit.
    m_captionTimer = new QTimer(this);
    m_captionTimer->setSingleShot(true);
    m_captionTimer->setInterval(250);
    // Joined to the edit that caused it, so one Undo takes back both; and
    // only after edits (undoCommandAdded), not after Undo/Redo themselves.
    connect(m_captionTimer, &QTimer::timeout, this, [this]() { renumberCaptions(true); });
    connect(m_editor->document(), &QTextDocument::undoCommandAdded, this, [this]() {
        if (!m_renumbering && !m_suppressDirty) {
            m_captionTimer->start();
        }
    });
    connect(m_editor, &QTextEdit::currentCharFormatChanged, this,
            [this](const QTextCharFormat &) { syncFormatActions(); });

    connect(m_findBar, &FindReplaceBar::findNext, this, &MainWindow::findNext);
    connect(m_findBar, &FindReplaceBar::findPrev, this, &MainWindow::findPrev);
    connect(m_findBar, &FindReplaceBar::replaceOne, this, &MainWindow::replaceOne);
    connect(m_findBar, &FindReplaceBar::replaceAll, this, &MainWindow::replaceAll);
    connect(m_findBar, &FindReplaceBar::closeRequested, this, &MainWindow::hideFindBar);

    m_editor->installEventFilter(this);
    m_editor->viewport()->setMouseTracking(true);
    m_editor->viewport()->installEventFilter(this);
    m_pageScroll->viewport()->installEventFilter(this); // Ctrl+scroll zoom on the desk
    setMouseTracking(true);
    if (m_formatBar) {
        m_formatBar->installEventFilter(this);
    }
    if (menuBar()) {
        menuBar()->installEventFilter(this);
    }

    applyDocumentDefaults();
    applyTheme();
    applyFullPageView();
    setChromeVisible(m_hideAwayPinned);
    m_dirty = false;
    syncViewActions();
    updateStats();
    syncFormatActions();
    updateWindowTitle();
    updateFocusHighlight();
    // Setting up the page layout above is not an edit.
    m_editor->document()->clearUndoRedoStacks();
    m_editor->document()->setModified(false);
    // Type straight away: the editor owns keyboard focus from launch (hiding the
    // chrome above can otherwise leave no focus widget at all).
    m_editor->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, this, [this]() {
        if (!QApplication::focusWidget() || QApplication::focusWidget()->window() != this) {
            m_editor->setFocus(Qt::OtherFocusReason);
        }
    });
}

void MainWindow::loadSettings()
{
    QSettings s;
    m_typewriterScroll = s.value(QStringLiteral("view/typewriterScroll"), true).toBool();
    m_focusMode = s.value(QStringLiteral("view/focusMode"), false).toBool();
    m_focusSentence = s.value(QStringLiteral("view/focusSentence"), false).toBool();
    m_smartQuotes = s.value(QStringLiteral("view/smartQuotes"), false).toBool();
    m_hideAwayPinned = s.value(QStringLiteral("view/chromePinned"), true).toBool();
    m_spellCheck = s.value(QStringLiteral("view/spellCheck"), true).toBool();
    m_fullPageView = s.value(QStringLiteral("view/fullPageView"), true).toBool();
    m_fitPageWidth = s.value(QStringLiteral("view/fitPageWidth"), false).toBool();
    m_viewZoomPercent = ViewZoom::clampPercent(
        s.value(QLatin1String(ViewZoom::kSettingsKey), 100).toInt());
    m_odtFullPageView = m_fullPageView;
    m_odtChromePinned = m_hideAwayPinned;
    m_markdownChromePinned = s.value(QStringLiteral("markdown/chromePinned"), true).toBool();
    m_themeId = s.value(QStringLiteral("theme/id"), QStringLiteral("paper")).toString();
    if (m_themeId != QLatin1String("dark") && m_themeId != QLatin1String("inverse")) {
        m_themeId = QStringLiteral("paper");
    }
    m_recentFiles = s.value(QStringLiteral("files/recent")).toStringList();
    m_lastDocDir = s.value(QStringLiteral("files/lastDir")).toString();
    // 1.17.0: page guides and full screen carry over to the next session.
    m_pageGuides = s.value(QStringLiteral("view/pageGuides"), false).toBool();
    m_restoreFullScreen = s.value(QStringLiteral("window/fullScreen"), false).toBool();
}

void MainWindow::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("view/typewriterScroll"), m_typewriterScroll);
    s.setValue(QStringLiteral("view/focusMode"), m_focusMode);
    s.setValue(QStringLiteral("view/focusSentence"), m_focusSentence);
    s.setValue(QStringLiteral("view/smartQuotes"), m_smartQuotes);
    // Markdown mode overrides these two for its documents; the stored values
    // stay the ODT-mode preferences.
    s.setValue(QStringLiteral("view/chromePinned"), m_markdownMode ? m_odtChromePinned : m_hideAwayPinned);
    s.setValue(QStringLiteral("markdown/chromePinned"),
               m_markdownMode ? m_hideAwayPinned : m_markdownChromePinned);
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    // 1.17.0: toolbar/dock layout, full screen and page guides persist.
    s.setValue(QStringLiteral("window/state"), saveState());
    if (isVisible()) { // a window that was never shown says nothing about full screen
        s.setValue(QStringLiteral("window/fullScreen"), isFullScreen());
    }
    s.setValue(QStringLiteral("view/pageGuides"), m_pageGuides);
    if (m_keySounds) {
        s.setValue(QStringLiteral("view/keySounds"), m_keySounds->isEnabled());
    }
    s.setValue(QStringLiteral("view/spellCheck"), m_spellCheck);
    s.setValue(QStringLiteral("view/fullPageView"), m_markdownMode ? m_odtFullPageView : m_fullPageView);
    s.setValue(QStringLiteral("view/fitPageWidth"), m_fitPageWidth);
    s.setValue(QLatin1String(ViewZoom::kSettingsKey), m_viewZoomPercent);
    s.setValue(QStringLiteral("theme/id"), m_themeId);
    s.setValue(QStringLiteral("files/recent"), m_recentFiles);
    s.setValue(QStringLiteral("files/lastDir"), m_lastDocDir);
    s.remove(QStringLiteral("files/lastSaveFilter")); // no longer used (untitled saves default to ODT)
    savePrinterSettings();
}

void MainWindow::loadPrinterSettings()
{
    if (!m_printer) {
        return;
    }
    QSettings s;
    // The page size is per document since 1.12.0 (PageSetup.hpp); the old
    // print/pageSize* keys are no longer read. Orientation and margins stay here.

    const int orientVal = s.value(QStringLiteral("print/orientation"),
                                  static_cast<int>(QPageLayout::Portrait)).toInt();
    m_printer->setPageOrientation(static_cast<QPageLayout::Orientation>(orientVal));

    const qreal ml = s.value(QStringLiteral("print/marginLeftMm"), 25.4).toDouble();
    const qreal mt = s.value(QStringLiteral("print/marginTopMm"), 25.4).toDouble();
    const qreal mr = s.value(QStringLiteral("print/marginRightMm"), 25.4).toDouble();
    const qreal mb = s.value(QStringLiteral("print/marginBottomMm"), 25.4).toDouble();
    m_printer->setPageMargins(QMarginsF(ml, mt, mr, mb), QPageLayout::Millimeter);
}

void MainWindow::savePrinterSettings() const
{
    if (!m_printer) {
        return;
    }
    QSettings s;
    const QPageLayout layout = m_printer->pageLayout();
    s.setValue(QStringLiteral("print/orientation"), static_cast<int>(layout.orientation()));
    const QMarginsF m = layout.margins(QPageLayout::Millimeter);
    s.setValue(QStringLiteral("print/marginLeftMm"), m.left());
    s.setValue(QStringLiteral("print/marginTopMm"), m.top());
    s.setValue(QStringLiteral("print/marginRightMm"), m.right());
    s.setValue(QStringLiteral("print/marginBottomMm"), m.bottom());
}

void MainWindow::buildFileMenu()
{
    m_fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    auto *newAct = m_fileMenu->addAction(QStringLiteral("&New"));
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &MainWindow::fileNew);

    m_newMarkdownAction = m_fileMenu->addAction(QStringLiteral("New &Markdown Document"));
    m_newMarkdownAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N));
    m_newMarkdownAction->setToolTip(QStringLiteral(
        "Start a plain-text Markdown document with iA-style view defaults (Ctrl+Alt+N)"));
    connect(m_newMarkdownAction, &QAction::triggered, this, &MainWindow::fileNewMarkdown);

    // Books: both open the Book sidebar first, so the result is visible.
    auto *newBookAct = m_fileMenu->addAction(QStringLiteral("New &Book…"));
    newBookAct->setObjectName(QStringLiteral("fileNewBook"));
    connect(newBookAct, &QAction::triggered, this, [this]() {
        if (!m_book->isVisibleTo(this)) {
            showSidebar(true);
        }
        m_book->chooseNewBook();
    });
    auto *newChapterAct = m_fileMenu->addAction(QStringLiteral("New &Chapter…"));
    newChapterAct->setObjectName(QStringLiteral("fileNewChapter"));
    connect(newChapterAct, &QAction::triggered, this, [this]() {
        if (!m_book->isVisibleTo(this)) {
            showSidebar(true);
        }
        m_book->chooseNewChapter();
    });

    auto *openAct = m_fileMenu->addAction(QStringLiteral("&Open…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::fileOpen);

    m_recentMenu = m_fileMenu->addMenu(QStringLiteral("Open &Recent"));
    rebuildRecentMenu();
    // Files deleted or moved since the list was built drop out whenever the
    // menu is about to be shown, instead of failing when clicked.
    connect(m_fileMenu, &QMenu::aboutToShow, this, [this]() {
        const int before = int(m_recentFiles.size());
        rebuildRecentMenu();
        if (m_recentFiles.size() != before) {
            saveSettings();
        }
    });

    m_fileMenu->addSeparator();

    auto *saveAct = m_fileMenu->addAction(QStringLiteral("&Save"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::fileSave);

    auto *saveAsAct = m_fileMenu->addAction(QStringLiteral("Save &As…"));
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::fileSaveAs);

    m_fileMenu->addSeparator();

    m_tocAction = m_fileMenu->addAction(QStringLiteral("Insert/Update &Table of Contents"));
    m_tocAction->setObjectName(QStringLiteral("fileToc"));
    m_tocAction->setToolTip(QStringLiteral("The book's chapters and their headings, in book order, at the cursor "
                                           "(or updates the one already in this document)"));
    connect(m_tocAction, &QAction::triggered, this, [this]() { insertBookToc(); });
    connect(m_fileMenu, &QMenu::aboutToShow, this, [this]() {
        m_tocAction->setEnabled(m_book && m_book->hasBook() && !m_markdownMode);
    });

    auto *exportAct = m_fileMenu->addAction(QStringLiteral("&Export as PDF…"));
    exportAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(exportAct, &QAction::triggered, this, &MainWindow::exportPdf);

    m_exportBookMenu = m_fileMenu->addMenu(QStringLiteral("Export &Book"));
    m_exportBookMenu->setObjectName(QStringLiteral("fileExportBook"));
    m_exportBookMenu->addAction(QStringLiteral("&PDF…"), this, &MainWindow::exportBookPdf)
        ->setObjectName(QStringLiteral("fileExportBookPdf"));
    m_exportBookMenu->addAction(QStringLiteral("&EPUB…"), this, &MainWindow::exportBookEpub)
        ->setObjectName(QStringLiteral("fileExportBookEpub"));
    m_printBookAction = m_fileMenu->addAction(QStringLiteral("Print B&ook…"));
    m_printBookAction->setObjectName(QStringLiteral("filePrintBook"));
    connect(m_printBookAction, &QAction::triggered, this, &MainWindow::printBook);
    connect(m_fileMenu, &QMenu::aboutToShow, this, [this]() {
        const bool book = m_book && m_book->hasBook() && m_book->book().count() > 0;
        m_exportBookMenu->setEnabled(book);
        m_printBookAction->setEnabled(book);
    });

    auto *printAct = m_fileMenu->addAction(QStringLiteral("&Print…"));
    printAct->setShortcut(QKeySequence::Print);
    connect(printAct, &QAction::triggered, this, &MainWindow::filePrint);

    auto *previewAct = m_fileMenu->addAction(QStringLiteral("Print Pre&view…"));
    connect(previewAct, &QAction::triggered, this, &MainWindow::filePrintPreview);

    auto *pageAct = m_fileMenu->addAction(QStringLiteral("Page Set&up…"));
    connect(pageAct, &QAction::triggered, this, &MainWindow::filePageSetup);

    auto *sizeAct = m_fileMenu->addAction(QStringLiteral("Page Si&ze && Ruler Units…"));
    sizeAct->setObjectName(QStringLiteral("filePageSizeUnits"));
    connect(sizeAct, &QAction::triggered, this, &MainWindow::filePageSizeAndUnits);

    m_fileMenu->addSeparator();

    auto *propsAct = m_fileMenu->addAction(QStringLiteral("Propert&ies…"));
    connect(propsAct, &QAction::triggered, this, &MainWindow::fileProperties);

    m_fileMenu->addSeparator();

    auto *quitAct = m_fileMenu->addAction(QStringLiteral("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    for (QAction *a : {newAct, m_newMarkdownAction, openAct, saveAct, saveAsAct, exportAct, printAct,
                       propsAct, quitAct}) {
        addAction(a);
    }
}

void MainWindow::buildEditMenu()
{
    m_editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));

    m_undoAction = m_editMenu->addAction(QStringLiteral("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this,
            [this]() { Alignment::undoRedoKeepingCaret(m_editor, false); });

    m_redoAction = m_editMenu->addAction(QStringLiteral("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this,
            [this]() { Alignment::undoRedoKeepingCaret(m_editor, true); });

    m_editMenu->addSeparator();

    m_cutAction = m_editMenu->addAction(QStringLiteral("Cu&t"));
    m_cutAction->setShortcut(QKeySequence::Cut);
    connect(m_cutAction, &QAction::triggered, m_editor, &QTextEdit::cut);

    m_copyAction = m_editMenu->addAction(QStringLiteral("&Copy"));
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, m_editor, &QTextEdit::copy);

    auto *pasteAct = m_editMenu->addAction(QStringLiteral("&Paste"));
    pasteAct->setShortcut(QKeySequence::Paste);
    connect(pasteAct, &QAction::triggered, m_editor, &QTextEdit::paste);

    auto *pastePlainAct = m_editMenu->addAction(QStringLiteral("Paste as P&lain Text"));
    pastePlainAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    connect(pastePlainAct, &QAction::triggered, this, &MainWindow::editPastePlain);

    m_editMenu->addSeparator();

    auto *selectAllAct = m_editMenu->addAction(QStringLiteral("Select &All"));
    selectAllAct->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAct, &QAction::triggered, m_editor, &QTextEdit::selectAll);

    m_editMenu->addSeparator();

    auto *findAct = m_editMenu->addAction(QStringLiteral("&Find…"));
    findAct->setShortcut(QKeySequence::Find);
    connect(findAct, &QAction::triggered, this, &MainWindow::showFind);

    auto *findNextAct = m_editMenu->addAction(QStringLiteral("Find &Next"));
    findNextAct->setShortcut(QKeySequence::FindNext);
    connect(findNextAct, &QAction::triggered, this, &MainWindow::findNext);

    auto *findPrevAct = m_editMenu->addAction(QStringLiteral("Find Pre&vious"));
    findPrevAct->setShortcut(QKeySequence::FindPrevious);
    connect(findPrevAct, &QAction::triggered, this, &MainWindow::findPrev);

    auto *replaceAct = m_editMenu->addAction(QStringLiteral("R&eplace…"));
    // QKeySequence::Replace is empty on Linux; Ctrl+H is the convention there
    // (Cmd+H hides the app on macOS, which uses Cmd+Option+F instead).
    QList<QKeySequence> replaceKeys = QKeySequence::keyBindings(QKeySequence::Replace);
#ifdef Q_OS_MACOS
    const QKeySequence replaceExtra(Qt::CTRL | Qt::ALT | Qt::Key_F);
#else
    const QKeySequence replaceExtra(Qt::CTRL | Qt::Key_H);
#endif
    if (!replaceKeys.contains(replaceExtra)) {
        replaceKeys.append(replaceExtra);
    }
    // KDE's standard Replace key is Ctrl+R, which is Align Right here (as in
    // LibreOffice and Word). Two actions on one key make Qt fire neither, so
    // Replace gives it up and keeps Ctrl+H.
    for (QAction *a : m_alignActions->actions()) {
        replaceKeys.removeAll(a->shortcut());
    }
    replaceAct->setShortcuts(replaceKeys);
    connect(replaceAct, &QAction::triggered, this, &MainWindow::showReplace);

    // Undo/redo/cut/copy availability follows the editor.
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    m_cutAction->setEnabled(false);
    m_copyAction->setEnabled(false);
    connect(m_editor, &QTextEdit::undoAvailable, m_undoAction, &QAction::setEnabled);
    connect(m_editor, &QTextEdit::redoAvailable, m_redoAction, &QAction::setEnabled);
    connect(m_editor, &QTextEdit::copyAvailable, m_cutAction, &QAction::setEnabled);
    connect(m_editor, &QTextEdit::copyAvailable, m_copyAction, &QAction::setEnabled);

    // The editor consumes its own standard shortcuts (ShortcutOverride); the
    // window only needs the Find/Replace and paste-plain ones.
    for (QAction *a : {findAct, findNextAct, findPrevAct, replaceAct, pastePlainAct}) {
        addAction(a);
    }
}

void MainWindow::createFormatActions()
{
    auto make = [this](const QString &text, const QKeySequence &keys, const QString &tip,
                       bool checkable = false) {
        auto *a = new QAction(text, this);
        if (!keys.isEmpty()) {
            a->setShortcut(keys);
        }
        a->setToolTip(tip);
        a->setCheckable(checkable);
        return a;
    };

    m_boldAction = make(QStringLiteral("&Bold"), QKeySequence::Bold,
                        QStringLiteral("Bold (Ctrl+B)"), true);
    m_italicAction = make(QStringLiteral("&Italic"), QKeySequence::Italic,
                          QStringLiteral("Italic (Ctrl+I)"), true);
    m_underlineAction = make(QStringLiteral("&Underline"), QKeySequence::Underline,
                             QStringLiteral("Underline (Ctrl+U)"), true);
    connect(m_boldAction, &QAction::triggered, this, &MainWindow::toggleBold);
    connect(m_italicAction, &QAction::triggered, this, &MainWindow::toggleItalic);
    connect(m_underlineAction, &QAction::triggered, this, &MainWindow::toggleUnderline);
    // Subscript / superscript (1.13.0): QTextCharFormat vertical alignment;
    // turning one on turns the other off.
    // 1.14.1: Ctrl+= / Ctrl++ are View > Zoom In (the standard everywhere), so
    // subscript and superscript live on Ctrl+, and Ctrl+. (the comma sits low,
    // the period high, like the marks they make).
    m_subscriptAction = make(QStringLiteral("Su&bscript"), QKeySequence(Qt::CTRL | Qt::Key_Comma),
                             QStringLiteral("Subscript, e.g. H₂O (Ctrl+,)"), true);
    m_subscriptAction->setObjectName(QStringLiteral("formatSubscript"));
    m_superscriptAction = make(QStringLiteral("Su&perscript"), QKeySequence(Qt::CTRL | Qt::Key_Period),
                               QStringLiteral("Superscript, e.g. x² (Ctrl+.)"), true);
    m_superscriptAction->setObjectName(QStringLiteral("formatSuperscript"));
    connect(m_subscriptAction, &QAction::triggered, this, [this](bool on) {
        setVerticalAlign(on ? QTextCharFormat::AlignSubScript : QTextCharFormat::AlignNormal);
    });
    connect(m_superscriptAction, &QAction::triggered, this, [this](bool on) {
        setVerticalAlign(on ? QTextCharFormat::AlignSuperScript : QTextCharFormat::AlignNormal);
    });

    // Paragraph styles (exclusive).
    auto *styleGroup = new QActionGroup(this);
    styleGroup->setExclusive(true);
    struct Style { QAction **slot; const char *text; int level; int key; };
    const Style styles[] = {
        {&m_paragraphAction, "&Body Text", 0, Qt::Key_0},
        {&m_h1Action, "Heading &1", 1, Qt::Key_1},
        {&m_h2Action, "Heading &2", 2, Qt::Key_2},
        {&m_h3Action, "Heading &3", 3, Qt::Key_3},
    };
    for (const Style &s : styles) {
        QAction *a = make(QString::fromLatin1(s.text),
                          QKeySequence(Qt::CTRL | Qt::ALT | static_cast<Qt::Key>(s.key)),
                          QString::fromLatin1(s.text).remove(QLatin1Char('&')), true);
        a->setData(s.level);
        styleGroup->addAction(a);
        *s.slot = a;
    }
    // Named paragraph styles (1.7.0), in the same exclusive group.
    for (const ParagraphStyles::Style &st : ParagraphStyles::named()) {
        QAction *a = new QAction(st.display, this);
        a->setCheckable(true);
        a->setData(-1);
        a->setProperty("zwStyle", st.name);
        a->setToolTip(st.display);
        styleGroup->addAction(a);
        m_namedStyleActions << a;
    }
    connect(styleGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        // A style chosen again (Heading 1 on a Heading 1 line) is not a toggle:
        // triggering the checked action unchecks it, which read as Body Text.
        if (!a->isChecked()) {
            a->setChecked(true); // (the group unchecks the others)
        }
        if (a->property("zwStyle").isValid()) {
            applyNamedStyle(a->property("zwStyle").toString());
        } else {
            applyHeading();
        }
    });

    // Styles… (1.15.0): font family and size per paragraph style.
    m_stylesDialogAction = new QAction(QStringLiteral("St&yles…"), this);
    m_stylesDialogAction->setObjectName(QStringLiteral("formatStyles"));
    m_stylesDialogAction->setToolTip(QStringLiteral("Font and size for Body Text, headings and the other styles"));
    connect(m_stylesDialogAction, &QAction::triggered, this, &MainWindow::showStylesDialog);

    // Paragraph… dialog and indent steps.
    m_paragraphDialogAction = new QAction(QStringLiteral("&Paragraph…"), this);
    m_paragraphDialogAction->setObjectName(QStringLiteral("formatParagraph"));
    m_paragraphDialogAction->setToolTip(QStringLiteral("Indents, spacing, line spacing and alignment"));
    connect(m_paragraphDialogAction, &QAction::triggered, this, &MainWindow::showParagraphDialog);
    m_indentAction = new QAction(QStringLiteral("&Increase Indent"), this);
    m_indentAction->setObjectName(QStringLiteral("formatIndent"));
    m_indentAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    m_indentAction->setToolTip(QStringLiteral("Increase indent (Ctrl+M)"));
    connect(m_indentAction, &QAction::triggered, this, [this]() {
        ParagraphStyles::changeIndent(m_editor->textCursor(), 1);
        m_editor->setFocus();
    });
    m_outdentAction = new QAction(QStringLiteral("&Decrease Indent"), this);
    m_outdentAction->setObjectName(QStringLiteral("formatOutdent"));
    m_outdentAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    m_outdentAction->setToolTip(QStringLiteral("Decrease indent (Ctrl+Shift+M)"));
    connect(m_outdentAction, &QAction::triggered, this, [this]() {
        ParagraphStyles::changeIndent(m_editor->textCursor(), -1);
        m_editor->setFocus();
    });
    addAction(m_indentAction);
    addAction(m_outdentAction);

    // Alignment (exclusive group, checked state follows the cursor).
    m_alignActions = new Alignment::Actions(m_editor, this);
    m_alignLeftAction = m_alignActions->action(Alignment::Kind::Left);
    m_alignCenterAction = m_alignActions->action(Alignment::Kind::Center);
    m_alignRightAction = m_alignActions->action(Alignment::Kind::Right);
    m_alignJustifyAction = m_alignActions->action(Alignment::Kind::Justify);
    connect(m_alignActions, &Alignment::Actions::applied, this, [this](Alignment::Kind, bool) {
        m_editor->setFocus();
    });

    // Lists.
    m_bulletListAction = make(QStringLiteral("Bulleted &List"),
                              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B),
                              QStringLiteral("Bulleted list (Ctrl+Shift+B)"), true);
    m_numberListAction = make(QStringLiteral("Nu&mbered List"),
                              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
                              QStringLiteral("Numbered list (Ctrl+Shift+N)"), true);
    connect(m_bulletListAction, &QAction::triggered, this, [this](bool on) {
        applyList(QTextListFormat::ListDisc, on);
    });
    connect(m_numberListAction, &QAction::triggered, this, [this](bool on) {
        applyList(QTextListFormat::ListDecimal, on);
    });

    m_clearFormatAction = make(QStringLiteral("&Clear Formatting"),
                               QKeySequence(Qt::CTRL | Qt::Key_Backslash),
                               QStringLiteral("Clear formatting (Ctrl+\\)"));
    connect(m_clearFormatAction, &QAction::triggered, this, &MainWindow::clearFormatting);

    // Table structure (enabled only inside a table; see updateTableActions()).
    m_tableInsertRowAction = new QAction(QStringLiteral("Insert &Row"), this);
    connect(m_tableInsertRowAction, &QAction::triggered, this, &MainWindow::tableInsertRow);
    m_tableInsertColAction = new QAction(QStringLiteral("Insert &Column"), this);
    connect(m_tableInsertColAction, &QAction::triggered, this, &MainWindow::tableInsertColumn);
    m_tableRemoveRowAction = new QAction(QStringLiteral("Remove Ro&w"), this);
    connect(m_tableRemoveRowAction, &QAction::triggered, this, &MainWindow::tableRemoveRow);
    m_tableRemoveColAction = new QAction(QStringLiteral("Remove Colu&mn"), this);
    connect(m_tableRemoveColAction, &QAction::triggered, this, &MainWindow::tableRemoveColumn);
    m_tableMergeAction = new QAction(QStringLiteral("M&erge Cells"), this);
    m_tableMergeAction->setObjectName(QStringLiteral("tableMergeCells"));
    m_tableMergeAction->setToolTip(QStringLiteral("Merge the selected cells into one"));
    connect(m_tableMergeAction, &QAction::triggered, this, &MainWindow::tableMergeCells);
    m_tableSplitAction = new QAction(QStringLiteral("&Split Cells"), this);
    m_tableSplitAction->setObjectName(QStringLiteral("tableSplitCells"));
    m_tableSplitAction->setToolTip(QStringLiteral("Split the merged cell back into single cells"));
    connect(m_tableSplitAction, &QAction::triggered, this, &MainWindow::tableSplitCell);
    m_tableHeaderAction = new QAction(QStringLiteral("Repeat &Header Row"), this);
    m_tableHeaderAction->setObjectName(QStringLiteral("tableHeaderRow"));
    m_tableHeaderAction->setCheckable(true);
    m_tableHeaderAction->setToolTip(QStringLiteral("Make the first row a header row, repeated at the top of every page"));
    connect(m_tableHeaderAction, &QAction::triggered, this, &MainWindow::tableToggleHeaderRow);
}

void MainWindow::buildFormatMenu()
{
    m_formatMenu = menuBar()->addMenu(QStringLiteral("F&ormat"));

    m_formatMenu->addAction(m_boldAction);
    m_formatMenu->addAction(m_italicAction);
    m_formatMenu->addAction(m_underlineAction);
    m_formatMenu->addAction(m_subscriptAction);
    m_formatMenu->addAction(m_superscriptAction);
    m_formatMenu->addSeparator();

    auto *styleMenu = m_formatMenu->addMenu(QStringLiteral("Paragraph &Style"));
    for (QAction *a : {m_paragraphAction, m_h1Action, m_h2Action, m_h3Action}) {
        styleMenu->addAction(a);
    }
    styleMenu->addSeparator();
    for (QAction *a : m_namedStyleActions) {
        styleMenu->addAction(a);
    }
    m_formatMenu->addAction(m_stylesDialogAction);
    m_formatMenu->addAction(m_paragraphDialogAction);
    m_formatMenu->addAction(m_indentAction);
    m_formatMenu->addAction(m_outdentAction);
    auto *alignMenu = m_formatMenu->addMenu(QStringLiteral("&Align"));
    for (QAction *a : {m_alignLeftAction, m_alignCenterAction, m_alignRightAction,
                       m_alignJustifyAction}) {
        alignMenu->addAction(a);
    }
    m_formatMenu->addAction(m_bulletListAction);
    m_formatMenu->addAction(m_numberListAction);
    m_formatMenu->addSeparator();

    auto *tableAct = m_formatMenu->addAction(QStringLiteral("Insert Tabl&e…"));
    m_insertTableAction = tableAct;
    tableAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    tableAct->setToolTip(QStringLiteral("Insert a table (Ctrl+Shift+I)"));
    connect(tableAct, &QAction::triggered, this, &MainWindow::insertTable);
    auto *tableMenu = m_formatMenu->addMenu(QStringLiteral("&Table"));
    tableMenu->addAction(m_tableInsertRowAction);
    tableMenu->addAction(m_tableInsertColAction);
    tableMenu->addSeparator();
    tableMenu->addAction(m_tableRemoveRowAction);
    tableMenu->addAction(m_tableRemoveColAction);
    tableMenu->addSeparator();
    tableMenu->addAction(m_tableMergeAction);
    tableMenu->addAction(m_tableSplitAction);
    tableMenu->addSeparator();
    tableMenu->addAction(m_tableHeaderAction);

    m_insertImageAction = m_formatMenu->addAction(QStringLiteral("Insert I&mage…"));
    m_insertImageAction->setToolTip(QStringLiteral("Insert a picture from a file (PNG, JPEG, GIF, WebP, BMP, SVG)"));
    connect(m_insertImageAction, &QAction::triggered, this, &MainWindow::insertImageFromFile);
    m_insertCaptionAction = m_formatMenu->addAction(QStringLiteral("Insert Ca&ption"));
    m_insertCaptionAction->setToolTip(QStringLiteral("Add a caption under the picture at the caret"));
    connect(m_insertCaptionAction, &QAction::triggered, this, &MainWindow::insertCaptionHere);

    auto *pageBreakAct = m_formatMenu->addAction(QStringLiteral("Insert Page Brea&k"));
    m_pageBreakAction = pageBreakAct;
    pageBreakAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Return),
                                QKeySequence(Qt::CTRL | Qt::Key_Enter)});
    pageBreakAct->setToolTip(QStringLiteral(
        "Start a new page here (Ctrl+Enter). To remove a break, press Backspace at the start of the new page."));
    connect(pageBreakAct, &QAction::triggered, this, &MainWindow::insertPageBreak);
    addAction(pageBreakAct);

    m_pageNumbersAction = m_formatMenu->addAction(QStringLiteral("Page &Numbers"));
    m_pageNumbersAction->setCheckable(true);
    m_pageNumbersAction->setToolTip(
        QStringLiteral("Show page numbers in the footer (off by default; more options in Header & Footer…)"));
    connect(m_pageNumbersAction, &QAction::triggered, this, &MainWindow::togglePageNumbers);

    auto *hfAct = m_formatMenu->addAction(QStringLiteral("&Header && Footer…"));
    m_headerFooterAction = hfAct;
    hfAct->setToolTip(QStringLiteral("Edit page header and footer (page numbers via {page} / {pages})"));
    connect(hfAct, &QAction::triggered, this, &MainWindow::editHeaderFooter);
    m_formatMenu->addSeparator();

    m_formatMenu->addAction(m_clearFormatAction);

    // Shortcuts must work with the menu closed and while a toolbar widget has focus.
    for (QAction *a : {m_boldAction, m_italicAction, m_underlineAction,
                       m_subscriptAction, m_superscriptAction,
                       m_paragraphAction, m_h1Action, m_h2Action, m_h3Action,
                       m_alignLeftAction, m_alignCenterAction, m_alignRightAction,
                       m_alignJustifyAction, m_bulletListAction, m_numberListAction,
                       m_clearFormatAction, tableAct, hfAct}) {
        addAction(a);
    }
    updateTableActions();
}

void MainWindow::buildInsertMenu()
{
    // Insert (1.13.0): symbols, plus the existing insert actions from Format.
    m_insertMenu = menuBar()->addMenu(QStringLiteral("&Insert"));
    m_symbolAction = m_insertMenu->addAction(QStringLiteral("&Symbol…"));
    m_symbolAction->setObjectName(QStringLiteral("insertSymbol"));
    // Ctrl+Shift+I is Insert Table, so Symbol takes Ctrl+Shift+Y.
    m_symbolAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y));
    m_symbolAction->setToolTip(QStringLiteral("Insert a Greek letter or EE symbol (Ctrl+Shift+Y)"));
    connect(m_symbolAction, &QAction::triggered, this, &MainWindow::showSymbolDialog);
    addAction(m_symbolAction);
    // The on/off switch lives in View (built next); created here so the key filter has it.
    m_symbolAutocompleteAction = new QAction(QStringLiteral("Symbol &Autocomplete (\\alpha → α)"), this);
    m_symbolAutocompleteAction->setObjectName(QStringLiteral("symbolAutocomplete"));
    m_symbolAutocompleteAction->setCheckable(true);
    m_symbolAutocompleteAction->setChecked(Symbols::autocompleteEnabled());
    m_symbolAutocompleteAction->setToolTip(QStringLiteral(
        "Type \\alpha, \\Omega, \\mu, \\deg, \\pm … then Space or Tab to get the symbol; Ctrl+Z brings the text back"));
    connect(m_symbolAutocompleteAction, &QAction::toggled, this, [](bool on) { Symbols::setAutocompleteEnabled(on); });
    m_mathReplaceAction = new QAction(QStringLiteral("&Math Auto-Replace (>= → ≥)"), this);
    m_mathReplaceAction->setObjectName(QStringLiteral("mathAutoReplace"));
    m_mathReplaceAction->setCheckable(true);
    m_mathReplaceAction->setChecked(Symbols::autoReplaceEnabled());
    m_mathReplaceAction->setToolTip(QStringLiteral(
        "Typing >= <= != -> => gives ≥ ≤ ≠ → ⇒ (not in code or monospace text); Ctrl+Z brings the typed pair back"));
    connect(m_mathReplaceAction, &QAction::toggled, this, [](bool on) { Symbols::setAutoReplaceEnabled(on); });
    m_insertMenu->addSeparator();
    for (QAction *a : {m_insertTableAction, m_insertImageAction, m_insertCaptionAction, m_pageBreakAction}) {
        if (a) {
            m_insertMenu->addAction(a);
        }
    }
}

SymbolDialog *MainWindow::symbolDialog() const
{
    return m_symbolDialog.data();
}

void MainWindow::showSymbolDialog()
{
    if (!m_symbolDialog) {
        m_symbolDialog = new SymbolDialog(this);
        connect(m_symbolDialog, &SymbolDialog::symbolChosen, this, [this](const QString &text) {
            Symbols::insertAtCursor(m_editor, text);
        });
    }
    m_symbolDialog->refreshRecent();
    m_symbolDialog->show();
    m_symbolDialog->raise();
    m_symbolDialog->activateWindow();
}

void MainWindow::buildViewMenu()
{
    m_viewMenu = menuBar()->addMenu(QStringLiteral("&View"));

    m_fullPageViewAction = m_viewMenu->addAction(QStringLiteral("Full &Page"));
    m_fullPageViewAction->setCheckable(true);
    m_fullPageViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    m_fullPageViewAction->setToolTip(
        QStringLiteral("Show a centered paper page on the desk (Ctrl+Shift+P); off = continuous strip"));
    connect(m_fullPageViewAction, &QAction::triggered, this, &MainWindow::toggleFullPageView);

    m_fitPageWidthAction = m_viewMenu->addAction(QStringLiteral("Fit Page to &Width"));
    m_fitPageWidthAction->setCheckable(true);
    m_fitPageWidthAction->setObjectName(QStringLiteral("viewFitWidth"));
    m_fitPageWidthAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
    m_fitPageWidthAction->setToolTip(QStringLiteral(
        "Zoom the page to fill the window width (Ctrl+Shift+W); lines still break as they print"));
    connect(m_fitPageWidthAction, &QAction::triggered, this, &MainWindow::toggleFitPageWidth);

    // View zoom (1.14.0; 1.14.1 shortcuts). Zoom In answers Ctrl+= and Ctrl++
    // however the layout reports the +/= key (with or without Shift), the
    // numpad + (Qt drops the keypad modifier when matching), and the 1.14.0
    // Ctrl+Alt+= kept as an extra.
    m_zoomInAction = m_viewMenu->addAction(QStringLiteral("Zoom &In"));
    m_zoomInAction->setObjectName(QStringLiteral("viewZoomIn"));
    m_zoomInAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Plus),
                                  QKeySequence(Qt::CTRL | Qt::Key_Equal),
                                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal),
                                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus),
                                  QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Equal),
                                  QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Plus)});
    m_zoomInAction->setToolTip(QStringLiteral("Make the text bigger on screen (Ctrl++ or Ctrl+=)"));
    connect(m_zoomInAction, &QAction::triggered, this,
            [this]() { setViewZoomPercent(ViewZoom::stepUp(shownZoomPercent())); });
    m_zoomOutAction = m_viewMenu->addAction(QStringLiteral("Zoom &Out"));
    m_zoomOutAction->setObjectName(QStringLiteral("viewZoomOut"));
    m_zoomOutAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    m_zoomOutAction->setToolTip(QStringLiteral("Make the text smaller on screen (Ctrl+-)"));
    connect(m_zoomOutAction, &QAction::triggered, this,
            [this]() { setViewZoomPercent(ViewZoom::stepDown(shownZoomPercent())); });
    m_zoomResetAction = m_viewMenu->addAction(QStringLiteral("&Actual Size"));
    m_zoomResetAction->setObjectName(QStringLiteral("viewZoomReset"));
    m_zoomResetAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    m_zoomResetAction->setToolTip(QStringLiteral("Show the page at 100 % (Ctrl+0)"));
    connect(m_zoomResetAction, &QAction::triggered, this, [this]() { setViewZoomPercent(100); });

    m_rulerAction = m_viewMenu->addAction(QStringLiteral("&Ruler"));
    m_rulerAction->setObjectName(QStringLiteral("viewRuler"));
    m_rulerAction->setCheckable(true);
    m_rulerAction->setChecked(QSettings().value(QStringLiteral("view/ruler"), false).toBool());
    m_rulerAction->setToolTip(QStringLiteral("Show the paragraph indents above the page; drag them to change"));
    connect(m_rulerAction, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("view/ruler"), on);
        if (m_ruler) {
            m_ruler->setVisible(on && !m_markdownMode);
        }
    });
    m_viewMenu->addAction(m_symbolAutocompleteAction);
    m_viewMenu->addAction(m_mathReplaceAction);
    m_pageGuidesAction = m_viewMenu->addAction(QStringLiteral("Page &Guides"));
    m_pageGuidesAction->setObjectName(QStringLiteral("viewPageGuides"));
    m_pageGuidesAction->setCheckable(true);
    // Ctrl+G is Find Next on Linux; keep guides on their own chord.
    m_pageGuidesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G));
    m_pageGuidesAction->setToolTip(QStringLiteral("Toggle page margin guides (Ctrl+Alt+G)"));
    connect(m_pageGuidesAction, &QAction::triggered, this, &MainWindow::togglePageGuides);

    m_viewMenu->addSeparator();

    m_typewriterScrollAction = m_viewMenu->addAction(QStringLiteral("&Typewriter Scroll"));
    m_typewriterScrollAction->setCheckable(true);
    m_typewriterScrollAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    m_typewriterScrollAction->setToolTip(
        QStringLiteral("Keep caret vertically centered while typing (Ctrl+Shift+T)"));
    connect(m_typewriterScrollAction, &QAction::triggered, this,
            &MainWindow::toggleTypewriterScroll);

    m_focusModeAction = m_viewMenu->addAction(QStringLiteral("&Focus Mode"));
    m_focusModeAction->setCheckable(true);
    m_focusModeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    m_focusModeAction->setToolTip(
        QStringLiteral("Dim everything except the current sentence/paragraph (Ctrl+Shift+F)"));
    connect(m_focusModeAction, &QAction::triggered, this, &MainWindow::toggleFocusMode);

    auto *scopeMenu = m_viewMenu->addMenu(QStringLiteral("Focus &Scope"));
    auto *scopeGroup = new QActionGroup(this);
    scopeGroup->setExclusive(true);
    m_focusParagraphAction = scopeMenu->addAction(QStringLiteral("&Paragraph"));
    m_focusParagraphAction->setCheckable(true);
    m_focusSentenceAction = scopeMenu->addAction(QStringLiteral("&Sentence"));
    m_focusSentenceAction->setCheckable(true);
    scopeGroup->addAction(m_focusParagraphAction);
    scopeGroup->addAction(m_focusSentenceAction);
    connect(m_focusParagraphAction, &QAction::triggered, this,
            &MainWindow::setFocusScopeParagraph);
    connect(m_focusSentenceAction, &QAction::triggered, this,
            &MainWindow::setFocusScopeSentence);

    m_keySoundsAction = m_viewMenu->addAction(QStringLiteral("Typewriter Key S&ounds"));
    m_keySoundsAction->setCheckable(true);
    m_keySoundsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    m_keySoundsAction->setToolTip(
        QStringLiteral("Play typewriter key clicks while typing (Ctrl+Shift+K, default off)"));
    connect(m_keySoundsAction, &QAction::triggered, this, &MainWindow::toggleKeySounds);

    m_viewMenu->addSeparator();

    m_spellCheckAction = m_viewMenu->addAction(QStringLiteral("Spe&ll Check"));
    m_spellCheckAction->setCheckable(true);
    m_spellCheckAction->setToolTip(
        QStringLiteral("Underline misspellings (Hunspell en_US); right-click for suggestions"));
    connect(m_spellCheckAction, &QAction::triggered, this, &MainWindow::toggleSpellCheck);

    m_smartQuotesAction = m_viewMenu->addAction(QStringLiteral("Smart &Quotes && Dashes"));
    m_smartQuotesAction->setCheckable(true);
    m_smartQuotesAction->setToolTip(
        QStringLiteral("Curly quotes and em/en dashes from ASCII while typing (default off)"));
    connect(m_smartQuotesAction, &QAction::triggered, this, &MainWindow::toggleSmartQuotes);

    m_viewMenu->addSeparator();

    auto *themeMenu = m_viewMenu->addMenu(QStringLiteral("Th&eme"));
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    m_themePaperAction = themeMenu->addAction(QStringLiteral("&Paper (default)"));
    m_themePaperAction->setCheckable(true);
    m_themePaperAction->setToolTip(QStringLiteral("Warm paper page and light chrome"));
    m_themeDarkAction = themeMenu->addAction(QStringLiteral("&Dark room"));
    m_themeDarkAction->setCheckable(true);
    m_themeDarkAction->setToolTip(QStringLiteral("Charcoal page and dark chrome"));
    m_themeInverseAction = themeMenu->addAction(QStringLiteral("&Inverse"));
    m_themeInverseAction->setCheckable(true);
    m_themeInverseAction->setToolTip(QStringLiteral("Black page, white text -- maximum contrast"));
    themeGroup->addAction(m_themePaperAction);
    themeGroup->addAction(m_themeDarkAction);
    themeGroup->addAction(m_themeInverseAction);
    connect(m_themePaperAction, &QAction::triggered, this, &MainWindow::setThemePaper);
    connect(m_themeDarkAction, &QAction::triggered, this, &MainWindow::setThemeDark);
    connect(m_themeInverseAction, &QAction::triggered, this, &MainWindow::setThemeInverse);

    m_viewMenu->addSeparator();

    m_alwaysShowChromeAction = m_viewMenu->addAction(QStringLiteral("Always Show Tool&bar\tEsc"));
    m_alwaysShowChromeAction->setCheckable(true);
    m_alwaysShowChromeAction->setToolTip(
        QStringLiteral("Keep the toolbar, menus and status bar visible (Esc); off = hide-away"));
    connect(m_alwaysShowChromeAction, &QAction::triggered, this, &MainWindow::setChromePinned);

    m_viewMenu->addSeparator();

    m_previewAction = m_viewMenu->addAction(QStringLiteral("Markdown Pre&view"));
    m_previewAction->setCheckable(true);
    m_previewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
    m_previewAction->setToolTip(QStringLiteral(
        "Show the rendered Markdown beside the editor (Ctrl+Alt+P; Markdown documents only)"));
    connect(m_previewAction, &QAction::triggered, this, &MainWindow::toggleMarkdownPreview);

    m_libraryAction = m_viewMenu->addAction(QStringLiteral("Li&brary Sidebar"));
    m_libraryAction->setCheckable(true);
    m_libraryAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
    m_libraryAction->setToolTip(
        QStringLiteral("Show the .md / .txt files of your library folder (Ctrl+Shift+L)"));
    connect(m_libraryAction, &QAction::triggered, this, &MainWindow::toggleLibrary);

    m_chooseLibraryAction = m_viewMenu->addAction(QStringLiteral("Choose Library &Folder…"));
    connect(m_chooseLibraryAction, &QAction::triggered, this, &MainWindow::chooseLibraryFolder);

    m_bookAction = m_viewMenu->addAction(QStringLiteral("Boo&k Sidebar"));
    m_bookAction->setCheckable(true);
    m_bookAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B));
    m_bookAction->setToolTip(QStringLiteral(
        "Show the book's chapters in book order; drag to reorder, right-click for details "
        "(Ctrl+Alt+B)"));
    connect(m_bookAction, &QAction::triggered, this, &MainWindow::toggleBook);

    auto *fullscreenAct = m_viewMenu->addAction(QStringLiteral("Full Sc&reen"));
    fullscreenAct->setObjectName(QStringLiteral("viewFullScreen"));
    fullscreenAct->setShortcut(QKeySequence(Qt::Key_F11));
    connect(fullscreenAct, &QAction::triggered, this, &MainWindow::toggleFullscreen);

    for (QAction *a : {m_typewriterScrollAction, m_focusModeAction, m_fullPageViewAction,
                       m_fitPageWidthAction, m_zoomInAction, m_zoomOutAction, m_zoomResetAction,
                       m_pageGuidesAction, m_keySoundsAction, fullscreenAct,
                       m_previewAction, m_libraryAction, m_bookAction}) {
        addAction(a);
    }
}

QTextTable *MainWindow::currentTable() const
{
    if (!m_editor) {
        return nullptr;
    }
    return m_editor->textCursor().currentTable();
}

void MainWindow::updateTableActions()
{
    QTextTable *table = m_markdownMode ? nullptr : currentTable();
    const bool inTable = table != nullptr;
    for (QAction *a : {m_tableInsertRowAction, m_tableInsertColAction,
                       m_tableRemoveRowAction, m_tableRemoveColAction}) {
        if (a) {
            a->setEnabled(inTable);
        }
    }
    bool canMerge = false;
    bool canSplit = false;
    if (table) {
        const QTextCursor c = m_editor->textCursor();
        int r = 0, nr = 0, col = 0, nc = 0;
        c.selectedTableCells(&r, &nr, &col, &nc);
        canMerge = r >= 0 && nr * nc > 1;
        const QTextTableCell cell = table->cellAt(c);
        canSplit = !canMerge && cell.isValid() && (cell.rowSpan() > 1 || cell.columnSpan() > 1);
    }
    if (m_tableMergeAction) {
        m_tableMergeAction->setEnabled(canMerge);
        m_tableSplitAction->setEnabled(canSplit);
        m_tableHeaderAction->setEnabled(inTable && table->rows() > 1);
        const QSignalBlocker block(m_tableHeaderAction);
        m_tableHeaderAction->setChecked(inTable && table->format().headerRowCount() > 0);
    }
}

void MainWindow::tableMergeCells()
{
    QTextTable *table = currentTable();
    if (!table) {
        return;
    }
    QTextCursor c = m_editor->textCursor();
    int r = 0, nr = 0, col = 0, nc = 0;
    c.selectedTableCells(&r, &nr, &col, &nc);
    if (r < 0 || nr * nc < 2) {
        return;
    }
    c.beginEditBlock(); // one undo step
    table->mergeCells(r, col, nr, nc);
    DocumentIo::styleTable(table);
    c.endEditBlock();
    m_editor->setTextCursor(table->cellAt(r, col).firstCursorPosition());
    updateTableActions();
    markDirty();
}

void MainWindow::tableSplitCell()
{
    QTextTable *table = currentTable();
    if (!table) {
        return;
    }
    QTextCursor c = m_editor->textCursor();
    const QTextTableCell cell = table->cellAt(c);
    if (!cell.isValid() || (cell.rowSpan() < 2 && cell.columnSpan() < 2)) {
        return;
    }
    const int r = cell.row();
    const int col = cell.column();
    c.beginEditBlock();
    table->splitCell(r, col, 1, 1);
    DocumentIo::styleTable(table); // the freed cells get the grid
    c.endEditBlock();
    m_editor->setTextCursor(table->cellAt(r, col).firstCursorPosition());
    updateTableActions();
    markDirty();
}

void MainWindow::tableToggleHeaderRow(bool on)
{
    QTextTable *table = currentTable();
    if (!table) {
        return;
    }
    QTextTableFormat f = table->format();
    if ((f.headerRowCount() > 0) == on) {
        return;
    }
    QTextCursor c = m_editor->textCursor();
    c.beginEditBlock();
    f.setHeaderRowCount(on ? 1 : 0);
    table->setFormat(f);
    c.endEditBlock();
    updateTableActions();
    markDirty();
}

void MainWindow::insertTable()
{
    InsertTableDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const int rows = dlg.rows();
    const int cols = dlg.columns();
    if (rows < 1 || cols < 1) {
        return;
    }

    QTextCursor cursor = m_editor->textCursor();
    cursor.beginEditBlock();
    QTextTable *table = cursor.insertTable(rows, cols);
    if (table) {
        DocumentIo::styleTable(table);
    }
    cursor.endEditBlock();
    if (table) {
        // Start typing in the first cell, not below the table.
        m_editor->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    }
    m_editor->setFocus();
    updateTableActions();
    markDirty();
}

bool MainWindow::moveToAdjacentCell(bool forward)
{
    QTextCursor c = m_editor->textCursor();
    QTextTable *table = c.currentTable();
    if (!table) {
        return false;
    }
    const QTextTableCell cell = table->cellAt(c);
    int row = cell.row();
    int col = cell.column();
    if (forward) {
        col += qMax(1, cell.columnSpan());
        if (col >= table->columns()) {
            col = 0;
            row += qMax(1, cell.rowSpan());
        }
        if (row >= table->rows()) {
            // Tab in the last cell adds a row, like other word processors.
            QTextCursor edit(c);
            edit.beginEditBlock();
            table->appendRows(1);
            DocumentIo::styleTable(table);
            edit.endEditBlock();
            row = table->rows() - 1;
            col = 0;
        }
    } else {
        --col;
        if (col < 0) {
            if (row == 0) {
                return true; // first cell: stay put
            }
            --row;
            col = table->columns() - 1;
        }
    }
    const QTextTableCell target = table->cellAt(row, col);
    if (!target.isValid()) {
        return true;
    }
    QTextCursor to = target.firstCursorPosition();
    to.setPosition(target.lastCursorPosition().position(), QTextCursor::KeepAnchor);
    if (!forward || to.selectedText().isEmpty()) {
        to = target.firstCursorPosition();
    }
    m_editor->setTextCursor(to);
    updateTableActions();
    return true;
}

void MainWindow::tableInsertRow()
{
    QTextTable *table = currentTable();
    if (!table) {
        return;
    }
    QTextCursor c = m_editor->textCursor();
    const int row = table->cellAt(c).row();
    c.beginEditBlock();
    table->insertRows(row + 1, 1);
    DocumentIo::styleTable(table); // new cells get the grid too
    c.endEditBlock();
    updateTableActions();
    markDirty();
}

void MainWindow::tableInsertColumn()
{
    QTextTable *table = currentTable();
    if (!table) {
        return;
    }
    QTextCursor c = m_editor->textCursor();
    const int col = table->cellAt(c).column();
    const QList<double> widths = TableColumns::storedPercents(table);
    c.beginEditBlock();
    table->insertColumns(col + 1, 1);
    if (!widths.isEmpty()) {
        TableColumns::setPercents(table, TableColumns::afterInsert(widths, col + 1, 1));
    }
    DocumentIo::styleTable(table); // new cells get the grid too
    c.endEditBlock();
    updateTableActions();
    markDirty();
}

void MainWindow::tableRemoveRow()
{
    QTextTable *table = currentTable();
    if (!table || table->rows() <= 1) {
        return;
    }
    QTextCursor c = m_editor->textCursor();
    const QTextTableCell cell = table->cellAt(c);
    const int row = cell.row();
    const int col = cell.column();
    table->removeRows(row, 1);
    // Keep the caret in the table (removing the first row would otherwise
    // leave it just outside, with the table actions greyed out).
    m_editor->setTextCursor(
        table->cellAt(qMin(row, table->rows() - 1), qMin(col, table->columns() - 1)).firstCursorPosition());
    updateTableActions();
    markDirty();
}

void MainWindow::tableRemoveColumn()
{
    QTextTable *table = currentTable();
    if (!table || table->columns() <= 1) {
        return;
    }
    QTextCursor c = m_editor->textCursor();
    const QTextTableCell cell = table->cellAt(c);
    const int row = cell.row();
    const int col = cell.column();
    const QList<double> widths = TableColumns::storedPercents(table);
    c.beginEditBlock();
    table->removeColumns(col, 1);
    if (!widths.isEmpty()) {
        TableColumns::setPercents(table, TableColumns::afterRemove(widths, col, 1));
    }
    c.endEditBlock();
    m_editor->setTextCursor(
        table->cellAt(qMin(row, table->rows() - 1), qMin(col, table->columns() - 1)).firstCursorPosition());
    updateTableActions();
    markDirty();
}


void MainWindow::buildHelpMenu()
{
    m_helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));

    auto *aboutAct = m_helpMenu->addAction(QStringLiteral("&About zwriter"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::helpAbout);

    m_checkUpdatesAction = m_helpMenu->addAction(QStringLiteral("Check for &Updates…"));
    connect(m_checkUpdatesAction, &QAction::triggered, this, &MainWindow::helpCheckUpdates);

    m_checkUpdatesAtStartupAction = m_helpMenu->addAction(QStringLiteral("Check for Updates at &Startup"));
    m_checkUpdatesAtStartupAction->setCheckable(true);
    m_checkUpdatesAtStartupAction->setChecked(zwriter::UpdateManager::checkAtStartupEnabled());
    connect(m_checkUpdatesAtStartupAction, &QAction::toggled, this,
            [](bool on) { zwriter::UpdateManager::setCheckAtStartupEnabled(on); });
}

void MainWindow::syncViewActions()
{
    if (m_pageNumbersAction) {
        const QSignalBlocker b(m_pageNumbersAction);
        m_pageNumbersAction->setChecked(pageNumbersOn());
    }
    if (m_typewriterScrollAction) {
        const QSignalBlocker b(m_typewriterScrollAction);
        m_typewriterScrollAction->setChecked(m_typewriterScroll);
    }
    if (m_focusModeAction) {
        const QSignalBlocker b(m_focusModeAction);
        m_focusModeAction->setChecked(m_focusMode);
    }
    if (m_smartQuotesAction) {
        const QSignalBlocker b(m_smartQuotesAction);
        m_smartQuotesAction->setChecked(m_smartQuotes);
    }
    if (m_spellCheckAction) {
        const QSignalBlocker b(m_spellCheckAction);
        m_spellCheckAction->setChecked(m_spellCheck);
        const bool avail = m_spellChecker && m_spellChecker->isAvailable();
        m_spellCheckAction->setEnabled(avail);
        if (!avail) {
            m_spellCheckAction->setToolTip(
                QStringLiteral("Spell check unavailable (install Hunspell + en_US dictionary)"));
        }
    }
    if (m_alwaysShowChromeAction) {
        const QSignalBlocker b(m_alwaysShowChromeAction);
        m_alwaysShowChromeAction->setChecked(m_hideAwayPinned);
    }
    if (m_keySoundsAction) {
        const QSignalBlocker b(m_keySoundsAction);
        m_keySoundsAction->setChecked(m_keySounds && m_keySounds->isEnabled());
        const bool avail = m_keySounds && m_keySounds->isAvailable();
        m_keySoundsAction->setEnabled(avail);
        if (!avail) {
            m_keySoundsAction->setToolTip(QStringLiteral(
                "Key sounds unavailable (needs Qt6 Multimedia and assets/sounds/key-1.wav)"));
        }
    }
    if (m_fullPageViewAction) {
        const QSignalBlocker b(m_fullPageViewAction);
        m_fullPageViewAction->setChecked(m_fullPageView);
        m_fullPageViewAction->setEnabled(!m_markdownMode); // Markdown is a continuous strip
    }
    if (m_pageGuidesAction) {
        m_pageGuidesAction->setEnabled(!m_markdownMode);
    }
    if (m_fitPageWidthAction) {
        const QSignalBlocker b(m_fitPageWidthAction);
        m_fitPageWidthAction->setChecked(m_fitPageWidth);
        m_fitPageWidthAction->setEnabled(m_fullPageView); // the continuous view already fills
    }
    syncZoomControls();
    if (m_focusParagraphAction && m_focusSentenceAction) {
        const QSignalBlocker b1(m_focusParagraphAction);
        const QSignalBlocker b2(m_focusSentenceAction);
        m_focusParagraphAction->setChecked(!m_focusSentence);
        m_focusSentenceAction->setChecked(m_focusSentence);
    }
    if (m_themePaperAction && m_themeDarkAction && m_themeInverseAction) {
        const QSignalBlocker b1(m_themePaperAction);
        const QSignalBlocker b2(m_themeDarkAction);
        const QSignalBlocker b3(m_themeInverseAction);
        m_themePaperAction->setChecked(m_themeId == QLatin1String("paper"));
        m_themeDarkAction->setChecked(m_themeId == QLatin1String("dark"));
        m_themeInverseAction->setChecked(m_themeId == QLatin1String("inverse"));
    }
    if (m_preview) {
        m_preview->setVisible(m_markdownMode && m_previewWanted); // ODT/TXT/RTF: never
    }
    if (m_previewAction) {
        const QSignalBlocker b(m_previewAction);
        m_previewAction->setChecked(m_previewWanted);
        m_previewAction->setEnabled(m_markdownMode);
    }
    if (m_libraryAction && m_library) {
        const QSignalBlocker b(m_libraryAction);
        m_libraryAction->setChecked(m_library->isVisibleTo(this));
    }
    if (m_bookAction && m_book) {
        const QSignalBlocker b(m_bookAction);
        m_bookAction->setChecked(m_book->isVisibleTo(this));
    }
}

void MainWindow::toggleMarkdownPreview()
{
    m_previewWanted = !m_previewWanted;
    PreviewPane::saveVisibleSetting(m_previewWanted);
    syncViewActions();
}

void MainWindow::buildFormatToolbar()
{
    m_formatBar = addToolBar(QStringLiteral("Format"));
    m_formatBar->setObjectName(QStringLiteral("formatBar"));
    m_formatBar->setMovable(false);
    m_formatBar->setFloatable(false);
    m_formatBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_fontCombo = new QFontComboBox(m_formatBar);
    m_fontCombo->setObjectName(QStringLiteral("fontCombo"));
    m_fontCombo->setToolTip(QStringLiteral("Font family — applies to selection or typing style"));
    m_fontCombo->setMaximumWidth(220);
    m_fontCombo->setCurrentFont(defaultDocumentFont());
    m_formatBar->addWidget(m_fontCombo);
    // Apply only once a font is chosen (Enter, or picking from the list): the
    // combo is editable, and applying on every keystroke of a typed name
    // would send the rest of the name into the document.
    connect(m_fontCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        onFontFamilyChosen(m_fontCombo->currentFont());
    });

    m_fontSizeSpin = new QSpinBox(m_formatBar);
    m_fontSizeSpin->setObjectName(QStringLiteral("fontSizeSpin"));
    m_fontSizeSpin->setToolTip(QStringLiteral("Font size (pt) — applies to selection or typing style"));
    m_fontSizeSpin->setRange(6, 96);
    m_fontSizeSpin->setValue(kDefaultBodyPointSize);
    m_fontSizeSpin->setSuffix(QStringLiteral(" pt"));
    m_fontSizeSpin->setFixedWidth(78);
    m_formatBar->addWidget(m_fontSizeSpin);
    // Arrows / wheel apply each step live and keep focus in the box; typed
    // digits apply on Enter (keyboard tracking off). Enter hands focus back.
    m_fontSizeSpin->setKeyboardTracking(false);
    connect(m_fontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onFontSizeChosen);
    connect(m_fontSizeSpin, &QSpinBox::editingFinished, this, [this]() {
        // Also emitted on focus loss; only Enter (focus still here) returns to the page.
        if (m_fontSizeSpin->hasFocus() && m_editor) {
            m_editor->setFocus();
        }
    });

    m_formatBar->addSeparator();

    // B / I / U as typographic glyphs (the actions live in the Format menu too).
    struct Glyph { QAction *action; const char *text; bool bold, italic, underline; };
    const Glyph glyphs[] = {
        {m_boldAction, "B", true, false, false},
        {m_italicAction, "I", false, true, false},
        {m_underlineAction, "U", false, false, true},
    };
    for (const Glyph &g : glyphs) {
        g.action->setIconText(QString::fromLatin1(g.text));
        m_formatBar->addAction(g.action);
        if (QWidget *w = m_formatBar->widgetForAction(g.action)) {
            QFont f = w->font();
            f.setBold(g.bold);
            f.setItalic(g.italic);
            f.setUnderline(g.underline);
            w->setFont(f);
        }
    }
    // x₂ / x² toggles (1.13.0).
    m_subscriptAction->setIconText(QStringLiteral("x₂"));
    m_superscriptAction->setIconText(QStringLiteral("x²"));
    for (QAction *a : {m_subscriptAction, m_superscriptAction}) {
        m_formatBar->addAction(a);
        if (QWidget *w = m_formatBar->widgetForAction(a)) {
            w->setObjectName(a == m_subscriptAction ? QStringLiteral("subscriptButton")
                                                    : QStringLiteral("superscriptButton"));
        }
    }

    m_formatBar->addSeparator();

    // Align left / center / right / justify: drawn icons in the theme's ink
    // (see applyTheme), one exclusive group whose checked button follows the
    // paragraph at the cursor.
    m_formatBar->setIconSize(QSize(16, 16));
    for (QAction *a : m_alignActions->actions()) {
        m_formatBar->addAction(a);
        if (auto *b = qobject_cast<QToolButton *>(m_formatBar->widgetForAction(a))) {
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
            b->setObjectName(QStringLiteral("align%1Button").arg(int(a->data().toInt())));
        }
    }

    m_formatBar->addAction(m_outdentAction);
    m_formatBar->addAction(m_indentAction);
    for (QAction *a : {m_outdentAction, m_indentAction}) {
        if (auto *b = qobject_cast<QToolButton *>(m_formatBar->widgetForAction(a))) {
            b->setToolButtonStyle(Qt::ToolButtonTextOnly);
            b->setText(a == m_indentAction ? QStringLiteral("Indent") : QStringLiteral("Outdent"));
            b->setObjectName(a == m_indentAction ? QStringLiteral("indentButton") : QStringLiteral("outdentButton"));
        }
    }

    m_formatBar->addSeparator();

    m_styleCombo = new QComboBox(m_formatBar);
    m_styleCombo->setObjectName(QStringLiteral("styleCombo"));
    m_styleCombo->setToolTip(QStringLiteral("Paragraph style"));
    m_styleCombo->addItems({QStringLiteral("Body Text"), QStringLiteral("Heading 1"),
                            QStringLiteral("Heading 2"), QStringLiteral("Heading 3")});
    for (const ParagraphStyles::Style &st : ParagraphStyles::named()) {
        m_styleCombo->addItem(st.display, st.name);
    }
    m_styleCombo->setFixedWidth(118);
    m_formatBar->addWidget(m_styleCombo);
    connect(m_styleCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        QAction *targets[] = {m_paragraphAction, m_h1Action, m_h2Action, m_h3Action};
        if (index >= 0 && index < 4) {
            targets[index]->trigger();
        } else if (index >= 4 && index - 4 < m_namedStyleActions.size()) {
            m_namedStyleActions.at(index - 4)->trigger();
        }
    });
}

void MainWindow::loadWindowIcon()
{
    // Set application-wide in main() from the embedded resources.
    setWindowIcon(QApplication::windowIcon());
}

bool MainWindow::isPaperTheme() const
{
    // "Is the page light?" -- drives ink colours for guides, dimmed focus text
    // and header/footer. Both dark themes answer no.
    return m_themeId == QLatin1String("paper");
}

QFont MainWindow::defaultDocumentFont() const
{
    return m_markdownMode ? markdownFont() : richDocumentFont();
}

QFont MainWindow::markdownFont() const
{
    // iA Writer Duo (bundled, registered in main.cpp), then Mono, then the
    // Courier-class faces should the bundled fonts fail to load.
    QFont font;
    QStringList families{QStringLiteral("iA Writer Duo S"), QStringLiteral("iA Writer Mono S")};
    families += DocumentIo::defaultFontFamilies();
    font.setFamilies(families);
    font.setPointSize(kMarkdownBodyPointSize);
    font.setStyleHint(QFont::TypeWriter);
    return font;
}

QFont MainWindow::richDocumentFont() const
{
    // Shipping default: Courier, 12 pt (falls back through Courier-class monospace
    // faces; user can switch via toolbar).
    // 1.15.0: Body Text's face and size from Format > Styles….
    const StyleFonts::Entry body = StyleFonts::entry(m_styleFonts, QStringLiteral("Body"));
    QFont font;
    font.setFamilies(StyleFonts::families(m_styleFonts, QStringLiteral("Body"), DocumentIo::defaultFontFamilies(),
                                          DocumentIo::headingFontFamilies()));
    font.setPointSizeF(body.pointSize);
    if (body.family.isEmpty()) {
        font.setStyleHint(QFont::TypeWriter);
        font.setFixedPitch(true);
    }
    return font;
}

void MainWindow::applyDocumentDefaults()
{
    if (!m_editor) {
        return;
    }
    const QFont font = defaultDocumentFont();
    m_editor->document()->setDefaultFont(font);
    QTextCharFormat fmt;
    // Markdown text carries no character formats at all (it follows the
    // document font, and nothing but the text is saved).
    if (!m_markdownMode) {
        fmt.setFont(font);
    }
    // No explicit foreground: text follows the theme's colour (so switching
    // Paper <-> Dark recolours existing text) and exports/prints as plain black.
    m_editor->setCurrentCharFormat(fmt);
    if (m_fontCombo) {
        const QSignalBlocker b(m_fontCombo);
        m_fontCombo->setCurrentFont(font);
    }
    if (m_fontSizeSpin) {
        const QSignalBlocker b(m_fontSizeSpin);
        m_fontSizeSpin->setValue(font.pointSize() > 0 ? font.pointSize() : kDefaultBodyPointSize);
    }
}

void MainWindow::setTheme(const QString &id)
{
    if (m_themeId == id) {
        return;
    }
    m_themeId = id;
    applyTheme();
    saveSettings();
    syncViewActions();
    updateFocusHighlight();
    if (m_pageGuides) {
        m_editor->viewport()->update();
    }
}

void MainWindow::setThemePaper()
{
    setTheme(QStringLiteral("paper"));
}

void MainWindow::setThemeDark()
{
    setTheme(QStringLiteral("dark"));
}

void MainWindow::setThemeInverse()
{
    setTheme(QStringLiteral("inverse"));
}

void MainWindow::applyTheme()
{
    // Restyling (fonts, margins) touches the document, but it is not an edit.
    const QScopedValueRollback guard(m_suppressDirty, true);
    const bool wasModified = m_editor && m_editor->document()->isModified();
    const ThemeColors c = Theme::colors(m_themeId);
    StyleOptions style;
    if (m_markdownMode) {
        style.editorFontFamilies = QStringLiteral(
            "'iA Writer Duo S', 'iA Writer Mono S', 'Courier New', 'Liberation Mono', monospace");
        style.slimStatusBar = true;
    }
    setStyleSheet(Theme::styleSheet(c, m_fullPageView,
                                    m_markdownMode ? kMarkdownBodyPointSize : kDefaultBodyPointSize,
                                    style));
    if (m_highlighter) {
        m_highlighter->setColors(c);
    }
    updateColumnCap();

    // Links in dialogs (About) use the accent colour, not default blue.
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Link, c.accent);
    pal.setColor(QPalette::LinkVisited, c.accent);
    QApplication::setPalette(pal);

    // The page's inner scroll area is transparent so the desk shows around the page.
    if (m_pageScroll) {
        m_pageScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    }
    if (m_pageCanvas) {
        m_pageCanvas->setPageBorderColor(c.pageBorder);
    }
    if (m_alignActions) {
        m_alignActions->setIconColors(c.fg, c.accentFg, c.muted);
    }
    if (m_splitter) {
        m_splitter->setStyleSheet(QStringLiteral("QSplitter#workspaceSplitter::handle { background-color: %1; }")
                                      .arg(c.border.name()));
        m_preview->applyTheme(c, markdownFont());
        m_library->applyTheme(c);
        m_book->applyTheme(c);
    }

    // Typed text carries no explicit colour, so it follows the theme.
    if (m_editor) {
        QTextCharFormat cur = m_editor->currentCharFormat();
        cur.clearForeground();
        m_editor->setCurrentCharFormat(cur);
        m_editor->document()->setModified(wasModified);
    }
}

void MainWindow::updateStats()
{
    const QString text = m_editor->toPlainText();
    const int chars = text.length();

    // A word is a run of non-space characters. In Markdown mode a run with no
    // letter or digit (#, -, *, >, ```) is markup, not a word.
    int words = 0;
    bool inWord = false;
    bool hasAlnum = false;
    for (const QChar c : text) {
        if (c.isSpace()) {
            if (inWord && (hasAlnum || !m_markdownMode)) {
                ++words;
            }
            inWord = false;
            hasAlnum = false;
        } else {
            inWord = true;
            hasAlnum = hasAlnum || c.isLetterOrNumber();
        }
    }
    if (inWord && (hasAlnum || !m_markdownMode)) {
        ++words;
    }

    // ~N min at kReadingWpm (documented assumption).
    const int minutes = (words == 0) ? 0 : qMax(1, (words + kReadingWpm - 1) / kReadingWpm);

    m_statsLabel->setText(
        QStringLiteral("%1 words  ·  %2 characters  ·  ~%3 min")
            .arg(words)
            .arg(chars)
            .arg(minutes));
}

void MainWindow::scheduleStatusUpdate()
{
    if (m_statusTimer) {
        m_statusTimer->start();
    }
}

void MainWindow::captureCleanBaseline()
{
    const QTextDocument *doc = m_editor->document();
    m_cleanText = doc->toRawText();
    // TXT / Markdown save the text only; ODT / RTF also keep the formatting.
    m_cleanRich = m_currentFormat != DocumentIo::Format::Txt
        && m_currentFormat != DocumentIo::Format::Markdown;
    m_cleanHtml = m_cleanRich ? doc->toHtml() : QString();
}

bool MainWindow::contentDiffersFromClean() const
{
    const QTextDocument *doc = m_editor->document();
    if (doc->toRawText() != m_cleanText) {
        return true;
    }
    return m_cleanRich && doc->toHtml() != m_cleanHtml;
}

void MainWindow::markDirty()
{
    if (m_suppressDirty) {
        return; // page-layout changes are not edits
    }
    if (!m_dirty) {
        m_dirty = true;
        updateWindowTitle();
    }
}

void MainWindow::updateWindowTitle()
{
    QString name;
    if (!m_currentPath.isEmpty()) {
        name = QFileInfo(m_currentPath).fileName();
    } else {
        name = QStringLiteral("untitled");
    }
    // "notes.odt — zwriter 1.2.1 (abc1234)": the version and commit tell
    // which build is running.
    setWindowTitle(QStringLiteral("%1%2 — zwriter %3")
                       .arg(m_dirty ? QStringLiteral("*") : QString())
                       .arg(name)
                       .arg(QString::fromStdString(zwriter::versionLabel())));
}

void MainWindow::setCurrentFile(const QString &path, DocumentIo::Format format)
{
    m_currentPath = path;
    m_currentFormat = format == DocumentIo::Format::Unknown ? DocumentIo::Format::Odt : format;
    m_dirty = false;
    // Opened, created or just saved: the document is the saved state. (Save did
    // not clear it, so the next edit never re-fired modificationChanged.)
    m_editor->document()->setModified(false);
    m_metaDirty = false;
    captureCleanBaseline();
    updateWindowTitle();
    if (!path.isEmpty()) {
        addToRecentFiles(path);
    }
    if (m_preview) {
        m_preview->setBaseDirectory(path.isEmpty() ? QString() : QFileInfo(path).absolutePath());
        m_library->setCurrentFile(path);
        if (m_book->isVisibleTo(this) && !path.isEmpty() && m_book->book().indexOf(path) >= 0) {
            m_book->reload(); // fresh word counts after a save
        }
        m_book->setCurrentFile(path);
    }
}

QString MainWindow::defaultBaseName() const
{
    if (!m_currentPath.isEmpty()) {
        return QFileInfo(m_currentPath).completeBaseName();
    }
    const QString plain = m_editor->toPlainText().trimmed();
    if (!plain.isEmpty()) {
        QString first = plain.section(QChar('\n'), 0, 0).trimmed();
        first.remove(QChar('/'));
        first.remove(QChar('\\'));
        if (first.size() > 48) {
            first = first.left(48).trimmed();
        }
        if (!first.isEmpty()) {
            return first;
        }
    }
    return QStringLiteral("untitled");
}

void MainWindow::toggleKeySounds()
{
    if (!m_keySounds) {
        return;
    }
    m_keySounds->setEnabled(!m_keySounds->isEnabled());
    syncViewActions();
    saveSettings();
}

void MainWindow::toggleTypewriterScroll()
{
    m_typewriterScroll = m_typewriterScrollAction && m_typewriterScrollAction->isChecked();
    saveSettings();
    if (m_typewriterScroll) {
        centerCaret();
    }
}

void MainWindow::toggleFocusMode()
{
    m_focusMode = m_focusModeAction && m_focusModeAction->isChecked();
    saveSettings();
    updateFocusHighlight();
}

void MainWindow::setFocusScopeSentence()
{
    m_focusSentence = true;
    saveSettings();
    syncViewActions();
    updateFocusHighlight();
}

void MainWindow::setFocusScopeParagraph()
{
    m_focusSentence = false;
    saveSettings();
    syncViewActions();
    updateFocusHighlight();
}

void MainWindow::toggleSmartQuotes()
{
    m_smartQuotes = m_smartQuotesAction && m_smartQuotesAction->isChecked();
    saveSettings();
}

void MainWindow::toggleSpellCheck()
{
    m_spellCheck = m_spellCheckAction && m_spellCheckAction->isChecked();
    if (m_spellChecker) {
        m_spellChecker->setEnabled(m_spellCheck);
    }
    if (m_highlighter) {
        m_highlighter->refreshAll();
    }
    saveSettings();
}

void MainWindow::showEditorContextMenu(const QPoint &pos)
{
    if (!m_editor) {
        return;
    }

    const QPoint docPos = m_editor->toDocument(pos); // pos is in the (zoomed) viewport
    QMenu *menu = m_editor->createStandardContextMenu(docPos);
    if (!menu) {
        menu = new QMenu(m_editor);
    }

    const int imagePos = m_markdownMode ? -1 : imagePositionAt(docPos);
    if (imagePos >= 0) {
        QAction *props = new QAction(QStringLiteral("Image Properties…"), menu);
        connect(props, &QAction::triggered, this, [this, imagePos]() { showImageProperties(imagePos); });
        QAction *first = menu->actions().isEmpty() ? nullptr : menu->actions().constFirst();
        menu->insertAction(first, props);
        QAction *caption = new QAction(QStringLiteral("Add Caption"), menu);
        connect(caption, &QAction::triggered, this, [this, imagePos]() { addCaption(imagePos); });
        menu->insertAction(first, caption);
        menu->insertSeparator(first);
    }

    if (m_spellChecker && m_spellChecker->isEnabled() && m_spellChecker->isAvailable()) {
        QTextCursor cursor = m_editor->cursorForPosition(docPos);
        cursor.select(QTextCursor::WordUnderCursor);
        const QString word = cursor.selectedText().trimmed();
        bool hasLetter = false;
        for (const QChar &ch : word) {
            if (ch.isLetter()) {
                hasLetter = true;
                break;
            }
        }
        if (hasLetter && !m_spellChecker->isCorrect(word)) {
            QAction *anchor = menu->actions().isEmpty() ? nullptr : menu->actions().constFirst();
            auto insertBefore = [&](QAction *action) {
                if (anchor) {
                    menu->insertAction(anchor, action);
                } else {
                    menu->addAction(action);
                }
            };

            QList<QAction *> toInsert;
            const QStringList tips = m_spellChecker->suggestions(word);
            for (const QString &s : tips) {
                QAction *act = new QAction(s, menu);
                QObject::connect(act, &QAction::triggered, m_editor, [this, cursor, s]() mutable {
                    QTextCursor c(cursor);
                    c.insertText(s);
                    if (m_highlighter) {
                        m_highlighter->refreshAll();
                    }
                });
                toInsert.append(act);
            }
            if (!tips.isEmpty()) {
                toInsert.append(new QAction(menu)); // separator marker
                toInsert.back()->setSeparator(true);
            }
            QAction *ignoreAct = new QAction(QStringLiteral("Ignore \"%1\"").arg(word), menu);
            QObject::connect(ignoreAct, &QAction::triggered, this, [this, word]() {
                if (m_spellChecker) {
                    m_spellChecker->ignoreWord(word);
                }
                if (m_highlighter) {
                    m_highlighter->refreshAll();
                }
            });
            toInsert.append(ignoreAct);
            QAction *addAct = new QAction(QStringLiteral("Add \"%1\" to dictionary").arg(word), menu);
            QObject::connect(addAct, &QAction::triggered, this, [this, word]() {
                if (m_spellChecker) {
                    m_spellChecker->addToUserDictionary(word);
                }
                if (m_highlighter) {
                    m_highlighter->refreshAll();
                }
            });
            toInsert.append(addAct);
            toInsert.append(new QAction(menu));
            toInsert.back()->setSeparator(true);

            // Each one goes in front of the first standard action, so inserting
            // in list order keeps the best suggestion on top and the separator
            // right above Undo.
            for (QAction *action : std::as_const(toInsert)) {
                insertBefore(action);
            }
        }
    }

    // Table commands (1.16.0) when the click is in a table; a right-click
    // there without a selection puts the caret in that cell first.
    if (!m_markdownMode && !m_editor->textCursor().hasSelection()) {
        const QTextCursor at = m_editor->cursorForPosition(docPos);
        if (at.currentTable()) {
            m_editor->setTextCursor(at);
        }
    }
    if (!m_markdownMode && currentTable()) {
        updateTableActions();
        menu->addSeparator();
        QMenu *table = menu->addMenu(QStringLiteral("Table"));
        for (QAction *a : {m_tableInsertRowAction, m_tableInsertColAction, m_tableRemoveRowAction,
                           m_tableRemoveColAction}) {
            table->addAction(a);
        }
        table->addSeparator();
        table->addAction(m_tableMergeAction);
        table->addAction(m_tableSplitAction);
        table->addSeparator();
        table->addAction(m_tableHeaderAction);
    }

    menu->exec(m_editor->viewport()->mapToGlobal(pos));
    delete menu;
}

void MainWindow::insertImageFromFile()
{
    QFileDialog dlg(this, QStringLiteral("Insert Image"));
    setupNativeFileDialog(dlg);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setAcceptMode(QFileDialog::AcceptOpen);
    dlg.setNameFilter(Images::imageFileFilter());
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) {
        return;
    }
    insertImageFile(dlg.selectedFiles().constFirst());
}

bool MainWindow::insertImageFile(const QString &path)
{
    if (!m_editor) {
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(QStringLiteral("Could not read %1.").arg(QFileInfo(path).fileName()), 5000);
        return false;
    }
    if (m_markdownMode) {
        return insertMarkdownPicture(f.readAll());
    }
    QTextCursor c = m_editor->textCursor();
    QString note;
    const bool ok = Images::insertImage(c, f.readAll(), &note);
    if (ok) {
        m_editor->setTextCursor(c);
        m_editor->setFocus();
    }
    if (!note.isEmpty()) {
        statusBar()->showMessage(note, 5000);
    }
    return ok;
}

bool MainWindow::insertMarkdownPicture(const QByteArray &data)
{
    if (!m_editor || !m_markdownMode) {
        return false;
    }
    QString note;
    const QString rel = Images::copyToMarkdownAssets(m_currentPath, data, &note);
    if (!note.isEmpty()) {
        statusBar()->showMessage(note, 6000);
    }
    if (rel.isEmpty()) {
        return false;
    }
    QTextCursor c = m_editor->textCursor();
    if (c.hasSelection()) { // keep the selected text: insert after it
        c.setPosition(c.selectionEnd());
    }
    c.insertText(Images::markdownImageLink(rel));
    m_editor->setTextCursor(c);
    m_editor->setFocus();
    return true;
}

bool MainWindow::addCaption(int position)
{
    QTextDocument *doc = m_editor->document();
    if (m_markdownMode || Images::imageAt(doc, position).isNull()) {
        return false;
    }
    QTextCursor c(doc);
    c.setPosition(position);
    c.movePosition(QTextCursor::EndOfBlock);
    c.beginEditBlock();
    QTextBlockFormat bf;
    bf.setAlignment(Qt::AlignHCenter);
    bf.setHeadingLevel(0);
    bf.setProperty(ParagraphStyles::StyleNameProperty, QStringLiteral("Caption"));
    QTextCharFormat cf;
    // The caption uses the face of the text around the picture.
    for (QTextBlock b = doc->findBlock(position); b.isValid(); b = b.previous()) {
        bool found = false;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat near = it.fragment().charFormat();
            if (!near.isImageFormat() && !near.fontFamilies().toStringList().isEmpty()) {
                cf.setFontFamilies(near.fontFamilies().toStringList());
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    cf.setFontItalic(true);
    const qreal body = doc->defaultFont().pointSizeF() > 0 ? doc->defaultFont().pointSizeF()
                                                           : qreal(kDefaultBodyPointSize);
    cf.setFontPointSize(qMax(8.0, body - 2.0));
    c.insertBlock(bf, cf);
    c.insertText(QStringLiteral("Caption"), cf);
    // "Figure <chapter>.<n>. " goes in front (same undo step); select the
    // word "Caption" to type over.
    const int captionBlock = c.blockNumber();
    renumberCaptions();
    c.endEditBlock();
    const QTextBlock cb = doc->findBlockByNumber(captionBlock);
    c = QTextCursor(cb);
    c.setPosition(cb.position() + Captions::labelLength(cb));
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    m_editor->setTextCursor(c);
    m_editor->setFocus();
    return true;
}

void MainWindow::insertCaptionHere()
{
    // The picture in the caret's paragraph (the last one, if several).
    const QTextBlock block = m_editor->textCursor().block();
    int found = -1;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        if (it.fragment().charFormat().isImageFormat()) {
            found = it.fragment().position() + it.fragment().length() - 1;
        }
    }
    if (found < 0 || !addCaption(found)) {
        statusBar()->showMessage(QStringLiteral("Put the caret in the paragraph with the picture first."), 4000);
    }
}

int MainWindow::imagePositionAt(const QPoint &documentPos) const
{
    if (!m_editor) {
        return -1;
    }
    QTextDocument *doc = m_editor->document();
    const int hit = doc->documentLayout()->hitTest(QPointF(documentPos), Qt::FuzzyHit);
    for (int pos : {hit, hit - 1}) {
        const QTextCursor c = Images::imageAt(doc, pos);
        if (c.isNull()) {
            continue;
        }
        // Only when the click is on the picture itself, not beside it.
        QTextCursor at(doc);
        at.setPosition(pos);
        const QRectF caret = m_editor->cursorRect(at);
        const QTextImageFormat f = c.charFormat().toImageFormat();
        const qreal w = f.width() > 0 ? f.width() : Images::naturalSize(doc, f.name()).width();
        const Images::Wrap wrap = Images::wrapAt(doc, pos);
        if (wrap != Images::Wrap::Inline
            || (documentPos.x() >= caret.left() - 2 && documentPos.x() <= caret.left() + w + 2)) {
            return pos;
        }
    }
    return -1;
}

void MainWindow::showImageProperties(int position)
{
    QTextDocument *doc = m_editor->document();
    QTextCursor sel = Images::imageAt(doc, position);
    if (sel.isNull()) {
        return;
    }
    const QTextImageFormat f = sel.charFormat().toImageFormat();
    const QSize natural = Images::naturalSize(doc, f.name());
    ImagePropertiesDialog dlg(natural, Images::columnWidth(doc), this);
    dlg.setImageSize(QSize(qRound(f.width() > 0 ? f.width() : natural.width()),
                           qRound(f.height() > 0 ? f.height() : natural.height())));
    dlg.setWrap(Images::wrapAt(doc, position));
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QSize size = dlg.imageSize();
    Images::applyImage(sel, f.name(), size.width(), size.height(), dlg.wrap());
    m_editor->setTextCursor(sel);
}

void MainWindow::onCursorMoved()
{
    syncFormatActions();
    centerCaret();
    updateFocusHighlight();
    updateTableActions();
    scheduleStatusUpdate();
}

void MainWindow::centerCaret()
{
    if (m_centering || !m_editor) {
        return;
    }
    const QRect cr = m_editor->cursorRect();

    if (m_fullPageView && m_pageScroll && m_pageCanvas) {
        const QRect shown = m_editor->toView(cr);
        // Pages stack in the canvas; the outer scroll area follows the caret.
        m_centering = true;
        const QPoint c = m_editor->mapTo(m_pageCanvas, shown.center());
        if (m_typewriterScroll) {
            if (!m_mouseActive) { // don't yank the view around on a mouse click
                QScrollBar *vs = m_pageScroll->verticalScrollBar();
                vs->setValue(c.y() - m_pageScroll->viewport()->height() / 2);
            }
        } else {
            m_pageScroll->ensureVisible(c.x(), c.y(), 20, 90);
        }
        m_centering = false;
        return;
    }

    if (!m_typewriterScroll) {
        return;
    }
    m_centering = true;
    QScrollBar *vs = m_editor->verticalScrollBar();
    // Scroll values and cursorRect() are document pixels; zoomed, the viewport
    // shows height/zoom of them.
    const int mid = qRound(m_editor->viewport()->height() / (2.0 * qMax(0.01, m_editor->zoom())));
    const int delta = cr.center().y() - mid;
    vs->setValue(vs->value() + delta);
    m_centering = false;
}

QPair<int, int> MainWindow::focusRange() const
{
    const QTextCursor cursor = m_editor->textCursor();
    const QTextBlock block = cursor.block();
    if (m_focusSentence) {
        // Unicode sentence boundaries (QTextBoundaryFinder) within the paragraph.
        const QPair<int, int> r = FocusRange::sentenceRange(block.text(), cursor.positionInBlock());
        return {block.position() + r.first, block.position() + r.second};
    }
    return {block.position(), block.position() + block.length() - 1};
}

void MainWindow::updateFocusHighlight()
{
    if (!m_editor) {
        return;
    }
    if (!m_focusMode) {
        if (!m_editor->extraSelections().isEmpty()) {
            m_editor->setExtraSelections({});
            if (m_editor->isZoomed()) {
                m_editor->viewport()->update();
            }
        }
        return;
    }

    const auto range = focusRange();
    const int docEnd = m_editor->document()->characterCount() - 1;
    QList<QTextEdit::ExtraSelection> extras;

    QTextCharFormat dimFmt;
    dimFmt.setForeground(Theme::colors(m_themeId).focusDim);

    if (range.first > 0) {
        QTextEdit::ExtraSelection before;
        before.format = dimFmt;
        before.cursor = QTextCursor(m_editor->document());
        before.cursor.setPosition(0);
        before.cursor.setPosition(range.first, QTextCursor::KeepAnchor);
        extras.append(before);
    }
    if (range.second < docEnd) {
        QTextEdit::ExtraSelection after;
        after.format = dimFmt;
        after.cursor = QTextCursor(m_editor->document());
        after.cursor.setPosition(qMax(range.second, 0));
        after.cursor.setPosition(docEnd, QTextCursor::KeepAnchor);
        extras.append(after);
    }
    m_editor->setExtraSelections(extras);
    if (m_editor->isZoomed()) {
        m_editor->viewport()->update(); // QTextEdit repaints unzoomed regions
    }
}

void MainWindow::showFind()
{
    const QString sel = m_editor->textCursor().selectedText();
    if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator)) {
        m_findBar->setFindText(sel);
    }
    m_findBar->showFind();
}

void MainWindow::showReplace()
{
    const QString sel = m_editor->textCursor().selectedText();
    if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator)) {
        m_findBar->setFindText(sel);
    }
    m_findBar->showReplace();
}

void MainWindow::hideFindBar()
{
    m_findBar->hide();
    m_editor->setFocus();
}

bool MainWindow::findInDoc(bool forward)
{
    const QString needle = m_findBar->findText();
    if (needle.isEmpty()) {
        m_findBar->setStatus(QString(), false);
        return false;
    }
    QTextDocument::FindFlags flags;
    if (!forward) {
        flags |= QTextDocument::FindBackward;
    }
    if (m_findBar->caseSensitive()) {
        flags |= QTextDocument::FindCaseSensitively;
    }
    const QTextCursor before = m_editor->textCursor();
    bool found = m_editor->find(needle, flags);
    bool wrapped = false;
    if (!found) {
        // Wrap around, but only move the caret if the text is really there.
        QTextCursor from(m_editor->document());
        from.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
        const QTextCursor hit = m_editor->document()->find(needle, from, flags);
        if (!hit.isNull()) {
            m_editor->setTextCursor(hit);
            found = true;
            wrapped = true;
        } else {
            m_editor->setTextCursor(before);
        }
    }
    if (!found) {
        const QString msg = QStringLiteral("Not found: \u201c%1\u201d").arg(needle);
        m_findBar->setStatus(QStringLiteral("Not found"), true);
        statusBar()->showMessage(msg, 4000);
    } else if (wrapped) {
        const QString msg = forward ? QStringLiteral("Wrapped to the top")
                                    : QStringLiteral("Wrapped to the bottom");
        m_findBar->setStatus(msg, false);
        statusBar()->showMessage(msg, 3000);
    } else {
        m_findBar->setStatus(QString(), false);
    }
    return found;
}

void MainWindow::findNext()
{
    // F3 with the bar closed repeats the last search (and stays in the text);
    // with nothing searched yet it opens the bar.
    if (!m_findBar->isVisible() && m_findBar->findText().isEmpty()) {
        showFind();
        return;
    }
    findInDoc(true);
}

void MainWindow::findPrev()
{
    if (!m_findBar->isVisible() && m_findBar->findText().isEmpty()) {
        showFind();
        return;
    }
    findInDoc(false);
}

void MainWindow::replaceOne()
{
    if (!m_findBar->isVisible()) {
        showReplace();
        return;
    }
    QTextCursor c = m_editor->textCursor();
    const QString needle = m_findBar->findText();
    if (c.hasSelection()) {
        const Qt::CaseSensitivity cs = m_findBar->caseSensitive()
            ? Qt::CaseSensitive
            : Qt::CaseInsensitive;
        if (QString::compare(c.selectedText(), needle, cs) == 0) {
            c.insertText(m_findBar->replaceText());
        }
    }
    findInDoc(true);
}

void MainWindow::replaceAll()
{
    if (!m_findBar->isVisible()) {
        showReplace();
        return;
    }
    const QString needle = m_findBar->findText();
    if (needle.isEmpty()) {
        return;
    }
    const QString replacement = m_findBar->replaceText();
    QTextDocument::FindFlags flags;
    if (m_findBar->caseSensitive()) {
        flags |= QTextDocument::FindCaseSensitively;
    }

    QTextCursor cursor(m_editor->document());
    cursor.beginEditBlock();
    int count = 0;
    QTextCursor found = m_editor->document()->find(needle, 0, flags);
    while (!found.isNull()) {
        found.insertText(replacement);
        ++count;
        found = m_editor->document()->find(needle, found.position(), flags);
    }
    cursor.endEditBlock();
    statusBar()->showMessage(QStringLiteral("Replaced %1 occurrence(s)").arg(count), 3000);
    setChromeVisible(true);
}

bool MainWindow::trySmartTypography(QKeyEvent *event)
{
    if (!m_smartQuotes || !m_editor) {
        return false;
    }
    // Only plain printable inserts — skip modifiers / navigation.
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    const QString text = event->text();
    if (text.isEmpty()) {
        return false;
    }

    QTextCursor cursor = m_editor->textCursor();
    const QChar ch = text.at(0);

    auto precedingChar = [&]() -> QChar {
        if (cursor.position() <= 0) {
            return QChar();
        }
        QTextCursor probe = cursor;
        probe.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
        const QString s = probe.selectedText();
        return s.isEmpty() ? QChar() : s.at(0);
    };

    if (ch == QLatin1Char('"')) {
        const QChar prev = precedingChar();
        const bool opening = prev.isNull() || prev.isSpace()
            || prev == QLatin1Char('(') || prev == QLatin1Char('[')
            || prev == QChar::ParagraphSeparator;
        cursor.insertText(opening ? QStringLiteral("“") : QStringLiteral("”"));
        m_editor->setTextCursor(cursor);
        return true;
    }
    if (ch == QLatin1Char('\'')) {
        const QChar prev = precedingChar();
        const bool opening = prev.isNull() || prev.isSpace()
            || prev == QLatin1Char('(') || prev == QLatin1Char('[')
            || prev == QChar::ParagraphSeparator;
        cursor.insertText(opening ? QStringLiteral("‘") : QStringLiteral("’"));
        m_editor->setTextCursor(cursor);
        return true;
    }
    if (ch == QLatin1Char('-')) {
        // --- → em dash; -- → en dash. If previous is already en dash + '-', make em.
        if (cursor.position() >= 1) {
            QTextCursor probe = cursor;
            probe.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
            const QString prev = probe.selectedText();
            if (prev == QStringLiteral("–")) {
                probe.insertText(QStringLiteral("—"));
                m_editor->setTextCursor(probe);
                return true;
            }
            if (prev == QStringLiteral("-")) {
                probe.insertText(QStringLiteral("–"));
                m_editor->setTextCursor(probe);
                return true;
            }
        }
    }
    return false;
}

void MainWindow::onFontFamilyChosen(const QFont &font)
{
    if (!m_editor) {
        return;
    }
    QTextCharFormat fmt;
    fmt.setFontFamilies(font.families().isEmpty()
                            ? QStringList{font.family()}
                            : font.families());
    // Preserve point size from spin (or current) — family change alone.
    const qreal size = m_fontSizeSpin ? m_fontSizeSpin->value()
                                      : m_editor->currentCharFormat().fontPointSize();
    if (size > 0) {
        fmt.setFontPointSize(size);
    }
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus(); // the choice is made: back to the page
}

void MainWindow::onFontSizeChosen(int pointSize)
{
    if (!m_editor || pointSize < 1) {
        return;
    }
    QTextCharFormat fmt;
    fmt.setFontPointSize(pointSize);
    m_editor->mergeCurrentCharFormat(fmt);
    // Focus stays in the size box so further arrow presses keep adjusting it.
}

void MainWindow::toggleUnderline()
{
    QTextCharFormat fmt;
    fmt.setFontUnderline(m_underlineAction->isChecked());
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void MainWindow::setVerticalAlign(QTextCharFormat::VerticalAlignment align)
{
    QTextCharFormat fmt;
    fmt.setVerticalAlignment(align);
    m_editor->mergeCurrentCharFormat(fmt);
    {
        const QSignalBlocker a(m_subscriptAction);
        const QSignalBlocker b(m_superscriptAction);
        m_subscriptAction->setChecked(align == QTextCharFormat::AlignSubScript);
        m_superscriptAction->setChecked(align == QTextCharFormat::AlignSuperScript);
    }
    m_editor->setFocus();
}

void MainWindow::editPastePlain()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QString text = mime ? mime->text() : QString();
    if (text.isEmpty() && mime && mime->hasHtml()) {
        // Browsers and some apps put only HTML on the clipboard.
        text = QTextDocumentFragment::fromHtml(mime->html()).toPlainText();
    }
    if (!text.isEmpty()) {
        m_editor->insertPlainText(text);
    }
    m_editor->setFocus();
}

void MainWindow::clearFormatting()
{
    QTextCharFormat plain;
    plain.setFontFamilies(defaultDocumentFont().families());
    plain.setFontPointSize(kDefaultBodyPointSize);
    plain.setFontWeight(QFont::Normal);
    plain.setFontItalic(false);
    plain.setFontUnderline(false);
    plain.setFontStrikeOut(false);
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        cursor.setCharFormat(plain);
        m_editor->setTextCursor(cursor);
    } else {
        m_editor->setCurrentCharFormat(plain);
    }
    m_editor->setFocus();
}

void MainWindow::applyList(int style, bool on)
{
    QTextCursor cursor = m_editor->textCursor();
    cursor.beginEditBlock();
    if (on) {
        QTextBlockFormat blockFmt = cursor.blockFormat();
        QTextListFormat listFmt;
        if (cursor.currentList()) {
            listFmt = cursor.currentList()->format();
        } else {
            listFmt.setIndent(blockFmt.indent() + 1);
            blockFmt.setIndent(0);
            cursor.setBlockFormat(blockFmt);
        }
        listFmt.setStyle(static_cast<QTextListFormat::Style>(style));
        cursor.createList(listFmt);
    } else {
        // Leave the list without the indent Qt would keep in its place (it is
        // saved as a margin). An indent the paragraph had before the list
        // (moved into the list's indent by createList above) comes back.
        // (Merging objectIndex -1 does not take a block out of its list:
        // setObjectIndex(-1) clears the property, so the merge keeps it.)
        QTextDocument *doc = m_editor->document();
        QTextBlock b = doc->findBlock(cursor.selectionStart());
        const QTextBlock end = doc->findBlock(cursor.selectionEnd());
        while (b.isValid()) {
            if (QTextList *list = b.textList()) {
                const int before = qMax(0, list->format().indent() - 1);
                list->remove(b);
                QTextCursor bc(b);
                QTextBlockFormat bf = bc.blockFormat();
                bf.setIndent(before);
                bc.setBlockFormat(bf);
            }
            if (b == end) {
                break;
            }
            b = b.next();
        }
    }
    cursor.endEditBlock();
    syncFormatActions();
    m_editor->setFocus();
}

void MainWindow::setMarkdownMode(bool on)
{
    if (m_markdownMode == on) {
        return;
    }
    if (on) {
        m_odtFullPageView = m_fullPageView;
        m_odtChromePinned = m_hideAwayPinned;
        m_fullPageView = false;                  // continuous strip
        m_hideAwayPinned = m_markdownChromePinned; // hide-away chrome by default
    } else {
        m_markdownChromePinned = m_hideAwayPinned;
        m_fullPageView = m_odtFullPageView;
        m_hideAwayPinned = m_odtChromePinned;
    }
    m_markdownMode = on;
    if (m_ruler) {
        m_ruler->setVisible(!on && m_rulerAction && m_rulerAction->isChecked());
    }
    for (QAction *a : {m_paragraphDialogAction, m_indentAction, m_outdentAction}) {
        if (a) {
            a->setEnabled(!on);
        }
    }

    // Switching modes restyles the view; it is not an edit.
    const QScopedValueRollback guard(m_suppressDirty, true);
    const bool wasModified = m_editor->document()->isModified();
    m_editor->setAcceptRichText(!on); // Markdown: paste and drop plain text only
    if (m_highlighter) {
        m_highlighter->setMarkdownEnabled(on);
    }
    syncRichActions();
    applyDocumentDefaults();
    applyTheme();
    applyFullPageView();
    if (m_hideTimer) {
        m_hideTimer->stop();
    }
    setChromeVisible(m_hideAwayPinned);
    syncViewActions();
    m_editor->document()->setModified(wasModified);
}

void MainWindow::applyMarkdownBlockFormat()
{
    // 150 % leading on every paragraph. Enter and plain-text paste copy the
    // current block's format, so new lines keep it. Layout only: callers clear
    // the undo stack afterwards, and the text (all that is saved) is unchanged.
    QTextDocument *doc = m_editor->document();
    const QScopedValueRollback guard(m_suppressDirty, true);
    const bool wasModified = doc->isModified();
    QTextBlockFormat fmt;
    fmt.setLineHeight(kMarkdownLineHeightPercent, QTextBlockFormat::ProportionalHeight);
    QTextCursor all(doc);
    all.select(QTextCursor::Document);
    all.mergeBlockFormat(fmt);
    doc->setModified(wasModified);
}

void MainWindow::syncRichActions()
{
    const bool rich = !m_markdownMode;
    for (QAction *a : {m_boldAction, m_italicAction, m_underlineAction, m_subscriptAction, m_superscriptAction, m_paragraphAction,
                       m_h1Action, m_h2Action, m_h3Action, m_bulletListAction, m_numberListAction,
                       m_clearFormatAction, m_insertTableAction, m_insertCaptionAction, m_pageBreakAction,
                       m_pageNumbersAction, m_headerFooterAction}) {
        if (a) {
            a->setEnabled(rich);
        }
    }
    if (m_alignActions) {
        for (QAction *a : m_alignActions->actions()) {
            a->setEnabled(rich);
        }
    }
    for (QWidget *w : {static_cast<QWidget *>(m_fontCombo), static_cast<QWidget *>(m_fontSizeSpin),
                       static_cast<QWidget *>(m_styleCombo)}) {
        if (w) {
            w->setEnabled(rich);
        }
    }
    if (m_formatMenu) {
        m_formatMenu->menuAction()->setVisible(rich); // nothing in it applies to Markdown
    }
    updateTableActions();
}

void MainWindow::updateColumnCap()
{
    if (!m_editor) {
        return;
    }
    if (!m_markdownMode) {
        m_editor->setMaxColumnWidth(0); // ODT/TXT/RTF keep the 60 % column
        return;
    }
    const QFontMetricsF fm(markdownFont(), m_editor);
    m_editor->setMaxColumnWidth(
        fm.horizontalAdvance(QString(kMarkdownColumnChars, QLatin1Char('n'))));
}

void MainWindow::toggleBold()
{
    QTextCharFormat fmt;
    fmt.setFontWeight(m_boldAction->isChecked() ? QFont::Bold : QFont::Normal);
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void MainWindow::toggleItalic()
{
    QTextCharFormat fmt;
    fmt.setFontItalic(m_italicAction->isChecked());
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void MainWindow::applyHeading()
{
    QAction *act = m_paragraphAction;
    if (m_h1Action->isChecked()) {
        act = m_h1Action;
    } else if (m_h2Action->isChecked()) {
        act = m_h2Action;
    } else if (m_h3Action->isChecked()) {
        act = m_h3Action;
    }

    const int level = act->data().toInt();
    // Body Text / Heading replaces a named style (Title, Bibliography, ...).
    ParagraphStyles::apply(m_editor->textCursor(), QString());

    // Size and face per style from Format > Styles… (1.15.0; built in:
    // Body 12, Heading 1 22, Heading 2 17, Heading 3 13 pt).
    const QString styleKey = StyleFonts::keyFor(level, QString());
    const StyleFonts::Entry styleFont = StyleFonts::entry(m_styleFonts, styleKey);
    QTextCharFormat charFmt;
    charFmt.setFontPointSize(styleFont.pointSize);
    if (level == 0) {
        charFmt.setFontWeight(QFont::Normal);
        if (!styleFont.family.isEmpty()) {
            charFmt.setFontFamilies(StyleFonts::families(m_styleFonts, styleKey, DocumentIo::defaultFontFamilies(),
                                                         DocumentIo::headingFontFamilies()));
        }
    } else {
        charFmt.setFontWeight(QFont::Bold);
        // Heading 3: smaller and italic, so it reads apart from Heading 2.
        if (level >= 3) {
            charFmt.setFontItalic(true);
        }
        // A heading uses the heading face (as a new chapter's title does),
        // whatever face the text had: typed after a Courier run, it used to stay Courier.
        charFmt.setFontFamilies(StyleFonts::families(m_styleFonts, styleKey, DocumentIo::defaultFontFamilies(),
                                                     DocumentIo::headingFontFamilies()));
    }

    // Apply to every block the selection touches. The style has to reach the
    // text runs themselves: typed text carries an explicit font, which would
    // otherwise override a block-level format.
    QTextCursor cursor = m_editor->textCursor();
    const int first = cursor.selectionStart();
    const int last = cursor.selectionEnd();
    cursor.beginEditBlock();
    QTextCursor walk(m_editor->document());
    walk.setPosition(first);
    while (true) {
        QTextBlockFormat blockFmt = walk.blockFormat();
        QTextCharFormat runFmt = charFmt;
        if (blockFmt.headingLevel() == 3 && level != 3) {
            runFmt.setFontItalic(false); // Heading 3's italic goes with it
        }
        blockFmt.setHeadingLevel(level);
        walk.setBlockFormat(blockFmt);
        walk.mergeBlockCharFormat(runFmt);
        QTextCursor text(walk.block());
        text.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        text.mergeCharFormat(runFmt);
        if (walk.block().next().isValid() && walk.block().next().position() <= last) {
            walk.setPosition(walk.block().next().position());
        } else {
            break;
        }
    }
    cursor.endEditBlock();

    // Keep typing in the new style, and show it (Bold must not stay checked
    // from the heading, or Ctrl+B would turn bold off instead of on).
    m_editor->mergeCurrentCharFormat(charFmt);
    syncFormatActions();
    m_editor->setFocus();
}

void MainWindow::syncFormatActions()
{
    if (!m_editor) {
        return;
    }

    const QTextCharFormat fmt = m_editor->currentCharFormat();
    const bool bold = fmt.fontWeight() >= QFont::Bold;
    const bool italic = fmt.fontItalic();

    if (m_boldAction) {
        const QSignalBlocker b(m_boldAction);
        m_boldAction->setChecked(bold);
    }
    if (m_italicAction) {
        const QSignalBlocker b(m_italicAction);
        m_italicAction->setChecked(italic);
    }
    if (m_underlineAction) {
        const QSignalBlocker b(m_underlineAction);
        m_underlineAction->setChecked(fmt.fontUnderline());
    }
    if (m_subscriptAction && m_superscriptAction) {
        const QSignalBlocker a(m_subscriptAction);
        const QSignalBlocker b(m_superscriptAction);
        m_subscriptAction->setChecked(fmt.verticalAlignment() == QTextCharFormat::AlignSubScript);
        m_superscriptAction->setChecked(fmt.verticalAlignment() == QTextCharFormat::AlignSuperScript);
    }

    if (m_alignActions) {
        m_alignActions->sync();
    }

    const QTextList *list = m_editor->textCursor().currentList();
    const auto listStyle = list ? list->format().style() : QTextListFormat::ListStyleUndefined;
    const bool bullets = listStyle == QTextListFormat::ListDisc
        || listStyle == QTextListFormat::ListCircle || listStyle == QTextListFormat::ListSquare;
    const bool numbers = list && !bullets && listStyle != QTextListFormat::ListStyleUndefined;
    if (m_bulletListAction) {
        const QSignalBlocker b(m_bulletListAction);
        m_bulletListAction->setChecked(bullets);
    }
    if (m_numberListAction) {
        const QSignalBlocker b(m_numberListAction);
        m_numberListAction->setChecked(numbers);
    }

    if (m_fontCombo) {
        const QSignalBlocker b(m_fontCombo);
        QFont shown = fmt.font();
        if (shown.family().isEmpty()) {
            shown = m_editor->document()->defaultFont();
        }
        m_fontCombo->setCurrentFont(shown);
    }
    if (m_fontSizeSpin) {
        const QSignalBlocker b(m_fontSizeSpin);
        qreal pts = fmt.fontPointSize();
        if (pts <= 0) {
            pts = m_editor->document()->defaultFont().pointSizeF();
        }
        if (pts <= 0) {
            pts = kDefaultBodyPointSize;
        }
        m_fontSizeSpin->setValue(int(pts + 0.5));
    }

    const int level = m_editor->textCursor().blockFormat().headingLevel();
    QAction *checked = m_paragraphAction;
    if (level == 1) {
        checked = m_h1Action;
    } else if (level == 2) {
        checked = m_h2Action;
    } else if (level >= 3) {
        checked = m_h3Action;
    }
    for (QAction *a : {m_paragraphAction, m_h1Action, m_h2Action, m_h3Action}) {
        if (!a) {
            continue;
        }
        const QSignalBlocker b(a);
        a->setChecked(a == checked);
    }
    syncStyleControls();
}

void MainWindow::insertPageBreak()
{
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.currentTable()) {
        statusBar()->showMessage(QStringLiteral("A page break can’t go inside a table."), 3000);
        return;
    }
    // Split the paragraph here; the second half starts a new page. (Removing the
    // break is just Backspace at the start of that paragraph.)
    cursor.beginEditBlock();
    QTextBlockFormat fmt = cursor.blockFormat();
    fmt.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    cursor.insertBlock(fmt);
    cursor.endEditBlock();
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
}

void MainWindow::handleEnterOnPageBreak(QKeyEvent *ke)
{
    QTextCursor c = m_editor->textCursor();
    const int blockBefore = c.blockNumber();
    const QTextBlockFormat before = c.blockFormat();
    const QTextFormat::PageBreakFlags breakFlags = before.pageBreakPolicy();

    if (c.block().text().isEmpty() && !c.currentList()) {
        // An empty paragraph at the top of a page: Qt would reset its format
        // (dropping the break) instead of adding a line. Add the line, and keep
        // the break on the first paragraph of the page.
        QTextBlockFormat next = before;
        next.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
        next.clearProperty(QTextFormat::HeadingLevel);
        c.insertBlock(next);
        m_editor->setTextCursor(c);
        return;
    }

    // Let Qt handle Enter (ends an empty list item, drops heading level on the
    // new paragraph, ...), then repair the page break in the same undo step.
    m_editor->removeEventFilter(this);
    QCoreApplication::sendEvent(m_editor, ke);
    m_editor->installEventFilter(this);

    QTextCursor after = m_editor->textCursor();
    QTextCursor fix(after.block());
    fix.joinPreviousEditBlock();
    if (after.blockNumber() == blockBefore) {
        // Same paragraph (e.g. empty list item ended): it still starts the page.
        QTextBlockFormat bf = after.blockFormat();
        if (bf.pageBreakPolicy() != breakFlags) {
            bf.setPageBreakPolicy(breakFlags);
            fix.setBlockFormat(bf);
        }
    } else {
        // A new paragraph was split off: only the first one keeps the break.
        QTextBlock first = m_editor->document()->findBlockByNumber(blockBefore);
        for (QTextBlock b = first.next(); b.isValid() && b.blockNumber() <= after.blockNumber(); b = b.next()) {
            QTextBlockFormat bf = b.blockFormat();
            if (bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) {
                bf.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
                QTextCursor(b).setBlockFormat(bf);
            }
        }
    }
    fix.endEditBlock();
}

bool MainWindow::pageNumbersOn() const
{
    for (const QString *band : {&m_meta.headerLeft, &m_meta.headerCenter, &m_meta.headerRight,
                                &m_meta.footerLeft, &m_meta.footerCenter, &m_meta.footerRight}) {
        if (band->contains(QLatin1String("{page}"))) {
            return true;
        }
    }
    return false;
}

QString *MainWindow::headerFooterBand(int index)
{
    switch (index) {
    case 0: return &m_meta.headerLeft;
    case 1: return &m_meta.headerCenter;
    case 2: return &m_meta.headerRight;
    case 3: return &m_meta.footerLeft;
    case 4: return &m_meta.footerCenter;
    default: return &m_meta.footerRight;
    }
}

void MainWindow::forgetPageNumberState()
{
    m_pageNumbersSnapshot = PageNumbersSnapshot{};
    m_pageNumberBand = -1;
    m_pageNumberBandBefore.clear();
}

void MainWindow::togglePageNumbers()
{
    const bool on = m_pageNumbersAction && m_pageNumbersAction->isChecked();
    if (on) {
        if (m_pageNumbersSnapshot.valid) {
            // Turning numbers back on restores the header/footer exactly as it
            // was before they were turned off (custom text and all).
            for (int i = 0; i < 6; ++i) {
                *headerFooterBand(i) = m_pageNumbersSnapshot.bands[i];
            }
            m_pageNumberBand = m_pageNumbersSnapshot.addedBand;
            m_pageNumberBandBefore = m_pageNumbersSnapshot.addedBandBefore;
            m_pageNumbersSnapshot = PageNumbersSnapshot{};
        }
        if (!pageNumbersOn()) {
            // Centre of the footer, else the first free footer slot, else
            // appended to the centre text. Remember what we added.
            m_pageNumberBand = -1;
            for (int i : {4, 5, 3}) {
                if (headerFooterBand(i)->isEmpty()) {
                    m_pageNumberBand = i;
                    break;
                }
            }
            if (m_pageNumberBand < 0) {
                m_pageNumberBand = 4;
            }
            QString *band = headerFooterBand(m_pageNumberBand);
            m_pageNumberBandBefore = *band;
            *band = band->isEmpty() ? QStringLiteral("{page}") : *band + QStringLiteral(" {page}");
        }
    } else {
        m_pageNumbersSnapshot.valid = true;
        for (int i = 0; i < 6; ++i) {
            m_pageNumbersSnapshot.bands[i] = *headerFooterBand(i);
        }
        m_pageNumbersSnapshot.addedBand = m_pageNumberBand;
        m_pageNumbersSnapshot.addedBandBefore = m_pageNumberBandBefore;
        // What the toggle added goes away exactly; page tokens the user typed
        // are hidden (a band that is only a page number is emptied) until the
        // toggle is turned back on, which restores the snapshot above.
        static const QRegularExpression onlyNumber(
            QStringLiteral(R"(^[\s\-–—|/.·•]*(page\s*)?\{page\}(\s*(of|/)\s*\{pages\})?[\s\-–—|/.·•]*$)"),
            QRegularExpression::CaseInsensitiveOption);
        for (int i = 0; i < 6; ++i) {
            QString *band = headerFooterBand(i);
            if (i == m_pageNumberBand) {
                *band = m_pageNumberBandBefore;
                continue;
            }
            if (!band->contains(QLatin1String("{page}"))) {
                continue;
            }
            if (onlyNumber.match(*band).hasMatch()) {
                band->clear();
            } else {
                band->remove(QLatin1String("{pages}")).remove(QLatin1String("{page}"));
                *band = band->simplified();
            }
        }
        m_pageNumberBand = -1;
        m_pageNumberBandBefore.clear();
    }
    m_meta.headerFooterSeeded = true;
    m_metaDirty = true;
    markDirty();
    syncViewActions();
    m_editor->viewport()->update();
}

void MainWindow::editHeaderFooter()
{
    m_meta.ensureDefaults();
    HeaderFooterDialog dlg(m_meta, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    dlg.applyTo(&m_meta);
    m_meta.headerFooterSeeded = true;
    forgetPageNumberState(); // the user edited the bands by hand
    m_metaDirty = true;
    m_dirty = true;
    updateWindowTitle();
    syncViewActions();
    if (m_editor) {
        m_editor->viewport()->update();
    }
}

void MainWindow::helpAbout()
{
    QMessageBox::about(
        this,
        QStringLiteral("About zwriter"),
        QStringLiteral(
            "<h3>zwriter %1</h3>"
            "<p>Distraction-free writing for Linux amd64 and Apple Silicon.</p>"
            "<p>Sibling to zedit — not a fork. Kinship to FocusWriter.</p>"
            "<p>MIT License — Copyright © 2026 Stephen B. Johnson</p>"
            "<p>Markdown mode uses the iA Writer Duo and Mono fonts — © Information Architects "
            "Inc., based on IBM Plex — under the SIL Open Font License 1.1.</p>"
            "<p><a href=\"https://github.com/sbj-ee/zwriter\">github.com/sbj-ee/zwriter</a></p>"
        ).arg(QString::fromStdString(zwriter::versionLabel())));
}

void MainWindow::helpCheckUpdates()
{
    // Check, offer, download, verify, install and restart prompt live in
    // UpdateManager (the same flow as zterminal's).
    setChromeVisible(true);
    zwriter::UpdateManager::instance().setHost(this);
    zwriter::UpdateManager::instance().checkNow(this);
}

QStringList MainWindow::restartArguments() const
{
    return m_currentPath.isEmpty() ? QStringList() : QStringList{QFileInfo(m_currentPath).absoluteFilePath()};
}

void MainWindow::captureDemoScreenshots(const QString &dir)
{
    // Tall enough to show a full A4 page at screen logical DPI without clipping.
    resize(1024, 1400);

    // Screenshots use the default paper theme (near-white page) + full page view.
    // Side panes stay off until their own shots, whatever the user's settings.
    m_previewWanted = false;
    m_library->hide();
    m_book->hide();
    m_themeId = QStringLiteral("paper");
    m_fullPageView = true;
    applyDocumentDefaults();
    m_meta.ensureDefaults();
    // Keep Lorem / chrome shots clean; dedicated spell-check.png enables underlines.
    m_spellCheck = false;
    if (m_spellChecker) {
        m_spellChecker->setEnabled(false);
    }
    if (m_highlighter) {
        m_highlighter->refreshAll();
    }
    applyTheme();
    applyFullPageView();
    syncViewActions();

    const QString loremBody = QStringLiteral(
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod "
        "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, "
        "quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\n\n"
        "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu "
        "fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum. Curabitur pretium tincidunt "
        "lacus. Nulla gravida orci a odio.\n\n"
        "Nullam varius, turpis et commodo pharetra, est eros bibendum elit, nec luctus magna "
        "felis sollicitudin mauris. Integer in mauris eu nibh euismod gravida. Duis ac tellus "
        "et risus vulputate vehicula. Donec lobortis risus a elit. Etiam tempor.\n\n"
        "Ut ullamcorper, ligula eu tempor congue, eros est euismod turpis, id tincidunt sapien "
        "risus a quam. Maecenas fermentum consequat mi. Donec fermentum. Pellentesque malesuada "
        "nulla a mi. Duis sapien sem, aliquet nec, commodo eget, consequat quis, neque.\n\n"
        "Aliquam faucibus, elit ut dictum aliquet, felis nisl adipiscing sapien, sed malesuada "
        "diam lacus eget erat. Cras mollis scelerisque nunc. Nullam arcu. Aliquam consequat. "
        "Curabitur augue lorem, dapibus quis, laoreet et, pretium ac, nisi.\n\n"
        "Aenean magna nisl, mollis quis, molestie eu, feugiat in, orci. In hac habitasse platea "
        "dictumst. Integer tempus convallis augue. Etiam facilisis. Nunc elementum fermentum "
        "wisi. Aenean placerat. Ut imperdiet, enim sed gravida sollicitudin.");

    m_editor->setPlainText(loremBody);
    m_editor->moveCursor(QTextCursor::Start);
    for (int i = 0; i < 2; ++i) {
        m_editor->moveCursor(QTextCursor::Down);
    }
    m_editor->moveCursor(QTextCursor::EndOfWord);

    // 1) Editor with hide-away chrome revealed (toolbar + status + reading time).
    m_hideAwayPinned = true;
    setChromeVisible(true);
    m_typewriterScroll = true;
    if (m_typewriterScrollAction) {
        const QSignalBlocker b(m_typewriterScrollAction);
        m_typewriterScrollAction->setChecked(true);
    }
    updateStats();
    centerCaret();
    QApplication::processEvents();
    grab().save(dir + QStringLiteral("/editor-chrome.png"), "PNG");

    // 2) Focus mode (paragraph) — dim surrounding text.
    m_focusMode = true;
    m_focusSentence = false;
    syncViewActions();
    updateFocusHighlight();
    QApplication::processEvents();
    grab().save(dir + QStringLiteral("/editor-focus.png"), "PNG");

    // 3) Find bar open.
    m_findBar->setFindText(QStringLiteral("ipsum"));
    m_findBar->showFind();
    findNext();
    QApplication::processEvents();
    grab().save(dir + QStringLiteral("/find-bar.png"), "PNG");
    hideFindBar();
    statusBar()->clearMessage(); // "Wrapped to the top" would linger in later shots

    // Table demo shot.
    {
        m_focusMode = false;
        updateFocusHighlight();
        QTextCursor cursor = m_editor->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertText(QStringLiteral("Scene notes"));
        cursor.insertBlock();
        QTextTable *table = cursor.insertTable(3, 3);
        DocumentIo::styleTable(table);
        const QStringList cells = {
            QStringLiteral("Beat"), QStringLiteral("Place"), QStringLiteral("Note"),
            QStringLiteral("1"), QStringLiteral("Harbor"), QStringLiteral("Rain"),
            QStringLiteral("2"), QStringLiteral("Desk"), QStringLiteral("First sentence"),
        };
        for (int i = 0; i < cells.size(); ++i) {
            table->cellAt(i / 3, i % 3).firstCursorPosition().insertText(cells.at(i));
        }
        m_editor->setTextCursor(table->cellAt(1, 1).firstCursorPosition());
        updateStats();
        updateTableActions();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/editor-table.png"), "PNG");
    }


    // Fonts toolbar: paper page + family/size controls + mixed face sample.
    {
        m_focusMode = false;
        updateFocusHighlight();
        m_hideAwayPinned = true;
        setChromeVisible(true);
        m_editor->clear();
        QTextCursor cursor(m_editor->document());
        auto insertStyled = [&](const QString &family, int pt, const QString &text) {
            QTextCharFormat fmt;
            fmt.setFontFamilies({family});
            fmt.setFontPointSize(pt);
            fmt.setForeground(QColor(QStringLiteral("#1a1a1a")));
            cursor.insertText(text, fmt);
            cursor.insertBlock();
        };
        insertStyled(QStringLiteral("Liberation Mono"), kDefaultBodyPointSize,
                     QStringLiteral("Liberation Mono — metric-compatible fallback when Courier is not installed."));
        insertStyled(QStringLiteral("Courier Prime"), kDefaultBodyPointSize,
                     QStringLiteral("Courier Prime — classic typewriter face."));
        insertStyled(QStringLiteral("DejaVu Sans"), kDefaultBodyPointSize,
                     QStringLiteral("DejaVu Sans — switch away via the font picker anytime."));
        insertStyled(QStringLiteral("Noto Serif"), 14,
                     QStringLiteral("Noto Serif 14pt — optional serif for long reading."));
        m_editor->moveCursor(QTextCursor::Start);
        syncFormatActions();
        updateStats();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/fonts-toolbar.png"), "PNG");
    }

    // Full page view: centered paper page + margins on desk (paper theme).
    {
        m_focusMode = false;
        updateFocusHighlight();
        hideFindBar();
        m_fullPageView = true;
        m_pageGuides = true;
        if (m_pageGuidesAction) {
            const QSignalBlocker b(m_pageGuidesAction);
            m_pageGuidesAction->setChecked(true);
        }
        m_hideAwayPinned = true;
        setChromeVisible(true);
        // Demo header/footer: page numbers are opt-in (Format > Page Numbers,
        // off by default); the demo turns them on and adds a title header.
        m_meta.ensureDefaults();
        m_meta.headerCenter = QStringLiteral("Lorem Draft");
        m_meta.footerCenter = QStringLiteral("{page}");
        m_meta.headerFooterSeeded = true;
        m_editor->setPlainText(
            loremBody + QStringLiteral("\n\n— full page view — A4 · 12pt typewriter body —"));
        m_editor->moveCursor(QTextCursor::Start);
        applyTheme();
        applyFullPageView();
        syncViewActions();
        updateStats();
        QApplication::processEvents();
        // Second pass after desk has a real size under xvfb.
        updateFullPageGeometry();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/full-page.png"), "PNG");
        m_pageGuides = false;
        if (m_pageGuidesAction) {
            const QSignalBlocker b(m_pageGuidesAction);
            m_pageGuidesAction->setChecked(false);
        }
    }

    // Multi-page Full Page view: two true-size A4 sheets stacked on the desk,
    // the second one started by a manual page break (Format > Insert Page
    // Break). The window is made tall enough for both pages; the page size is
    // not scaled.
    {
        m_focusMode = false;
        updateFocusHighlight();
        hideFindBar();
        m_fullPageView = true;
        m_hideAwayPinned = true;
        setChromeVisible(true);
        m_meta.ensureDefaults();
        m_meta.headerCenter = QStringLiteral("Lorem Draft");
        m_meta.footerCenter = QStringLiteral("Page {page} of {pages}");
        m_meta.headerFooterSeeded = true;
        m_editor->setPlainText(QStringLiteral("Chapter One\n\n") + loremBody);
        QTextCursor cursor(m_editor->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertBlock();
        cursor.insertText(QStringLiteral("— end of chapter one —"));
        m_editor->setTextCursor(cursor);
        insertPageBreak();
        cursor = m_editor->textCursor();
        cursor.insertText(QStringLiteral("Chapter Two"));
        // Plain paragraphs after the break (a bare insertBlock() would copy it).
        cursor.insertBlock(QTextBlockFormat());
        cursor.insertBlock(QTextBlockFormat());
        cursor.insertText(QStringLiteral(
            "A manual page break (Ctrl+Enter) starts this chapter on a new sheet. "
            "What you see here is what print and PDF produce: same A4 page, same "
            "margins, same line breaks, page numbers in the footer."));
        m_editor->setTextCursor(cursor);
        // Chapter titles as Heading 1.
        for (int blockNo : {0, m_editor->document()->blockCount() - 3}) {
            QTextCursor h(m_editor->document()->findBlockByNumber(blockNo));
            m_editor->setTextCursor(h);
            if (m_h1Action) {
                m_h1Action->setChecked(true);
                applyHeading();
            }
        }
        m_editor->moveCursor(QTextCursor::Start);
        resize(1024, 2440);
        applyTheme();
        applyFullPageView();
        syncViewActions();
        updateStats();
        QApplication::processEvents();
        updateFullPageGeometry();
        QApplication::processEvents();
        m_pageScroll->verticalScrollBar()->setValue(0);
        updatePageLabel();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/full-page-multipage.png"), "PNG");
        resize(1024, 1400);
        m_meta.headerCenter.clear();
        m_meta.footerCenter.clear();
        // Drop the Heading 1 char format the cursor is sitting in.
        m_editor->clear();
        applyDocumentDefaults();
        QApplication::processEvents();
    }

    // Spell check: English body with intentional misspellings + live underlines.
    {
        m_focusMode = false;
        updateFocusHighlight();
        hideFindBar();
        m_fullPageView = true;
        m_pageGuides = false;
        m_spellCheck = true;
        if (m_spellChecker) {
            m_spellChecker->setEnabled(true);
        }
        if (m_spellCheckAction) {
            const QSignalBlocker b(m_spellCheckAction);
            m_spellCheckAction->setChecked(true);
        }
        m_hideAwayPinned = true;
        setChromeVisible(true);
        m_editor->setPlainText(
            QStringLiteral(
                "She opened the notebook and wrote the first true sentence of the day.\n\n"
                "Everything else could wait — except this mispeled word and teh extra typo "
                "sitting right there on the page. The typewriter clicked once, then again.\n\n"
                "Spell check underlines misspellings; right-click for suggestions, ignore, "
                "or add to the user dictionary."));
        m_editor->moveCursor(QTextCursor::Start);
        applyTheme();
        applyFullPageView();
        syncViewActions();
        if (m_highlighter) {
            m_highlighter->refreshAll();
        }
        updateStats();
        QApplication::processEvents();
        updateFullPageGeometry();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/spell-check.png"), "PNG");
        m_spellCheck = false;
        if (m_spellChecker) {
            m_spellChecker->setEnabled(false);
        }
        if (m_highlighter) {
            m_highlighter->refreshAll();
        }
    }

    // Markdown mode: iA-style continuous column (iA Writer Duo, ~66 characters,
    // 150 % leading), inline styling with the markup kept visible, hide-away
    // chrome; then the chrome revealed (slim status bar), then sentence focus
    // in the Dark room theme.
    {
        m_focusMode = false;
        updateFocusHighlight();
        hideFindBar();
        resize(1024, 760);
        statusBar()->clearMessage(); // e.g. the find bar's "Wrapped to the top"
        m_editor->clear();
        setMarkdownMode(true);
        m_hideAwayPinned = false;
        DocumentIo::resetMarkdownFileFormat(m_editor->document());
        m_editor->setPlainText(QStringLiteral(
            "# The Quiet Draft\n"
            "\n"
            "Write first, *polish* later. Markdown stays **visible**: the markup recedes into a "
            "soft grey so the words come forward. Nothing is hidden and nothing is converted.\n"
            "\n"
            "## Notes for chapter two\n"
            "\n"
            "- Keep the harbor scene short\n"
            "- [x] Move the letter to page one\n"
            "- Check the `draft-2.md` outline\n"
            "\n"
            "> The best way out is always through.\n"
            "\n"
            "See [the style notes](https://example.com/style) before the next pass.\n"
            "\n"
            "```text\n"
            "Fenced code keeps its own colour.\n"
            "```\n"));
        applyFullPageView(); // setPlainText() reset the root-frame margins
        applyMarkdownBlockFormat();
        m_editor->document()->clearUndoRedoStacks();
        QTextCursor caret(m_editor->document()->findBlockByNumber(2));
        caret.setPosition(caret.block().position() + 45);
        m_editor->setTextCursor(caret);
        setChromeVisible(false);
        updateStats();
        QApplication::processEvents();
        centerCaret();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/markdown-mode.png"), "PNG");

        setChromeVisible(true);
        updateStats();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/markdown-chrome.png"), "PNG");
        setChromeVisible(false);

        m_themeId = QStringLiteral("dark");
        applyTheme();
        m_focusMode = true;
        m_focusSentence = true;
        updateFocusHighlight();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/markdown-focus-dark.png"), "PNG");

        m_themeId = QStringLiteral("paper");
        m_focusMode = false;
        m_focusSentence = false;
        updateFocusHighlight();
        setMarkdownMode(false);
        m_editor->clear();
        applyDocumentDefaults();
        applyTheme();
        resize(1024, 1400);
        setChromeVisible(true);
        QApplication::processEvents();
    }

    // Markdown preview and the library sidebar (phase 2). The library is a demo
    // folder in a temporary directory; only .md / .txt files and folders show.
    {
        QTemporaryDir demoDir;
        const QString lib = demoDir.path() + QStringLiteral("/Writing");
        QDir().mkpath(lib + QStringLiteral("/Chapters"));
        QDir().mkpath(lib + QStringLiteral("/Research"));
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(bytes);
            }
        };
        const QByteArray draft =
            "# The Quiet Draft\n"
            "\n"
            "Write first, *polish* later. The preview shows the page as a reader will see it, "
            "while the editor keeps every **marker** in plain sight.\n"
            "\n"
            "## Notes for chapter two\n"
            "\n"
            "- Keep the harbor scene short\n"
            "- [x] Move the letter to page one\n"
            "- Check the `draft-2.md` outline\n"
            "\n"
            "> The best way out is always through.\n"
            "\n"
            "See [the style notes](https://example.com/style) before the next pass.\n"
            "\n"
            "```text\n"
            "Fenced code keeps its own colour.\n"
            "```\n";
        write(lib + QStringLiteral("/The Quiet Draft.md"), draft);
        write(lib + QStringLiteral("/Notes on light.md"), "# Notes on light\n");
        write(lib + QStringLiteral("/todo.txt"), "call the printer\n");
        write(lib + QStringLiteral("/letter.odt"), "not listed");
        write(lib + QStringLiteral("/cover.png"), "not listed");
        write(lib + QStringLiteral("/Chapters/01 Harbor.md"), "# Harbor\n");
        write(lib + QStringLiteral("/Chapters/02 The Letter.md"), "# The Letter\n");
        write(lib + QStringLiteral("/Chapters/outline.docx"), "not listed");
        write(lib + QStringLiteral("/Research/sources.txt"), "sources\n");

        m_focusMode = false;
        updateFocusHighlight();
        hideFindBar();
        resize(1280, 780);
        statusBar()->clearMessage();
        m_themeId = QStringLiteral("paper");
        openPath(lib + QStringLiteral("/The Quiet Draft.md"));
        statusBar()->clearMessage();
        m_hideAwayPinned = false;
        setChromeVisible(false);
        applyTheme();

        // a) Editor + preview.
        m_previewWanted = true; // not saved: capture leaves the settings alone
        syncViewActions();
        m_splitter->setSizes({0, 0, 720, 560});
        m_preview->renderNow();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/markdown-preview.png"), "PNG");

        // b) The same in the Dark room theme.
        m_themeId = QStringLiteral("dark");
        applyTheme();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/markdown-preview-dark.png"), "PNG");
        m_themeId = QStringLiteral("paper");
        applyTheme();

        // c) Library sidebar + editor (preview off), Chapters expanded.
        m_previewWanted = false;
        m_library->setRootPath(lib);
        m_library->show();
        syncViewActions();
        m_splitter->setSizes({250, 0, 1030, 0});
        QElapsedTimer loading;
        loading.start();
        const QModelIndex chapters = m_library->model()->index(lib + QStringLiteral("/Chapters"));
        while (loading.elapsed() < 3000
               && (m_library->model()->rowCount(m_library->view()->rootIndex()) < 5
                   || m_library->model()->rowCount(chapters) < 2)) {
            m_library->model()->fetchMore(chapters);
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        m_library->view()->expand(chapters);
        m_library->setCurrentFile(m_currentPath);
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/library-sidebar.png"), "PNG");

        // d) Everything: library, editor, preview.
        m_previewWanted = true;
        syncViewActions();
        m_splitter->setSizes({250, 0, 560, 470});
        m_preview->renderNow();
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/library-and-preview.png"), "PNG");

        // e) Book sidebar (1.4.0): a demo book with book.json, chapters in book
        //    order (not name order), details per chapter, one missing file.
        {
            // A library holding two books (one book.json per subfolder) and a
            // plain folder that could become one.
            const QString shelf = demoDir.path() + QStringLiteral("/Shelf");
            const QString bookDir = shelf + QStringLiteral("/The Harbor");
            QDir().mkpath(shelf + QStringLiteral("/Short Pieces"));
            write(shelf + QStringLiteral("/Short Pieces/fog.md"), "# Fog\n\nA short piece.\n");
            QDir().mkpath(shelf + QStringLiteral("/Salt Roads/parts"));
            write(shelf + QStringLiteral("/Salt Roads/parts/01 Inland.md"), "# Inland\n");
            write(shelf + QStringLiteral("/Salt Roads/parts/02 The Pass.md"), "# The Pass\n");
            {
                BookManifest other = BookManifest::fromFolder(shelf + QStringLiteral("/Salt Roads"));
                other.save();
            }
            QDir().mkpath(bookDir + QStringLiteral("/chapters"));
            const QByteArray prose =
                "The ferry came in low and late, its lamps smeared by the rain. Mara counted the "
                "passengers as they stepped onto the quay, the way her father had taught her, and "
                "lost count at the eleventh because the eleventh was carrying her mother's coat.\n\n"
                "She did not call out. The harbor had a way of keeping words for itself.\n\n";
            write(bookDir + QStringLiteral("/chapters/harbor.md"), "# Harbor\n\n" + prose);
            write(bookDir + QStringLiteral("/chapters/the-letter.md"),
                  "# The Letter\n\n" + prose + prose);
            write(bookDir + QStringLiteral("/chapters/low-tide.md"), "# Low Tide\n\n" + prose);
            {
                // An .odt chapter (1.4.1) next to the Markdown ones.
                QTextDocument odt;
                odt.setPlainText(QString::fromUtf8("The Lighthouse\n\n" + prose + prose + prose));
                DocumentIo::save(&odt, bookDir + QStringLiteral("/chapters/the-lighthouse.odt"),
                                 DocumentIo::Format::Odt);
            }
            write(bookDir + QStringLiteral("/notes.txt"), "keep the coat\n");
            BookManifest demo;
            demo.setBookDir(bookDir);
            demo.setTitle(QStringLiteral("The Harbor"));
            const struct {
                const char *path, *title, *status, *pov, *synopsis;
                int target;
            } rows[] = {
                {"chapters/harbor.md", "Harbor", "Revised", "Mara",
                 "Mara meets the late ferry and sees her mother's coat.", 2500},
                {"chapters/the-letter.md", "The Letter", "Draft", "Mara",
                 "The coat holds a letter addressed to no one.", 3000},
                {"chapters/the-lighthouse.odt", "The Lighthouse", "Draft", "Ivo", "", 3000},
                {"chapters/low-tide.md", "Low Tide", "Outline", "", "", 0},
                {"chapters/epilogue.md", "Epilogue", "Idea", "", "", 0}, // not written yet
            };
            for (const auto &r : rows) {
                BookChapter c;
                c.path = QString::fromUtf8(r.path);
                c.title = QString::fromUtf8(r.title);
                c.status = QString::fromUtf8(r.status);
                c.pov = QString::fromUtf8(r.pov);
                c.synopsis = QString::fromUtf8(r.synopsis);
                c.wordTarget = r.target;
                if (c.title == QLatin1String("Harbor")) {
                    c.tags = {QStringLiteral("opening"), QStringLiteral("sea")};
                    c.notes = QStringLiteral("Trim the second paragraph.");
                }
                demo.setChapter(demo.add(c.path), c);
            }
            demo.save();

            m_previewWanted = false;
            m_library->hide();
            openPath(bookDir + QStringLiteral("/chapters/the-letter.md"));
            statusBar()->clearMessage();
            m_book->setRootPath(shelf);
            m_book->selectBook(QStringLiteral("The Harbor"));
            m_book->show();
            m_book->setCurrentFile(m_currentPath);
            syncViewActions();
            m_splitter->setSizes({0, 270, 1010, 0});
            QApplication::processEvents();
            grab().save(dir + QStringLiteral("/book-sidebar.png"), "PNG");

            // Right-click menu on chapter 2, drawn over the window grab (a popup
            // is its own window, so grab() alone would not show it).
            {
                QMenu *menu = m_book->buildContextMenu(1);
                menu->ensurePolished();
                menu->adjustSize();
                const QRect itemRect = m_book->view()->visualItemRect(m_book->view()->item(1));
                const QPoint at = m_book->view()->viewport()->mapTo(
                    this, itemRect.center() + QPoint(-40, 0));
                menu->show();
                QApplication::processEvents();
                QPixmap shot = grab();
                {
                    QPainter p(&shot);
                    p.drawPixmap(at, menu->grab());
                }
                menu->hide();
                delete menu;
                shot.save(dir + QStringLiteral("/book-context-menu.png"), "PNG");
            }

            // The book picker open: one entry per book subfolder.
            {
                QComboBox *picker = m_book->picker();
                picker->showPopup();
                QApplication::processEvents();
                QWidget *popup = picker->view()->window();
                const QPoint at = mapFromGlobal(popup->mapToGlobal(QPoint(0, 0)));
                QPixmap shot = grab();
                {
                    QPainter p(&shot);
                    p.drawPixmap(at, popup->grab());
                }
                picker->hidePopup();
                shot.save(dir + QStringLiteral("/book-picker.png"), "PNG");
            }

            // New Book…: pick a subfolder (or type a new name).
            m_book->chooseNewBook();
            if (QInputDialog *nb = m_book->newBookDialog()) {
                QApplication::processEvents();
                nb->grab().save(dir + QStringLiteral("/book-new.png"), "PNG");
                nb->reject();
            }

            // Edit Details… on chapter 1.
            m_book->editChapter(0);
            if (BookChapterDialog *dlg = m_book->chapterDialog()) {
                QApplication::processEvents();
                dlg->grab().save(dir + QStringLiteral("/book-chapter-details.png"), "PNG");
                dlg->reject();
            }

            // Dark room theme.
            m_themeId = QStringLiteral("dark");
            applyTheme();
            QApplication::processEvents();
            grab().save(dir + QStringLiteral("/book-sidebar-dark.png"), "PNG");
            m_themeId = QStringLiteral("paper");
            applyTheme();
            // 1.4.1: an .odt chapter open from the book (word count from content.xml).
            openPath(bookDir + QStringLiteral("/chapters/the-lighthouse.odt"));
            statusBar()->clearMessage();
            m_book->setCurrentFile(m_currentPath);
            QApplication::processEvents();
            grab().save(dir + QStringLiteral("/book-odt-chapter.png"), "PNG");
            m_book->hide();
            m_book->setRootPath(QString());
        }

        m_previewWanted = false;
        m_library->hide();
        m_library->setRootPath(QString());
        setMarkdownMode(false);
        m_editor->clear();
        setCurrentFile(QString(), DocumentIo::Format::Odt);
        applyDocumentDefaults();
        applyTheme();
        syncViewActions();
        resize(1024, 1400);
        setChromeVisible(true);
        QApplication::processEvents();
    }

    // 4) About dialog grab.
    QMessageBox about(this);
    about.setWindowTitle(QStringLiteral("About zwriter"));
    about.setTextFormat(Qt::RichText);
    about.setText(
        QStringLiteral(
            "<h3>zwriter %1</h3>"
            "<p>Distraction-free writing for Linux amd64 and Apple Silicon.</p>"
            "<p>Sibling to zedit — not a fork. Kinship to FocusWriter.</p>"
            "<p>MIT License — Copyright © 2026 Stephen B. Johnson</p>")
            .arg(QString::fromUtf8(zwriter::kVersionString)));
    about.setStandardButtons(QMessageBox::Ok);
    about.show();
    QApplication::processEvents();
    about.grab().save(dir + QStringLiteral("/about.png"), "PNG");
    about.close();

    // 5) Save Document dialog (non-native under xvfb so grab works).
    {
        QFileDialog dlg(this, QStringLiteral("Save Document"));
        dlg.setOption(QFileDialog::DontUseNativeDialog, true);
        dlg.setAcceptMode(QFileDialog::AcceptSave);
        dlg.setFileMode(QFileDialog::AnyFile);
        dlg.setNameFilters(DocumentIo::saveFilter().split(QStringLiteral(";;")));
        dlg.selectNameFilter(QStringLiteral("OpenDocument Text (*.odt)"));
        dlg.setDefaultSuffix(QStringLiteral("odt"));
        dlg.setDirectory(documentsStartDir());
        dlg.selectFile(dlg.directory().filePath(defaultBaseName() + QStringLiteral(".odt")));
        dlg.resize(780, 520);
        dlg.show();
        QApplication::processEvents();
        dlg.grab().save(dir + QStringLiteral("/save-document.png"), "PNG");
        dlg.close();
    }

    // Deferred exit so dialog teardown cannot keep the event loop alive under xvfb.
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    QTimer::singleShot(250, qApp, &QCoreApplication::quit);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) {
        saveSettings();
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_findBar && m_findBar->isVisible()) {
            hideFindBar();
            event->accept();
            return;
        }
        toggleChrome();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Ctrl+scroll is View zoom. Never let it reach QTextEdit, whose own
    // Ctrl+wheel zoomIn() changes the document's default font (and so exports).
    if (event->type() == QEvent::Wheel
        && (watched == (m_editor ? m_editor->viewport() : nullptr)
            || watched == (m_pageScroll ? m_pageScroll->viewport() : nullptr))) {
        auto *we = static_cast<QWheelEvent *>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            const int dy = we->angleDelta().y();
            if (dy != 0) {
                setViewZoomPercent(dy > 0 ? ViewZoom::stepUp(shownZoomPercent())
                                          : ViewZoom::stepDown(shownZoomPercent()));
            }
            return true;
        }
    }
    if (watched == m_desk && event->type() == QEvent::Resize && m_fullPageView && !m_centering) {
        // Defer so we don't re-enter layout mid-resize.
        QTimer::singleShot(0, this, [this]() {
            if (m_fullPageView) {
                updateFullPageGeometry();
            }
        });
    }

    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        // Tab types a tab; Ctrl+Tab / Ctrl+Shift+Tab leave the editor (to the
        // chapter list when the Book sidebar is shown). Physical Ctrl is
        // MetaModifier on macOS, where Cmd+Tab belongs to the system.
        if ((ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab)
            && (ke->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
            && !(ke->modifiers() & Qt::AltModifier)) {
            swapEditorAndChapters(); // same as the shortcut, if the editor saw the key first
            return true;
        }
        if (m_fullPageView && m_pageScroll
            && (ke->key() == Qt::Key_PageDown || ke->key() == Qt::Key_PageUp)
            && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            // The editor is as tall as the whole document, so its own Page keys
            // would jump to the end. Move by what is actually visible instead.
            const int step = qMax(40, m_pageScroll->viewport()->height() - 60);
            const QRect cr = m_editor->cursorRect();
            // cursorRect()/cursorForPosition() are in unzoomed document units.
            const int docStep = qRound(step / m_editor->zoom());
            const int dy = ke->key() == Qt::Key_PageDown ? docStep : -docStep;
            const QTextCursor target = m_editor->cursorForPosition(
                QPoint(cr.center().x(), cr.center().y() + dy));
            QTextCursor tc = m_editor->textCursor();
            tc.setPosition(target.position(),
                           (ke->modifiers() & Qt::ShiftModifier) ? QTextCursor::KeepAnchor
                                                                 : QTextCursor::MoveAnchor);
            m_editor->setTextCursor(tc);
            return true;
        }
        // The editor takes Ctrl+Z / Ctrl+Shift+Z itself (before the Edit menu
        // actions); route them through the same caret-keeping undo.
        if (ke->matches(QKeySequence::Undo) || ke->matches(QKeySequence::Redo)) {
            Alignment::undoRedoKeepingCaret(m_editor, ke->matches(QKeySequence::Redo));
            return true;
        }
        // Typed operators (1.13.0): >= -> ≥ etc. Not in Markdown (code-ish text).
        if (!m_markdownMode && m_mathReplaceAction && m_mathReplaceAction->isChecked()
            && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
            && ke->text().size() == 1
            && (ke->text().at(0) == QLatin1Char('=') || ke->text().at(0) == QLatin1Char('>'))
            && Symbols::typeOperator(m_editor, ke->text().at(0))) {
            return true;
        }
        // \name autocomplete (1.13.0): \alpha + Space -> "α ", + Tab -> "α".
        // Not in Markdown, where \alpha may be LaTeX math meant for pandoc.
        if (!m_markdownMode && (ke->key() == Qt::Key_Space || ke->key() == Qt::Key_Tab)
            && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
            && m_symbolAutocompleteAction && m_symbolAutocompleteAction->isChecked()
            && Symbols::expandBeforeCursor(m_editor, ke->key() == Qt::Key_Space ? QStringLiteral(" ") : QString())) {
            return true;
        }
        const bool soundsOn = m_keySounds && m_keySounds->isEnabled();
        const QPair<int, int> lineBefore = soundsOn ? caretVisualLine(m_editor) : QPair<int, int>();
        if (trySmartTypography(ke)) {
            if (soundsOn) {
                m_keySounds->playKey();
                if (caretVisualLine(m_editor) == qMakePair(lineBefore.first, lineBefore.second + 1)) {
                    m_keySounds->playReturn();
                }
            }
            return true;
        }
        // Manual page breaks: Qt copies a block's format (including its page
        // break) to the paragraph created by Enter, and cannot delete across a
        // table. Fix those cases up; everything else is Qt's own behaviour.
        const Qt::KeyboardModifiers chord =
            ke->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
#ifndef Q_OS_MACOS
        // Chrome hidden away: Alt+letter must open the menu, not type a letter.
        // (Not on macOS, where Option+letter types accented characters.)
        if (!m_chromeVisible && menuBar() && (ke->modifiers() & Qt::AltModifier)
            && !(ke->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
            && ke->key() >= Qt::Key_A && ke->key() <= Qt::Key_Z) {
            revealHideAway();
            const QKeySequence wanted(QKeyCombination(Qt::AltModifier, Qt::Key(ke->key())));
            for (QAction *a : menuBar()->actions()) {
                if (QKeySequence::mnemonic(a->text()) == wanted) {
                    menuBar()->setActiveAction(a);
                    break;
                }
            }
            return true;
        }
#endif
        // Markdown is plain text: Shift+Enter starts a new line like Enter
        // instead of inserting a hidden line separator.
        if (m_markdownMode && (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && chord == Qt::ShiftModifier) {
            QTextCursor c = m_editor->textCursor();
            c.insertBlock();
            m_editor->setTextCursor(c);
            if (m_keySounds && m_keySounds->isEnabled()) {
                m_keySounds->playReturn();
            }
            return true;
        }
        // Tab / Shift+Tab move between table cells (Tab in the last cell adds a row).
        if (((ke->key() == Qt::Key_Tab && !chord)
             || (ke->key() == Qt::Key_Backtab && (chord & ~Qt::ShiftModifier) == Qt::NoModifier))
            && m_editor->textCursor().currentTable()) {
            return moveToAdjacentCell(ke->key() == Qt::Key_Tab);
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !chord) {
            QTextCursor c = m_editor->textCursor();
            if (!c.hasSelection() && !c.currentTable()
                && (c.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore)) {
                if (m_keySounds && m_keySounds->isEnabled()) {
                    m_keySounds->playReturn();
                }
                handleEnterOnPageBreak(ke);
                return true;
            }
            if (Alignment::keepAlignmentOnEnter(m_editor)) {
                if (m_keySounds && m_keySounds->isEnabled()) {
                    m_keySounds->playReturn();
                }
                return true;
            }
        }
        if (ke->key() == Qt::Key_Backspace && !chord) {
            QTextCursor c = m_editor->textCursor();
            if (!c.hasSelection() && c.atBlockStart() && c.currentList()) {
                // Backspace at the start of a list item ends the list there.
                // Qt keeps an indent in the bullet's place, which is saved as
                // a left margin; drop it instead.
                QTextList *list = c.currentList();
                const int before = qMax(0, list->format().indent() - 1);
                c.beginEditBlock();
                list->remove(c.block());
                QTextBlockFormat bf = c.blockFormat();
                bf.setIndent(before);
                bf.setObjectIndex(-1);
                c.setBlockFormat(bf);
                c.endEditBlock();
                syncFormatActions();
                return true;
            }
            const QTextBlock prev = c.block().previous();
            if (!c.hasSelection() && c.atBlockStart() && !c.currentList() && !c.currentTable()
                && c.blockFormat().indent() == 0
                && (c.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore)
                && (!prev.isValid() || QTextCursor(prev).currentTable())) {
                // Nothing to merge with (document start, or a table right
                // before): Backspace just removes the break.
                QTextBlockFormat bf = c.blockFormat();
                bf.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
                c.setBlockFormat(bf);
                return true;
            }
        }
        // Mechanical typewriter clicks: printable typing + Return only.
        // Skip pure navigation, modifiers alone, and chorded shortcuts.
        if (m_keySounds && m_keySounds->isEnabled()) {
            const int key = ke->key();
            const Qt::KeyboardModifiers mods = ke->modifiers()
                & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
            if (mods == Qt::NoModifier) {
                if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                    m_keySounds->playReturn();
                } else if (key == Qt::Key_Space || key == Qt::Key_Backspace
                           || key == Qt::Key_Delete) {
                    m_keySounds->playSpace();
                } else if (!ke->text().isEmpty() && ke->text().at(0).isPrint()) {
                    m_keySounds->playKey();
                    // The editor inserts the character after this filter returns;
                    // if that soft-wraps the caret onto the next line, ding.
                    QTimer::singleShot(0, this, [this, lineBefore]() {
                        if (m_keySounds && m_keySounds->isEnabled()
                            && caretVisualLine(m_editor)
                                   == qMakePair(lineBefore.first, lineBefore.second + 1)) {
                            m_keySounds->playReturn();
                        }
                    });
                }
            }
        }
    }

    if (watched == m_editor->viewport() && event->type() == QEvent::Paint
        && (m_pageGuides || m_fullPageView)) {
        watched->removeEventFilter(this);
        QCoreApplication::sendEvent(watched, event);
        watched->installEventFilter(this);
        paintPageOverlays(static_cast<QPaintEvent *>(event)->rect()); // breaks, header/footer, guides
        return true;
    }

    if (watched == m_editor->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            m_mouseActive = true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_mouseActive = false;
        }
    }

    if (event->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        considerMouseHideAway(me->globalPosition().toPoint());
    } else if (event->type() == QEvent::Leave
               && (watched == m_formatBar || watched == m_editor->viewport()
                   || watched == menuBar())) {
        if (!m_hideAwayPinned) {
            scheduleHideAway();
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::applyNamedStyle(const QString &name)
{
    ParagraphStyles::apply(m_editor->textCursor(), name);
    syncStyleControls();
    m_editor->setFocus();
}

void MainWindow::syncStyleControls()
{
    const QTextBlockFormat bf = m_editor->textCursor().blockFormat();
    const QString named = ParagraphStyles::styleOf(bf);
    const int level = bf.headingLevel();
    int index = qBound(0, level, 3);
    for (int i = 0; i < m_namedStyleActions.size(); ++i) {
        QAction *a = m_namedStyleActions.at(i);
        const QSignalBlocker b(a);
        const bool on = a->property("zwStyle").toString() == named;
        if (on) {
            index = 4 + i;
            for (QAction *h : {m_paragraphAction, m_h1Action, m_h2Action, m_h3Action}) {
                if (h) {
                    const QSignalBlocker hb(h);
                    h->setChecked(false);
                }
            }
        }
        a->setChecked(on);
    }
    if (m_styleCombo) {
        const QSignalBlocker b(m_styleCombo);
        m_styleCombo->setCurrentIndex(index);
    }
    if (m_styleLabel) {
        m_styleLabel->setText(m_markdownMode || !m_styleCombo ? QString() : m_styleCombo->itemText(index));
    }
}

void MainWindow::adoptStyleFonts(const StyleFonts::Map &map)
{
    m_styleFonts = map;
    StyleFonts::setActive(map); // Format > Paragraph Style's named styles
}

void MainWindow::setStyleFonts(const StyleFonts::Map &map, bool saveAsDefault)
{
    if (saveAsDefault) {
        QSettings s;
        StyleFonts::saveAppDefaults(s, map);
    }
    const StyleFonts::Map old = m_styleFonts;
    bool changed = false;
    for (const QString &k : StyleFonts::keys()) {
        changed = changed || StyleFonts::entry(old, k) != StyleFonts::entry(map, k);
    }
    if (!changed) {
        return;
    }
    adoptStyleFonts(map);
    if (!m_markdownMode && m_editor) {
        // Text already in each style follows it (one undo step); the
        // document's default font is Body Text's, for new typing.
        StyleFonts::restyle(m_editor->document(), old, map, DocumentIo::defaultFontFamilies(),
                            DocumentIo::headingFontFamilies());
        const QFont body = richDocumentFont();
        m_editor->document()->setDefaultFont(body);
        if (m_editor->textCursor().blockFormat().headingLevel() == 0
            && ParagraphStyles::styleOf(m_editor->textCursor().blockFormat()).isEmpty()) {
            QTextCharFormat cf = m_editor->currentCharFormat();
            cf.setFontFamilies(body.families());
            cf.setFontPointSize(body.pointSizeF());
            m_editor->setCurrentCharFormat(cf);
        }
    }
    // The map is saved in the file, so a change is an edit even when no text
    // was in a changed style.
    m_metaDirty = true;
    markDirty();
    syncFormatActions();
    if (m_editor) {
        m_editor->viewport()->update();
    }
}

void MainWindow::showStylesDialog()
{
    auto *dlg = new StylesDialog(m_styleFonts, DocumentIo::defaultFontFamilies(), DocumentIo::headingFontFamilies(),
                                 this);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->setWindowModality(Qt::WindowModal);
    connect(dlg, &StylesDialog::applied, this,
            [this](const StyleFonts::Map &map, bool asDefault) { setStyleFonts(map, asDefault); });
    dlg->show();
}

void MainWindow::showParagraphDialog()
{
    if (m_markdownMode) {
        return;
    }
    if (m_paragraphDialog) {
        m_paragraphDialog->raise();
        return;
    }
    auto *dlg = new ParagraphDialog(m_editor->textCursor().blockFormat(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->setWindowModality(Qt::WindowModal);
    connect(dlg, &QDialog::accepted, this, [this, dlg]() {
        ParagraphStyles::setGeometry(m_editor->textCursor(), dlg->format());
        m_editor->setFocus();
    });
    m_paragraphDialog = dlg;
    dlg->open();
}
