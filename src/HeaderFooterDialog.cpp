#include "HeaderFooterDialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

HeaderFooterDialog::HeaderFooterDialog(const DocumentMeta &meta, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Header & Footer"));
    setModal(true);
    resize(520, 320);

    auto makeRow = [this](QLineEdit **left, QLineEdit **center, QLineEdit **right,
                          const QString &l, const QString &c, const QString &r) {
        *left = new QLineEdit(this);
        *center = new QLineEdit(this);
        *right = new QLineEdit(this);
        (*left)->setText(l);
        (*center)->setText(c);
        (*right)->setText(r);
        (*left)->setPlaceholderText(QStringLiteral("Left"));
        (*center)->setPlaceholderText(QStringLiteral("Center"));
        (*right)->setPlaceholderText(QStringLiteral("Right"));
        for (QLineEdit *edit : {*left, *center, *right}) {
            connect(edit, &QLineEdit::selectionChanged, this, [this, edit]() {
                m_lastFocused = edit;
            });
            connect(edit, &QLineEdit::cursorPositionChanged, this, [this, edit](int, int) {
                m_lastFocused = edit;
            });
        }
        auto *row = new QHBoxLayout();
        row->addWidget(*left, 1);
        row->addWidget(*center, 1);
        row->addWidget(*right, 1);
        return row;
    };

    auto *headerBox = new QGroupBox(QStringLiteral("Header"), this);
    auto *headerForm = new QVBoxLayout(headerBox);
    headerForm->addLayout(makeRow(&m_headerLeft, &m_headerCenter, &m_headerRight,
                                  meta.headerLeft, meta.headerCenter, meta.headerRight));

    auto *footerBox = new QGroupBox(QStringLiteral("Footer"), this);
    auto *footerForm = new QVBoxLayout(footerBox);
    footerForm->addLayout(makeRow(&m_footerLeft, &m_footerCenter, &m_footerRight,
                                  meta.footerLeft, meta.footerCenter, meta.footerRight));

    auto *hint = new QLabel(
        QStringLiteral("Tokens: <code>{page}</code> = page number, "
                       "<code>{pages}</code> = page count. "
                       "Page numbers are off by default; Format → Page Numbers adds a footer number."),
        this);
    hint->setTextFormat(Qt::RichText);
    hint->setWordWrap(true);

    auto *tokenRow = new QHBoxLayout();
    auto *pageBtn = new QPushButton(QStringLiteral("Insert {page}"), this);
    auto *pagesBtn = new QPushButton(QStringLiteral("Insert {pages}"), this);
    auto *pageOfBtn = new QPushButton(QStringLiteral("Insert {page} of {pages}"), this);
    connect(pageBtn, &QPushButton::clicked, this, [this]() { insertToken(QStringLiteral("{page}")); });
    connect(pagesBtn, &QPushButton::clicked, this, [this]() { insertToken(QStringLiteral("{pages}")); });
    connect(pageOfBtn, &QPushButton::clicked, this, [this]() {
        insertToken(QStringLiteral("{page} of {pages}"));
    });
    tokenRow->addWidget(pageBtn);
    tokenRow->addWidget(pagesBtn);
    tokenRow->addWidget(pageOfBtn);
    tokenRow->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(headerBox);
    layout->addWidget(footerBox);
    layout->addWidget(hint);
    layout->addLayout(tokenRow);
    layout->addWidget(buttons);

    m_lastFocused = m_footerCenter;
    m_footerCenter->setFocus();
}

void HeaderFooterDialog::insertToken(const QString &token)
{
    QLineEdit *edit = m_lastFocused ? m_lastFocused : m_footerCenter;
    if (!edit) {
        return;
    }
    edit->insert(token);
    edit->setFocus();
}

void HeaderFooterDialog::applyTo(DocumentMeta *meta) const
{
    if (!meta) {
        return;
    }
    meta->headerLeft = m_headerLeft->text();
    meta->headerCenter = m_headerCenter->text();
    meta->headerRight = m_headerRight->text();
    meta->footerLeft = m_footerLeft->text();
    meta->footerCenter = m_footerCenter->text();
    meta->footerRight = m_footerRight->text();
}
