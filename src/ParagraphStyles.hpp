#pragma once

// Paragraph formatting (1.7.0): named paragraph styles beyond Body Text and
// Headings, indents, spacing and line spacing, and their CSS for export.
//
// A named style is stored on the block as a custom property (its ODF style
// name, e.g. "Bibliography"), so it survives editing and is written to ODT
// as style:parent-style-name. Geometry lives in the normal QTextBlockFormat
// fields (left/right margin, text indent, top/bottom margin, line height),
// all in document pixels (96 per inch).

#include <QString>
#include <QStringList>
#include <QTextBlockFormat>
#include <QTextCharFormat>

class QTextCursor;
class QTextDocument;

namespace ParagraphStyles {

constexpr int StyleNameProperty = QTextFormat::UserProperty + 0x5A01;

struct Style {
    QString name;    // ODF style name, also stored on the block
    QString display; // shown in the menu / combo
    Qt::Alignment align = Qt::AlignLeft;
    qreal left = 0, right = 0, indent = 0, top = 0, bottom = 0; // px
    qreal pointSize = 12;
    int weight = 400;
    bool italic = false;
};

// Title, Subtitle, Block Quote, Caption, Bibliography (in menu order).
const QList<Style> &named();
const Style *find(const QString &name);
QString styleOf(const QTextBlockFormat &format); // "" = Body Text / Heading

// Applies a named style to every block the cursor's selection touches (or
// clears it with name ""). Undoable as one step.
void apply(QTextCursor cursor, const QString &name);

// Increase / Decrease Indent: left margin in steps of 1.27 cm (0.5 in).
constexpr qreal kIndentStep = 48.0; // px
void changeIndent(QTextCursor cursor, int steps);

// Writes the paragraph geometry of `src` (margins, indent, spacing, line
// height, alignment) onto every block of the selection.
void setGeometry(QTextCursor cursor, const QTextBlockFormat &src);

// px <-> cm / pt at 96 dpi.
inline qreal pxToCm(qreal px) { return px * 2.54 / 96.0; }
inline qreal cmToPx(qreal cm) { return cm * 96.0 / 2.54; }
inline qreal pxToPt(qreal px) { return px * 72.0 / 96.0; }
inline qreal ptToPx(qreal pt) { return pt * 96.0 / 72.0; }

// CSS for the block's geometry (margins, text-indent, line-height,
// text-align), for HTML/EPUB export. Empty when there is nothing to say.
QString blockCss(const QTextBlockFormat &format);

} // namespace ParagraphStyles
