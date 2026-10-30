// Style fonts (1.15.0): a font family and size per paragraph style.
//   M  model: keys, built-in defaults, entry fallback
//   J  per-document JSON: round trip, unknown keys ignored, garbage, missing keys
//   Q  app defaults in QSettings
//   O  ODT meta.xml: "zwriter:style-fonts" round trip; a pre-1.15 file has none
//   F  the QTextCharFormat each style applies (named styles via ParagraphStyles)
//   R  restyle: runs in the old face/size follow, hand-formatted runs keep theirs
//   C  CSS per style for EPUB (only what differs from the built-in defaults)

#include "DocumentIo.hpp"
#include "DocumentMeta.hpp"
#include "ParagraphStyles.hpp"
#include "StyleFonts.hpp"

#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <cstdio>

namespace {
int g_failures = 0;
void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.isEmpty() ? "" : "  -- ", qPrintable(detail));
    if (!ok) {
        ++g_failures;
    }
}

const QStringList kBody{QStringLiteral("Courier New"), QStringLiteral("monospace")};
const QStringList kHead{QStringLiteral("Noto Sans"), QStringLiteral("sans-serif")};

QString firstFamily(const QTextCharFormat &f)
{
    return f.fontFamilies().toStringList().value(0);
}

StyleFonts::Map sample()
{
    StyleFonts::Map m = StyleFonts::builtinDefaults();
    m[QStringLiteral("Body")] = {QStringLiteral("Georgia"), 13};
    m[QStringLiteral("Heading1")] = {QStringLiteral("Futura"), 28};
    m[QStringLiteral("Caption")] = {QStringLiteral("Gill Sans"), 9};
    return m;
}

void testModel()
{
    const QStringList k = StyleFonts::keys();
    check(k.size() == 9 && k.first() == QLatin1String("Body") && k.contains(QStringLiteral("Heading3"))
              && k.contains(QStringLiteral("Title")) && k.contains(QStringLiteral("Quotations"))
              && k.contains(QStringLiteral("Caption")) && k.contains(QStringLiteral("Bibliography")),
          "M every paragraph style has a key", k.join(QLatin1Char(',')));
    const StyleFonts::Map d = StyleFonts::builtinDefaults();
    check(StyleFonts::entry(d, QStringLiteral("Body")).pointSize == 12
              && StyleFonts::entry(d, QStringLiteral("Heading1")).pointSize == 22
              && StyleFonts::entry(d, QStringLiteral("Heading2")).pointSize == 17
              && StyleFonts::entry(d, QStringLiteral("Heading3")).pointSize == 13
              && StyleFonts::entry(d, QStringLiteral("Title")).pointSize == 26
              && StyleFonts::entry(d, QStringLiteral("Caption")).pointSize == 10,
          "M built-in sizes are the pre-1.15 ones");
    check(StyleFonts::entry(StyleFonts::Map{}, QStringLiteral("Heading2")).pointSize == 17,
          "M a missing entry falls back to built-in");
    check(StyleFonts::keyFor(2, QString()) == QLatin1String("Heading2")
              && StyleFonts::keyFor(0, QStringLiteral("Caption")) == QLatin1String("Caption")
              && StyleFonts::keyFor(0, QString()) == QLatin1String("Body")
              && StyleFonts::keyFor(5, QString()) == QLatin1String("Heading3"),
          "M keyFor");
    check(StyleFonts::displayName(QStringLiteral("Quotations")) == QLatin1String("Block Quote"),
          "M display names come from ParagraphStyles");
}

