// MainWindow, part: File: new/open/save/save-as, recent files, export PDF, print, page setup, properties.
// Moved verbatim from MainWindow.cpp (1.19.0); no behaviour change.
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

void MainWindow::fileNew()
{
    startNewDocument(false);
}

void MainWindow::fileNewMarkdown()
{
    startNewDocument(true);
}

void MainWindow::startNewDocument(bool markdown)
{
    if (!maybeSave()) {
        return;
    }
    m_editor->clear();
    DocumentIo::resetMarkdownFileFormat(m_editor->document()); // LF, no BOM
    m_meta = DocumentMeta{};
    m_meta.ensureDefaults();
    {
        QSettings s;
        adoptStyleFonts(StyleFonts::appDefaults(s));
    }
    forgetPageNumberState();
    setDocumentPageSize(QPageSize(PageSetup::newDocumentSizeId()), false);
    setMarkdownMode(markdown);
    applyDocumentDefaults();
    applyFullPageView(); // clear() resets the page margins/size
    if (markdown) {
        applyMarkdownBlockFormat();
    }
    m_editor->document()->clearUndoRedoStacks();
    m_editor->document()->setModified(false);
    setCurrentFile(QString(), markdown ? DocumentIo::Format::Markdown
                                       : DocumentIo::Format::Odt); // clean: no '*'
    syncViewActions();
    updateStats();
    syncFormatActions();
    updateFocusHighlight();
    m_editor->setFocus();
}

