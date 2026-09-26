#pragma once

#include <QColor>
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

    // View zoom (View > Fit Page to Width). The document keeps its true-size
    // layout, so lines and pages break exactly as they print; only painting
    // and pointer positions are scaled. The widget must be sized to the zoomed
    // page by the caller. cursorRect() and cursorForPosition() stay in
    // unzoomed document coordinates: convert with toView() / toDocument().
    void setZoom(qreal zoom);
    qreal zoom() const { return m_zoom; }
    bool isZoomed() const { return m_zoom != 1.0; }
    QPoint toDocument(const QPoint &viewportPos) const;
    QRect toView(const QRectF &documentRect) const;

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

protected:
    // Rich paste keeps structure and emphasis but drops hard-coded text and
    // background colours, which would clash with the theme (text follows it).
    void insertFromMimeData(const QMimeData *source) override;
    void resizeEvent(QResizeEvent *event) override;
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

    QSizeF m_pageSize; // invalid until set
    bool m_continuousInset = false;
    QMarginsF m_documentMargins;
    qreal m_zoom = 1.0;
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
