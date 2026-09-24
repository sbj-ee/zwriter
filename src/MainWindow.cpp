#include "MainWindow.hpp"
#include "DocumentIo.hpp"
#include <QPainter>
#include <QPen>
#include <QColor>
#include "PropertiesDialog.hpp"
#include "DocumentMeta.hpp"
#include "TypewriterSounds.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
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
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("zwriter"));
    resize(900, 700);
    loadWindowIcon();

    m_meta.ensureDefaults();

    m_printer = new QPrinter(QPrinter::HighResolution);
    m_printer->setPageSize(QPageSize(QPageSize::Letter));

    m_editor = new QTextEdit(this);
    m_editor->setAcceptRichText(true);
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setPlaceholderText(QStringLiteral("Start writing…"));
    setCentralWidget(m_editor);

    m_keySounds = new TypewriterSounds(this);

    // Bottom status bar: live word + char counts (hide-away with chrome).
    m_statsLabel = new QLabel(this);
    m_keysLabel = new QLabel(this);
    m_keysLabel->setCursor(Qt::PointingHandCursor);
    m_keysLabel->setToolTip(QStringLiteral(
        "Click or press Ctrl+Shift+K to toggle typewriter key sounds (default off)"));
    statusBar()->addWidget(m_keysLabel);
    statusBar()->addPermanentWidget(m_statsLabel);
    statusBar()->setSizeGripEnabled(false);

    buildFileMenu();
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
    connect(m_editor, &QTextEdit::cursorPositionChanged, this,
            &MainWindow::syncFormatActions);
    connect(m_editor, &QTextEdit::currentCharFormatChanged, this,
            [this](const QTextCharFormat &) { syncFormatActions(); });

    m_keysLabel->installEventFilter(this);
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
    updateStats();
    updateKeySoundsLabel();
    syncFormatActions();
    updateWindowTitle();
}

void MainWindow::buildFileMenu()
{
    // Native menu bar participates in hide-away chrome (not a permanent ribbon).
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

    // Keep shortcuts alive when chrome (menu) is hidden.
    addAction(openAct);
    addAction(saveAct);
    addAction(saveAsAct);
    addAction(exportAct);
    addAction(printAct);
    addAction(propsAct);
    addAction(m_pageGuidesAction);
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

    m_statsLabel->setText(
        QStringLiteral("%1 words  ·  %2 characters").arg(words).arg(chars));
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
            // Document body saved; metadata patch is best-effort.
            statusBar()->showMessage(
                metaErr.isEmpty() ? QStringLiteral("Saved (metadata patch skipped)")
                                  : metaErr,
                4000);
        }
    }

    setCurrentFile(path, format);
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
    QString error;
    if (!DocumentIo::load(m_editor->document(), path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Open failed"), error);
        return;
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
    m_editor->document()->setModified(false);
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
        format = pathFmt; // explicit extension wins
    } else {
        // No/unknown suffix → append extension for the chosen filter (default ODT).
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

    // Lean page-column guides (~ printable width), not a Word ruler.
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
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

    if (watched == m_editor->viewport() && event->type() == QEvent::Paint && m_pageGuides) {
        // Deliver paint to the viewport first, then overlay guides on top.
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