void testJson()
{
    const StyleFonts::Map m = sample();
    StyleFonts::Map back;
    check(StyleFonts::parse(StyleFonts::serialize(m), &back) && back == m, "J round trip");
    StyleFonts::Map untouched = m;
    check(!StyleFonts::parse(QString(), &untouched) && untouched == m, "J empty string: not stored, map untouched");
    check(!StyleFonts::parse(QStringLiteral("{not json"), &untouched) && untouched == m, "J garbage is rejected");
    StyleFonts::Map partial;
    check(StyleFonts::parse(QStringLiteral(R"({"Heading2":{"family":"Optima","size":19},"Sidebar":{"family":"X","size":9},"Body":{"family":"","size":1000}})"),
                            &partial),
          "J partial map with an unknown key parses");
    check(!partial.contains(QStringLiteral("Sidebar")), "J unknown key ignored");
    check(StyleFonts::entry(partial, QStringLiteral("Heading2")) == StyleFonts::Entry{QStringLiteral("Optima"), 19},
          "J known key read");
    check(StyleFonts::entry(partial, QStringLiteral("Heading1")).pointSize == 22, "J missing key is built-in");
    check(StyleFonts::entry(partial, QStringLiteral("Body")).pointSize == 12, "J out-of-range size ignored");
}

void testSettings(const QString &dir)
{
    QSettings s(dir + QStringLiteral("/s.ini"), QSettings::IniFormat);
    check(StyleFonts::appDefaults(s) == StyleFonts::builtinDefaults(), "Q nothing saved: built-in");
    StyleFonts::saveAppDefaults(s, sample());
    s.sync();
    QSettings again(dir + QStringLiteral("/s.ini"), QSettings::IniFormat);
    check(StyleFonts::appDefaults(again) == sample(), "Q saved defaults read back");
}

void testOdtMeta(const QString &dir)
{
    const QString path = dir + QStringLiteral("/doc.odt");
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("Hello"));
    QString err;
    check(DocumentIo::save(&doc, path, DocumentIo::Format::Odt, &err), "O save odt", err);
    DocumentMeta meta;
    meta.ensureDefaults();
    check(OdtMeta::writeToOdt(path, meta, &err), "O write meta without style fonts", err);
    DocumentMeta legacy;
    OdtMeta::readFromOdt(path, &legacy);
    StyleFonts::Map map = StyleFonts::builtinDefaults();
    check(legacy.styleFonts.isEmpty() && !StyleFonts::parse(legacy.styleFonts, &map)
              && map == StyleFonts::builtinDefaults(),
          "O legacy file: no style fonts, built-in map");
    meta.styleFonts = StyleFonts::serialize(sample());
    check(OdtMeta::writeToOdt(path, meta, &err), "O write meta with style fonts", err);
    DocumentMeta read;
    OdtMeta::readFromOdt(path, &read);
    StyleFonts::Map back;
    check(StyleFonts::parse(read.styleFonts, &back) && back == sample(), "O style fonts survive meta.xml",
          read.styleFonts);
    check(read.author == meta.author, "O other meta still read");
}

void testFormats()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("A caption"));
    StyleFonts::setActive(sample());
    ParagraphStyles::apply(QTextCursor(&doc), QStringLiteral("Caption"));
    QTextCursor c(&doc);
    c.setPosition(2);
    check(firstFamily(c.charFormat()) == QLatin1String("Gill Sans") && c.charFormat().fontPointSize() == 9,
          "F named style uses its configured face and size",
          firstFamily(c.charFormat()) + QLatin1Char(' ') + QString::number(c.charFormat().fontPointSize()));
    check(c.charFormat().fontItalic(), "F named style keeps its italic");
    StyleFonts::setActive(StyleFonts::builtinDefaults());
    ParagraphStyles::apply(QTextCursor(&doc), QStringLiteral("Title"));
    c.setPosition(2);
    check(c.charFormat().fontPointSize() == 26, "F built-in Title is still 26 pt");
    check(StyleFonts::families(sample(), QStringLiteral("Heading1"), kBody, kHead)
              == QStringList({QStringLiteral("Futura"), QStringLiteral("Noto Sans"), QStringLiteral("sans-serif")}),
          "F heading families: chosen face, then the built-in chain");
    check(StyleFonts::families(StyleFonts::builtinDefaults(), QStringLiteral("Body"), kBody, kHead) == kBody,
          "F built-in body families are the Courier chain");
    check(StyleFonts::families(StyleFonts::builtinDefaults(), QStringLiteral("Caption"), kBody, kHead).isEmpty(),
          "F built-in named style sets no face");
}

