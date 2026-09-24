#pragma once

#include <QDialog>

class QSpinBox;

// Lean rows × cols picker for Insert → Table (FocusWriter-class, not Word).
class InsertTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InsertTableDialog(QWidget *parent = nullptr);

    int rows() const;
    int columns() const;

private:
    QSpinBox *m_rows = nullptr;
    QSpinBox *m_cols = nullptr;
};
