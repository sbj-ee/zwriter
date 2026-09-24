#pragma once

#include "DocumentMeta.hpp"

#include <QDialog>

class QLineEdit;

// Lean Insert → Header & Footer… (left / center / right per band).
class HeaderFooterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HeaderFooterDialog(const DocumentMeta &meta, QWidget *parent = nullptr);

    void applyTo(DocumentMeta *meta) const;

private:
    void insertToken(const QString &token);

    QLineEdit *m_headerLeft = nullptr;
    QLineEdit *m_headerCenter = nullptr;
    QLineEdit *m_headerRight = nullptr;
    QLineEdit *m_footerLeft = nullptr;
    QLineEdit *m_footerCenter = nullptr;
    QLineEdit *m_footerRight = nullptr;
    QLineEdit *m_lastFocused = nullptr;
};
