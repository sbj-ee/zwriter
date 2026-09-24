#include "MainWindow.hpp"
#include "DocumentIo.hpp"
#include "DocumentMeta.hpp"
#include "FindReplaceBar.hpp"
#include "PropertiesDialog.hpp"
#include "TypewriterSounds.hpp"
#include "UpdateChecker.hpp"
#include "version.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPageLayout>
#include <QPageSetupDialog>
#include <QPageSize>
#include <QPainter>
#include <QPen>
#include <QColor>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace {
// Reading-time assumption: average adult silent reading ~225 WPM.
constexpr int kReadingWpm = 225;
constexpr int kMaxRecentFiles = 8;
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("zwriter"));
    resize(900, 700);
    loadWindowIcon();
    loadSettings();

    m_meta.ensureDefaults();

    m_printer = new QPrinter(QPrinter::HighResolution);
    m_printer->setPageSize(QPageSize(QPageSize::Letter));

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_findBar = new FindReplaceBar(central);
    layout->addWidget(m_findBar);

    m_editor = new QTextEdit(central);
    m_editor->setAcceptRichText(true);
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setPlaceholderText(QStringLiteral("Start writing…"));
    layout->addWidget(m_editor, 1);
    setCentralWidget(central);

    m_keySounds = new TypewriterSounds(this);
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailable, this,
            &MainWindow::onUpdateAvailable);
    connect(m_updateChecker, &UpdateChecker::upToDate, this,
            &MainWindow::onUpToDate);
    connect(m_updateChecker, &UpdateChecker::checkFailed, this,
            &MainWindow::onUpdateCheckFailed);

    m_statsLabel = new QLabel(this);
    m_keysLabel = new QLabel(this);
    m_keysLabel->setCursor(Qt::PointingHandCursor);
    m_keysLabel->setToolTip(QStringLiteral(
        "Click or press Ctrl+Shift+K to toggle typewriter key sounds (default off)"));
    statusBar()->addWidget(m_keysLabel);
    statusBar()->addPermanentWidget(m_statsLabel);
    statusBar()->setSizeGripEnabled(false);

    buildFileMenu();
    buildViewMenu();
    buildHelpMenu();
    buildFormatToolbar();

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(1200);
    connect(m_hideTimer, &QTimer::timeout, this, &MainWindow::hideAwayIdle);

    connect(m_editor, &QTextEdit::textChanged, this, &MainWindow::updateStats);
    connect(m_editor, &QTextEdit::textChanged, this, &MainWindow::markDirty);
    connect(m_editor, &QTextEdit::textChanged, this, [this]() {
        if (m_keySounds) {
            m_keySounds->playKey();
        }
    });
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, &MainWindow::onCursorMoved);
    connect(m_editor, &QTextEdit::currentCharFormatChanged, this,
            [this](const QTextCharFormat &) { syncFormatActions(); });

    connect(m_findBar, &FindReplaceBar::findNext, this, &MainWindow::findNext);
    connect(m_findBar, &FindReplaceBar::findPrev, this, &MainWindow::findPrev);
    connect(m_findBar, &FindReplaceBar::replaceOne, this, &MainWindow::replaceOne);
    connect(m_findBar, &FindReplaceBar::replaceAll, this, &MainWindow::replaceAll);
    connect(m_findBar, &FindReplaceBar::closeRequested, this, &MainWindow::hideFindBar);

    auto *findAct = new QAction(QStringLiteral("Find…"), this);
    findAct->setShortcut(QKeySequence::Find);
    connect(findAct, &QAction::triggered, this, &MainWindow::showFind);
    addAction(findAct);

    auto *replaceAct = new QAction(QStringLiteral("Replace…"), this);
    replaceAct->setShortcut(QKeySequence::Replace);
    connect(replaceAct, &QAction::triggered, this, &MainWindow::showReplace);
    addAction(replaceAct);

    auto *findNextAct = new QAction(this);
    findNextAct->setShortcut(QKeySequence::FindNext);
    connect(findNextAct, &QAction::triggered, this, &MainWindow::findNext);
    addAction(findNextAct);

    auto *findPrevAct = new QAction(this);
    findPrevAct->setShortcut(QKeySequence::FindPrevious);
    connect(findPrevAct, &QAction::triggered, this, &MainWindow::findPrev);
    addAction(findPrevAct);

    m_keysLabel->installEventFilter(this);
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

    applyDarkTheme();
    setChromeVisible(false);
    m_dirty = false;
    syncViewActions();
    updateStats();
    updateKeySoundsLabel();
    syncFormatActions();
    updateWindowTitle();
    updateFocusHighlight();
}

