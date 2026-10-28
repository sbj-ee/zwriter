#include "PageSettingsDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

PageSettingsDialog::PageSettingsDialog(PageSetup::Unit unit, const QPageSize &current,
                                       QPageSize::PageSizeId newDocDefault, QWidget *parent)
    : QDialog(parent)
    , m_current(current)
{
    setObjectName(QStringLiteral("pageSettingsDialog"));
    setWindowTitle(QStringLiteral("Page Size & Ruler Units"));
    auto *form = new QFormLayout;

    m_unit = new QComboBox(this);
    m_unit->setObjectName(QStringLiteral("rulerUnitCombo"));
    m_unit->addItem(QStringLiteral("Inches (in)"), int(PageSetup::Unit::Inch));
    m_unit->addItem(QStringLiteral("Centimetres (cm)"), int(PageSetup::Unit::Cm));
    m_unit->setCurrentIndex(unit == PageSetup::Unit::Cm ? 1 : 0);
    form->addRow(QStringLiteral("Ruler &units:"), m_unit);

    auto fill = [](QComboBox *c) {
        for (QPageSize::PageSizeId id : PageSetup::offeredSizes()) {
            c->addItem(PageSetup::sizeLabel(id), int(id));
        }
    };
    m_doc = new QComboBox(this);
    m_doc->setObjectName(QStringLiteral("documentSizeCombo"));
    fill(m_doc);
    int at = m_doc->findData(int(current.id()));
    if (at < 0) { // a size from a file that is not in the list: keep it on offer
        const QSizeF mm = current.size(QPageSize::Millimeter);
        m_doc->insertItem(0, QStringLiteral("%1 (%2 × %3 mm)")
                                 .arg(current.id() == QPageSize::Custom ? QStringLiteral("Custom") : current.name())
                                 .arg(mm.width(), 0, 'f', 1).arg(mm.height(), 0, 'f', 1),
                          -1);
        at = 0;
    }
    m_doc->setCurrentIndex(at);
    form->addRow(QStringLiteral("&This document:"), m_doc);

    m_newDoc = new QComboBox(this);
    m_newDoc->setObjectName(QStringLiteral("newDocumentSizeCombo"));
    fill(m_newDoc);
    m_newDoc->setCurrentIndex(qMax(0, m_newDoc->findData(int(newDocDefault))));
    form->addRow(QStringLiteral("&New documents:"), m_newDoc);

    auto *note = new QLabel(QStringLiteral("The page size is saved in each .odt file. Documents that "
                                           "do not store one (older files, Markdown, text) open on A4. "
                                           "Margins and orientation stay in File > Page Setup…"),
                            this);
    note->setWordWrap(true);
    note->setEnabled(false);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *v = new QVBoxLayout(this);
    v->addLayout(form);
    v->addWidget(note);
    v->addWidget(buttons);
    setMinimumWidth(420);
}

PageSetup::Unit PageSettingsDialog::unit() const
{
    return static_cast<PageSetup::Unit>(m_unit->currentData().toInt());
}

QPageSize PageSettingsDialog::documentSize() const
{
    const int id = m_doc->currentData().toInt();
    return id < 0 ? m_current : QPageSize(static_cast<QPageSize::PageSizeId>(id));
}

QPageSize::PageSizeId PageSettingsDialog::newDocumentSize() const
{
    return static_cast<QPageSize::PageSizeId>(m_newDoc->currentData().toInt());
}
