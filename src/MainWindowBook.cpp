// MainWindow, part: Book: library/book sidebar, chapter titles and numbering, book ToC, book export and print.
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

void MainWindow::toggleLibrary()
{
    const bool show = !m_library->isVisibleTo(this);
    if (show && m_book->isVisibleTo(this)) {
        m_book->hide();
        saveBookSettings();
    }
    m_library->setVisible(show);
    // No folder yet: the pane shows a hint and a "Choose Folder…" button
    // instead of popping a dialog the user did not ask for.
    if (show) {
        m_library->setCurrentFile(m_currentPath);
    }
    saveLibrarySettings();
    syncViewActions();
}

void MainWindow::chooseLibraryFolder()
{
    // Non-blocking; once a folder is picked rootPathChosen saves it and shows
    // the sidebar.
    m_library->chooseRootFolder();
}

void MainWindow::saveLibrarySettings() const
{
    LibrarySidebar::saveSettings({m_library->rootPath(), m_library->isVisibleTo(this)});
}

void MainWindow::saveBookSettings() const
{
    BookSidebar::saveSettings({m_book->currentBook(), m_book->isVisibleTo(this)});
}

void MainWindow::showSidebar(bool book)
{
    if (book) {
        m_library->hide();
        if (m_book->rootPath() != m_library->rootPath()) {
            m_book->setRootPath(m_library->rootPath());
        } else {
            m_book->reload(); // picks up books / edits made outside zwriter
        }
        m_book->show();
        m_book->setCurrentFile(m_currentPath);
    } else {
        m_book->hide();
        m_library->show();
        m_library->setCurrentFile(m_currentPath);
    }
    saveLibrarySettings();
    saveBookSettings();
    syncViewActions();
}

void MainWindow::toggleBook()
{
    if (m_book->isVisibleTo(this)) {
        m_book->hide();
        saveBookSettings();
        syncViewActions();
    } else {
        showSidebar(true);
    }
}

bool MainWindow::insertBookToc()
{
    if (!m_book || !m_book->hasBook()) {
        statusBar()->showMessage(QStringLiteral("Open a book to insert its table of contents."), 5000);
        return false;
    }
    // The table of contents lives in the book's Contents chapter (created as
    // "00 Contents.odt", first in the book, if there is none), not wherever the
    // caret happens to be; it lists every other chapter.
    int row = m_book->contentsRow();
    QString contents = row >= 0 ? m_book->book().absolutePath(row) : QString();
    const bool isOpen = !contents.isEmpty() && !m_currentPath.isEmpty()
        && QFileInfo(m_currentPath).absoluteFilePath() == QFileInfo(contents).absoluteFilePath();
    if (!isOpen) {
        if (!maybeSave()) {
            return false;
        }
        if (contents.isEmpty()) {
            contents = m_book->createContentsChapter();
            if (contents.isEmpty()) {
                return false;
            }
        }
        if (!openPath(contents) || m_markdownMode) {
            statusBar()->showMessage(QStringLiteral("The Contents chapter must be an .odt file for a table of contents."), 6000);
            return false;
        }
    }
    QTextCursor at(m_editor->document());
    const bool had = BookToc::hasToc(m_editor->document());
    BookToc::insertOrUpdate(m_editor->document(), &at, bookTocEntries());
    alignKeptTocTabs();
    if (!at.block().next().isValid()) {
        at.movePosition(QTextCursor::End);
        at.insertBlock(QTextBlockFormat(), QTextCharFormat());
    }
    fileSave(); // the Contents chapter is saved with its table
    statusBar()->showMessage(had ? QStringLiteral("Table of contents updated in %1").arg(QFileInfo(contents).fileName())
                                 : QStringLiteral("Table of contents inserted in %1").arg(QFileInfo(contents).fileName()),
                             5000);
    m_editor->setFocus();
    return true;
}

int MainWindow::chapterNumber() const
{
    if (!m_book || !m_book->hasBook() || m_currentPath.isEmpty()) {
        return 0;
    }
    const int i = m_book->book().indexOf(QFileInfo(m_currentPath).absoluteFilePath());
    return i >= 0 ? m_book->book().chapterNumber(i) : 0;
}

int MainWindow::renumberCaptions(bool joinPrevious)
{
    if (m_markdownMode || m_renumbering || !m_editor) {
        return 0;
    }
    const QScopedValueRollback guard(m_renumbering, true);
    return Captions::renumber(m_editor->document(), chapterNumber(), joinPrevious);
}