void MainWindow::loadSettings()
{
    QSettings s;
    m_typewriterScroll = s.value(QStringLiteral("view/typewriterScroll"), true).toBool();
    m_focusMode = s.value(QStringLiteral("view/focusMode"), false).toBool();
    m_focusSentence = s.value(QStringLiteral("view/focusSentence"), false).toBool();
    m_smartQuotes = s.value(QStringLiteral("view/smartQuotes"), false).toBool();
    m_recentFiles = s.value(QStringLiteral("files/recent")).toStringList();
}

void MainWindow::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("view/typewriterScroll"), m_typewriterScroll);
    s.setValue(QStringLiteral("view/focusMode"), m_focusMode);
    s.setValue(QStringLiteral("view/focusSentence"), m_focusSentence);
    s.setValue(QStringLiteral("view/smartQuotes"), m_smartQuotes);
    s.setValue(QStringLiteral("files/recent"), m_recentFiles);
}

void MainWindow::buildFileMenu()
{
    m_fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    auto *openAct = m_fileMenu->addAction(QStringLiteral("&Open…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::fileOpen);

    auto *saveAct = m_fileMenu->addAction(QStringLiteral("&Save"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::fileSave);

    auto *saveAsAct = m_fileMenu->addAction(QStringLiteral("Save &As…"));
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::fileSaveAs);

    m_fileMenu->addSeparator();
    m_recentMenu = m_fileMenu->addMenu(QStringLiteral("Open &Recent"));
    rebuildRecentMenu();

    m_fileMenu->addSeparator();

    auto *exportAct = m_fileMenu->addAction(QStringLiteral("&Export PDF…"));
    exportAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(exportAct, &QAction::triggered, this, &MainWindow::exportPdf);

    m_fileMenu->addSeparator();

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

    m_pageGuidesAction = m_fileMenu->addAction(QStringLiteral("Page &Guides"));
    m_pageGuidesAction->setCheckable(true);
    m_pageGuidesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    m_pageGuidesAction->setToolTip(QStringLiteral("Toggle page margin guides (Ctrl+G)"));
    connect(m_pageGuidesAction, &QAction::triggered, this, &MainWindow::togglePageGuides);

    addAction(openAct);
    addAction(saveAct);
    addAction(saveAsAct);
    addAction(exportAct);
    addAction(printAct);
    addAction(propsAct);
    addAction(m_pageGuidesAction);
}

void MainWindow::buildViewMenu()
{
    m_viewMenu = menuBar()->addMenu(QStringLiteral("&View"));

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

    m_viewMenu->addSeparator();

    m_smartQuotesAction = m_viewMenu->addAction(QStringLiteral("&Smart Quotes / Dashes"));
    m_smartQuotesAction->setCheckable(true);
    m_smartQuotesAction->setToolTip(
        QStringLiteral("Curly quotes and em/en dashes from ASCII while typing (default off)"));
    connect(m_smartQuotesAction, &QAction::triggered, this, &MainWindow::toggleSmartQuotes);

    addAction(m_typewriterScrollAction);
    addAction(m_focusModeAction);
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
    if (m_focusParagraphAction && m_focusSentenceAction) {
        const QSignalBlocker b1(m_focusParagraphAction);
        const QSignalBlocker b2(m_focusSentenceAction);
        m_focusParagraphAction->setChecked(!m_focusSentence);
        m_focusSentenceAction->setChecked(m_focusSentence);
    }
}

void MainWindow::buildFormatToolbar()
{
    m_formatBar = addToolBar(QStringLiteral("Format"));
    m_formatBar->setObjectName(QStringLiteral("formatBar"));
    m_formatBar->setMovable(false);
    m_formatBar->setFloatable(false);
    m_formatBar->setIconSize(QSize(16, 16));
    m_formatBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_boldAction = m_formatBar->addAction(QStringLiteral("Bold"));
    m_boldAction->setCheckable(true);
    m_boldAction->setShortcut(QKeySequence::Bold);
    m_boldAction->setToolTip(QStringLiteral("Bold (Ctrl+B)"));
    connect(m_boldAction, &QAction::triggered, this, &MainWindow::toggleBold);

    m_italicAction = m_formatBar->addAction(QStringLiteral("Italic"));
    m_italicAction->setCheckable(true);
    m_italicAction->setShortcut(QKeySequence::Italic);
    m_italicAction->setToolTip(QStringLiteral("Italic (Ctrl+I)"));
    connect(m_italicAction, &QAction::triggered, this, &MainWindow::toggleItalic);

    m_formatBar->addSeparator();

    auto *headingGroup = new QActionGroup(this);
    headingGroup->setExclusive(true);

    m_paragraphAction = m_formatBar->addAction(QStringLiteral("P"));
    m_paragraphAction->setCheckable(true);
    m_paragraphAction->setToolTip(QStringLiteral("Paragraph"));
    m_paragraphAction->setData(0);
    headingGroup->addAction(m_paragraphAction);

    m_h1Action = m_formatBar->addAction(QStringLiteral("H1"));
    m_h1Action->setCheckable(true);
    m_h1Action->setToolTip(QStringLiteral("Heading 1"));
    m_h1Action->setData(1);
    headingGroup->addAction(m_h1Action);

    m_h2Action = m_formatBar->addAction(QStringLiteral("H2"));
    m_h2Action->setCheckable(true);
    m_h2Action->setToolTip(QStringLiteral("Heading 2"));
    m_h2Action->setData(2);
    headingGroup->addAction(m_h2Action);

    m_h3Action = m_formatBar->addAction(QStringLiteral("H3"));
    m_h3Action->setCheckable(true);
    m_h3Action->setToolTip(QStringLiteral("Heading 3"));
    m_h3Action->setData(3);
    headingGroup->addAction(m_h3Action);

    connect(headingGroup, &QActionGroup::triggered, this, [this](QAction *) {
        applyHeading();
    });

    m_formatBar->addSeparator();

    auto *exportAction = m_formatBar->addAction(QStringLiteral("Export PDF…"));
    exportAction->setToolTip(QStringLiteral("Export as PDF (Ctrl+Shift+E)"));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportPdf);

    addAction(m_boldAction);
    addAction(m_italicAction);
}

void MainWindow::loadWindowIcon()
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/icons/zwriter-128.png"),
        QStringLiteral("assets/icons/zwriter-128.png"),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            QStringLiteral("../assets/icons/zwriter-128.png")),
        QStringLiteral(":/icons/zwriter-128.png"),
    };
    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            setWindowIcon(QIcon(path));
            return;
        }
    }
}

