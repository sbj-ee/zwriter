#pragma once

// File > Page Size & Ruler Units… (1.12.0): the ruler's units, this
// document's page size and the size new documents start with.

#include "PageSetup.hpp"

#include <QDialog>
#include <QPageSize>

class QComboBox;
class QLabel;

class PageSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // `current` is the open document's page size (may be a custom size).
    PageSettingsDialog(PageSetup::Unit unit, const QPageSize &current,
                       QPageSize::PageSizeId newDocDefault, QWidget *parent = nullptr);

    PageSetup::Unit unit() const;
    QPageSize documentSize() const;          // unchanged custom size when not re-picked
    QPageSize::PageSizeId newDocumentSize() const;

    QComboBox *unitCombo() const { return m_unit; }
    QComboBox *documentCombo() const { return m_doc; }
    QComboBox *newDocCombo() const { return m_newDoc; }

private:
    QComboBox *m_unit;
    QComboBox *m_doc;
    QComboBox *m_newDoc;
    QPageSize m_current;
};