namespace {
// The chapter's title heading: the first heading, when it is a Heading 1 and
// either says the old title or is the only Heading 1. In a Markdown file
// (plain text in the editor) that is a "# ..." line. -1: none.
int titleHeadingBlock(const QTextDocument *doc, bool markdown, const QString &oldTitle)
{
    static const QRegularExpression atx(QStringLiteral("^ {0,3}(#{1,6})[ \\t]+(.*?)[ \\t#]*$"));
    int first = -1;
    QString firstText;
    int h1s = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        int level = 0;
        QString text;
        if (markdown) {
            const auto m = atx.match(b.text());
            if (m.hasMatch()) {
                level = int(m.capturedLength(1));
                text = m.captured(2);
            }
        } else {
            level = b.blockFormat().headingLevel();
            text = b.text();
        }
        if (level <= 0) {
            continue;
        }
        if (first < 0) {
            if (level != 1) {
                return -1;
            }
            first = b.blockNumber();
            firstText = text.trimmed();
        }
        h1s += level == 1 ? 1 : 0;
    }
    if (first < 0) {
        return -1;
    }
    return (firstText.compare(oldTitle.trimmed(), Qt::CaseInsensitive) == 0 || h1s == 1) ? first : -1;
}

// Replace the text of block `number` (keeping "# " in Markdown), as one edit step.
void setHeadingText(QTextDocument *doc, int number, bool markdown, const QString &title)
{
    const QTextBlock b = doc->findBlockByNumber(number);
    QTextCursor c(b);
    int start = b.position();
    if (markdown) {
        int i = 0;
        const QString t = b.text();
        while (i < t.size() && (t.at(i) == QLatin1Char(' ') || t.at(i) == QLatin1Char('#'))) {
            ++i;
        }
        start += i;
    }
    c.beginEditBlock();
    c.setPosition(start);
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    const QTextCharFormat keep = c.charFormat();
    c.insertText(title, keep);
    c.endEditBlock();
}
} // namespace

void MainWindow::retitleChapterHeading(const QString &path, const QString &oldTitle, const QString &newTitle)
{
    const bool isOpen = !m_currentPath.isEmpty()
        && QFileInfo(m_currentPath).absoluteFilePath() == QFileInfo(path).absoluteFilePath();
    if (isOpen) {
        QTextDocument *doc = m_editor->document();
        const int block = titleHeadingBlock(doc, m_markdownMode, oldTitle);
        if (block < 0) {
            return;
        }
        const bool unsaved = doc->isModified();
        if (unsaved) {
            QMessageBox box(QMessageBox::Question, QStringLiteral("Rename Chapter"),
                            QStringLiteral("Change the chapter heading to \"%1\" too?").arg(newTitle),
                            QMessageBox::NoButton, this);
            box.setObjectName(QStringLiteral("renameHeadingDialog"));
            box.setInformativeText(QStringLiteral("The chapter has unsaved changes, so the heading is changed in "
                                                  "the editor only. Save when you are ready; Ctrl+Z undoes it."));
            QPushButton *yes = box.addButton(QStringLiteral("Change Heading"), QMessageBox::AcceptRole);
            box.addButton(QStringLiteral("Keep Heading"), QMessageBox::RejectRole);
            box.setDefaultButton(yes);
            box.exec();
            if (box.clickedButton() != yes) {
                return;
            }
        }
        setHeadingText(doc, block, m_markdownMode, newTitle);
        if (!unsaved) {
            // Nothing else was pending: save, so the file agrees with the book.
            // Ctrl+Z still brings the old heading back.
            fileSave();
        }
        refreshBookToc();
        return;
    }
    // A chapter that is not open: rewrite its file (written safely, all or nothing).
    const DocumentIo::Format format = DocumentIo::formatFromPath(path);
    const bool markdown = format == DocumentIo::Format::Markdown;
    if (format == DocumentIo::Format::Unknown || format == DocumentIo::Format::Txt) {
        return;
    }
    QTextDocument doc;
    if (!DocumentIo::load(&doc, path)) {
        return;
    }
    const int block = titleHeadingBlock(&doc, markdown, oldTitle);
    if (block < 0) {
        return;
    }
    setHeadingText(&doc, block, markdown, newTitle);
    QString err;
    if (!DocumentIo::save(&doc, path, format, &err)) {
        statusBar()->showMessage(QStringLiteral("Could not update the heading in %1: %2")
                                     .arg(QFileInfo(path).fileName(), err), 8000);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Heading changed to \"%1\" in %2 (rename back to undo)")
                                 .arg(newTitle, QFileInfo(path).fileName()), 6000);
    refreshBookToc();
}

