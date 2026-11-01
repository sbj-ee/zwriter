// MainWindow, part: View: chrome hide-away, full screen, full-page view, zoom, page metrics, page guides.
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

// Hidden-away chrome keeps its place: the bar is drawn fully transparent and
// lets clicks through, so the editor and the chapter list do not move up and
// down under the mouse (a click aimed at a chapter could land on another).
static void veilBar(QWidget *bar, bool veiled)
{
    if (!bar) {
        return;
    }
    bar->setAttribute(Qt::WA_TransparentForMouseEvents, veiled);
    if (veiled) {
        auto *fx = qobject_cast<QGraphicsOpacityEffect *>(bar->graphicsEffect());
        if (!fx) {
            fx = new QGraphicsOpacityEffect(bar);
            bar->setGraphicsEffect(fx);
        }
        fx->setOpacity(0.0);
    } else if (bar->graphicsEffect()) {
        bar->setGraphicsEffect(nullptr);
    }
    for (QWidget *child : bar->findChildren<QWidget *>()) {
        child->setAttribute(Qt::WA_TransparentForMouseEvents, veiled);
    }
}

void MainWindow::setChromeVisible(bool visible)
{
    m_chromeVisible = visible;
    statusBar()->setVisible(true);
    veilBar(statusBar(), !visible);
    if (m_formatBar) {
        // Markdown mode has no rich formatting: the toolbar never shows.
        m_formatBar->setVisible(!m_markdownMode);
        veilBar(m_formatBar, !visible);
    }
    if (menuBar()) {
        menuBar()->setVisible(true);
        veilBar(menuBar(), !visible);
    }
}

void MainWindow::swapEditorAndChapters()
{
    QListWidget *list = m_book ? m_book->view() : nullptr;
    const bool listUsable = list && list->isVisibleTo(this) && list->isEnabled();
    if (m_editor->hasFocus() || !listUsable) {
        if (listUsable) {
            list->setFocus(Qt::TabFocusReason);
        } else {
            m_editor->setFocus(Qt::TabFocusReason);
        }
    } else {
        m_editor->setFocus(Qt::TabFocusReason);
    }
}

bool MainWindow::chromeShown() const
{
    return m_chromeVisible;
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
    QSettings().setValue(QStringLiteral("window/fullScreen"), isFullScreen());
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_restoreFullScreen) {
        // Full screen from the last session (1.17.0), applied after the first
        // show so the window manager sees a mapped window and the normal
        // geometry restored above stays the one to return to.
        m_restoreFullScreen = false;
        QTimer::singleShot(0, this, [this]() {
            if (!isFullScreen()) {
                showFullScreen();
            }
        });
    }
}

void MainWindow::toggleFullPageView()
{
    if (m_markdownMode) {
        syncViewActions(); // Markdown documents are always a continuous strip
        return;
    }
    m_fullPageView = m_fullPageViewAction && m_fullPageViewAction->isChecked();
    applyTheme();
    applyFullPageView();
    saveSettings();
    syncViewActions();
    if (m_pageGuides) {
        m_editor->viewport()->update();
    }
}

void MainWindow::setViewZoomPercent(int percent)
{
    percent = ViewZoom::clampPercent(percent);
    const bool changed = percent != m_viewZoomPercent || m_fitPageWidth;
    m_viewZoomPercent = percent;
    m_fitPageWidth = false; // a chosen zoom replaces Fit Page to Width
    if (changed) {
        if (m_fullPageView) {
            updateFullPageGeometry();
        } else if (m_editor) {
            // Continuous and Markdown views (1.14.1): the text rewraps to the
            // zoomed column; the document itself is untouched.
            m_editor->setZoom(percent / 100.0);
            if (m_ruler) {
                m_ruler->update();
            }
        }
        centerCaret();
        saveSettings();
    }
    syncViewActions();
}

