#include "MainWindow.hpp"
#include "AlignmentActions.hpp"
#include "DocumentIo.hpp"
#include "DocumentMeta.hpp"
#include "FindReplaceBar.hpp"
#include "HeaderFooterDialog.hpp"
#include "SpellChecker.hpp"
#include "SpellHighlighter.hpp"
#include "InsertTableDialog.hpp"
#include "PageWidgets.hpp"
#include "PrintLayout.hpp"
#include "PropertiesDialog.hpp"
#include "TypewriterSounds.hpp"
#include "UpdateChecker.hpp"
#include "version.hpp"

#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontInfo>
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
#include <QComboBox>
#include <QClipboard>
#include <QMimeData>
#include <QTextDocumentFragment>
#include <QKeyEvent>
#include <QKeySequence>
#include <QToolButton>
#include <QLabel>
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
#include <QSignalBlocker>
#include <QSpinBox>
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
#include <QTimer>
#include <QToolBar>
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

namespace {
// Reading-time assumption: average adult silent reading ~225 WPM.
constexpr int kReadingWpm = 225;
constexpr int kMaxRecentFiles = 8;
// Typewriter body density on A4 (~pica / 10 CPI). User can enlarge via the picker.
constexpr int kDefaultBodyPointSize = 12;

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

    m_printer = new QPrinter(QPrinter::HighResolution);
    // Shipping default: A4 (210×297 mm). Overridden by QSettings when present.
    m_printer->setPageSize(QPageSize(QPageSize::A4));
    m_printer->setPageMargins(QMarginsF(25.4, 25.4, 25.4, 25.4), QPageLayout::Millimeter);
    loadPrinterSettings();

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

    layout->addWidget(m_desk, 1);
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
    m_updateChecker = new UpdateChecker(this);
    m_spellChecker = new SpellChecker(this);
    m_spellChecker->setEnabled(m_spellCheck);
    m_spellHighlighter = new SpellHighlighter(m_editor->document(), m_spellChecker);
    m_editor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_editor, &QWidget::customContextMenuRequested,
            this, &MainWindow::showEditorContextMenu);
    connect(m_updateChecker, &UpdateChecker::updateAvailable, this,
            &MainWindow::onUpdateAvailable);
    connect(m_updateChecker, &UpdateChecker::upToDate, this,
            &MainWindow::onUpToDate);
    connect(m_updateChecker, &UpdateChecker::checkFailed, this,
            &MainWindow::onUpdateCheckFailed);

    m_pageLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_pageLabel);
    m_statsLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statsLabel);
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
    buildViewMenu();
    buildHelpMenu();
    buildFormatToolbar();

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
    connect(m_editor, &QTextEdit::textChanged, this, &MainWindow::markDirty);
    // Key sounds fire from the editor keyPress eventFilter (typing + Return),
    // not textChanged — avoids clicks on paste/programmatic edits/nav side-effects.
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, &MainWindow::onCursorMoved);
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
    m_themeId = s.value(QStringLiteral("theme/id"), QStringLiteral("paper")).toString();
    if (m_themeId != QLatin1String("dark") && m_themeId != QLatin1String("inverse")) {
        m_themeId = QStringLiteral("paper");
    }
    m_recentFiles = s.value(QStringLiteral("files/recent")).toStringList();
    m_lastDocDir = s.value(QStringLiteral("files/lastDir")).toString();
}

void MainWindow::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("view/typewriterScroll"), m_typewriterScroll);
    s.setValue(QStringLiteral("view/focusMode"), m_focusMode);
    s.setValue(QStringLiteral("view/focusSentence"), m_focusSentence);
    s.setValue(QStringLiteral("view/smartQuotes"), m_smartQuotes);
    s.setValue(QStringLiteral("view/chromePinned"), m_hideAwayPinned);
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    if (m_keySounds) {
        s.setValue(QStringLiteral("view/keySounds"), m_keySounds->isEnabled());
    }
    s.setValue(QStringLiteral("view/spellCheck"), m_spellCheck);
    s.setValue(QStringLiteral("view/fullPageView"), m_fullPageView);
    s.setValue(QStringLiteral("view/fitPageWidth"), m_fitPageWidth);
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
    // First-run / missing key → A4 (210 × 297 mm).
    const int idVal = s.value(QStringLiteral("print/pageSizeId"),
                              static_cast<int>(QPageSize::A4)).toInt();
    QPageSize pageSize;
    if (idVal == static_cast<int>(QPageSize::Custom)) {
        const qreal wMm = s.value(QStringLiteral("print/pageWidthMm"), 210.0).toDouble();
        const qreal hMm = s.value(QStringLiteral("print/pageHeightMm"), 297.0).toDouble();
        pageSize = QPageSize(QSizeF(wMm, hMm), QPageSize::Millimeter,
                             QStringLiteral("Custom"), QPageSize::ExactMatch);
    } else {
        pageSize = QPageSize(static_cast<QPageSize::PageSizeId>(idVal));
    }
    if (!pageSize.isValid()) {
        pageSize = QPageSize(QPageSize::A4);
    }
    m_printer->setPageSize(pageSize);

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
    const QPageSize ps = layout.pageSize();
    s.setValue(QStringLiteral("print/pageSizeId"), static_cast<int>(ps.id()));
    if (ps.id() == QPageSize::Custom) {
        const QSizeF mm = ps.size(QPageSize::Millimeter);
        s.setValue(QStringLiteral("print/pageWidthMm"), mm.width());
        s.setValue(QStringLiteral("print/pageHeightMm"), mm.height());
    }
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

    auto *exportAct = m_fileMenu->addAction(QStringLiteral("&Export as PDF…"));
    exportAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(exportAct, &QAction::triggered, this, &MainWindow::exportPdf);

    auto *printAct = m_fileMenu->addAction(QStringLiteral("&Print…"));
    printAct->setShortcut(QKeySequence::Print);
    connect(printAct, &QAction::triggered, this, &MainWindow::filePrint);

    auto *previewAct = m_fileMenu->addAction(QStringLiteral("Print Pre&view…"));
    connect(previewAct, &QAction::triggered, this, &MainWindow::filePrintPreview);

    auto *pageAct = m_fileMenu->addAction(QStringLiteral("Page Set&up…"));
    connect(pageAct, &QAction::triggered, this, &MainWindow::filePageSetup);

    m_fileMenu->addSeparator();

    auto *propsAct = m_fileMenu->addAction(QStringLiteral("Propert&ies…"));
    connect(propsAct, &QAction::triggered, this, &MainWindow::fileProperties);

    m_fileMenu->addSeparator();

    auto *quitAct = m_fileMenu->addAction(QStringLiteral("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    for (QAction *a : {newAct, openAct, saveAct, saveAsAct, exportAct, printAct, propsAct,
                       quitAct}) {
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
    connect(styleGroup, &QActionGroup::triggered, this, [this](QAction *) { applyHeading(); });

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
}

