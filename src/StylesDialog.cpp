#include "StylesDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

StylesDialog::StylesDialog(const StyleFonts::Map &current, const QStringList &bodyChain,
                           const QStringList &headingChain, QWidget *parent)
    : QDialog(parent), m_bodyChain(bodyChain), m_headingChain(headingChain)
{
    setWindowTitle(QStringLiteral("Styles"));
    setObjectName(QStringLiteral("stylesDialog"));
    auto *outer = new QVBoxLayout(this);
    auto *intro = new QLabel(QStringLiteral("Font and size for each paragraph style. Text you gave another font "
                                            "or size by hand keeps it."),
                             this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(QStringLiteral("<b>Style</b>"), this), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("<b>Font</b>"), this), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("<b>Size (pt)</b>"), this), 0, 2);
    int row = 1;
    for (const QString &key : StyleFonts::keys()) {
        auto *label = new QLabel(StyleFonts::displayName(key), this);
        auto *combo = new QFontComboBox(this);
        combo->setObjectName(QStringLiteral("styleFamily_") + key);
        combo->setMinimumWidth(220);
        auto *spin = new QDoubleSpinBox(this);
        spin->setObjectName(QStringLiteral("styleSize_") + key);
        spin->setRange(4, 200);
        spin->setDecimals(1);
        spin->setSingleStep(1);
        label->setBuddy(combo);
        grid->addWidget(label, row, 0);
        grid->addWidget(combo, row, 1);
        grid->addWidget(spin, row, 2);
        m_family.insert(key, combo);
        m_size.insert(key, spin);
        connect(combo, &QFontComboBox::currentFontChanged, this, &StylesDialog::updatePreview);
        connect(spin, &QDoubleSpinBox::valueChanged, this, &StylesDialog::updatePreview);
        ++row;
    }
    outer->addLayout(grid);

    outer->addWidget(new QLabel(QStringLiteral("Preview"), this));
    m_preview = new QTextBrowser(this);
    m_preview->setObjectName(QStringLiteral("stylesPreview"));
    m_preview->setMinimumHeight(220);
    outer->addWidget(m_preview, 1);

    m_saveDefault = new QCheckBox(QStringLiteral("Save as default for new documents"), this);
    m_saveDefault->setObjectName(QStringLiteral("stylesSaveDefault"));
    outer->addWidget(m_saveDefault);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply
                                             | QDialogButtonBox::RestoreDefaults,
                                         this);
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("Reset to Defaults"));
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [this]() { setMap(StyleFonts::builtinDefaults()); });
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            [this]() { emit applied(map(), saveAsDefault()); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        emit applied(map(), saveAsDefault());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    setMap(current);
}

QString StylesDialog::builtinFamily(const QString &key) const
{
    // The face the built-in chain resolves to on this machine.
    const QStringList chain = key == QLatin1String("Body") ? m_bodyChain : m_headingChain;
    const QStringList installed = QFontDatabase::families();
    for (const QString &f : chain) {
        if (installed.contains(f, Qt::CaseInsensitive)) {
            return f;
        }
    }
    QFont f;
    f.setFamilies(chain.isEmpty() ? QStringList{QFont().family()} : chain);
    return QFontInfo(f).family();
}

void StylesDialog::setMap(const StyleFonts::Map &map)
{
    m_loading = true;
    for (const QString &key : StyleFonts::keys()) {
        const StyleFonts::Entry e = StyleFonts::entry(map, key);
        const QString shown = e.family.isEmpty() ? builtinFamily(key) : e.family;
        m_family[key]->setCurrentFont(QFont(shown));
        if (e.family.isEmpty()) {
            m_shownBuiltin.insert(key, m_family[key]->currentFont().family());
        } else {
            m_shownBuiltin.remove(key);
        }
        m_size[key]->setValue(e.pointSize);
    }
    m_loading = false;
    updatePreview();
}

StyleFonts::Map StylesDialog::map() const
{
    StyleFonts::Map m = StyleFonts::builtinDefaults();
    for (const QString &key : StyleFonts::keys()) {
        StyleFonts::Entry e;
        const QString fam = m_family[key]->currentFont().family();
        // Still showing the built-in face: keep it built-in (the whole chain).
        e.family = m_shownBuiltin.contains(key) && m_shownBuiltin.value(key) == fam ? QString() : fam;
        e.pointSize = m_size[key]->value();
        m.insert(key, e);
    }
    return m;
}

bool StylesDialog::saveAsDefault() const { return m_saveDefault->isChecked(); }

QString StylesDialog::previewHtml() const
{
    QString html;
    for (const QString &key : StyleFonts::keys()) {
        const QString fam = m_family[key]->currentFont().family().toHtmlEscaped().remove(QLatin1Char('\''));
        const bool bold = key.startsWith(QLatin1String("Heading")) || key == QLatin1String("Title");
        const bool italic = key == QLatin1String("Subtitle") || key == QLatin1String("Caption")
            || key == QLatin1String("Heading3");
        html += QStringLiteral("<p style=\"margin:2px 0; font-family:'%1'; font-size:%2pt;%3%4\">%5</p>")
                    .arg(fam, QString::number(m_size[key]->value(), 'g', 4),
                         bold ? QStringLiteral(" font-weight:bold;") : QString(),
                         italic ? QStringLiteral(" font-style:italic;") : QString(),
                         StyleFonts::displayName(key).toHtmlEscaped()
                             + (key == QLatin1String("Body")
                                    ? QStringLiteral(" — The quick brown fox jumps over the lazy dog.")
                                    : QString()));
    }
    return html;
}

void StylesDialog::updatePreview()
{
    if (m_loading || !m_preview) {
        return;
    }
    m_preview->setHtml(previewHtml());
}
