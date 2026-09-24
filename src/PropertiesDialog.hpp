#pragma once

#include "DocumentMeta.hpp"

#include <QDialog>

class QLineEdit;
class QDateTimeEdit;

// Lean File → Properties / Document info dialog.
class PropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PropertiesDialog(const DocumentMeta &meta, QWidget *parent = nullptr);

    DocumentMeta meta() const;

private:
    QLineEdit *m_author = nullptr;
    QDateTimeEdit *m_created = nullptr;
    QDateTimeEdit *m_edited = nullptr;
};