// The generated ToC's entries with page numbers: the pages the whole-book PDF
// (File > Export Book > PDF, with its current options and the page setup) puts
// each chapter and heading on. They are found by laying the book out exactly as
// the export does, so they match the PDF, not the editor's own pages.
QList<BookToc::Entry> MainWindow::bookTocEntries() const
{
    QList<BookToc::Entry> list = BookToc::entries(m_book->book(), 2, m_currentPath);
    BookExport::Options options = m_bookExportOptions;
    options.pagesOnly = true;
    const QPageLayout layout = m_printer ? m_printer->pageLayout() : QPageLayout();
    PrintLayout::Page page;
    page.sheetMm = PrintLayout::sheetSizeMm(layout);
    page.marginsMm = layout.margins(QPageLayout::Millimeter);
    const BookExport::Result r = BookExport::build(m_book->book(), options, page, m_editor->document()->defaultFont());
    QHash<int, int> chapterPage;
    QHash<QPair<int, QString>, int> headingPage;
    int chapter = -1;
    for (const BookExport::TocLine &line : r.toc) {
        if (line.level == 1) {
            chapter = line.chapter;
            if (chapter >= 0 && !chapterPage.contains(chapter)) {
                chapterPage.insert(chapter, line.page);
            }
        } else if (chapter >= 0) {
            const QPair<int, QString> key{chapter, line.text};
            if (!headingPage.contains(key)) {
                headingPage.insert(key, line.page);
            }
        }
    }
    for (BookToc::Entry &e : list) {
        e.page = e.level == 1 ? chapterPage.value(e.chapter) : headingPage.value({e.chapter, e.text});
    }
    return list;
}

bool MainWindow::refreshBookToc()
{
    if (m_markdownMode || !m_book || !m_book->hasBook() || !BookToc::hasToc(m_editor->document())) {
        return false;
    }
    const bool changed = BookToc::insertOrUpdate(m_editor->document(), nullptr, bookTocEntries());
    alignKeptTocTabs();
    return changed;
}

bool MainWindow::bookReadyForExport()
{
    if (!m_book || !m_book->hasBook() || m_book->book().count() == 0) {
        statusBar()->showMessage(QStringLiteral("Open a book with chapters first."), 4000);
        return false;
    }
    // The export reads the chapter files; unsaved changes would be left out.
    if (m_editor->document()->isModified() && !m_currentPath.isEmpty() && m_book->book().indexOf(m_currentPath) >= 0) {
        return maybeSave();
    }
    return true;
}

// Export Book opens in the folder used last time; the very first time, the
// book's own folder.
QString MainWindow::bookExportStartDir() const
{
    // The folder used last for this book (1.11.3: not one left from another book).
    const QString bookDir = m_book && m_book->hasBook() ? m_book->book().bookDir() : QString();
    const QSettings s;
    const QString last = s.value(QStringLiteral("export/bookDir")).toString();
    const QString lastFor = s.value(QStringLiteral("export/bookDirFor")).toString();
    if (!last.isEmpty() && QFileInfo(last).isDir()
        && (bookDir.isEmpty() || QDir::cleanPath(lastFor) == QDir::cleanPath(bookDir))) {
        return last;
    }
    return !bookDir.isEmpty() ? bookDir : openStartDir();
}

void MainWindow::rememberBookExportDir(const QString &exportedFile)
{
    QSettings s;
    s.setValue(QStringLiteral("export/bookDir"), QFileInfo(exportedFile).absolutePath());
    s.setValue(QStringLiteral("export/bookDirFor"), m_book && m_book->hasBook() ? m_book->book().bookDir() : QString());
}

QString MainWindow::openStartDir() const
{
    // The open book's folder, else the open file's, else the last used / Documents.
    if (m_book && m_book->hasBook() && QDir(m_book->book().bookDir()).exists()) {
        return m_book->book().bookDir();
    }
    if (!m_currentPath.isEmpty() && QFileInfo(m_currentPath).absoluteDir().exists()) {
        return QFileInfo(m_currentPath).absolutePath();
    }
    return documentsStartDir();
}

QString MainWindow::runBookExportPath(const QString &title, const QString &filter, const QString &suffix)
{
    QFileDialog dlg(this, title);
    setupNativeFileDialog(dlg);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setNameFilters({filter});
    dlg.setDefaultSuffix(suffix);
    const BookManifest &b = m_book->book();
    const QString dir = bookExportStartDir();
    dlg.setDirectory(dir);
    const QString name = b.title().trimmed().isEmpty() ? QFileInfo(b.bookDir()).fileName() : b.title().trimmed();
    dlg.selectFile(QDir(dir).filePath(name + QLatin1Char('.') + suffix));
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) {
        return QString();
    }
    const QString path = dlg.selectedFiles().constFirst();
    rememberBookExportDir(path);
    return path;
}