bool MainWindow::maybeSave()
{
    if (!m_dirty) {
        return true;
    }
    if (!m_metaDirty && !contentDiffersFromClean()) {
        // Marked dirty, but the document is exactly what was opened or last
        // saved (e.g. an edit typed and deleted again): nothing to lose.
        m_dirty = false;
        m_editor->document()->setModified(false);
        updateWindowTitle();
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

bool MainWindow::confirmImagesDropped(const QTextDocument *doc, DocumentIo::Format format)
{
    if (Images::formatKeepsImages(format)) {
        return true;
    }
    const int n = int(Images::imageFormats(doc).size());
    if (n == 0) {
        return true;
    }
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Images will be left out"),
                    QStringLiteral("This document has %1 image%2. TXT files can't hold images, so %3 "
                                   "left out of the saved file.")
                        .arg(n)
                        .arg(n == 1 ? QString() : QStringLiteral("s"))
                        .arg(n == 1 ? QStringLiteral("it will be") : QStringLiteral("they will be")),
                    QMessageBox::NoButton, this);
    box.setInformativeText(QStringLiteral("Save as ODT or RTF to keep them in the file, or as Markdown "
                                          "to copy them into a folder next to it."));
    QPushButton *save = box.addButton(QStringLiteral("Save Without Images"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == save;
}

int MainWindow::offerAssetCleanup(const QString &markdownPath, const QString &text)
{
    const QStringList unused = Images::unreferencedMarkdownAssets(markdownPath, text);
    if (unused.isEmpty()) {
        return 0;
    }
    QStringList shown = unused.mid(0, 15);
    if (unused.size() > shown.size()) {
        shown << QStringLiteral("… and %1 more").arg(unused.size() - shown.size());
    }
    QMessageBox box(QMessageBox::Question, QStringLiteral("Unused pictures"),
                    QStringLiteral("%1 picture%2 in the assets folder %3 no longer used in this file:")
                        .arg(unused.size())
                        .arg(unused.size() == 1 ? QString() : QStringLiteral("s"))
                        .arg(unused.size() == 1 ? QStringLiteral("is") : QStringLiteral("are")),
                    QMessageBox::NoButton, this);
    box.setInformativeText(shown.join(QLatin1Char('\n'))
                           + QStringLiteral("\n\nDelete them? Only pictures zwriter added are listed."));
    QPushButton *keep = box.addButton(QStringLiteral("Keep Files"), QMessageBox::RejectRole);
    QPushButton *del = box.addButton(QStringLiteral("Delete Files"), QMessageBox::DestructiveRole);
    box.setDefaultButton(keep);
    box.setEscapeButton(keep);
    box.exec();
    if (box.clickedButton() != del) {
        return 0;
    }
    const QDir base = QFileInfo(markdownPath).absoluteDir();
    int removed = 0;
    for (const QString &rel : unused) {
        removed += QFile::remove(base.filePath(rel)) ? 1 : 0;
    }
    statusBar()->showMessage(QStringLiteral("Deleted %1 unused picture%2.")
                                 .arg(removed).arg(removed == 1 ? QString() : QStringLiteral("s")), 4000);
    return removed;
}

bool MainWindow::saveToPath(const QString &path, DocumentIo::Format format)
{
    m_meta.ensureDefaults();
    m_meta.touchEdited();

    // Saving to the other family (Markdown <-> ODT/TXT/RTF) switches the
    // window to that file's mode: save, then reopen what was written.
    const bool changesMode = (format == DocumentIo::Format::Markdown) != m_markdownMode;
    QTextDocument *doc = m_editor->document();
    std::unique_ptr<QTextDocument> plainCopy;
    if (changesMode && m_markdownMode) {
        // Markdown text into a rich format: drop Markdown mode's line spacing
        // and give it the rich default font.
        plainCopy.reset(doc->clone());
        QTextCursor all(plainCopy.get());
        all.select(QTextCursor::Document);
        all.setBlockFormat(QTextBlockFormat());
        all.setCharFormat(QTextCharFormat());
        plainCopy->setDefaultFont(richDocumentFont());
        doc = plainCopy.get();
    }

    if (!confirmImagesDropped(doc, format)) {
        return false;
    }
    // A rich document saved as Markdown: its pictures go into <name>.assets/
    // and the text gets links to them.
    std::unique_ptr<QTextDocument> markdownCopy;
    if (format == DocumentIo::Format::Markdown && !m_markdownMode && Images::hasImages(doc)) {
        QString note;
        markdownCopy.reset(Images::withPicturesAsMarkdownLinks(doc, path, &note));
        doc = markdownCopy.get();
        if (!note.isEmpty()) {
            statusBar()->showMessage(note, 6000);
        }
    }

    // ODT: meta.xml and styles.xml (page layout, header/footer) are written
    // into the temp file before it replaces the target, so a failed patch
    // fails the save and the file on disk is left as it was.
    DocumentIo::SavePatch patch;
    if (format == DocumentIo::Format::Odt) {
        OdtPageStyle page;
        if (m_printer) {
            const QPageLayout layout = m_printer->pageLayout();
            page.sheetMm = PrintLayout::sheetSizeMm(layout);
            page.marginsMm = layout.margins(QPageLayout::Millimeter);
            page.landscape = layout.orientation() == QPageLayout::Landscape;
        }
        const QFont body = doc->defaultFont();
        if (!body.family().isEmpty()) {
            page.fontFamily = body.family();
        }
        const qreal bodyPt = body.pointSizeF() > 0 ? body.pointSizeF() : qreal(kDefaultBodyPointSize);
        page.fontPointSize = qMax(8.0, bodyPt - 2.0); // as paintHeaderFooter()
        m_meta.styleFonts = StyleFonts::serialize(m_styleFonts);
        const DocumentMeta meta = m_meta;
        patch = [meta, page](const QString &tempPath, QString *err) {
            QString metaErr;
            if (!OdtMeta::writeToOdt(tempPath, meta, &metaErr, page)) {
                if (err) {
                    *err = QStringLiteral("Could not write the document's metadata and page layout (%1)")
                               .arg(metaErr.isEmpty() ? QStringLiteral("unknown error") : metaErr);
                }
                return false;
            }
            return true;
        };
    }

    QString error;
    if (!DocumentIo::save(doc, path, format, &error, patch)) {
        QMessageBox::warning(this, QStringLiteral("Save failed"),
                             error + QStringLiteral("\n\nThe file on disk was not changed."));
        return false;
    }

    if (format == DocumentIo::Format::Markdown && m_markdownMode) {
        offerAssetCleanup(path, DocumentIo::markdownText(doc));
    }

    if (changesMode) {
        if (openPath(path)) {
            statusBar()->showMessage(
                format == DocumentIo::Format::Markdown
                    ? QStringLiteral("Saved as Markdown and reopened in Markdown mode")
                    : QStringLiteral("Saved and reopened as %1").arg(DocumentIo::formatName(format).toUpper()),
                5000);
        }
        return true;
    }
    setCurrentFile(path, format);
    return true;
}

// A LibreOffice table of contents is kept verbatim (it is written back as it
// was); in the editor its "entry<TAB>page" lines get a right-aligned tab at
// the text column's edge, so the page numbers line up on the right.
int MainWindow::alignKeptTocTabs()
{
    QTextDocument *doc = m_editor->document();
    const QTextFrameFormat ff = doc->rootFrame()->frameFormat();
    const qreal column = doc->textWidth() - ff.leftMargin() - ff.rightMargin() - 2 * (ff.border() + ff.padding());
    if (column <= 0) {
        return 0;
    }
    int n = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        QTextBlockFormat f = b.blockFormat();
        if (!(f.hasProperty(DocumentIo::TocProperty) || f.hasProperty(DocumentIo::GeneratedTocProperty))
            || !b.text().contains(QLatin1Char('\t'))) {
            continue;
        }
        const QList<QTextOption::Tab> tabs{QTextOption::Tab(column - f.leftMargin() - f.rightMargin() - 2, QTextOption::RightTab)};
        if (f.tabPositions() == tabs) {
            continue;
        }
        f.setTabPositions(tabs);
        QTextCursor(b).setBlockFormat(f);
        ++n;
    }
    return n;
}

bool MainWindow::openPath(const QString &path)
{
    QString error;
    // The highlighter keeps per-block state in QTextBlock::userState, which
    // the ODT import also uses to mark paragraphs (a kept table of contents):
    // load without it, then let it run over the loaded text.
    if (m_highlighter) {
        m_highlighter->setDocument(nullptr);
    }
    const bool loaded = DocumentIo::load(m_editor->document(), path, &error);
    if (m_highlighter) {
        // Highlight now (this also cancels the pass setDocument() queues,
        // which would mark the just-opened document modified).
        m_highlighter->setDocument(m_editor->document());
        m_highlighter->refreshAll();
    }
    if (!loaded) {
        QMessageBox::warning(this, QStringLiteral("Open failed"), error);
        return false;
    }
    auto fmt = DocumentIo::formatFromPath(path);
    if (fmt == DocumentIo::Format::Unknown) {
        // e.g. notes.md, app.conf: it was read as plain text, so it must be saved
        // back as plain text — never silently rewritten as an ODT zip.
        fmt = DocumentIo::Format::Txt;
    }
    const bool markdown = fmt == DocumentIo::Format::Markdown;
    if (!markdown) {
        DocumentIo::resetMarkdownFileFormat(m_editor->document());
    }
    m_meta = DocumentMeta{};
    m_meta.ensureDefaults();
    {
        // The file's own page size; legacy ODT (none stored), Markdown and
        // text open on A4, the size they always had (1.12.0).
        QSizeF sheetMm;
        bool landscape = false;
        if (fmt == DocumentIo::Format::Odt && OdtMeta::readPageSizeFromOdt(path, &sheetMm, &landscape)) {
            setDocumentPageSize(PageSetup::sizeFromMm(sheetMm), landscape);
        } else {
            setDocumentPageSize(QPageSize(PageSetup::kLegacySizeId), false);
        }
    }
    if (fmt == DocumentIo::Format::Odt) {
        OdtMeta::readFromOdt(path, &m_meta);
        m_meta.ensureDefaults();
    }
    {
        // The file's style fonts (1.15.0). A file without them (older zwriter,
        // LibreOffice, RTF, text) was formatted with the built-in faces.
        StyleFonts::Map map = StyleFonts::builtinDefaults();
        StyleFonts::parse(m_meta.styleFonts, &map);
        adoptStyleFonts(map);
    }
    forgetPageNumberState();
    {
        // Everything below is part of opening, not an edit.
        const QScopedValueRollback guard(m_suppressDirty, true);
        setMarkdownMode(markdown);
        // Keep shipping default for new typing; loaded spans keep their own faces.
        m_editor->document()->setDefaultFont(defaultDocumentFont());
        // Loading replaces the whole document, which resets its page margins/size:
        // put the page layout back.
        applyFullPageView();
        if (markdown) {
            applyMarkdownBlockFormat();
        }
        alignKeptTocTabs();
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
    if (markdown && DocumentIo::markdownLoadWasLossy(m_editor->document())) {
        statusBar()->showMessage(
            QStringLiteral("Note: this file mixes line endings or is not valid UTF-8; "
                           "saving will normalise it"),
            8000);
    }
    // A generated table of contents catches up with chapters edited since,
    // and figure numbers with the chapter's place in the book.
    if (refreshBookToc()) {
        statusBar()->showMessage(QStringLiteral("Table of contents updated to the current book"), 5000);
    }
    if (renumberCaptions() > 0) {
        statusBar()->showMessage(QStringLiteral("Figure numbers updated"), 5000);
    }
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

QString MainWindow::saveStartDir(const QString &bookDir) const
{
    // A saved file: next to it. Untitled with a book open: the book folder.
    if (!m_currentPath.isEmpty() && QFileInfo(m_currentPath).absoluteDir().exists()) {
        return QFileInfo(m_currentPath).absolutePath();
    }
    if (!bookDir.isEmpty() && QDir(bookDir).exists()) {
        return bookDir;
    }
    return documentsStartDir();
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
    dlg.setDirectory(saveStartDir(m_book && m_book->hasBook() ? m_book->book().bookDir() : QString()));

    // New documents start as ODT (the native format); an existing file keeps
    // its own format. The type last used for some other file doesn't carry over.
    QString filter = QStringLiteral("OpenDocument Text (*.odt)");
    if (m_markdownMode) {
        filter = QStringLiteral("Markdown (*.md)");
    } else if (!m_currentPath.isEmpty()) {
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
    dlg.selectNameFilter(m_markdownMode ? QStringLiteral("Markdown (*.md *.markdown)")
                                        : QStringLiteral("OpenDocument Text (*.odt)"));
    dlg.setDirectory(openStartDir());

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
    dlg.setDirectory(saveStartDir(m_book && m_book->hasBook() ? m_book->book().bookDir() : QString()));
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
    exportPdfTo(path);
}

bool MainWindow::exportPdfTo(const QString &path)
{
    QPrinter pdf(QPrinter::HighResolution);
    pdf.setOutputFormat(QPrinter::PdfFormat);
    pdf.setOutputFileName(path);
    pdf.setPageSize(m_printer->pageLayout().pageSize());
    pdf.setPageOrientation(m_printer->pageLayout().orientation());
    pdf.setPageMargins(m_printer->pageLayout().margins(QPageLayout::Millimeter),
                       QPageLayout::Millimeter);

    doPrint(&pdf);
    statusBar()->showMessage(QStringLiteral("Exported PDF: %1").arg(QFileInfo(path).fileName()), 4000);
    return QFileInfo(path).size() > 0;
}

// ------------------------------------------------------------ whole book

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

QPageLayout MainWindow::documentPageLayout() const
{
    return m_printer ? m_printer->pageLayout() : QPageLayout();
}

void MainWindow::setDocumentPageSize(const QPageSize &size, bool landscape)
{
    if (!m_printer) {
        return;
    }
    m_printer->setPageSize(size.isValid() ? size : QPageSize(PageSetup::kLegacySizeId));
    m_printer->setPageOrientation(landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
}

void MainWindow::filePageSizeAndUnits()
{
    const QPageLayout before = m_printer->pageLayout();
    PageSettingsDialog dlg(m_ruler ? m_ruler->unit() : PageSetup::rulerUnit(), before.pageSize(),
                           PageSetup::newDocumentSizeId(), this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    PageSetup::setRulerUnit(dlg.unit());
    if (m_ruler) {
        m_ruler->setUnit(dlg.unit());
    }
    PageSetup::setNewDocumentSizeId(dlg.newDocumentSize());
    const QPageSize size = dlg.documentSize();
    if (!size.isEquivalentTo(before.pageSize())) {
        setDocumentPageSize(size, before.orientation() == QPageLayout::Landscape);
        applyFullPageView();
        m_editor->viewport()->update();
        if (m_currentFormat == DocumentIo::Format::Odt) {
            m_editor->document()->setModified(true); // the size is saved in the .odt
        }
    }
}

void MainWindow::filePageSetup()
{
    const QPageLayout before = m_printer->pageLayout();
    QPageSetupDialog dlg(m_printer, this);
    if (dlg.exec() == QDialog::Accepted) {
        const QPageLayout after = m_printer->pageLayout();
        if (m_currentFormat == DocumentIo::Format::Odt && (!after.pageSize().isEquivalentTo(before.pageSize())
                                || after.orientation() != before.orientation())) {
            m_editor->document()->setModified(true); // saved with the document now
        }
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

void MainWindow::fileProperties()
{
    m_meta.ensureDefaults();
    PropertiesDialog dlg(m_meta, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    m_meta = dlg.meta();
    m_metaDirty = true;
    m_dirty = true;
    updateWindowTitle();
}


