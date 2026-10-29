#pragma once

#include <QColor>
#include <functional>
#include <QMarginsF>
#include <QSizeF>
#include <QTextEdit>
#include <QWidget>

class QTimer;
class QVBoxLayout;

// QTextEdit resets its document's page size to "widget width, unpaginated"
// whenever it relayouts (resize, style/font change). This subclass re-applies a
// fixed page size afterwards, so the document really paginates (A4 pages with
// margins) and page count / page breaks work.
class QPainter;
class PageTextEdit : public QTextEdit
{
    Q_OBJECT

public:
    using QTextEdit::QTextEdit;

    // Invalid (default) size = leave Qt's continuous, unpaginated layout alone.
    void setFixedPageSize(const QSizeF &size);
    QSizeF fixedPageSize() const { return m_pageSize; }

    // Continuous view: the document keeps the page margins in its root frame;
    // this adds a viewport inset on top so the reading column matches the
    // classic look (about 20 % of the width each side, 52 px at the top).
    void setContinuousInset(bool on, const QMarginsF &documentMargins);

    // Continuous view only: cap the text column at this many pixels (e.g.
    // ~66 characters of the body font) and centre it, instead of the default
    // 60 %-of-the-window column. 0 = no cap (default).
    void setMaxColumnWidth(qreal px);
    qreal maxColumnWidth() const { return m_maxColumn; }

    // View zoom (View > Zoom, Fit Page to Width). The document keeps its
    // true-size layout, so pages break exactly as they print; only painting
    // and pointer positions are scaled. In Full Page view the widget must be
    // sized to the zoomed page by the caller; in the continuous views (and
    // Markdown) the text rewraps to the zoomed column and the editor scrolls
    // itself, with scroll bar values in document pixels. cursorRect() and
    // cursorForPosition() stay in unzoomed document coordinates: convert with
    // toView() / toDocument().
    void setZoom(qreal zoom);
    qreal zoom() const { return m_zoom; }
    // Dot leaders across the tab of table-of-contents lines (painted only).
    void paintLeaders(QPainter *p, const QRectF &docClip) const;
    bool isZoomed() const { return m_zoom != 1.0; }
    QPoint toDocument(const QPoint &viewportPos) const;
    QRect toView(const QRectF &documentRect) const;

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

    // Paste / drop of pictures (image data, or local image files). Returns how
    // many were inserted; problems go out through imageNote().
    int insertImagesFromMimeData(const QMimeData *source);
    bool canInsertFromMimeData(const QMimeData *source) const override;
    // Plain-text (Markdown) mode: pictures go through this instead (MainWindow
    // copies them next to the file and types a link). Returns true if inserted.
    void setPlainImageHandler(std::function<bool(const QByteArray &)> handler)
    {
        m_plainImageHandler = std::move(handler);
    }

signals:
    // A status-bar note about a pasted / dropped picture (refused, scaled down).
    void imageNote(const QString &message);

protected:
    // Rich paste keeps structure and emphasis but drops hard-coded text and
    // background colours, which would clash with the theme (text follows it).
    void insertFromMimeData(const QMimeData *source) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void reapplyPageSize();
    void updateInset();
    void restartCaretBlink();
    qreal continuousLayoutWidth() const;
    void reapplyContinuousWidth();
    void adjustZoomedScrollBars();
    void ensureZoomedCaretVisible();
    QPointF scrollOffset() const;

    QSizeF m_pageSize; // invalid until set
    bool m_continuousInset = false;
    QMarginsF m_documentMargins;
    qreal m_maxColumn = 0.0;
    qreal m_zoom = 1.0;
    bool m_adjustingBars = false;
    std::function<bool(const QByteArray &)> m_plainImageHandler;
    // QTextEdit's own caret blink state is private, so zoomed painting runs
    // its own blink.
    QTimer *m_blinkTimer = nullptr;
    bool m_caretOn = true;
};

// Holds the paper. In "paper" mode the page is centred with breathing room and
// a soft shadow painted cheaply here (a QGraphicsEffect would render the whole
// multi-page sheet offscreen); otherwise the page simply fills the canvas.
class PageCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit PageCanvas(QWidget *page, QWidget *parent = nullptr);
    void setPaperMode(bool paper);
    void setPageBorderColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QWidget *m_page;
    QVBoxLayout *m_layout;
    bool m_paper = false;
    QColor m_border{0xd8, 0xd2, 0xc6};
};