void MainWindow::buildFormatMenu()
{
    m_formatMenu = menuBar()->addMenu(QStringLiteral("F&ormat"));

    m_formatMenu->addAction(m_boldAction);
    m_formatMenu->addAction(m_italicAction);
    m_formatMenu->addAction(m_underlineAction);
    m_formatMenu->addSeparator();

    auto *styleMenu = m_formatMenu->addMenu(QStringLiteral("Paragraph &Style"));
    for (QAction *a : {m_paragraphAction, m_h1Action, m_h2Action, m_h3Action}) {
        styleMenu->addAction(a);
    }
    auto *alignMenu = m_formatMenu->addMenu(QStringLiteral("&Align"));
    for (QAction *a : {m_alignLeftAction, m_alignCenterAction, m_alignRightAction,
                       m_alignJustifyAction}) {
        alignMenu->addAction(a);
    }
    m_formatMenu->addAction(m_bulletListAction);
    m_formatMenu->addAction(m_numberListAction);
    m_formatMenu->addSeparator();

    auto *tableAct = m_formatMenu->addAction(QStringLiteral("Insert Tabl&e…"));
    tableAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    tableAct->setToolTip(QStringLiteral("Insert a table (Ctrl+Shift+I)"));
    connect(tableAct, &QAction::triggered, this, &MainWindow::insertTable);
    auto *tableMenu = m_formatMenu->addMenu(QStringLiteral("&Table"));
    tableMenu->addAction(m_tableInsertRowAction);
    tableMenu->addAction(m_tableInsertColAction);
    tableMenu->addSeparator();
    tableMenu->addAction(m_tableRemoveRowAction);
    tableMenu->addAction(m_tableRemoveColAction);

    auto *pageBreakAct = m_formatMenu->addAction(QStringLiteral("Insert Page Brea&k"));
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
    hfAct->setToolTip(QStringLiteral("Edit page header and footer (page numbers via {page} / {pages})"));
    connect(hfAct, &QAction::triggered, this, &MainWindow::editHeaderFooter);
    m_formatMenu->addSeparator();

    m_formatMenu->addAction(m_clearFormatAction);

    // Shortcuts must work with the menu closed and while a toolbar widget has focus.
    for (QAction *a : {m_boldAction, m_italicAction, m_underlineAction,
                       m_paragraphAction, m_h1Action, m_h2Action, m_h3Action,
                       m_alignLeftAction, m_alignCenterAction, m_alignRightAction,
                       m_alignJustifyAction, m_bulletListAction, m_numberListAction,
                       m_clearFormatAction, tableAct, hfAct}) {
        addAction(a);
    }
    updateTableActions();
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
    m_fitPageWidthAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
    m_fitPageWidthAction->setToolTip(QStringLiteral(
        "Zoom the page to fill the window width (Ctrl+Shift+W); lines still break as they print"));
    connect(m_fitPageWidthAction, &QAction::triggered, this, &MainWindow::toggleFitPageWidth);

    m_pageGuidesAction = m_viewMenu->addAction(QStringLiteral("Page &Guides"));
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

    m_alwaysShowChromeAction = m_viewMenu->addAction(QStringLiteral("Always Show Tool&bar"));
    m_alwaysShowChromeAction->setCheckable(true);
    m_alwaysShowChromeAction->setToolTip(
        QStringLiteral("Keep the toolbar, menus and status bar visible (Esc); off = hide-away"));
    connect(m_alwaysShowChromeAction, &QAction::triggered, this, &MainWindow::setChromePinned);

    auto *fullscreenAct = m_viewMenu->addAction(QStringLiteral("Full Sc&reen"));
    fullscreenAct->setShortcut(QKeySequence(Qt::Key_F11));
    connect(fullscreenAct, &QAction::triggered, this, &MainWindow::toggleFullscreen);

    for (QAction *a : {m_typewriterScrollAction, m_focusModeAction, m_fullPageViewAction,
                       m_fitPageWidthAction, m_pageGuidesAction, m_keySoundsAction, fullscreenAct}) {
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
    const bool inTable = currentTable() != nullptr;
    for (QAction *a : {m_tableInsertRowAction, m_tableInsertColAction,
                       m_tableRemoveRowAction, m_tableRemoveColAction}) {
        if (a) {
            a->setEnabled(inTable);
        }
    }
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
    c.beginEditBlock();
    table->insertColumns(col + 1, 1);
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
    table->removeColumns(col, 1);
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
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu) {
        return;
    }
    m_recentMenu->clear();

    // Drop stale paths.
    QStringList kept;
    for (const QString &path : m_recentFiles) {
        if (QFile::exists(path)) {
            kept.append(path);
        }
    }
    if (kept.size() != m_recentFiles.size()) {
        m_recentFiles = kept;
    }

    if (m_recentFiles.isEmpty()) {
        auto *empty = m_recentMenu->addAction(QStringLiteral("(no recent files)"));
        empty->setEnabled(false);
        return;
    }

    int i = 0;
    for (const QString &path : m_recentFiles) {
        auto *act = m_recentMenu->addAction(QFileInfo(path).fileName());
        act->setData(path);
        act->setToolTip(path);
        if (i < 9) {
            act->setShortcut(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + i)));
        }
        connect(act, &QAction::triggered, this, &MainWindow::openRecentFile);
        ++i;
    }
    m_recentMenu->addSeparator();
    auto *clearAct = m_recentMenu->addAction(QStringLiteral("Clear Recent"));
    connect(clearAct, &QAction::triggered, this, &MainWindow::clearRecentFiles);
}

void MainWindow::addToRecentFiles(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    const QString abs = QFileInfo(path).absoluteFilePath();
    m_recentFiles.removeAll(abs);
    m_recentFiles.prepend(abs);
    while (m_recentFiles.size() > kMaxRecentFiles) {
        m_recentFiles.removeLast();
    }
    rebuildRecentMenu();
    saveSettings();
}

void MainWindow::openRecentFile()
{
    auto *act = qobject_cast<QAction *>(sender());
    if (!act) {
        return;
    }
    const QString path = act->data().toString();
    if (path.isEmpty()) {
        return;
    }
    if (!QFile::exists(path)) {
        m_recentFiles.removeAll(path);
        rebuildRecentMenu();
        saveSettings();
        QMessageBox::warning(this, QStringLiteral("Open failed"),
                             QStringLiteral("File no longer exists:\n%1").arg(path));
        return;
    }
    if (!maybeSave()) {
        return;
    }
    openPath(path);
}

