#pragma once

// Style fonts (1.15.0): a font family and point size for every paragraph
// style -- Body Text, Heading 1-3 and the named styles of ParagraphStyles
// (Title, Subtitle, Block Quote, Caption, Bibliography).
//
// Design: app-wide defaults live in QSettings ("styleFonts/<key>/family|size");
// a document keeps its own copy in its ODT meta.xml (meta:user-defined
// "zwriter:style-fonts", a JSON object). New documents start from the app
// defaults; a document saved before 1.15.0 has no stored map and uses the
// built-in defaults (what its text was formatted with). Unknown keys in a
// stored map are ignored, missing keys fall back to the built-in entry.
//
// An empty family means "the built-in face": the Courier chain for Body Text,
// the UI sans chain for headings, and "leave the face alone" for the named
// styles (they never set one before 1.15.0).

#include <QHash>
#include <QString>
#include <QStringList>

class QSettings;
class QTextDocument;

namespace StyleFonts {

struct Entry {
    QString family;     // "" = built-in face
    qreal pointSize = 12;
    bool operator==(const Entry &o) const { return family == o.family && qFuzzyCompare(pointSize, o.pointSize); }
    bool operator!=(const Entry &o) const { return !(*this == o); }
};

using Map = QHash<QString, Entry>;

// Style keys in dialog order: Body, Heading1, Heading2, Heading3, then the
// named styles' ODF names (Title, Subtitle, Quotations, Caption, Bibliography).
const QStringList &keys();
QString displayName(const QString &key);

// Key of a paragraph: its named style, else Heading<N>, else Body.
QString keyFor(int headingLevel, const QString &namedStyle);

Map builtinDefaults();
Entry entry(const Map &map, const QString &key); // falls back to built-in

// App defaults (QSettings); missing values are the built-in ones.
Map appDefaults(QSettings &settings);
void saveAppDefaults(QSettings &settings, const Map &map);

// Per-document JSON. parse() of "" or garbage returns false (map untouched).
QString serialize(const Map &map);
bool parse(const QString &json, Map *map);

// The map style-applying code (Format > Paragraph Style, headings) uses.
const Map &active();
void setActive(const Map &map);

// The families written onto runs for `key`: the entry's family (then the
// built-in chain as fallback), or the built-in chain when the entry has none.
// Empty for a named style without a family (= leave the face alone).
QStringList families(const Map &map, const QString &key, const QStringList &bodyChain,
                     const QStringList &headingChain);

// CSS for one style (font-family + font-size), only for what differs from
// the built-in defaults; empty when the style is unchanged. For EPUB export.
QString css(const Map &map, const QString &key);

// Re-formats every paragraph whose style's entry differs between `from` and
// `to`: runs still in the old face / size get the new one; text the user gave
// another face or size by hand keeps it. One undo step. Returns the number of
// paragraphs touched.
int restyle(QTextDocument *doc, const Map &from, const Map &to, const QStringList &bodyChain,
            const QStringList &headingChain);

} // namespace StyleFonts
