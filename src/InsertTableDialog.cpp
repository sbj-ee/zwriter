#include "InsertTableDialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

InsertTableDialog::InsertTableDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Insert Table"));
    setModal(true);
    resize(280, 140);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Choose table size:"), this));

    auto *form = new QFormLayout;
    m_rows = new QSpinBox(this);
    m_rows->setRange(1, 40);
    m_rows->setValue(3);
    m_cols = new QSpinBox(this);
    m_cols->setRange(1, 20);
    m_cols->setValue(3);
    form->addRow(QStringLiteral("Rows:"), m_rows);
    form->addRow(QStringLiteral("Columns:"), m_cols);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

int InsertTableDialog::rows() const
{
    return m_rows->value();
}

int InsertTableDialog::columns() const
{
    return m_cols->value();
}
