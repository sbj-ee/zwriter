#include "SymbolDialog.hpp"

#include "Symbols.hpp"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kColumns = 12;
}

SymbolDialog::SymbolDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Insert Symbol"));
    setObjectName(QStringLiteral("symbolDialog"));
    auto *outer = new QVBoxLayout(this);

    m_recentBox = new QGroupBox(QStringLiteral("Recently used"), this);
    m_recentGrid = new QGridLayout(m_recentBox);
    m_recentGrid->setSpacing(2);
    outer->addWidget(m_recentBox);

    auto addSection = [&](const QString &title, const QList<Symbols::Symbol> &list, const QString &section) {
        auto *box = new QGroupBox(title, this);
        auto *grid = new QGridLayout(box);
        grid->setSpacing(2);
        for (int i = 0; i < list.size(); ++i) {
            const Symbols::Symbol &s = list.at(i);
            grid->addWidget(makeButton(s.text, QStringLiteral("%1 (\\%2)").arg(s.label, s.name), section),
                            i / kColumns, i % kColumns);
        }
        outer->addWidget(box);
    };
    addSection(QStringLiteral("Greek"), Symbols::greek(), QStringLiteral("greek"));
    addSection(QStringLiteral("Engineering"), Symbols::engineering(), QStringLiteral("ee"));

    auto *hint = new QLabel(QStringLiteral("Tip: type \\alpha, \\Omega, \\mu, \\deg, \\pm … then Space or Tab."), this);
    hint->setEnabled(false);
    outer->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    refreshRecent();
}

QToolButton *SymbolDialog::makeButton(const QString &text, const QString &tip, const QString &section)
{
    auto *b = new QToolButton(this);
    b->setText(text);
    b->setToolTip(tip);
    b->setObjectName(QStringLiteral("sym-%1-%2").arg(section, text));
    b->setFixedSize(34, 34);
    QFont f = b->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    b->setFont(f);
    b->setAutoRaise(false);
    connect(b, &QToolButton::clicked, this, [this, text] { choose(text); });
    return b;
}

void SymbolDialog::choose(const QString &text)
{
    Symbols::addRecent(text);
    emit symbolChosen(text);
    refreshRecent();
}

void SymbolDialog::refreshRecent()
{
    while (QLayoutItem *item = m_recentGrid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
            w->setObjectName(QString());
            w->hide();
        }
        delete item;
    }
    const QStringList r = Symbols::recent();
    if (r.isEmpty()) {
        auto *none = new QLabel(QStringLiteral("Symbols you insert appear here."), m_recentBox);
        none->setEnabled(false);
        m_recentGrid->addWidget(none, 0, 0);
        return;
    }
    for (int i = 0; i < r.size(); ++i) {
        m_recentGrid->addWidget(makeButton(r.at(i), Symbols::describe(r.at(i)), QStringLiteral("recent")),
                                0, i);
    }
    m_recentGrid->setColumnStretch(kColumns, 1);
}

QToolButton *SymbolDialog::buttonFor(const QString &section, const QString &text) const
{
    return findChild<QToolButton *>(QStringLiteral("sym-%1-%2").arg(section, text));
}
