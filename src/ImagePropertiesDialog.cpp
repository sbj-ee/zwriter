#include "ImagePropertiesDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ImagePropertiesDialog::ImagePropertiesDialog(const QSize &natural, qreal column, QWidget *parent)
    : QDialog(parent)
    , m_natural(natural)
    , m_column(column)
{
    setWindowTitle(QStringLiteral("Image Properties"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_width = new QSpinBox(this);
    m_width->setRange(8, 20000);
    m_width->setSuffix(QStringLiteral(" px"));
    m_height = new QSpinBox(this);
    m_height->setRange(8, 20000);
    m_height->setSuffix(QStringLiteral(" px"));
    m_keep = new QCheckBox(QStringLiteral("Keep proportions"), this);
    m_keep->setChecked(true);
    form->addRow(QStringLiteral("Width:"), m_width);
    form->addRow(QStringLiteral("Height:"), m_height);
    form->addRow(QString(), m_keep);

    auto *sizeButtons = new QHBoxLayout;
    auto *fit = new QPushButton(QStringLiteral("Fit Column Width"), this);
    auto *orig = new QPushButton(QStringLiteral("Original Size"), this);
    fit->setAutoDefault(false);
    orig->setAutoDefault(false);
    sizeButtons->addWidget(fit);
    sizeButtons->addWidget(orig);
    sizeButtons->addStretch(1);
    form->addRow(QString(), sizeButtons);

    m_wrap = new QComboBox(this);
    m_wrap->addItem(QStringLiteral("In line with text"), int(Images::Wrap::Inline));
    m_wrap->addItem(QStringLiteral("Left, text flows on the right"), int(Images::Wrap::Left));
    m_wrap->addItem(QStringLiteral("Right, text flows on the left"), int(Images::Wrap::Right));
    form->addRow(QStringLiteral("Wrap:"), m_wrap);

    m_info = new QLabel(this);
    m_info->setText(natural.isValid()
                        ? QStringLiteral("Picture: %1 × %2 px · column: %3 px")
                              .arg(natural.width()).arg(natural.height()).arg(qRound(column))
                        : QStringLiteral("Column: %1 px").arg(qRound(column)));
    m_info->setEnabled(false); // secondary text colour from the theme
    layout->addLayout(form);
    layout->addWidget(m_info);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_width, &QSpinBox::valueChanged, this, &ImagePropertiesDialog::widthChanged);
    connect(m_height, &QSpinBox::valueChanged, this, &ImagePropertiesDialog::heightChanged);
    connect(fit, &QPushButton::clicked, this, &ImagePropertiesDialog::fitColumn);
    connect(orig, &QPushButton::clicked, this, &ImagePropertiesDialog::originalSize);
}

void ImagePropertiesDialog::setImageSize(const QSize &size)
{
    m_syncing = true;
    m_width->setValue(size.width());
    m_height->setValue(size.height());
    m_syncing = false;
}

QSize ImagePropertiesDialog::imageSize() const
{
    return QSize(m_width->value(), m_height->value());
}

void ImagePropertiesDialog::setWrap(Images::Wrap wrap)
{
    m_wrap->setCurrentIndex(qMax(0, m_wrap->findData(int(wrap))));
}

Images::Wrap ImagePropertiesDialog::wrap() const
{
    return Images::Wrap(m_wrap->currentData().toInt());
}

void ImagePropertiesDialog::setKeepProportions(bool on)
{
    m_keep->setChecked(on);
}

void ImagePropertiesDialog::fitColumn()
{
    const QSize ref = m_natural.isValid() ? m_natural : imageSize();
    if (ref.width() <= 0) {
        return;
    }
    const int w = qMax(8, qRound(m_column));
    setImageSize(QSize(w, qMax(8, qRound(qreal(ref.height()) * w / ref.width()))));
}

void ImagePropertiesDialog::originalSize()
{
    if (!m_natural.isValid()) {
        return;
    }
    if (m_column > 0 && m_natural.width() > m_column) {
        fitColumn();
        return;
    }
    setImageSize(m_natural);
}

void ImagePropertiesDialog::widthChanged(int w)
{
    if (m_syncing || !m_keep->isChecked()) {
        return;
    }
    const QSize ref = m_natural.isValid() ? m_natural : QSize();
    if (!ref.isValid() || ref.width() <= 0) {
        return;
    }
    m_syncing = true;
    m_height->setValue(qMax(8, qRound(qreal(ref.height()) * w / ref.width())));
    m_syncing = false;
}

void ImagePropertiesDialog::heightChanged(int h)
{
    if (m_syncing || !m_keep->isChecked()) {
        return;
    }
    const QSize ref = m_natural.isValid() ? m_natural : QSize();
    if (!ref.isValid() || ref.height() <= 0) {
        return;
    }
    m_syncing = true;
    m_width->setValue(qMax(8, qRound(qreal(ref.width()) * h / ref.height())));
    m_syncing = false;
}
