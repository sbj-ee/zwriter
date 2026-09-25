#include "Theme.hpp"

namespace Theme {

ThemeColors colors(const QString &themeId)
{
    ThemeColors c;
    const bool inverse = (themeId == QLatin1String("inverse"));
    const bool paper = !inverse && (themeId != QLatin1String("dark"));
    // Only two arrow-image variants ship; the dark pair suits both dark themes.
    c.id = paper ? QStringLiteral("paper") : QStringLiteral("dark");
    if (paper) {
        // Warm paper and ink.
        c.windowBg    = QColor("#efeae1");
        c.barBg       = QColor("#f5f1ea");
        c.menuBg      = QColor("#fcfaf6");
        c.inputBg     = QColor("#fcfaf6");
        c.fg          = QColor("#2a2722");
        c.muted       = QColor("#847d70");
        c.border      = QColor("#ddd6c9");
        c.hover       = QColor("#e8e2d6");
        c.pressed     = QColor("#ddd5c6");
        c.accent      = QColor("#2f6a99");
        c.accentSoft  = QColor("#d9e6f1");
        c.accentFg    = QColor("#16374f");
        c.scroll      = QColor("#b9b1a2");
        c.scrollHover = QColor("#9c9484");
        c.pageBg      = QColor("#faf7f1");
        c.pageFg      = QColor("#1a1a1a");
        c.pageBorder  = QColor("#d8d2c6");
        c.desk        = QColor("#d3cdc1");
        c.selBg       = QColor("#bcd4ec");
        c.selFg       = QColor("#000000");
    } else if (inverse) {
        // Inverse video: a true black sheet with white ink, maximum contrast.
        c.windowBg    = QColor("#000000");
        c.barBg       = QColor("#0d0d0d");
        c.menuBg      = QColor("#151515");
        c.inputBg     = QColor("#000000");
        c.fg          = QColor("#ffffff");
        c.muted       = QColor("#9a9a9a");
        c.border      = QColor("#3c3c3c");
        c.hover       = QColor("#242424");
        c.pressed     = QColor("#333333");
        c.accent      = QColor("#8ab4f8");
        c.accentSoft  = QColor("#20375a");
        c.accentFg    = QColor("#ffffff");
        c.scroll      = QColor("#4a4a4a");
        c.scrollHover = QColor("#6d6d6d");
        c.pageBg      = QColor("#000000");
        c.pageFg      = QColor("#ffffff");
        // The sheet is pure black, so the desk lifts a shade and the border
        // stays bright -- otherwise the page edge vanishes in full-page view.
        c.pageBorder  = QColor("#4f4f4f");
        c.desk        = QColor("#0b0b0b");
        c.selBg       = QColor("#ffffff");
        c.selFg       = QColor("#000000");
    } else {
        // Calm charcoal.
        c.windowBg    = QColor("#1b1c1f");
        c.barBg       = QColor("#25272b");
        c.menuBg      = QColor("#2a2c31");
        c.inputBg     = QColor("#1d1e21");
        c.fg          = QColor("#dcdad5");
        c.muted       = QColor("#8b8d94");
        c.border      = QColor("#36383d");
        c.hover       = QColor("#303338");
        c.pressed     = QColor("#3a3d43");
        c.accent      = QColor("#6fa8dc");
        c.accentSoft  = QColor("#2c4762");
        c.accentFg    = QColor("#eaf3fb");
        c.scroll      = QColor("#4a4d54");
        c.scrollHover = QColor("#62666e");
        c.pageBg      = QColor("#202124");
        c.pageFg      = QColor("#d8d5cf");
        c.pageBorder  = QColor("#34363b");
        c.desk        = QColor("#141517");
        c.selBg       = QColor("#2f5580");
        c.selFg       = QColor("#ffffff");
    }
    return c;
}

QString styleSheet(const ThemeColors &c, bool fullPage, int bodyPt)
{
    QString css = QStringLiteral(R"CSS(
QMainWindow { background-color: @windowBg; }
#desk { background-color: @desk; }
#pageFrame { background-color: @pageBg; border: none; } /* outline painted by PageCanvas: the editor must be exactly page-sized */

QTextEdit {
  background-color: @pageBg;
  color: @pageFg;
  selection-background-color: @selBg;
  selection-color: @selFg;
  font-family: 'Courier New', 'Courier', 'Courier Prime', 'Nimbus Mono PS', 'Liberation Mono', 'Noto Sans Mono', 'Menlo', 'Monaco', 'DejaVu Sans Mono', monospace;
  font-size: @bodyPt;
  padding: @editorPad;
}

/* ---- Menu bar and menus ------------------------------------------------ */
QMenuBar {
  background-color: @barBg;
  color: @fg;
  border-bottom: 1px solid @border;
  padding: 3px 8px;
  spacing: 2px;
  font-size: 10pt;
}
QMenuBar::item { padding: 5px 11px; border-radius: 5px; background: transparent; }
QMenuBar::item:selected { background-color: @hover; }
QMenuBar::item:pressed { background-color: @pressed; }

QMenu {
  background-color: @menuBg;
  color: @fg;
  border: 1px solid @border;
  padding: 6px 0;
  font-size: 10pt;
}
QMenu::item { padding: 6px 36px 6px 30px; margin: 0 6px; border-radius: 5px; }
QMenu::item:selected { background-color: @accentSoft; color: @accentFg; }
QMenu::item:disabled { color: @muted; background: transparent; }
QMenu::separator { height: 1px; background: @border; margin: 6px 14px; }
QMenu::indicator { width: 14px; height: 14px; left: 8px; }

/* ---- Toolbar ------------------------------------------------------------ */
QToolBar {
  background-color: @barBg;
  border: none;
  border-bottom: 1px solid @border;
  padding: 5px 10px;
  spacing: 3px;
}
QToolBar QToolButton {
  color: @fg;
  background: transparent;
  border: 1px solid transparent;
  border-radius: 5px;
  padding: 4px 8px;
  min-width: 18px;
  min-height: 18px;
  font-size: 10pt;
}
QToolBar QToolButton:hover { background-color: @hover; }
QToolBar QToolButton:pressed { background-color: @pressed; }
QToolBar QToolButton:checked { background-color: @accentSoft; color: @accentFg; }
QToolBar QToolButton:disabled { color: @muted; }
QToolBar::separator { background-color: @border; width: 1px; margin: 5px 9px; }

QToolBar QFontComboBox, QToolBar QSpinBox, QToolBar QComboBox {
  background-color: @inputBg;
  color: @fg;
  border: 1px solid @border;
  border-radius: 5px;
  padding: 3px 8px;
  min-height: 22px;
  font-size: 10pt;
  selection-background-color: @accentSoft;
  selection-color: @accentFg;
}
QToolBar QFontComboBox:hover, QToolBar QSpinBox:hover, QToolBar QComboBox:hover { border-color: @muted; }
QToolBar QFontComboBox:focus, QToolBar QSpinBox:focus, QToolBar QComboBox:focus { border-color: @accent; }
QToolBar QComboBox::drop-down, QToolBar QFontComboBox::drop-down { border: none; width: 20px; }
/* Size field: type a number, use the wheel or arrow keys — no stub buttons. */
QToolBar QSpinBox { padding-right: 6px; }
QToolBar QSpinBox::up-button, QToolBar QSpinBox::down-button { width: 0; border: none; }
QToolBar QSpinBox::up-arrow, QToolBar QSpinBox::down-arrow { image: none; width: 0; height: 0; }

QComboBox::down-arrow, QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
  image: url(:/ui/arrow-down-@id.png); width: 10px; height: 6px;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
  image: url(:/ui/arrow-up-@id.png); width: 10px; height: 6px;
}

QComboBox QAbstractItemView {
  background-color: @menuBg;
  color: @fg;
  border: 1px solid @border;
  selection-background-color: @accentSoft;
  selection-color: @accentFg;
  outline: 0;
  padding: 3px;
}

/* ---- Status bar --------------------------------------------------------- */
QStatusBar {
  background-color: @barBg;
  color: @muted;
  border-top: 1px solid @border;
  font-size: 9pt;
}
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @muted; padding: 1px 10px; }
#keysChip { border-radius: 9px; padding: 1px 10px; }
#keysChip:hover { background-color: @hover; color: @fg; }

/* ---- Scrollbars: slim, quiet ------------------------------------------- */
QScrollBar:vertical { background: transparent; width: 13px; margin: 2px; border: none; }
QScrollBar::handle:vertical { background: @scroll; border-radius: 4px; min-height: 40px; margin: 0 2px; }
QScrollBar::handle:vertical:hover { background: @scrollHover; }
QScrollBar:horizontal { background: transparent; height: 13px; margin: 2px; border: none; }
QScrollBar::handle:horizontal { background: @scroll; border-radius: 4px; min-width: 40px; margin: 2px 0; }
QScrollBar::handle:horizontal:hover { background: @scrollHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QScrollArea { background: transparent; border: none; }

/* ---- Tooltips ------------------------------------------------------------ */
QToolTip {
  background-color: @menuBg;
  color: @fg;
  border: 1px solid @border;
  padding: 4px 8px;
}

/* ---- Find / replace bar ------------------------------------------------- */
#findReplaceBar { background-color: @barBg; border-bottom: 1px solid @border; }
#findReplaceBar QLabel { color: @fg; }
#findReplaceBar QLineEdit {
  background-color: @inputBg;
  color: @fg;
  border: 1px solid @border;
  border-radius: 5px;
  padding: 4px 8px;
  selection-background-color: @accentSoft;
  selection-color: @accentFg;
}
#findReplaceBar QLineEdit:focus { border-color: @accent; }
#findReplaceBar QPushButton {
  color: @fg;
  background-color: @inputBg;
  border: 1px solid @border;
  border-radius: 5px;
  padding: 4px 10px;
}
#findReplaceBar QPushButton:hover { background-color: @hover; }
#findReplaceBar QPushButton:pressed { background-color: @pressed; }
#findReplaceBar QCheckBox { color: @fg; background: transparent; }

