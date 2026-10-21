#include "BookExportDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

BookExportDialog::BookExportDialog(const QString &title, const QString &okText,
                                   const BookExport::Options &options, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("bookExportDialog"));
    setWindowTitle(title);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(QStringLiteral("All chapters of the book, in book order. Include:"), this);
    layout->addWidget(intro);
    m_titlePage = new QCheckBox(QStringLiteral("&Title page"), this);
    m_titlePage->setObjectName(QStringLiteral("bookExportTitlePage"));
    m_toc = new QCheckBox(QStringLiteral("Table of &contents"), this);
    m_toc->setObjectName(QStringLiteral("bookExportToc"));
    m_figures = new QCheckBox(QStringLiteral("&Figure numbers (Figure <chapter>.<n>)"), this);
    m_figures->setObjectName(QStringLiteral("bookExportFigures"));
    m_titlePage->setChecked(options.titlePage);
    m_toc->setChecked(options.toc);
    m_figures->setChecked(options.figureNumbers);
    for (QCheckBox *b : {m_titlePage, m_toc, m_figures}) {
        layout->addWidget(b);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(okText);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    m_titlePage->setFocus();
}

BookExport::Options BookExportDialog::options() const
{
    BookExport::Options o;
    o.titlePage = m_titlePage->isChecked();
    o.toc = m_toc->isChecked();
    o.figureNumbers = m_figures->isChecked();
    return o;
}
