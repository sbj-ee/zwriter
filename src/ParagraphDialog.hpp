#pragma once

// Format → Paragraph…: indents (left, right, first line / hanging), spacing
// before / after, line spacing and alignment of the selected paragraphs.

#include <QDialog>
#include <QTextBlockFormat>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

class ParagraphDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ParagraphDialog(const QTextBlockFormat &initial, QWidget *parent = nullptr);
    QTextBlockFormat format() const; // geometry only (see ParagraphStyles::setGeometry)

    // For tests.
    QDoubleSpinBox *leftBox() const { return m_left; }
    QDoubleSpinBox *rightBox() const { return m_right; }
    QComboBox *specialBox() const { return m_special; }
    QDoubleSpinBox *byBox() const { return m_by; }
    QSpinBox *beforeBox() const { return m_before; }
    QSpinBox *afterBox() const { return m_after; }
    QComboBox *lineBox() const { return m_line; }
    QSpinBox *percentBox() const { return m_percent; }
    QComboBox *alignBox() const { return m_align; }

private:
    void syncEnabled();
    QDoubleSpinBox *m_left = nullptr;
    QDoubleSpinBox *m_right = nullptr;
    QComboBox *m_special = nullptr;
    QDoubleSpinBox *m_by = nullptr;
    QSpinBox *m_before = nullptr;
    QSpinBox *m_after = nullptr;
    QComboBox *m_line = nullptr;
    QSpinBox *m_percent = nullptr;
    QComboBox *m_align = nullptr;
};