void testRestyle()
{
    QTextDocument doc;
    QTextCursor c(&doc);
    QTextCharFormat head;
    head.setFontFamilies(kHead);
    head.setFontPointSize(22);
    QTextBlockFormat hb;
    hb.setHeadingLevel(1);
    c.setBlockFormat(hb);
    c.insertText(QStringLiteral("Chapter"), head);
    QTextCharFormat body;
    body.setFontFamilies(kBody);
    body.setFontPointSize(12);
    c.insertBlock(QTextBlockFormat(), body);
    c.insertText(QStringLiteral("Plain "), body);
    QTextCharFormat hand = body;
    hand.setFontFamilies({QStringLiteral("Comic Neue")});
    hand.setFontPointSize(18);
    c.insertText(QStringLiteral("hand"), hand);
    c.insertText(QStringLiteral(" end"), body);
    const int undoBefore = doc.availableUndoSteps();

    const int n = StyleFonts::restyle(&doc, StyleFonts::builtinDefaults(), sample(), kBody, kHead);
    check(n == 2, "R two paragraphs restyled", QString::number(n));
    auto at = [&](int pos) {
        QTextCursor k(&doc);
        k.setPosition(pos);
        return k.charFormat();
    };
    check(firstFamily(at(3)) == QLatin1String("Futura") && at(3).fontPointSize() == 28, "R heading follows Heading 1",
          firstFamily(at(3)) + QLatin1Char(' ') + QString::number(at(3).fontPointSize()));
    const int plain = doc.toPlainText().indexOf(QLatin1String("Plain")) + 2;
    check(firstFamily(at(plain)) == QLatin1String("Georgia") && at(plain).fontPointSize() == 13,
          "R body follows Body Text", firstFamily(at(plain)));
    const int h = doc.toPlainText().indexOf(QLatin1String("hand")) + 2;
    check(firstFamily(at(h)) == QLatin1String("Comic Neue") && at(h).fontPointSize() == 18,
          "R hand-formatted run keeps its face and size", firstFamily(at(h)));
    doc.undo(); // one step undoes the whole restyle
    check(firstFamily(at(3)) == QLatin1String("Noto Sans") && at(3).fontPointSize() == 22
              && firstFamily(at(plain)) == QLatin1String("Courier New") && at(plain).fontPointSize() == 12,
          "R one undo restores heading and body");
    check(doc.availableUndoSteps() == undoBefore, "R ... and leaves the earlier history",
          QString::number(doc.availableUndoSteps() - undoBefore));
    doc.redo();
    StyleFonts::restyle(&doc, sample(), StyleFonts::builtinDefaults(), kBody, kHead);
    check(firstFamily(at(plain)) == QLatin1String("Courier New") && at(plain).fontPointSize() == 12,
          "R back to built-in: Courier 12 again");
    check(StyleFonts::restyle(&doc, sample(), sample(), kBody, kHead) == 0, "R nothing changed: nothing touched");
}

void testCss()
{
    check(StyleFonts::css(StyleFonts::builtinDefaults(), QStringLiteral("Heading1")).isEmpty(),
          "C built-in style: no CSS (EPUB unchanged)");
    const QString h1 = StyleFonts::css(sample(), QStringLiteral("Heading1"));
    check(h1 == QLatin1String("font-family:'Futura', sans-serif;font-size:28pt;"), "C Heading 1 CSS", h1);
    StyleFonts::Map evil = StyleFonts::builtinDefaults();
    evil[QStringLiteral("Body")] = {QStringLiteral("X'; } body { color:red"), 12};
    const QString b = StyleFonts::css(evil, QStringLiteral("Body"));
    check(!b.contains(QLatin1Char('}')) && b.count(QLatin1Char('\'')) == 2, "C family is quoted safely", b);
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    testModel();
    testJson();
    testSettings(dir.path());
    testOdtMeta(dir.path());
    testFormats();
    testRestyle();
    testCss();
    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
