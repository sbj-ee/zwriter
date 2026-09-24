#include "FindReplaceBar.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

FindReplaceBar::FindReplaceBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("findReplaceBar"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 6);
    outer->setSpacing(4);

    auto *findRow = new QHBoxLayout;
    findRow->setSpacing(6);
    findRow->addWidget(new QLabel(QStringLiteral("Find:"), this));
    m_findEdit = new QLineEdit(this);
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setPlaceholderText(QStringLiteral("Search…"));
    findRow->addWidget(m_findEdit, 1);

    m_findPrevBtn = new QPushButton(QStringLiteral("Prev"), this);
    m_findNextBtn = new QPushButton(QStringLiteral("Next"), this);
    findRow->addWidget(m_findPrevBtn);
    findRow->addWidget(m_findNextBtn);

    m_caseBox = new QCheckBox(QStringLiteral("Match case"), this);
    findRow->addWidget(m_caseBox);

    m_closeBtn = new QPushButton(QStringLiteral("×"), this);
    m_closeBtn->setFixedWidth(28);
    m_closeBtn->setToolTip(QStringLiteral("Close (Esc)"));
    findRow->addWidget(m_closeBtn);
    outer->addLayout(findRow);

    m_replaceRow = new QWidget(this);
    auto *replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(6);
    replaceLayout->addWidget(new QLabel(QStringLiteral("Replace:"), m_replaceRow));
    m_replaceEdit = new QLineEdit(m_replaceRow);
    m_replaceEdit->setClearButtonEnabled(true);
    m_replaceEdit->setPlaceholderText(QStringLiteral("Replace with…"));
    replaceLayout->addWidget(m_replaceEdit, 1);
    m_replaceBtn = new QPushButton(QStringLiteral("Replace"), m_replaceRow);
    m_replaceAllBtn = new QPushButton(QStringLiteral("Replace All"), m_replaceRow);
    replaceLayout->addWidget(m_replaceBtn);
    replaceLayout->addWidget(m_replaceAllBtn);
    outer->addWidget(m_replaceRow);

    connect(m_findNextBtn, &QPushButton::clicked, this, &FindReplaceBar::findNext);
    connect(m_findPrevBtn, &QPushButton::clicked, this, &FindReplaceBar::findPrev);
    connect(m_replaceBtn, &QPushButton::clicked, this, &FindReplaceBar::replaceOne);
    connect(m_replaceAllBtn, &QPushButton::clicked, this, &FindReplaceBar::replaceAll);
    connect(m_closeBtn, &QPushButton::clicked, this, &FindReplaceBar::closeRequested);
    connect(m_findEdit, &QLineEdit::returnPressed, this, &FindReplaceBar::findNext);
    connect(m_replaceEdit, &QLineEdit::returnPressed, this, &FindReplaceBar::replaceOne);

    setReplaceVisible(false);
    hide();
}

void FindReplaceBar::setReplaceVisible(bool visible)
{
    m_replaceRow->setVisible(visible);
}

void FindReplaceBar::showFind()
{
    setReplaceVisible(false);
    show();
    raise();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void FindReplaceBar::showReplace()
{
    setReplaceVisible(true);
    show();
    raise();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void FindReplaceBar::setFindText(const QString &text)
{
    m_findEdit->setText(text);
}

QString FindReplaceBar::findText() const
{
    return m_findEdit->text();
}

QString FindReplaceBar::replaceText() const
{
    return m_replaceEdit->text();
}

bool FindReplaceBar::caseSensitive() const
{
    return m_caseBox->isChecked();
}

void FindReplaceBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void FindReplaceBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit closeRequested();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F3) {
        if (event->modifiers() & Qt::ShiftModifier) {
            emit findPrev();
        } else {
            emit findNext();
        }
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
