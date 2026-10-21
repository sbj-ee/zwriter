#include "ParagraphDialog.hpp"

#include "ParagraphStyles.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace ParagraphStyles;

namespace {
QDoubleSpinBox *cmBox(QWidget *parent, qreal min = 0.0)
{
    auto *b = new QDoubleSpinBox(parent);
    b->setRange(min, 20.0);
    b->setDecimals(2);
    b->setSingleStep(0.25);
    b->setSuffix(QStringLiteral(" cm"));
    return b;
}
QSpinBox *ptBox(QWidget *parent)
{
    auto *b = new QSpinBox(parent);
    b->setRange(0, 144);
    b->setSuffix(QStringLiteral(" pt"));
    return b;
}
} // namespace

ParagraphDialog::ParagraphDialog(const QTextBlockFormat &f, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("paragraphDialog"));
    setWindowTitle(QStringLiteral("Paragraph"));
    auto *top = new QVBoxLayout(this);

    auto *indents = new QGroupBox(QStringLiteral("Indents"), this);
    auto *fi = new QFormLayout(indents);
    m_left = cmBox(indents);
    m_right = cmBox(indents);
    fi->addRow(QStringLiteral("&Left:"), m_left);
    fi->addRow(QStringLiteral("&Right:"), m_right);
    auto *specialRow = new QHBoxLayout;
    m_special = new QComboBox(indents);
    m_special->addItems({QStringLiteral("None"), QStringLiteral("First line"), QStringLiteral("Hanging")});
    m_by = cmBox(indents);
    specialRow->addWidget(m_special);
    specialRow->addWidget(m_by);
    fi->addRow(QStringLiteral("&Special:"), specialRow);
    top->addWidget(indents);

    auto *spacing = new QGroupBox(QStringLiteral("Spacing"), this);
    auto *fs = new QFormLayout(spacing);
    m_before = ptBox(spacing);
    m_after = ptBox(spacing);
    fs->addRow(QStringLiteral("&Before:"), m_before);
    fs->addRow(QStringLiteral("A&fter:"), m_after);
    auto *lineRow = new QHBoxLayout;
    m_line = new QComboBox(spacing);
    m_line->addItem(QStringLiteral("Single"), 100);
    m_line->addItem(QStringLiteral("1.15"), 115);
    m_line->addItem(QStringLiteral("1.5 lines"), 150);
    m_line->addItem(QStringLiteral("Double"), 200);
    m_line->addItem(QStringLiteral("Proportional"), 0);
    m_percent = new QSpinBox(spacing);
    m_percent->setRange(50, 400);
    m_percent->setSuffix(QStringLiteral(" %"));
    lineRow->addWidget(m_line);
    lineRow->addWidget(m_percent);
    fs->addRow(QStringLiteral("Li&ne spacing:"), lineRow);
    top->addWidget(spacing);

    auto *fa = new QFormLayout;
    m_align = new QComboBox(this);
    m_align->addItem(QStringLiteral("Left"), int(Qt::AlignLeft));
    m_align->addItem(QStringLiteral("Centre"), int(Qt::AlignHCenter));
    m_align->addItem(QStringLiteral("Right"), int(Qt::AlignRight));
    m_align->addItem(QStringLiteral("Justified"), int(Qt::AlignJustify));
    fa->addRow(QStringLiteral("&Alignment:"), m_align);
    top->addLayout(fa);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    top->addWidget(buttons);

    // Initial values.
    m_left->setValue(pxToCm(f.leftMargin()));
    m_right->setValue(pxToCm(f.rightMargin()));
    const qreal ind = f.textIndent();
    m_special->setCurrentIndex(ind > 0 ? 1 : ind < 0 ? 2 : 0);
    m_by->setValue(pxToCm(qAbs(ind)));
    m_before->setValue(qRound(pxToPt(f.topMargin())));
    m_after->setValue(qRound(pxToPt(f.bottomMargin())));
    int pct = 100;
    if (f.lineHeightType() == QTextBlockFormat::ProportionalHeight && f.lineHeight() > 0) {
        pct = qRound(f.lineHeight());
    }
    const int preset = m_line->findData(pct);
    m_line->setCurrentIndex(preset >= 0 ? preset : m_line->count() - 1);
    m_percent->setValue(pct);
    const Qt::Alignment a = f.alignment() & Qt::AlignHorizontal_Mask;
    m_align->setCurrentIndex(a & Qt::AlignHCenter ? 1 : a & Qt::AlignRight ? 2 : a & Qt::AlignJustify ? 3 : 0);
    connect(m_special, &QComboBox::currentIndexChanged, this, &ParagraphDialog::syncEnabled);
    connect(m_line, &QComboBox::currentIndexChanged, this, [this]() {
        if (m_line->currentData().toInt() > 0) {
            m_percent->setValue(m_line->currentData().toInt());
        }
        syncEnabled();
    });
    syncEnabled();
}

void ParagraphDialog::syncEnabled()
{
    m_by->setEnabled(m_special->currentIndex() != 0);
    m_percent->setEnabled(m_line->currentData().toInt() == 0);
}

QTextBlockFormat ParagraphDialog::format() const
{
    QTextBlockFormat f;
    f.setLeftMargin(cmToPx(m_left->value()));
    f.setRightMargin(cmToPx(m_right->value()));
    const qreal by = cmToPx(m_by->value());
    f.setTextIndent(m_special->currentIndex() == 1 ? by : m_special->currentIndex() == 2 ? -by : 0.0);
    f.setTopMargin(ptToPx(m_before->value()));
    f.setBottomMargin(ptToPx(m_after->value()));
    const int pct = m_line->currentData().toInt() > 0 ? m_line->currentData().toInt() : m_percent->value();
    f.setLineHeight(pct, QTextBlockFormat::ProportionalHeight);
    f.setAlignment(Qt::Alignment(m_align->currentData().toInt()));
    return f;
}
