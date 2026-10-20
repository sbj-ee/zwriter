#pragma once

#include "Images.hpp"

#include <QDialog>
#include <QSize>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

// Right-click a picture → Image Properties…: display size and text wrap.
// Plain form; no handles, borders or effects.
class ImagePropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    // `natural`: the picture's own pixel size; `column`: the text column width.
    ImagePropertiesDialog(const QSize &natural, qreal column, QWidget *parent = nullptr);

    void setImageSize(const QSize &size);
    QSize imageSize() const;
    void setWrap(Images::Wrap wrap);
    Images::Wrap wrap() const;
    void setKeepProportions(bool on);

    void fitColumn();     // width = column, height in proportion
    void originalSize();  // the picture's own size (capped to the column)

private:
    void widthChanged(int w);
    void heightChanged(int h);

    QSize m_natural;
    qreal m_column = 0.0;
    bool m_syncing = false;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QCheckBox *m_keep = nullptr;
    QComboBox *m_wrap = nullptr;
    QLabel *m_info = nullptr;
};