void MainWindow::exportBookPdf()
{
    if (!bookReadyForExport()) {
        return;
    }
    BookExportDialog opts(QStringLiteral("Export Book as PDF"), QStringLiteral("Export…"), m_bookExportOptions, this);
    if (opts.exec() != QDialog::Accepted) {
        return;
    }
    m_bookExportOptions = opts.options();
    const QString path = runBookExportPath(QStringLiteral("Export Book as PDF"), QStringLiteral("PDF (*.pdf)"),
                                           QStringLiteral("pdf"));
    if (!path.isEmpty()) {
        exportBookPdfTo(path, m_bookExportOptions);
    }
}

void MainWindow::exportBookEpub()
{
    if (!bookReadyForExport()) {
        return;
    }
    BookExportDialog opts(QStringLiteral("Export Book as EPUB"), QStringLiteral("Export…"), m_bookExportOptions, this);
    if (opts.exec() != QDialog::Accepted) {
        return;
    }
    m_bookExportOptions = opts.options();
    const QString path = runBookExportPath(QStringLiteral("Export Book as EPUB"), QStringLiteral("EPUB (*.epub)"),
                                           QStringLiteral("epub"));
    if (!path.isEmpty()) {
        exportBookEpubTo(path, m_bookExportOptions);
    }
}

void MainWindow::printBook()
{
    if (!bookReadyForExport()) {
        return;
    }
    BookExportDialog opts(QStringLiteral("Print Book"), QStringLiteral("Continue…"), m_bookExportOptions, this);
    if (opts.exec() != QDialog::Accepted) {
        return;
    }
    m_bookExportOptions = opts.options();
    QPrintDialog dlg(m_printer, this);
    dlg.setWindowTitle(QStringLiteral("Print Book"));
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    savePrinterSettings();
    printBookTo(m_printer, m_bookExportOptions);
}

int MainWindow::printBookTo(QPrinter *printer, const BookExport::Options &options)
{
    if (!printer || !m_book || !m_book->hasBook()) {
        return 0;
    }
    const QPageLayout layout = printer->pageLayout();
    PrintLayout::Page page;
    page.sheetMm = PrintLayout::sheetSizeMm(layout);
    page.marginsMm = layout.margins(QPageLayout::Millimeter);
    const BookExport::Result r = BookExport::build(m_book->book(), options, page, m_editor->document()->defaultFont());
    if (r.chapters == 0) {
        statusBar()->showMessage(QStringLiteral("No chapter of the book could be read."), 5000);
        return 0;
    }
    const int pages = BookExport::paint(*r.doc, printer, options);
    statusBar()->showMessage(QStringLiteral("Printed %1 pages%2").arg(pages).arg(
                                 r.skipped.isEmpty() ? QString()
                                                     : QStringLiteral(" (skipped: %1)").arg(r.skipped.join(QStringLiteral(", ")))),
                             5000);
    return pages;
}

bool MainWindow::exportBookPdfTo(const QString &path, const BookExport::Options &options, int *pages)
{
    if (!m_book || !m_book->hasBook()) {
        return false;
    }
    QString err;
    int n = 0;
    const bool ok = BookExport::writePdf(m_book->book(), options, m_printer->pageLayout(),
                                         m_editor->document()->defaultFont(), path, &n, &err);
    if (ok) {
        rememberBookExportDir(path);
    }
    if (pages) {
        *pages = n;
    }
    statusBar()->showMessage(ok ? QStringLiteral("Exported book PDF: %1 (%2 pages)").arg(QFileInfo(path).fileName()).arg(n)
                                : err,
                             5000);
    return ok;
}

bool MainWindow::exportBookEpubTo(const QString &path, const BookExport::Options &options)
{
    if (!m_book || !m_book->hasBook()) {
        return false;
    }
    QString err;
    QStringList skipped;
    BookExport::Options withFonts = options;
    withFonts.styleFonts = m_styleFonts; // EPUB CSS per style (1.15.0)
    const bool ok = BookExport::writeEpub(m_book->book(), withFonts, path, &err, &skipped);
    if (ok) {
        rememberBookExportDir(path);
    }
    statusBar()->showMessage(ok ? QStringLiteral("Exported book EPUB: %1").arg(QFileInfo(path).fileName()) : err, 5000);
    return ok;
}