int MainWindow::shownZoomPercent() const
{
    return (m_fitPageWidth && m_fullPageView && m_editor) ? qRound(m_editor->zoom() * 100)
                                                          : m_viewZoomPercent;
}

void MainWindow::syncZoomControls()
{
    // 1.14.1: zoom works in every view (Full Page, Continuous, Markdown), so
    // the menu items and the status-bar box are always there and enabled.
    // At the 50 % / 300 % limits a further step is simply a no-op.
    const int shown = shownZoomPercent();
    if (m_zoomInAction) {
        m_zoomInAction->setEnabled(true);
        m_zoomOutAction->setEnabled(true);
        m_zoomResetAction->setEnabled(true);
    }
    if (m_zoomCombo) {
        const QSignalBlocker b(m_zoomCombo);
        m_zoomCombo->setEnabled(true);
        m_zoomCombo->setVisible(true);
        const int idx = m_zoomCombo->findData(shown);
        if (idx >= 0) {
            m_zoomCombo->setCurrentIndex(idx);
        }
        m_zoomCombo->setEditText(QStringLiteral("%1%").arg(shown));
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
    if (!m_fullPageView || !m_pageScroll) {
        return 1.0;
    }
    if (!m_fitPageWidth) {
        return m_viewZoomPercent / 100.0; // View > Zoom (display only)
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
    if (m_markdownMode) {
        // Markdown has no pages: the reading column comes from the viewport
        // inset alone (PageTextEdit::setMaxColumnWidth), so the root frame
        // carries no page margins.
        setRootFrameMargins(0, 0, 0, 0, 0);
        m_editor->setContinuousInset(true, QMarginsF());
    } else {
        const QMarginsF m = pageMarginsPx();
        setRootFrameMargins(m.left(), m.top(), m.right(), m.bottom(), 0);
        m_editor->setContinuousInset(true, m);
    }
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
        clearDocumentPageMetrics();
        m_editor->setZoom(m_viewZoomPercent / 100.0); // View zoom applies here too (1.14.1)
        m_editor->viewport()->update();
    }
    updatePageLabel();
}

void MainWindow::togglePageGuides()
{
    m_pageGuides = m_pageGuidesAction && m_pageGuidesAction->isChecked();
    QSettings().setValue(QStringLiteral("view/pageGuides"), m_pageGuides);
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
        // Continuous view: the guides mark where the text column really is.
        // They used to sit at a fixed tenth of the viewport, which ignored the
        // page margins the text is laid out with and the viewport inset that
        // centres the column, so text started left of the left guide (worse in
        // wide windows / with the sidebar). Painting is in document units
        // (the painter is scaled by the zoom), so the height is too.
        const QPair<qreal, qreal> col = pageGuideColumn();
        const qreal zoom = qMax(0.01, m_editor->zoom());
        const qreal h = vp->height() / zoom;
        // The right edge of the column is the viewport's last pixel column
        // when the root frame has no margins; keep that line on screen.
        const qreal right = qMin(col.second, (vp->width() - 1) / zoom);
        painter.drawLine(QPointF(col.first, 0), QPointF(col.first, h));
        painter.drawLine(QPointF(right, 0), QPointF(right, h));
    }
}

QPair<qreal, qreal> MainWindow::pageGuideColumn() const
{
    if (!m_editor || !m_editor->document()) {
        return {0.0, 0.0};
    }
    const QTextDocument *doc = m_editor->document();
    const QTextFrameFormat fmt = doc->rootFrame()->frameFormat();
    const qreal dm = doc->documentMargin();
    const qreal zoom = qMax(0.01, m_editor->zoom());
    const bool paged = m_fullPageView && doc->pageSize().height() > 1.0;
    const qreal width = paged ? doc->pageSize().width() : m_editor->viewport()->width() / zoom;
    const qreal left = fmt.leftMargin() + fmt.border() + fmt.padding() + dm;
    const qreal right = width - fmt.rightMargin() - fmt.border() - fmt.padding() - dm;
    return {left, qMax(left, right)};
}

