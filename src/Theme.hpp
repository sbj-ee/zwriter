#pragma once

#include <QColor>
#include <QString>

// Design tokens for the whole window. One palette per theme; the chrome
// (menus, toolbar, status bar, dialogs) follows the page theme so the app
// reads as one warm "paper and ink" surface or one calm charcoal one, not a
// light page in a dark frame.
struct ThemeColors
{
    QString id;        // arrow-image variant: "paper" or "dark" (inverse reuses dark)
    // Chrome
    QColor windowBg;   // window / dialog background
    QColor barBg;      // menu bar, toolbar, status bar
    QColor menuBg;     // popup menus and list popups
    QColor inputBg;    // text fields, combo boxes, buttons
    QColor fg;         // primary chrome text and icons
    QColor muted;      // secondary text, disabled, status bar
    QColor border;     // hairlines
    QColor hover;      // hovered item
    QColor pressed;    // pressed item
    QColor accent;     // focus ring, links, default button
    QColor accentSoft; // checked toolbar button / selected menu item background
    QColor accentFg;   // text/icon on accentSoft
    QColor scroll;     // scrollbar handle
    QColor scrollHover;
    // Writing surface
    QColor pageBg;
    QColor pageFg;
    QColor pageBorder;
    QColor desk;       // behind the page in full-page view
    QColor selBg;
    QColor selFg;
};

namespace Theme {

// themeId: "paper" | "dark" | "inverse". Anything else is treated as "paper".
ThemeColors colors(const QString &themeId);

// Complete application stylesheet for the given palette.
//   fullPage    - page sits centred on a desk (true) or fills the window (false)
//   bodyPt      - editor font size in points
QString styleSheet(const ThemeColors &c, bool fullPage, int bodyPt);

} // namespace Theme