void MainWindow::applyDarkTheme()
{
    const QString style = QStringLiteral(
        "QMainWindow { background-color: #1e1e1e; }"
        "QTextEdit {"
        "  background-color: #1e1e1e;"
        "  color: #d4d4d4;"
        "  selection-background-color: #264f78;"
        "  selection-color: #ffffff;"
        "  font-family: 'Georgia', 'Times New Roman', serif;"
        "  font-size: 16pt;"
        "  padding: 48px 20%;"
        "}"
        "QMenuBar {"
        "  background-color: #252526;"
        "  color: #d4d4d4;"
        "  border-bottom: 1px solid #3c3c3c;"
        "  font-family: 'Segoe UI', 'Helvetica Neue', sans-serif;"
        "  font-size: 11pt;"
        "}"
        "QMenuBar::item:selected { background-color: #3c3c3c; }"
        "QMenu {"
        "  background-color: #2d2d2d;"
        "  color: #d4d4d4;"
        "  border: 1px solid #3c3c3c;"
        "}"
        "QMenu::item:selected { background-color: #264f78; }"
        "QStatusBar {"
        "  background-color: #252526;"
        "  color: #a0a0a0;"
        "  border-top: 1px solid #3c3c3c;"
        "  font-family: 'Segoe UI', 'Helvetica Neue', sans-serif;"
        "  font-size: 11pt;"
        "}"
        "QStatusBar QLabel { color: #a0a0a0; padding: 0 8px; }"
        "QToolBar {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #3c3c3c;"
        "  spacing: 4px;"
        "  padding: 2px 6px;"
        "}"
        "QToolBar QToolButton {"
        "  color: #d4d4d4;"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 3px;"
        "  padding: 4px 8px;"
        "  font-family: 'Segoe UI', 'Helvetica Neue', sans-serif;"
        "  font-size: 11pt;"
        "}"
        "QToolBar QToolButton:hover {"
        "  background-color: #3c3c3c;"
        "  border-color: #505050;"
        "}"
        "QToolBar QToolButton:checked {"
        "  background-color: #264f78;"
        "  color: #ffffff;"
        "}"
        "QToolBar::separator {"
        "  background-color: #3c3c3c;"
        "  width: 1px;"
        "  margin: 4px 6px;"
        "}"
        "#findReplaceBar {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #3c3c3c;"
        "}"
        "#findReplaceBar QLabel { color: #d4d4d4; }"
        "#findReplaceBar QLineEdit {"
        "  background-color: #1e1e1e;"
        "  color: #d4d4d4;"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 3px;"
        "  padding: 3px 6px;"
        "  selection-background-color: #264f78;"
        "}"
        "#findReplaceBar QPushButton, #findReplaceBar QCheckBox {"
        "  color: #d4d4d4;"
        "  background-color: #3c3c3c;"
        "  border: 1px solid #505050;"
        "  border-radius: 3px;"
        "  padding: 3px 8px;"
        "}"
        "#findReplaceBar QCheckBox { background: transparent; border: none; }"
        "#findReplaceBar QPushButton:hover { background-color: #505050; }"
    );
    setStyleSheet(style);
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

void MainWindow::updateKeySoundsLabel()
{
    const bool on = m_keySounds && m_keySounds->isEnabled();
    m_keysLabel->setText(on ? QStringLiteral("Keys: on") : QStringLiteral("Keys: off"));
}

void MainWindow::markDirty()
{
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
    m_hideAwayPinned = !m_hideAwayPinned;
    m_hideTimer->stop();
    setChromeVisible(m_hideAwayPinned);
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
    updateKeySoundsLabel();
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

void MainWindow::onCursorMoved()
{
    syncFormatActions();
    centerCaret();
    updateFocusHighlight();
}

void MainWindow::centerCaret()
{
    if (!m_typewriterScroll || m_centering || !m_editor) {
        return;
    }
    m_centering = true;
    const QRect cr = m_editor->cursorRect();
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
        m_editor->setExtraSelections({});
        return;
    }

    const auto range = focusRange();
    const int docEnd = m_editor->document()->characterCount() - 1;
    QList<QTextEdit::ExtraSelection> extras;

    QTextCharFormat dimFmt;
    dimFmt.setForeground(QColor(QStringLiteral("#5a5a5a")));

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
        return false;
    }
    QTextDocument::FindFlags flags;
    if (!forward) {
        flags |= QTextDocument::FindBackward;
    }
    if (m_findBar->caseSensitive()) {
        flags |= QTextDocument::FindCaseSensitively;
    }
    bool found = m_editor->find(needle, flags);
    if (!found) {
        // Wrap around.
        QTextCursor c(m_editor->document());
        if (forward) {
            c.movePosition(QTextCursor::Start);
        } else {
            c.movePosition(QTextCursor::End);
        }
        m_editor->setTextCursor(c);
        found = m_editor->find(needle, flags);
    }
    return found;
}