void MainWindow::clearRecentFiles()
{
    m_recentFiles.clear();
    rebuildRecentMenu();
    saveSettings();
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
    }
    if (m_fitPageWidthAction) {
        const QSignalBlocker b(m_fitPageWidthAction);
        m_fitPageWidthAction->setChecked(m_fitPageWidth);
        m_fitPageWidthAction->setEnabled(m_fullPageView); // the continuous view already fills
    }
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

    m_formatBar->addSeparator();

    m_styleCombo = new QComboBox(m_formatBar);
    m_styleCombo->setObjectName(QStringLiteral("styleCombo"));
    m_styleCombo->setToolTip(QStringLiteral("Paragraph style"));
    m_styleCombo->addItems({QStringLiteral("Body Text"), QStringLiteral("Heading 1"),
                            QStringLiteral("Heading 2"), QStringLiteral("Heading 3")});
    m_styleCombo->setFixedWidth(118);
    m_formatBar->addWidget(m_styleCombo);
    connect(m_styleCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        QAction *targets[] = {m_paragraphAction, m_h1Action, m_h2Action, m_h3Action};
        if (index >= 0 && index < 4) {
            targets[index]->trigger();
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
    // Shipping default: Courier, 12 pt (falls back through Courier-class monospace
    // faces; user can switch via toolbar).
    QFont font;
    font.setFamilies(DocumentIo::defaultFontFamilies());
    font.setPointSize(kDefaultBodyPointSize);
    font.setStyleHint(QFont::TypeWriter);
    font.setFixedPitch(true);
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
    fmt.setFont(font);
    // No explicit foreground: text follows the theme's colour (so switching
    // Paper <-> Dark recolours existing text) and exports/prints as plain black.
    m_editor->setCurrentCharFormat(fmt);
    if (m_fontCombo) {
        const QSignalBlocker b(m_fontCombo);
        m_fontCombo->setCurrentFont(font);
    }
    if (m_fontSizeSpin) {
        const QSignalBlocker b(m_fontSizeSpin);
        m_fontSizeSpin->setValue(kDefaultBodyPointSize);
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
    setStyleSheet(Theme::styleSheet(c, m_fullPageView, kDefaultBodyPointSize));

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

    // Typed text carries no explicit colour, so it follows the theme.
    if (m_editor) {
        QTextCharFormat cur = m_editor->currentCharFormat();
        cur.clearForeground();
        m_editor->setCurrentCharFormat(cur);
        m_editor->document()->setModified(wasModified);
    }
}

void MainWindow::setChromeVisible(bool visible)
{
    m_chromeVisible = visible;
    statusBar()->setVisible(visible);
    if (m_formatBar) {
        m_formatBar->setVisible(visible);
    }
    if (menuBar()) {
        menuBar()->setVisible(visible);
    }
}

void MainWindow::revealHideAway()
{
    m_hideTimer->stop();
    setChromeVisible(true);
}

void MainWindow::scheduleHideAway()
{
    if (m_hideAwayPinned) {
        return;
    }
    m_hideTimer->start();
}

void MainWindow::hideAwayIdle()
{
    if (m_hideAwayPinned) {
        return;
    }
    // Never pull the bar away from under an open menu (e.g. one opened with
    // Alt+letter while the chrome was hidden); try again later.
    if (QApplication::activePopupWidget() || (menuBar() && menuBar()->activeAction())) {
        m_hideTimer->start();
        return;
    }
    setChromeVisible(false);
}

void MainWindow::considerMouseHideAway(const QPoint &globalPos)
{
    const QPoint local = mapFromGlobal(globalPos);
    const int edgePx = 10;
    const bool nearTop = local.y() >= 0 && local.y() < edgePx;
    const int bottomEdge = height() - edgePx;
    const bool nearBottom = local.y() >= bottomEdge && local.y() <= height();
    const bool overBar = m_formatBar && m_formatBar->isVisible()
        && m_formatBar->geometry().contains(local);
    const bool overMenu = menuBar() && menuBar()->isVisible()
        && menuBar()->geometry().contains(local);
    const bool overStatus = statusBar()->isVisible()
        && statusBar()->geometry().contains(local);

    if (nearTop || nearBottom || overBar || overMenu || overStatus) {
        revealHideAway();
    } else if (!m_hideAwayPinned && m_chromeVisible) {
        scheduleHideAway();
    }
}

void MainWindow::updateStats()
{
    const QString text = m_editor->toPlainText();
    const int chars = text.length();

    int words = 0;
    bool inWord = false;
    for (const QChar c : text) {
        if (c.isSpace()) {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            ++words;
        }
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
    setWindowTitle(QStringLiteral("%1%2 — zwriter")
                       .arg(m_dirty ? QStringLiteral("*") : QString())
                       .arg(name));
}

void MainWindow::setCurrentFile(const QString &path, DocumentIo::Format format)
{
    m_currentPath = path;
    m_currentFormat = format == DocumentIo::Format::Unknown ? DocumentIo::Format::Odt : format;
    m_dirty = false;
    updateWindowTitle();
    if (!path.isEmpty()) {
        addToRecentFiles(path);
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

void MainWindow::toggleChrome()
{
    setChromePinned(!m_hideAwayPinned);
}

void MainWindow::setChromePinned(bool pinned)
{
    m_hideAwayPinned = pinned;
    m_hideTimer->stop();
    setChromeVisible(pinned);
    syncViewActions();
    saveSettings();
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
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
    if (m_spellHighlighter) {
        m_spellHighlighter->refreshAll();
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
                    if (m_spellHighlighter) {
                        m_spellHighlighter->refreshAll();
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
                if (m_spellHighlighter) {
                    m_spellHighlighter->refreshAll();
                }
            });
            toInsert.append(ignoreAct);
            QAction *addAct = new QAction(QStringLiteral("Add \"%1\" to dictionary").arg(word), menu);
            QObject::connect(addAct, &QAction::triggered, this, [this, word]() {
                if (m_spellChecker) {
                    m_spellChecker->addToUserDictionary(word);
                }
                if (m_spellHighlighter) {
                    m_spellHighlighter->refreshAll();
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

    menu->exec(m_editor->viewport()->mapToGlobal(pos));
    delete menu;
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
    const int mid = m_editor->viewport()->height() / 2;
    const int delta = cr.center().y() - mid;
    vs->setValue(vs->value() + delta);
    m_centering = false;
}

QPair<int, int> MainWindow::focusRange() const
{
    QTextCursor cursor = m_editor->textCursor();
    if (m_focusSentence) {
        QTextCursor start = cursor;
        QTextCursor end = cursor;
        // Walk back to previous sentence end (. ! ? or block start).
        QTextDocument *doc = m_editor->document();
        int pos = cursor.position();
        int s = pos;
        while (s > 0) {
            const QChar prev = doc->characterAt(s - 1);
            if (prev == QLatin1Char('.') || prev == QLatin1Char('!')
                || prev == QLatin1Char('?') || prev == QChar::ParagraphSeparator
                || prev == QChar::LineSeparator) {
                break;
            }
            --s;
        }
        // Skip leading whitespace after the break.
        while (s < doc->characterCount() - 1 && doc->characterAt(s).isSpace()
               && doc->characterAt(s) != QChar::ParagraphSeparator) {
            ++s;
        }
        int e = pos;
        const int last = doc->characterCount() - 1;
        while (e < last) {
            const QChar c = doc->characterAt(e);
            if (c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?')) {
                ++e;
                break;
            }
            if (c == QChar::ParagraphSeparator || c == QChar::LineSeparator) {
                break;
            }
            ++e;
        }
        return {s, e};
    }

    QTextBlock block = cursor.block();
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
    dimFmt.setForeground(isPaperTheme() ? QColor(QStringLiteral("#b0b0b0"))
                                        : QColor(QStringLiteral("#5a5a5a")));

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
        QTextBlockFormat plain;
        plain.setObjectIndex(-1);
        cursor.mergeBlockFormat(plain);
    }
    cursor.endEditBlock();
    syncFormatActions();
    m_editor->setFocus();
}

void MainWindow::fileNew()
{
    if (!maybeSave()) {
        return;
    }
    m_editor->clear();
    m_meta = DocumentMeta{};
    m_meta.ensureDefaults();
    forgetPageNumberState();
    applyDocumentDefaults();
    applyFullPageView(); // clear() resets the page margins/size
    m_editor->document()->clearUndoRedoStacks();
    m_editor->document()->setModified(false);
    setCurrentFile(QString(), DocumentIo::Format::Odt); // clean: no '*'
    syncViewActions();
    updateStats();
    syncFormatActions();
    updateFocusHighlight();
    m_editor->setFocus();
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

    QTextCharFormat charFmt;
    if (level == 0) {
        charFmt.setFontPointSize(kDefaultBodyPointSize);
        charFmt.setFontWeight(QFont::Normal);
    } else if (level == 1) {
        charFmt.setFontPointSize(22);
        charFmt.setFontWeight(QFont::Bold);
    } else if (level == 2) {
        charFmt.setFontPointSize(18);
        charFmt.setFontWeight(QFont::Bold);
    } else {
        charFmt.setFontPointSize(14);
        charFmt.setFontWeight(QFont::DemiBold);
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
        blockFmt.setHeadingLevel(level);
        walk.setBlockFormat(blockFmt);
        walk.mergeBlockCharFormat(charFmt);
        QTextCursor text(walk.block());
        text.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        text.mergeCharFormat(charFmt);
        if (walk.block().next().isValid() && walk.block().next().position() <= last) {
            walk.setPosition(walk.block().next().position());
        } else {
            break;
        }
    }
    cursor.endEditBlock();

    // Keep typing in the new style.
    m_editor->mergeCurrentCharFormat(charFmt);
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
    if (m_styleCombo) {
        const QSignalBlocker b(m_styleCombo);
        m_styleCombo->setCurrentIndex(qBound(0, level, 3));
    }
}

bool MainWindow::maybeSave()
{
    if (!m_dirty) {
        return true;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setWindowTitle(QStringLiteral("zwriter"));
    box.setText(QStringLiteral("Save changes before continuing?"));
    box.setInformativeText(QStringLiteral("Your changes will be lost if you don’t save them."));
    QPushButton *save = box.addButton(QStringLiteral("&Save"), QMessageBox::AcceptRole);
    QPushButton *discard = box.addButton(QStringLiteral("Do&n’t Save"), QMessageBox::DestructiveRole);
    QPushButton *cancel = box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(save);
    box.setEscapeButton(cancel);
    // QMessageBox gives every button one fixed width that ignores its text, which
    // clips longer labels with wider fonts. Size them from their own text instead
    // (one shared width keeps the row tidy).
    int buttonWidth = 84;
    for (QPushButton *b : {save, discard, cancel}) {
        const int textWidth = b->fontMetrics().horizontalAdvance(
            QString(b->text()).remove(QLatin1Char('&')));
        buttonWidth = qMax(buttonWidth, textWidth + 2 * 16 + 16); // padding + slack
    }
    for (QPushButton *b : {save, discard, cancel}) {
        b->setMinimumWidth(buttonWidth);
    }
    box.exec();
    if (box.clickedButton() == discard) {
        return true;
    }
    if (box.clickedButton() != save) {
        return false;
    }
    fileSave();
    return !m_dirty;
}

bool MainWindow::saveToPath(const QString &path, DocumentIo::Format format)
{
    m_meta.ensureDefaults();
    m_meta.touchEdited();

    QString error;
    if (!DocumentIo::save(m_editor->document(), path, format, &error)) {
        QMessageBox::warning(this, QStringLiteral("Save failed"), error);
        return false;
    }

    if (format == DocumentIo::Format::Odt) {
        QString metaErr;
        OdtPageStyle page;
        if (m_printer) {
            const QPageLayout layout = m_printer->pageLayout();
            page.sheetMm = PrintLayout::sheetSizeMm(layout);
            page.marginsMm = layout.margins(QPageLayout::Millimeter);
            page.landscape = layout.orientation() == QPageLayout::Landscape;
        }
        const QFont body = m_editor->document()->defaultFont();
        if (!body.family().isEmpty()) {
            page.fontFamily = body.family();
        }
        const qreal bodyPt = body.pointSizeF() > 0 ? body.pointSizeF() : qreal(kDefaultBodyPointSize);
        page.fontPointSize = qMax(8.0, bodyPt - 2.0); // as paintHeaderFooter()
        if (!OdtMeta::writeToOdt(path, m_meta, &metaErr, page)) {
            statusBar()->showMessage(
                metaErr.isEmpty() ? QStringLiteral("Saved (metadata patch skipped)")
                                  : metaErr,
                4000);
        }
    }

    setCurrentFile(path, format);
    return true;
}

bool MainWindow::openPath(const QString &path)
{
    QString error;
    if (!DocumentIo::load(m_editor->document(), path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Open failed"), error);
        return false;
    }
    auto fmt = DocumentIo::formatFromPath(path);
    if (fmt == DocumentIo::Format::Unknown) {
        // e.g. notes.md, app.conf: it was read as plain text, so it must be saved
        // back as plain text — never silently rewritten as an ODT zip.
        fmt = DocumentIo::Format::Txt;
    }
    m_meta = DocumentMeta{};
    m_meta.ensureDefaults();
    if (fmt == DocumentIo::Format::Odt) {
        OdtMeta::readFromOdt(path, &m_meta);
        m_meta.ensureDefaults();
    }
    forgetPageNumberState();
    {
        // Everything below is part of opening, not an edit.
        const QScopedValueRollback guard(m_suppressDirty, true);
        // Keep shipping default for new typing; loaded spans keep their own faces.
        m_editor->document()->setDefaultFont(defaultDocumentFont());
        // Loading replaces the whole document, which resets its page margins/size:
        // put the page layout back.
        applyFullPageView();
    }
    // Show the top of the document, caret at the start (loading leaves it at the end).
    m_editor->moveCursor(QTextCursor::Start);
    m_editor->verticalScrollBar()->setValue(0);
    if (m_pageScroll) {
        m_pageScroll->verticalScrollBar()->setValue(0);
    }
    m_editor->document()->clearUndoRedoStacks(); // Ctrl+Z right after open: nothing
    m_editor->document()->setModified(false);
    setCurrentFile(path, fmt); // clean: no '*', no save prompt
    syncViewActions();
    updateStats();
    syncFormatActions();
    updateFocusHighlight();
    m_editor->document()->setModified(false);
    return true;
}


void MainWindow::openExternalFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        QMessageBox::warning(this, QStringLiteral("Open failed"),
                             QStringLiteral("Could not open “%1”: it is not a readable file.")
                                 .arg(info.absoluteFilePath()));
        return;
    }
    if (!maybeSave()) {
        return;
    }
    if (openPath(info.absoluteFilePath())) {
        raise();
        activateWindow();
    }
}

QString MainWindow::documentsStartDir() const
{
    if (!m_lastDocDir.isEmpty() && QDir(m_lastDocDir).exists()) {
        return m_lastDocDir;
    }
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!docs.isEmpty() && QDir(docs).exists()) {
        return docs;
    }
    return QDir::homePath();
}

void MainWindow::rememberDocDir(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_lastDocDir = QFileInfo(path).absolutePath();
    saveSettings();
}

void MainWindow::setupNativeFileDialog(QFileDialog &dlg) const
{
    // Native OS panel so Create Directory / New Folder stays available.
    // DontUseNativeDialog must stay OFF — non-native Qt dialogs are only used
    // for xvfb screenshot capture, never for normal Save/Open/Export.
    dlg.setOption(QFileDialog::DontUseNativeDialog, false);
    // Prefer OS overwrite confirm (do NOT set DontConfirmOverwrite).
    dlg.setOption(QFileDialog::DontConfirmOverwrite, false);
    // Must NOT set ShowDirsOnly (would hide files and break Save/Open).
    // Must NOT set ReadOnly (would strip New Folder on some platforms).
    dlg.setOption(QFileDialog::ShowDirsOnly, false);
    dlg.setOption(QFileDialog::ReadOnly, false);
    dlg.setViewMode(QFileDialog::Detail);
    dlg.setOption(QFileDialog::HideNameFilterDetails, false);

    QList<QUrl> sidebar;
    const QString home = QDir::homePath();
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString desk = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!home.isEmpty()) {
        sidebar << QUrl::fromLocalFile(home);
    }
    if (!docs.isEmpty()) {
        sidebar << QUrl::fromLocalFile(docs);
    }
    if (!desk.isEmpty()) {
        sidebar << QUrl::fromLocalFile(desk);
    }
    if (!m_lastDocDir.isEmpty() && QDir(m_lastDocDir).exists()) {
        sidebar << QUrl::fromLocalFile(m_lastDocDir);
    }
    dlg.setSidebarUrls(sidebar);
}

QString MainWindow::suffixForFilter(const QString &filter)
{
    const DocumentIo::Format fmt = DocumentIo::formatFromFilter(filter, QString());
    if (fmt != DocumentIo::Format::Unknown) {
        return DocumentIo::formatName(fmt);
    }
    if (filter.contains(QLatin1String("*.pdf"), Qt::CaseInsensitive)) {
        return QStringLiteral("pdf");
    }
    return QStringLiteral("odt");
}

void MainWindow::syncSaveNameToFilter(QFileDialog &dlg, const QString &filter)
{
    const QString suffix = suffixForFilter(filter);
    dlg.setDefaultSuffix(suffix);

    QStringList selected = dlg.selectedFiles();
    if (selected.isEmpty()) {
        return;
    }
    QFileInfo info(selected.constFirst());
    QString base = info.completeBaseName();
    if (base.isEmpty()) {
        base = info.fileName(); // bare name with no dot yet
    }
    if (base.isEmpty()) {
        return;
    }
    // Keep directory; rewrite extension to match the active filter.
    const QString dir = dlg.directory().absolutePath();
    dlg.selectFile(dir + QLatin1Char('/') + base + QLatin1Char('.') + suffix);
}

QString MainWindow::runSaveDocumentDialog(DocumentIo::Format *outFormat)
{
    QFileDialog dlg(this, QStringLiteral("Save Document"));
    setupNativeFileDialog(dlg);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setNameFilters(DocumentIo::saveFilter().split(QStringLiteral(";;")));
    dlg.setDirectory(documentsStartDir());

    // New documents start as ODT (the native format); an existing file keeps
    // its own format. The type last used for some other file doesn't carry over.
    QString filter = QStringLiteral("OpenDocument Text (*.odt)");
    if (!m_currentPath.isEmpty()) {
        const DocumentIo::Format cur = m_currentFormat;
        if (cur == DocumentIo::Format::Txt) {
            filter = QStringLiteral("Plain Text (*.txt)");
        } else if (cur == DocumentIo::Format::Rtf) {
            filter = QStringLiteral("Rich Text Format (*.rtf)");
        } else {
            filter = QStringLiteral("OpenDocument Text (*.odt)");
        }
    }
    dlg.selectNameFilter(filter);
    dlg.setDefaultSuffix(suffixForFilter(filter));

    const QString suggestedName = defaultBaseName() + QLatin1Char('.') + suffixForFilter(filter);
    dlg.selectFile(dlg.directory().filePath(suggestedName));

    QObject::connect(&dlg, &QFileDialog::filterSelected, &dlg, [&dlg](const QString &f) {
        MainWindow::syncSaveNameToFilter(dlg, f);
    });

    if (dlg.exec() != QDialog::Accepted) {
        return {};
    }
    QStringList files = dlg.selectedFiles();
    if (files.isEmpty()) {
        return {};
    }
    QString path = files.constFirst();
    const QString selectedFilter = dlg.selectedNameFilter();
    // A typed .odt / .txt / .rtf extension decides the format (and wins over
    // the file-type box); otherwise the chosen type's extension is appended,
    // so there is never a bare filename.
    DocumentIo::Format format = DocumentIo::formatFromPath(path);
    if (format == DocumentIo::Format::Unknown) {
        format = DocumentIo::formatFromFilter(selectedFilter, QString());
        path += QLatin1Char('.') + DocumentIo::formatName(format);
    }

    rememberDocDir(path);
    if (outFormat) {
        *outFormat = format;
    }
    return path;
}

QString MainWindow::runOpenDocumentDialog()
{
    QFileDialog dlg(this, QStringLiteral("Open Document"));
    setupNativeFileDialog(dlg);
    dlg.setAcceptMode(QFileDialog::AcceptOpen);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilters(DocumentIo::openFilter().split(QStringLiteral(";;")));
    dlg.selectNameFilter(QStringLiteral("OpenDocument Text (*.odt)"));
    dlg.setDirectory(documentsStartDir());

    if (dlg.exec() != QDialog::Accepted) {
        return {};
    }
    const QStringList files = dlg.selectedFiles();
    if (files.isEmpty()) {
        return {};
    }
    rememberDocDir(files.constFirst());
    return files.constFirst();
}

QString MainWindow::runExportPdfDialog()
{
    QFileDialog dlg(this, QStringLiteral("Export PDF"));
    setupNativeFileDialog(dlg);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setNameFilters({QStringLiteral("PDF (*.pdf)")});
    dlg.selectNameFilter(QStringLiteral("PDF (*.pdf)"));
    dlg.setDefaultSuffix(QStringLiteral("pdf"));
    dlg.setDirectory(documentsStartDir());
    dlg.selectFile(dlg.directory().filePath(defaultBaseName() + QStringLiteral(".pdf")));

    if (dlg.exec() != QDialog::Accepted) {
        return {};
    }
    QStringList files = dlg.selectedFiles();
    if (files.isEmpty()) {
        return {};
    }
    QString path = files.constFirst();
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".pdf");
    }
    rememberDocDir(path);
    return path;
}


void MainWindow::fileOpen()
{
    if (!maybeSave()) {
        return;
    }
    const QString path = runOpenDocumentDialog();
    if (path.isEmpty()) {
        return;
    }
    openPath(path);
}

void MainWindow::fileSave()
{
    if (m_currentPath.isEmpty()) {
        fileSaveAs();
        return;
    }
    saveToPath(m_currentPath, m_currentFormat);
}

void MainWindow::fileSaveAs()
{
    DocumentIo::Format format = DocumentIo::Format::Odt;
    const QString path = runSaveDocumentDialog(&format);
    if (path.isEmpty()) {
        return;
    }
    saveToPath(path, format);
}

void MainWindow::exportPdf()
{
    const QString path = runExportPdfDialog();
    if (path.isEmpty()) {
        return;
    }

    QPrinter pdf(QPrinter::HighResolution);
    pdf.setOutputFormat(QPrinter::PdfFormat);
    pdf.setOutputFileName(path);
    pdf.setPageSize(m_printer->pageLayout().pageSize());
    pdf.setPageOrientation(m_printer->pageLayout().orientation());
    pdf.setPageMargins(m_printer->pageLayout().margins(QPageLayout::Millimeter),
                       QPageLayout::Millimeter);

    doPrint(&pdf);
    statusBar()->showMessage(QStringLiteral("Exported PDF: %1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::doPrint(QPrinter *printer)
{
    if (!printer || !m_editor || !m_editor->document()) {
        return;
    }

    // Paint the full physical sheet so margin bands match Full Page (header/footer).
    const bool savedFullPage = printer->fullPage();
    printer->setFullPage(true);

    const QPageLayout layout = printer->pageLayout();
    PrintLayout::Page page;
    page.sheetMm = PrintLayout::sheetSizeMm(layout);
    page.marginsMm = layout.margins(QPageLayout::Millimeter);

    QPainter painter(printer);
    painter.setRenderHint(QPainter::Antialiasing, false);
    PrintLayout::paintDocument(
        *m_editor->document(), &painter, page, printer->logicalDpiX(), printer->logicalDpiY(),
        [printer]() { return printer->newPage(); },
        [this, &layout](QPainter *p, const QRectF &sheet, int pageNo, int pages) {
            paintHeaderFooter(p, sheet, pageNo, pages, &layout);
        });
    painter.end();

    printer->setFullPage(savedFullPage);
}

void MainWindow::filePrint()
{
    QPrintDialog dlg(m_printer, this);
    dlg.setWindowTitle(QStringLiteral("Print"));
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    savePrinterSettings();
    if (m_fullPageView) {
        updateFullPageGeometry();
    }
    doPrint(m_printer);
}

void MainWindow::filePageSetup()
{
    QPageSetupDialog dlg(m_printer, this);
    if (dlg.exec() == QDialog::Accepted) {
        savePrinterSettings();
        applyFullPageView(); // new sheet size / orientation / margins, either view
        m_editor->viewport()->update();
    }
}

void MainWindow::filePrintPreview()
{
    // The preview fixes its page origin when it is created, so full-page mode
    // (origin at the sheet corner, as doPrint paints) must be on beforehand;
    // otherwise everything is shifted by the margins.
    const bool savedFullPage = m_printer->fullPage();
    m_printer->setFullPage(true);
    QPrintPreviewDialog dlg(m_printer, this);
    dlg.setWindowTitle(QStringLiteral("Print Preview"));
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, &MainWindow::printPreview);
    dlg.exec();
    m_printer->setFullPage(savedFullPage);
}

void MainWindow::printPreview(QPrinter *printer)
{
    doPrint(printer);
}

void MainWindow::toggleFullPageView()
{
    m_fullPageView = m_fullPageViewAction && m_fullPageViewAction->isChecked();
    applyTheme();
    applyFullPageView();
    saveSettings();
    syncViewActions();
    if (m_pageGuides) {
        m_editor->viewport()->update();
    }
}

void MainWindow::toggleFitPageWidth()
{
    m_fitPageWidth = m_fitPageWidthAction && m_fitPageWidthAction->isChecked();
    updateFullPageGeometry();
    saveSettings();
    syncViewActions();
    centerCaret();
}

qreal MainWindow::fitPageZoom() const
{
    if (!m_fitPageWidth || !m_fullPageView || !m_pageScroll) {
        return 1.0;
    }
    // Leave room for the vertical scroll bar whether or not it is showing, so
    // the zoom does not flip when the page grows tall enough to need it, and
    // for PageCanvas's 28 px paper-mode side margins (the shadow lives there).
    const int bar = m_pageScroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr,
                                                       m_pageScroll->verticalScrollBar());
    const int avail = m_pageScroll->width() - bar - 2 * 28 - 4;
    const qreal pageW = printerPageSizePx().width();
    if (avail <= 0 || pageW <= 0) {
        return 1.0;
    }
    return qBound(0.25, avail / pageW, 8.0);
}

QSizeF MainWindow::printerPageSizePx() const
{
    // True physical page in device-independent pixels (mm → px via logical DPI).
    // Do NOT use a fitted on-screen frame size here — that classic Qt trap makes
    // point-size fonts look huge relative to the page.
    if (!m_printer) {
        return QSizeF(794, 1123); // A4 @ 96 DPI fallback (210×297 mm)
    }
    const QSizeF sizeMm = PrintLayout::sheetSizeMm(m_printer->pageLayout());
    const qreal dpiX = m_editor ? m_editor->logicalDpiX() : 96.0;
    const qreal dpiY = m_editor ? m_editor->logicalDpiY() : 96.0;
    return QSizeF(sizeMm.width() * dpiX / 25.4, sizeMm.height() * dpiY / 25.4);
}

QMarginsF MainWindow::pageMarginsPx() const
{
    if (!m_printer) {
        return QMarginsF(96, 96, 96, 96);
    }
    // Same px-per-mm as the (rounded) sheet in Full Page view, so both views
    // compute identical margins and switching views never touches them.
    const QSizeF native = printerPageSizePx();
    const QSizeF pageMm = PrintLayout::sheetSizeMm(m_printer->pageLayout());
    const QMarginsF marginsMm = m_printer->pageLayout().margins(QPageLayout::Millimeter);
    const qreal sx = pageMm.width() > 0 ? qMax(1, qRound(native.width())) / pageMm.width() : 1.0;
    const qreal sy = pageMm.height() > 0 ? qMax(1, qRound(native.height())) / pageMm.height() : 1.0;
    return QMarginsF(marginsMm.left() * sx, marginsMm.top() * sy, marginsMm.right() * sx,
                     marginsMm.bottom() * sy);
}

void MainWindow::applyDocumentPageMetrics(const QSize &pagePx)
{
    if (!m_editor || !m_printer) {
        return;
    }
    QTextDocument *doc = m_editor->document();
    if (doc->pageSize() != QSizeF(pagePx)) {
        doc->setPageSize(QSizeF(pagePx));
    }
    m_editor->setFixedPageSize(QSizeF(pagePx)); // QTextEdit would reset it to unpaginated
    m_editor->setContinuousInset(false, QMarginsF());
    const QMarginsF m = pageMarginsPx();
    setRootFrameMargins(m.left(), m.top(), m.right(), m.bottom(), 0);
}

void MainWindow::setRootFrameMargins(qreal left, qreal top, qreal right, qreal bottom,
                                     qreal documentMargin)
{
    // A root-frame format change is a document edit to Qt (undo entry,
    // modified flag, textChanged). Only touch it when something differs, and
    // never let a page-layout change mark the document dirty.
    QTextDocument *doc = m_editor->document();
    QTextFrameFormat fmt = doc->rootFrame()->frameFormat();
    const bool same = qFuzzyCompare(1.0 + fmt.leftMargin(), 1.0 + left)
        && qFuzzyCompare(1.0 + fmt.rightMargin(), 1.0 + right)
        && qFuzzyCompare(1.0 + fmt.topMargin(), 1.0 + top)
        && qFuzzyCompare(1.0 + fmt.bottomMargin(), 1.0 + bottom);
    if (!same) {
        const bool wasModified = doc->isModified();
        const QScopedValueRollback guard(m_suppressDirty, true);
        fmt.setLeftMargin(left);
        fmt.setRightMargin(right);
        fmt.setTopMargin(top);
        fmt.setBottomMargin(bottom);
        doc->rootFrame()->setFrameFormat(fmt);
        doc->setModified(wasModified);
    }
    if (!qFuzzyCompare(1.0 + doc->documentMargin(), 1.0 + documentMargin)) {
        doc->setDocumentMargin(documentMargin);
    }
}

void MainWindow::clearDocumentPageMetrics()
{
    if (!m_editor) {
        return;
    }
    m_editor->setFixedPageSize(QSizeF());
    QTextDocument *doc = m_editor->document();
    doc->setPageSize(QSizeF(0, 0)); // continuous layout
    // The root-frame margins stay the page margins in both views: changing them
    // is an undoable document edit to Qt, so a view toggle would cost an undo
    // step (and Ctrl+Z would revert the layout, not the text). The continuous
    // view gets its reading column from the editor's viewport inset instead.
    const QMarginsF m = pageMarginsPx();
    setRootFrameMargins(m.left(), m.top(), m.right(), m.bottom(), 0);
    m_editor->setContinuousInset(true, m);
    refreshPageCount();
}

void MainWindow::updateFullPageGeometry()
{
    if (!m_fullPageView || !m_desk || !m_pageFrame || !m_editor) {
        return;
    }

    // Always size the paper + QTextDocument to the true physical page in DIPs.
    // Small windows scroll (m_pageScroll); we never shrink pageSize under the font DPI.
    const QSizeF native = printerPageSizePx();
    const int pageW = qMax(1, qRound(native.width()));
    const int pageH = qMax(1, qRound(native.height()));

    applyDocumentPageMetrics(QSize(pageW, pageH));
    // Fit Page to Width only scales the view: the document stays true size.
    m_editor->setZoom(fitPageZoom());
    syncPageFrameHeight();
    m_editor->viewport()->update();
}

void MainWindow::syncPageFrameHeight()
{
    if (!m_fullPageView || !m_pageFrame || !m_editor) {
        return;
    }
    // The paper is exactly N true-size pages tall; the outer scroll area pans
    // it, so the text never scrolls "inside" a page.
    const QSizeF native = printerPageSizePx();
    const int pageW = qMax(1, qRound(native.width()));
    const int pageH = qMax(1, qRound(native.height()));
    refreshPageCount();
    const int pages = m_pageCount;
    const qreal zoom = m_editor->zoom();
    const QSize want(qRound(pageW * zoom), qRound(qreal(pageH) * pages * zoom));
    if (m_pageFrame->size() != want) {
        m_pageFrame->setFixedSize(want);
        // The caret may have just moved onto a new page: bring it into view.
        QTimer::singleShot(0, this, [this]() { centerCaret(); });
    }
    m_editor->viewport()->update();
}

void MainWindow::applyFullPageView()
{
    if (!m_desk || !m_pageFrame || !m_editor) {
        return;
    }
    // Page size / margins are layout, not content: never an unsaved change.
    const QScopedValueRollback guard(m_suppressDirty, true);
    const bool wasModified = m_editor->document()->isModified();
    struct RestoreModified {
        QTextDocument *doc;
        bool modified;
        ~RestoreModified() { doc->setModified(modified); }
    } restoreModified{m_editor->document(), wasModified};

    if (m_fullPageView) {
        m_pageCanvas->setPaperMode(true);
        m_pageScroll->setWidgetResizable(true);
        m_pageFrame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        // The paper is N true-size pages tall and the outer scroll area pans it,
        // so the editor itself never scrolls.
        m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        updateFullPageGeometry();
    } else {
        m_pageCanvas->setPaperMode(false);
        m_pageScroll->setWidgetResizable(true);
        m_pageFrame->setMinimumSize(0, 0);
        m_pageFrame->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        m_pageFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_editor->setZoom(1.0);
        clearDocumentPageMetrics();
        m_editor->viewport()->update();
    }
    updatePageLabel();
}

void MainWindow::togglePageGuides()
{
    m_pageGuides = m_pageGuidesAction && m_pageGuidesAction->isChecked();
    m_editor->viewport()->update();
}

QString MainWindow::expandHeaderFooterTokens(const QString &pattern, int pageNumber,
                                             int pageCount) const
{
    QString out = pattern;
    out.replace(QStringLiteral("{page}"), QString::number(pageNumber));
    out.replace(QStringLiteral("{pages}"), QString::number(pageCount));
    return out;
}

int MainWindow::documentPageCount() const
{
    // Cached: QTextDocument::pageCount() forces the whole document to be laid
    // out, so it is only asked when the document size changes.
    return m_pageCount;
}

void MainWindow::refreshPageCount()
{
    int n = 1;
    if (m_editor && m_editor->document() && m_fullPageView
        && m_editor->document()->pageSize().height() > 1.0) {
        n = qMax(1, m_editor->document()->pageCount());
    }
    m_pageCount = n;
}

int MainWindow::visiblePageNumber() const
{
    if (!m_editor || !m_editor->document()) {
        return 1;
    }
    const qreal pageH = m_editor->document()->pageSize().height();
    if (pageH <= 1.0) {
        return 1;
    }
    // The editor never scrolls internally, so viewport y == document y.
    const qreal y = m_editor->cursorRect().center().y();
    return qBound(1, int(y / pageH) + 1, documentPageCount());
}

void MainWindow::updatePageLabel()
{
    if (!m_pageLabel) {
        return;
    }
    if (!m_fullPageView) {
        m_pageLabel->hide();
        return;
    }
    m_pageLabel->setText(QStringLiteral("Page %1 of %2")
                             .arg(visiblePageNumber())
                             .arg(documentPageCount()));
    m_pageLabel->show();
}

void MainWindow::paintHeaderFooter(QPainter *painter, const QRectF &pageRect,
                                   int pageNumber, int pageCount,
                                   const QPageLayout *printLayout) const
{
    if (!painter || !m_meta.hasHeaderFooter()) {
        return;
    }

    QFont font = m_editor ? m_editor->document()->defaultFont() : QFont();
    const qreal bodyPt = font.pointSizeF() > 0 ? font.pointSizeF() : qreal(kDefaultBodyPointSize);
    font.setPointSizeF(qMax(8.0, bodyPt - 2.0));
    painter->setFont(font);
    // On screen the ink follows the theme; on paper/PDF it is always dark,
    // since a dark theme must not print near-invisible grey on white stock.
    const bool toScreen = m_editor
        && painter->device() == static_cast<const QPaintDevice *>(m_editor->viewport());
    if (!toScreen || isPaperTheme()) {
        painter->setPen(QColor(QStringLiteral("#1a1a1a")));
    } else if (m_themeId == QLatin1String("inverse")) {
        painter->setPen(QColor(QStringLiteral("#ffffff")));
    } else {
        painter->setPen(QColor(QStringLiteral("#d4d4d4")));
    }

    qreal leftM = pageRect.width() * 0.12;
    qreal rightM = leftM;
    qreal topM = pageRect.height() * 0.085;
    qreal bottomM = topM;

    if (m_editor && m_editor->document() && m_fullPageView
        && painter->device() == static_cast<const QPaintDevice *>(m_editor->viewport())) {
        const QTextFrameFormat fmt = m_editor->document()->rootFrame()->frameFormat();
        leftM = fmt.leftMargin();
        rightM = fmt.rightMargin();
        topM = fmt.topMargin();
        bottomM = fmt.bottomMargin();
    } else if (printLayout || m_printer) {
        const QPageLayout layout = printLayout ? *printLayout : m_printer->pageLayout();
        const QMarginsF mm = layout.margins(QPageLayout::Millimeter);
        const QSizeF paperMm = PrintLayout::sheetSizeMm(layout);
        if (paperMm.width() > 0 && paperMm.height() > 0) {
            const qreal sx = pageRect.width() / paperMm.width();
            const qreal sy = pageRect.height() / paperMm.height();
            leftM = mm.left() * sx;
            rightM = mm.right() * sx;
            topM = mm.top() * sy;
            bottomM = mm.bottom() * sy;
        }
    }

    const QRectF headerBand(pageRect.left() + leftM,
                            pageRect.top(),
                            qMax(0.0, pageRect.width() - leftM - rightM),
                            topM);
    const QRectF footerBand(pageRect.left() + leftM,
                            pageRect.bottom() - bottomM,
                            qMax(0.0, pageRect.width() - leftM - rightM),
                            bottomM);

    auto drawBand = [&](const QRectF &band, const QString &left, const QString &center,
                        const QString &right) {
        const QString l = expandHeaderFooterTokens(left, pageNumber, pageCount);
        const QString c = expandHeaderFooterTokens(center, pageNumber, pageCount);
        const QString r = expandHeaderFooterTokens(right, pageNumber, pageCount);
        if (!l.isEmpty()) {
            painter->drawText(band, Qt::AlignLeft | Qt::AlignVCenter, l);
        }
        if (!c.isEmpty()) {
            painter->drawText(band, Qt::AlignHCenter | Qt::AlignVCenter, c);
        }
        if (!r.isEmpty()) {
            painter->drawText(band, Qt::AlignRight | Qt::AlignVCenter, r);
        }
    };

    drawBand(headerBand, m_meta.headerLeft, m_meta.headerCenter, m_meta.headerRight);
    drawBand(footerBand, m_meta.footerLeft, m_meta.footerCenter, m_meta.footerRight);
}

void MainWindow::paintPageOverlays(const QRect &clip)
{
    QWidget *vp = m_editor->viewport();
    QPainter painter(vp);
    // Overlays are drawn in document units, like the text under them.
    const qreal zoom = m_editor->zoom();
    painter.scale(zoom, zoom);
    const QRectF docClip(QPointF(clip.topLeft()) / zoom, QSizeF(clip.size()) / zoom);
    painter.setClipRect(docClip);
    const bool paged = m_fullPageView && m_editor->document()->pageSize().height() > 1.0;
    const int pages = paged ? documentPageCount() : 1;
    const qreal pageH = paged ? m_editor->document()->pageSize().height() : vp->height();
    const qreal pageW = paged ? m_editor->document()->pageSize().width() : vp->width();
    // Only the pages that intersect the repainted area (a long document has
    // hundreds; a caret blink repaints a few pixels).
    const int firstPage = paged ? qBound(0, int(docClip.top() / pageH), pages - 1) : 0;
    const int lastPage = paged ? qBound(0, int(docClip.bottom() / pageH), pages - 1) : 0;

    if (paged) {
        // Page breaks: a strip of desk between sheets. It is clamped to the
        // page margins so it can never cover text, even with tiny margins.
        const ThemeColors c = Theme::colors(m_themeId);
        const QTextFrameFormat fmt = m_editor->document()->rootFrame()->frameFormat();
        const qreal above = qBound(0.0, fmt.bottomMargin() - 2.0, 9.0);
        const qreal below = qBound(0.0, fmt.topMargin() - 2.0, 9.0);
        for (int n = qMax(1, firstPage); n <= qMin(pages - 1, lastPage + 1); ++n) {
            const qreal y = n * pageH;
            const QRectF band(-2, y - above, pageW + 4, above + below);
            painter.fillRect(band, c.desk);
            painter.setPen(QPen(c.pageBorder, 0)); // cosmetic: 1 device px at any zoom
            painter.drawLine(QPointF(0, band.top()), QPointF(pageW, band.top()));
            painter.drawLine(QPointF(0, band.bottom()), QPointF(pageW, band.bottom()));
        }
        if (m_meta.hasHeaderFooter()) {
            for (int i = firstPage; i <= lastPage; ++i) {
                paintHeaderFooter(&painter, QRectF(0, i * pageH, pageW, pageH),
                                  i + 1, pages);
            }
        }
    }
    if (m_pageGuides) {
        paintPageGuides(painter, firstPage, lastPage, pageH);
    }
}

void MainWindow::paintPageGuides(QPainter &painter, int firstPage, int lastPage, qreal pageH)
{
    QWidget *vp = m_editor->viewport();
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(isPaperTheme() ? QColor(160, 150, 140, 180) : QColor(80, 80, 80, 160));
    pen.setStyle(Qt::DotLine);
    pen.setCosmetic(true); // 1 device px at any zoom
    painter.setPen(pen);

    const bool paged = m_fullPageView && m_editor->document() && pageH > 1.0;
    const int w = paged ? qRound(m_editor->document()->pageSize().width()) : vp->width();
    if (paged) {
        const QTextFrameFormat fmt = m_editor->document()->rootFrame()->frameFormat();
        const int left = qRound(fmt.leftMargin());
        const int right = w - qRound(fmt.rightMargin());
        for (int i = firstPage; i <= lastPage; ++i) {
            const int top = qRound(i * pageH + fmt.topMargin());
            const int bottom = qRound((i + 1) * pageH - fmt.bottomMargin());
            painter.drawLine(left, top, left, bottom);
            painter.drawLine(right, top, right, bottom);
            painter.drawLine(left, top, right, top);
            painter.drawLine(left, bottom, right, bottom);
        }
    } else {
        const int h = vp->height();
        const int left = qMax(24, w / 10);
        painter.drawLine(left, 0, left, h);
        painter.drawLine(w - left, 0, w - left, h);
    }
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
    m_dirty = true;
    updateWindowTitle();
    syncViewActions();
    if (m_editor) {
        m_editor->viewport()->update();
    }
}

void MainWindow::fileProperties()
{
    m_meta.ensureDefaults();
    PropertiesDialog dlg(m_meta, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    m_meta = dlg.meta();
    m_dirty = true;
    updateWindowTitle();
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
            "<p><a href=\"https://github.com/sbj-ee/zwriter\">github.com/sbj-ee/zwriter</a></p>"
        ).arg(QString::fromUtf8(zwriter::kVersionString)));
}

void MainWindow::helpCheckUpdates()
{
    if (!m_updateChecker || m_updateChecker->isChecking()) {
        return;
    }
    if (m_checkUpdatesAction) {
        m_checkUpdatesAction->setEnabled(false);
        m_checkUpdatesAction->setText(QStringLiteral("Checking…"));
    }
    statusBar()->showMessage(QStringLiteral("Checking for updates…"), 3000);
    setChromeVisible(true);
    m_updateChecker->checkForUpdates(QString::fromUtf8(zwriter::kVersionString));
}

void MainWindow::onUpdateAvailable(const QString &tag, const QString &url)
{
    if (m_checkUpdatesAction) {
        m_checkUpdatesAction->setEnabled(true);
        m_checkUpdatesAction->setText(QStringLiteral("Check for &Updates…"));
    }
    setChromeVisible(true);
    statusBar()->showMessage(QStringLiteral("Update available: %1").arg(tag), 8000);
    const auto ret = QMessageBox::information(
        this,
        QStringLiteral("Update available"),
        QStringLiteral("Update available: %1\n\nOpen the release page in your browser?").arg(tag),
        QMessageBox::Open | QMessageBox::Cancel,
        QMessageBox::Open);
    if (ret == QMessageBox::Open && !url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(url));
    }
}

void MainWindow::onUpToDate()
{
    if (m_checkUpdatesAction) {
        m_checkUpdatesAction->setEnabled(true);
        m_checkUpdatesAction->setText(QStringLiteral("Check for &Updates…"));
    }
    statusBar()->showMessage(QStringLiteral("zwriter is up to date."), 4000);
}

void MainWindow::onUpdateCheckFailed(const QString &reason)
{
    if (m_checkUpdatesAction) {
        m_checkUpdatesAction->setEnabled(true);
        m_checkUpdatesAction->setText(QStringLiteral("Check for &Updates…"));
    }
    // Checks only run from Help > Check for Updates, so say why it failed.
    setChromeVisible(true);
    statusBar()->showMessage(QStringLiteral("Update check failed: %1").arg(reason), 10000);
    QMessageBox::warning(this, QStringLiteral("Check for Updates"),
                         QStringLiteral("Couldn't check for updates.\n\n%1").arg(reason));
}


void MainWindow::captureDemoScreenshots(const QString &dir)
{
    // Tall enough to show a full A4 page at screen logical DPI without clipping.
    resize(1024, 1400);

    // Screenshots use the default paper theme (near-white page) + full page view.
    m_themeId = QStringLiteral("paper");
    m_fullPageView = true;
    applyDocumentDefaults();
    m_meta.ensureDefaults();
    // Keep Lorem / chrome shots clean; dedicated spell-check.png enables underlines.
    m_spellCheck = false;
    if (m_spellChecker) {
        m_spellChecker->setEnabled(false);
    }
    if (m_spellHighlighter) {
        m_spellHighlighter->refreshAll();
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
        if (m_spellHighlighter) {
            m_spellHighlighter->refreshAll();
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
        if (m_spellHighlighter) {
            m_spellHighlighter->refreshAll();
        }
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
