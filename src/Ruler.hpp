#pragma once

// View → Ruler (off by default): a strip above the page showing the current
// paragraph's indents in inches or centimetres (PageSetup::rulerUnit(), labelled
// "in" / "cm" at the left end; 1.12.0). Drag the top marker (first line), the
// bottom-left marker (left indent) or the right marker (right indent).

#include "PageSetup.hpp"

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
    PageSetup::Unit unit() const { return m_unit; }
    void setUnit(PageSetup::Unit unit);
    QString unitLabel() const { return PageSetup::unitSuffix(m_unit); }
    // x (ruler coordinates) of the tick numbered `n` units from the text's left edge.
    int unitX(int n) const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    Marker hit(const QPoint &p) const;
    void dragTo(int x);
    QTextEdit *m_editor;
    PageSetup::Unit m_unit = PageSetup::Unit::Inch;
    Marker m_drag = Marker::None;
    bool m_dragChanged = false; // a drag's first change opens the undo step, later ones join it
};