/* ---- Dialogs ------------------------------------------------------------ */
QDialog, QMessageBox { background-color: @windowBg; color: @fg; }
QDialog QLabel, QMessageBox QLabel { color: @fg; }
QDialog QPushButton, QMessageBox QPushButton {
  color: @fg;
  background-color: @inputBg;
  border: 1px solid @border;
  border-radius: 6px;
  padding: 6px 16px;
}
QDialog QPushButton { min-width: 68px; }
QDialog QPushButton:hover, QMessageBox QPushButton:hover { background-color: @hover; }
QDialog QPushButton:pressed, QMessageBox QPushButton:pressed { background-color: @pressed; }
QDialog QPushButton:default, QMessageBox QPushButton:default { border-color: @accent; }
QDialog QPushButton:focus, QMessageBox QPushButton:focus { border-color: @accent; }
QDialogButtonBox { dialogbuttonbox-buttons-have-icons: 0; }
QDialog QSpinBox::up-button, QDialog QSpinBox::down-button,
QDialog QDoubleSpinBox::up-button, QDialog QDoubleSpinBox::down-button {
  width: 20px; border: none; background: transparent; subcontrol-origin: border;
}
QDialog QSpinBox::up-button, QDialog QDoubleSpinBox::up-button { subcontrol-position: top right; }
QDialog QSpinBox::down-button, QDialog QDoubleSpinBox::down-button { subcontrol-position: bottom right; }
QDialog QComboBox::drop-down { border: none; width: 22px; }
QDialog QLineEdit, QDialog QSpinBox, QDialog QComboBox, QDialog QDoubleSpinBox {
  background-color: @inputBg;
  color: @fg;
  border: 1px solid @border;
  border-radius: 5px;
  padding: 4px 8px;
  selection-background-color: @accentSoft;
  selection-color: @accentFg;
}
QDialog QLineEdit:focus, QDialog QSpinBox:focus, QDialog QComboBox:focus { border-color: @accent; }
QDialog QCheckBox, QDialog QRadioButton, QDialog QGroupBox { color: @fg; }
QDialog QGroupBox { border: 1px solid @border; border-radius: 6px; margin-top: 10px; padding-top: 8px; }
QDialog QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QDialog QTableWidget, QDialog QTableView, QDialog QListView, QDialog QTreeView {
  background-color: @inputBg;
  color: @fg;
  border: 1px solid @border;
  gridline-color: @border;
}
QDialog QHeaderView::section { background-color: @barBg; color: @fg; border: none; padding: 5px 8px; }
)CSS");

    const QString pad = fullPage ? QStringLiteral("0px") : QStringLiteral("48px 20%");
    const auto hex = [](const QColor &col) { return col.name(); };
    const struct { const char *key; QString value; } tokens[] = {
        {"@id", c.id},
        {"@windowBg", hex(c.windowBg)},   {"@barBg", hex(c.barBg)},
        {"@menuBg", hex(c.menuBg)},       {"@inputBg", hex(c.inputBg)},
        {"@fg", hex(c.fg)},               {"@muted", hex(c.muted)},
        {"@border", hex(c.border)},       {"@hover", hex(c.hover)},
        {"@pressed", hex(c.pressed)},     {"@accentSoft", hex(c.accentSoft)},
        {"@accentFg", hex(c.accentFg)},   {"@accent", hex(c.accent)},
        {"@scrollHover", hex(c.scrollHover)}, {"@scroll", hex(c.scroll)},
        {"@pageBg", hex(c.pageBg)},       {"@pageFg", hex(c.pageFg)},
        {"@pageBorder", hex(c.pageBorder)},
        {"@desk", hex(fullPage ? c.desk : c.pageBg)},
        {"@selBg", hex(c.selBg)},         {"@selFg", hex(c.selFg)},
        {"@bodyPt", QString::number(bodyPt) + QStringLiteral("pt")},
        {"@editorPad", pad},
    };
    for (const auto &t : tokens) {
        css.replace(QLatin1String(t.key), t.value);
    }
    return css;
}

} // namespace Theme