void MainWindow::findNext()
{
    if (!m_findBar->isVisible()) {
        showFind();
        return;
    }
    findInDoc(true);
}

void MainWindow::findPrev()
{
    if (!m_findBar->isVisible()) {
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
    QTextCursor cursor = m_editor->textCursor();
    cursor.beginEditBlock();

    QTextBlockFormat blockFmt = cursor.blockFormat();
    blockFmt.setHeadingLevel(level);
    cursor.mergeBlockFormat(blockFmt);

    QTextCharFormat charFmt;
    if (level == 0) {
        charFmt.setFontPointSize(16);
        charFmt.setFontWeight(QFont::Normal);
    } else if (level == 1) {
        charFmt.setFontPointSize(28);
        charFmt.setFontWeight(QFont::Bold);
    } else if (level == 2) {
        charFmt.setFontPointSize(22);
        charFmt.setFontWeight(QFont::Bold);
    } else {
        charFmt.setFontPointSize(18);
        charFmt.setFontWeight(QFont::DemiBold);
    }
    cursor.mergeBlockCharFormat(charFmt);
    cursor.endEditBlock();

    m_editor->setTextCursor(cursor);
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
}

bool MainWindow::maybeSave()
{
    if (!m_dirty) {
        return true;
    }
    const auto ret = QMessageBox::question(
        this,
        QStringLiteral("zwriter"),
        QStringLiteral("Save changes before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (ret == QMessageBox::Cancel) {
        return false;
    }
    if (ret == QMessageBox::Discard) {
        return true;
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
        if (!OdtMeta::writeToOdt(path, m_meta, &metaErr)) {
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
    const auto fmt = DocumentIo::formatFromPath(path);
    m_meta = DocumentMeta{};
    m_meta.ensureDefaults();
    if (fmt == DocumentIo::Format::Odt) {
        OdtMeta::readFromOdt(path, &m_meta);
        m_meta.ensureDefaults();
    }
    setCurrentFile(path, fmt);
    updateStats();
    syncFormatActions();
    updateFocusHighlight();
    m_editor->document()->setModified(false);
    return true;
}

void MainWindow::fileOpen()
{
    if (!maybeSave()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open"),
        QDir::homePath(),
        DocumentIo::openFilter());
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
    QString selectedFilter = QStringLiteral("OpenDocument Text (*.odt)");
    const QString suggested = QDir::home().filePath(
        defaultBaseName() + QStringLiteral(".odt"));
    QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save As"),
        suggested,
        DocumentIo::saveFilter(),
        &selectedFilter);
    if (path.isEmpty()) {
        return;
    }

    DocumentIo::Format format = DocumentIo::formatFromFilter(selectedFilter, path);
    const DocumentIo::Format pathFmt = DocumentIo::formatFromPath(path);
    if (pathFmt != DocumentIo::Format::Unknown) {
        format = pathFmt;
    } else {
        path += QLatin1Char('.') + DocumentIo::formatName(format);
    }

    saveToPath(path, format);
}

void MainWindow::exportPdf()
{
    const QString suggested = QDir::home().filePath(defaultBaseName() + QStringLiteral(".pdf"));
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export PDF"),
        suggested,
        QStringLiteral("PDF files (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }

    QString out = path;
    if (!out.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        out += QStringLiteral(".pdf");
    }

    QPrinter pdf(QPrinter::HighResolution);
    pdf.setOutputFormat(QPrinter::PdfFormat);
    pdf.setOutputFileName(out);
    pdf.setPageSize(m_printer->pageLayout().pageSize());
    pdf.setPageOrientation(m_printer->pageLayout().orientation());
    pdf.setPageMargins(m_printer->pageLayout().margins(), QPageLayout::Millimeter);

    m_editor->document()->print(&pdf);
}

void MainWindow::doPrint(QPrinter *printer)
{
    m_editor->document()->print(printer);
}

void MainWindow::filePrint()
{
    QPrintDialog dlg(m_printer, this);
    dlg.setWindowTitle(QStringLiteral("Print"));
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    doPrint(m_printer);
}

void MainWindow::filePageSetup()
{
    QPageSetupDialog dlg(m_printer, this);
    dlg.exec();
}

void MainWindow::filePrintPreview()
{
    QPrintPreviewDialog dlg(m_printer, this);
    dlg.setWindowTitle(QStringLiteral("Print Preview"));
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, &MainWindow::printPreview);
    dlg.exec();
}

void MainWindow::printPreview(QPrinter *printer)
{
    doPrint(printer);
}

void MainWindow::togglePageGuides()
{
    m_pageGuides = m_pageGuidesAction && m_pageGuidesAction->isChecked();
    m_editor->viewport()->update();
}

void MainWindow::paintPageGuides()
{
    QWidget *vp = m_editor->viewport();
    QPainter painter(vp);
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(QColor(80, 80, 80, 160));
    pen.setStyle(Qt::DotLine);
    painter.setPen(pen);

    const int w = vp->width();
    const int h = vp->height();
    const int left = qMax(24, w / 10);
    const int right = w - left;
    painter.drawLine(left, 0, left, h);
    painter.drawLine(right, 0, right, h);
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

void MainWindow::onUpdateCheckFailed()
{
    if (m_checkUpdatesAction) {
        m_checkUpdatesAction->setEnabled(true);
        m_checkUpdatesAction->setText(QStringLiteral("Check for &Updates…"));
    }
    // Soft fail — no scary dialog (no network / no releases yet).
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
    if (event->key() == Qt::Key_F11) {
        toggleFullscreen();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_K
        && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        toggleKeySounds();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_keysLabel && event->type() == QEvent::MouseButtonRelease) {
        toggleKeySounds();
        return true;
    }

    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (trySmartTypography(ke)) {
            return true;
        }
    }

    if (watched == m_editor->viewport() && event->type() == QEvent::Paint && m_pageGuides) {
        watched->removeEventFilter(this);
        QCoreApplication::sendEvent(watched, event);
        watched->installEventFilter(this);
        paintPageGuides();
        return true;
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
