#include "PropertiesDialog.hpp"

#include <QDialogButtonBox>
#include <QDateTimeEdit>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>

PropertiesDialog::PropertiesDialog(const DocumentMeta &meta, QWidget *parent)
    : QDialog(parent)
    , m_source(meta)
{
    setWindowTitle(QStringLiteral("Document Properties"));
    setModal(true);
    resize(420, 180);

    m_author = new QLineEdit(this);
    m_author->setText(meta.author);

    m_created = new QDateTimeEdit(this);
    m_created->setCalendarPopup(true);
    m_created->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_created->setDateTime(meta.created.toLocalTime());

    m_edited = new QDateTimeEdit(this);
    m_edited->setCalendarPopup(true);
    m_edited->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_edited->setDateTime(meta.lastEdited.toLocalTime());

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("Author"), m_author);
    form->addRow(QStringLiteral("Created"), m_created);
    form->addRow(QStringLiteral("Last edit"), m_edited);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

DocumentMeta PropertiesDialog::meta() const
{
    DocumentMeta m = m_source;
    m.author = m_author->text().trimmed();
    m.created = m_created->dateTime().toUTC();
    m.lastEdited = m_edited->dateTime().toUTC();
    m.ensureDefaults();
    return m;
}
