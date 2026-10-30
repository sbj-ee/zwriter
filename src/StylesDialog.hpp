#pragma once

// Format > Styles… (1.15.0): a font family and size for every paragraph
// style, with a live preview, Reset to Defaults and "Save as default for new
// documents". OK / Apply emit applied(); Cancel changes nothing.

#include "StyleFonts.hpp"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QDoubleSpinBox;
class QFontComboBox;
class QTextBrowser;

class StylesDialog : public QDialog {
    Q_OBJECT
public:
    // `bodyChain` / `headingChain`: the built-in faces, shown when a style has
    // no family of its own.
    StylesDialog(const StyleFonts::Map &current, const QStringList &bodyChain, const QStringList &headingChain,
                 QWidget *parent = nullptr);

    StyleFonts::Map map() const;
    void setMap(const StyleFonts::Map &map);
    bool saveAsDefault() const;

    QFontComboBox *familyCombo(const QString &key) const { return m_family.value(key); }
    QDoubleSpinBox *sizeSpin(const QString &key) const { return m_size.value(key); }
    QCheckBox *saveAsDefaultBox() const { return m_saveDefault; }
    QString previewHtml() const;

signals:
    void applied(const StyleFonts::Map &map, bool saveAsDefault);

private:
    QString builtinFamily(const QString &key) const;
    void updatePreview();

    QStringList m_bodyChain;
    QStringList m_headingChain;
    QHash<QString, QFontComboBox *> m_family;
    QHash<QString, QDoubleSpinBox *> m_size;
    QHash<QString, QString> m_shownBuiltin; // what the combo shows for "built-in"
    QCheckBox *m_saveDefault = nullptr;
    QTextBrowser *m_preview = nullptr;
    bool m_loading = false;
};
