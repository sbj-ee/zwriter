#pragma once

// View → Ruler (off by default): a strip above the page showing the current
// paragraph's indents in centimetres. Drag the top marker (first line), the
// bottom-left marker (left indent) or the right marker (right indent).

#include <QWidget>

class QTextEdit;

class Ruler : public QWidget
{
    Q_OBJECT

public:
    enum class Marker { None, FirstLine, Left, Right };
    explicit Ruler(QTextEdit *editor, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    // Marker position in ruler coordinates (for tests).
    int markerX(Marker m) const;
    int originX() const;      // x of the text column's left edge
    qreal columnWidth() const; // px, document coordinates
    qreal zoom() const;        // View > Fit Page to Width scale (1 = none)
    Marker hitTest(const QPoint &p) const { return hit(p); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    Marker hit(const QPoint &p) const;
    void dragTo(int x);
    QTextEdit *m_editor;
    Marker m_drag = Marker::None;
    bool m_dragChanged = false; // a drag's first change opens the undo step, later ones join it
};
