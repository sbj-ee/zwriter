// Markdown mode tests (ctest, headless):
//   R  byte-faithful round-trip: open a .md, save it unedited -> identical bytes;
//      open, edit, save -> exactly the edit (LF / CRLF / BOM / no final newline
//      / no-break spaces / tabs / trailing spaces kept); undoing the edit gives
//      the original bytes again
//   S  MarkdownSyntax scanner: headings, emphasis, code, fences, links, lists,
//      quotes, escapes, snake_case
//   H  WritingHighlighter: Markdown formats on the layout, fenced-code state
//      across blocks, spell underline skipped inside code / URLs
//   F  focus-mode sentence ranges (QTextBoundaryFinder)
//   T  every theme has focus / markup colours; bundled iA fonts register
// Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "DocumentIo.hpp"
#include "FocusRange.hpp"
#include "MarkdownSyntax.hpp"
#include "Theme.hpp"
#include "WritingHighlighter.hpp"

#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.isEmpty() ? "" : "  -- ",
                qPrintable(detail));
    if (!ok) {
        ++g_failures;
    }
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString show(const QByteArray &bytes)
{
    QString out = QString::fromUtf8(bytes.left(80));
    out.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    out.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return out;
}

// ------------------------------------------------------------- round trip

void testRoundTrip(const QString &dir)
{
    using DocumentIo::Format;
    check(DocumentIo::formatFromPath(QStringLiteral("a.md")) == Format::Markdown, "R .md is Markdown");
    check(DocumentIo::formatFromPath(QStringLiteral("a.MARKDOWN")) == Format::Markdown,
          "R .markdown (any case) is Markdown");
    check(DocumentIo::formatFromPath(QStringLiteral("a.txt")) == Format::Txt, "R .txt stays plain text");
    check(DocumentIo::formatFromFilter(QStringLiteral("Markdown (*.md)"), QString()) == Format::Markdown,
          "R Markdown save filter maps to Markdown");
    check(DocumentIo::formatName(Format::Markdown) == QLatin1String("md"), "R Markdown suffix is md");

    struct Case { const char *name; QByteArray bytes; };
    const QByteArray nbsp = QByteArrayLiteral("\xC2\xA0");
    const Case cases[] = {
        {"LF with final newline",
         QByteArrayLiteral("# Title\n\nSome *emphasis* and **strong** text.\n\n- one\n- two\n")},
        {"no final newline", QByteArrayLiteral("Line one\nLine two")},
        {"CRLF", QByteArrayLiteral("# Title\r\n\r\nBody line.\r\nSecond line.\r\n")},
        {"UTF-8 BOM", QByteArrayLiteral("\xEF\xBB\xBF# Caf\xC3\xA9\n\nna\xC3\xAFve \xE2\x80\x94 \xF0\x9F\x93\x9D\n")},
        {"no-break spaces, tabs, trailing spaces, blank lines",
         QByteArrayLiteral("a") + nbsp + QByteArrayLiteral("b\tc  \n\n\n\t```\n\tcode\n```\n   \n")},
        {"empty file", QByteArray()},
        {"only newlines", QByteArrayLiteral("\n\n\n")},
    };
    int n = 0;
    for (const Case &c : cases) {
        const QString path = dir + QStringLiteral("/case%1.md").arg(++n);
        writeFile(path, c.bytes);
        QTextDocument doc;
        QString err;
        const bool loaded = DocumentIo::load(&doc, path, &err);
        const QString out = dir + QStringLiteral("/case%1-out.md").arg(n);
        const bool saved = loaded && DocumentIo::save(&doc, out, Format::Markdown, &err);
        const QByteArray back = readFile(out);
        check(loaded && saved && back == c.bytes,
              qPrintable(QStringLiteral("R unedited round-trip is byte-identical: %1")
                             .arg(QString::fromLatin1(c.name))),
              back == c.bytes ? QString() : show(back) + QStringLiteral(" | err ") + err);
        check(!doc.isModified() && !DocumentIo::markdownLoadWasLossy(&doc),
              qPrintable(QStringLiteral("R opening does not mark modified, not lossy: %1")
                             .arg(QString::fromLatin1(c.name))));
    }

    // Files that cannot round-trip byte for byte are flagged (the UI says so).
    for (const QByteArray &bytes : {QByteArrayLiteral("a\rb\n"), QByteArrayLiteral("a\r\nb\nc\r\n"),
                                    QByteArrayLiteral("x\xff\x80y\n")}) {
        const QString path = dir + QStringLiteral("/lossy.md");
        writeFile(path, bytes);
        QTextDocument doc;
        DocumentIo::load(&doc, path);
        check(DocumentIo::markdownLoadWasLossy(&doc), "R mixed line endings / invalid UTF-8 flagged lossy",
              show(bytes));
    }

    // Edit: append to the first line and add a new paragraph in a CRLF file.
    {
        const QByteArray original = QByteArrayLiteral("\xEF\xBB\xBF# Title\r\n\r\nBody") + nbsp
            + QByteArrayLiteral("line.\r\n");
        const QString path = dir + QStringLiteral("/edit.md");
        writeFile(path, original);
        QTextDocument doc;
        DocumentIo::load(&doc, path);
        QTextCursor c(doc.firstBlock());
        c.movePosition(QTextCursor::EndOfBlock);
        c.insertText(QStringLiteral(" two"));
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("New *line*"));
        c.insertBlock();
        const QString out = dir + QStringLiteral("/edit-out.md");
        DocumentIo::save(&doc, out, Format::Markdown);
        const QByteArray expected = QByteArrayLiteral("\xEF\xBB\xBF# Title two\r\n\r\nBody") + nbsp
            + QByteArrayLiteral("line.\r\nNew *line*\r\n");
        const QByteArray got = readFile(out);
        check(got == expected, "R edited CRLF+BOM file saves exactly the edit", show(got));

        while (doc.isUndoAvailable()) {
            doc.undo();
        }
        DocumentIo::save(&doc, out, Format::Markdown);
        check(readFile(out) == original, "R undoing every edit gives the original bytes",
              show(readFile(out)));
        check(!readFile(out).startsWith("PK"), "R Markdown is written as text, never as an ODT zip");
    }

    // Shift+Enter style line separators are written as the file's line ending.
    {
        QTextDocument doc;
        DocumentIo::resetMarkdownFileFormat(&doc);
        QTextCursor c(&doc);
        c.insertText(QStringLiteral("one"));
        c.insertText(QString(QChar::LineSeparator));
        c.insertText(QStringLiteral("two"));
        const QString out = dir + QStringLiteral("/linesep.md");
        DocumentIo::save(&doc, out, Format::Markdown);
        check(readFile(out) == QByteArrayLiteral("one\ntwo"), "R line separator saved as LF",
              show(readFile(out)));
    }

    // Markdown mode's 150 % line height is layout only: the bytes do not change.
    {
        const QByteArray original = QByteArrayLiteral("a\nb\n");
        const QString path = dir + QStringLiteral("/leading.md");
        writeFile(path, original);
        QTextDocument doc;
        DocumentIo::load(&doc, path);
        QTextBlockFormat fmt;
        fmt.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
        QTextCursor all(&doc);
        all.select(QTextCursor::Document);
        all.mergeBlockFormat(fmt);
        const QString out = dir + QStringLiteral("/leading-out.md");
        DocumentIo::save(&doc, out, Format::Markdown);
        check(readFile(out) == original, "R block formats (line height) never reach the file");
    }
}

// ---------------------------------------------------------------- scanner

using namespace MarkdownSyntax;

bool allHave(const BlockResult &r, int from, int to, quint16 bits)
{
    for (int i = from; i < to; ++i) {
        if ((r.flags.at(i) & bits) != bits) {
            return false;
        }
    }
    return true;
}

bool noneHave(const BlockResult &r, int from, int to, quint16 bits)
{
    for (int i = from; i < to; ++i) {
        if (r.flags.at(i) & bits) {
            return false;
        }
    }
    return true;
}

void testScanner()
{
    {
        const BlockResult r = scan(QStringLiteral("## Heading *two*"), kStateNormal);
        check(r.headingLevel == 2, "S ATX heading level 2");
        check(allHave(r, 0, 3, Markup | Heading) && noneHave(r, 3, 11, Markup),
              "S heading marker '## ' is markup, the title is not");
        check(allHave(r, 12, 15, Emphasis) && allHave(r, 11, 12, Markup)
                  && allHave(r, 15, 16, Markup) && noneHave(r, 12, 15, Markup),
              "S emphasis inside a heading");
    }
    check(scan(QStringLiteral("#hashtag"), kStateNormal).headingLevel == 0,
          "S '#word' without a space is not a heading");
    {
        const QString s = QStringLiteral("a **bold** and *it* and ***both***");
        const BlockResult r = scan(s, kStateNormal);
        check(allHave(r, 2, 4, Markup | Strong) && allHave(r, 4, 8, Strong) && noneHave(r, 4, 8, Markup)
                  && allHave(r, 8, 10, Markup),
              "S **strong** with greyed delimiters");
        const int it = int(s.indexOf(QLatin1String("*it*")));
        check(allHave(r, it + 1, it + 3, Emphasis) && noneHave(r, it + 1, it + 3, Strong),
              "S *emphasis*");
        const int both = int(s.indexOf(QLatin1String("***both")));
        check(allHave(r, both + 3, both + 7, Strong | Emphasis), "S ***strong emphasis***");
        check(noneHave(r, 0, 1, Strong | Emphasis | Markup), "S plain text stays plain");
    }
    {
        const BlockResult r = scan(QStringLiteral("snake_case_name and 2 * 3 * 4"), kStateNormal);
        check(noneHave(r, 0, int(r.flags.size()), Emphasis | Markup),
              "S intraword _ and spaced * are not emphasis");
    }
    {
        const QString s = QStringLiteral("use `a *b* c` here");
        const BlockResult r = scan(s, kStateNormal);
        check(allHave(r, 4, 5, Markup | Code) && allHave(r, 5, 12, Code) && allHave(r, 12, 13, Markup | Code),
              "S inline code span");
        check(noneHave(r, 5, 12, Emphasis), "S no emphasis inside code");
    }
    {
        const QString s = QStringLiteral("see [the *docs*](https://example.com/a_b_c) now");
        const BlockResult r = scan(s, kStateNormal);
        const int open = int(s.indexOf(QLatin1Char('[')));
        const int close = int(s.indexOf(QLatin1Char(']')));
        const int paren = int(s.indexOf(QLatin1Char(')')));
        check(allHave(r, open, open + 1, Markup) && allHave(r, open + 1, close, LinkText)
                  && allHave(r, close, close + 2, Markup) && allHave(r, close + 2, paren, Url)
                  && allHave(r, paren, paren + 1, Markup),
              "S [link](url): brackets markup, text, url");
        check(allHave(r, open + 6, open + 10, Emphasis), "S emphasis inside link text");
        check(noneHave(r, close + 2, paren, Emphasis), "S underscores in a URL are not emphasis");
    }
    {
        const QString s = QStringLiteral("![alt](pic.png) and <https://x.org> and https://y.org/p.");
        const BlockResult r = scan(s, kStateNormal);
        check(allHave(r, 0, 2, Markup) && allHave(r, 2, 5, LinkText), "S ![image](src)");
        const int a = int(s.indexOf(QLatin1Char('<')));
        check(allHave(r, a, a + 1, Markup | Url) && allHave(r, a + 1, a + 14, Url), "S <autolink>");
        const int b = int(s.indexOf(QLatin1String("https://y")));
        check(allHave(r, b, int(s.size()) - 1, Url) && noneHave(r, int(s.size()) - 1, int(s.size()), Url),
              "S bare URL, trailing full stop excluded");
    }
    {
        const BlockResult r = scan(QStringLiteral("  - [x] done *now*"), kStateNormal);
        check(noneHave(r, 0, 2, Markup) && allHave(r, 2, 3, Markup | ListMarker)
                  && allHave(r, 4, 7, Markup | ListMarker),
              "S list bullet and task box are list markup");
        check(allHave(r, 13, 16, Emphasis), "S inline styling in a list item");
        const BlockResult n = scan(QStringLiteral("12. item"), kStateNormal);
        check(allHave(n, 0, 3, Markup | ListMarker) && noneHave(n, 4, 8, Markup), "S ordered list marker");
    }
    {
        const BlockResult r = scan(QStringLiteral("> > quoted **text**"), kStateNormal);
        check(allHave(r, 0, 1, Markup) && allHave(r, 2, 3, Markup) && allHave(r, 0, 19, Quote),
              "S nested blockquote markers, whole line quoted");
        check(allHave(r, 13, 17, Strong), "S strong inside a quote");
    }
    {
        const BlockResult r = scan(QStringLiteral("* * *"), kStateNormal);
        check(allHave(r, 0, 5, Markup | Rule), "S '* * *' is a thematic break, not a list");
    }
    {
        const BlockResult r = scan(QStringLiteral("not \\*emphasis\\*"), kStateNormal);
        check(noneHave(r, 0, int(r.flags.size()), Emphasis) && allHave(r, 4, 5, Markup),
              "S backslash escapes: markup, no emphasis");
    }
    {
        const BlockResult open = scan(QStringLiteral("```cpp"), kStateNormal);
        check(open.state != kStateNormal && allHave(open, 0, 6, Markup | CodeBlock), "S fence opens");
        const BlockResult inner = scan(QStringLiteral("# not a heading *x*"), open.state);
        check(inner.state == open.state && inner.headingLevel == 0
                  && allHave(inner, 0, int(inner.flags.size()), CodeBlock)
                  && noneHave(inner, 0, int(inner.flags.size()), Markup | Emphasis),
              "S inside a fence everything is code");
        const BlockResult tilde = scan(QStringLiteral("~~~"), open.state);
        check(tilde.state == open.state, "S a ~~~ line does not close a ``` fence");
        const BlockResult shorter = scan(QStringLiteral("``"), open.state);
        check(shorter.state == open.state, "S a shorter run does not close the fence");
        const BlockResult close = scan(QStringLiteral("```"), open.state);
        check(close.state == kStateNormal && allHave(close, 0, 3, Markup | CodeBlock), "S fence closes");
    }
    check(!isSpellCheckable(Code) && !isSpellCheckable(Url) && !isSpellCheckable(Markup)
              && isSpellCheckable(Strong | Heading),
          "S spell check skips code / URLs / markup only");
}

// ------------------------------------------------------------- highlighter

bool rangeHas(const QTextBlock &block, int from, int to,
              const std::function<bool(const QTextCharFormat &)> &pred)
{
    for (int i = from; i < to; ++i) {
        bool hit = false;
        for (const QTextLayout::FormatRange &r : block.layout()->formats()) {
            if (i >= r.start && i < r.start + r.length && pred(r.format)) {
                hit = true;
            }
        }
        if (!hit) {
            return false;
        }
    }
    return true;
}

void testHighlighter()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("# Title\nteh **bold** `teh`\n```\nint teh;\n```\nafter *x*"));
    WritingHighlighter hl(&doc);
    const ThemeColors paper = Theme::colors(QStringLiteral("paper"));
    hl.setColors(paper);
    hl.setMisspelledFunction([](const QString &w) { return w == QLatin1String("teh"); });

    const QTextBlock line2 = doc.findBlockByNumber(1);
    const auto underlined = [](const QTextCharFormat &f) {
        return f.underlineStyle() == QTextCharFormat::SpellCheckUnderline;
    };
    check(rangeHas(line2, 0, 3, underlined), "H spell check alone underlines (Markdown off)");
    check(!rangeHas(line2, 6, 10, [](const QTextCharFormat &f) { return f.fontWeight() >= QFont::Bold; }),
          "H no Markdown styling while Markdown is off");

    hl.setMarkdownEnabled(true);
    const QTextBlock title = doc.firstBlock();
    check(rangeHas(title, 2, 7, [](const QTextCharFormat &f) { return f.fontWeight() >= QFont::Bold; }),
          "H heading text is bold");
    check(rangeHas(title, 0, 1, [&](const QTextCharFormat &f) { return f.foreground().color() == paper.markup; }),
          "H heading marker in the theme's markup colour");
    check(rangeHas(line2, 6, 10, [](const QTextCharFormat &f) { return f.fontWeight() >= QFont::Bold; }),
          "H **bold** text is bold");
    check(rangeHas(line2, 0, 3, underlined), "H misspelling in prose is underlined");
    check(!rangeHas(line2, 14, 17, underlined), "H no underline inside inline code");
    const QTextBlock code = doc.findBlockByNumber(3);
    check(rangeHas(code, 0, 8, [&](const QTextCharFormat &f) { return f.foreground().color() == paper.code; }),
          "H fenced code line in the code colour (state carried across blocks)");
    check(!rangeHas(code, 4, 7, underlined), "H no underline inside fenced code");
    const QTextBlock after = doc.findBlockByNumber(5);
    check(rangeHas(after, 7, 8, [](const QTextCharFormat &f) { return f.fontItalic(); }),
          "H text after the closing fence is Markdown again");

    const ThemeColors dark = Theme::colors(QStringLiteral("dark"));
    hl.setColors(dark);
    check(rangeHas(title, 0, 1, [&](const QTextCharFormat &f) { return f.foreground().color() == dark.markup; }),
          "H theme change recolours the markup");
    check(doc.toPlainText().startsWith(QLatin1String("# Title")), "H highlighting never edits the text");
}

// ------------------------------------------------------------------ focus

void testFocus()
{
    const QString t = QStringLiteral("First one. Second is 3.5 long. Third ends here.");
    auto range = [&](int pos) {
        const QPair<int, int> r = FocusRange::sentenceRange(t, pos);
        return t.mid(r.first, r.second - r.first);
    };
    check(range(0) == QLatin1String("First one."), "F caret at start: first sentence", range(0));
    check(range(10) == QLatin1String("First one."), "F caret just after the full stop stays in it", range(10));
    check(range(11) == QLatin1String("Second is 3.5 long."), "F caret at next sentence start",
          range(11));
    check(range(22) == QLatin1String("Second is 3.5 long."), "F decimal point is not a sentence end",
          range(22));
    check(range(int(t.size())) == QLatin1String("Third ends here."), "F caret at paragraph end",
          range(int(t.size())));
    const QString q = QStringLiteral("He waited... then left. \u201CGo.\u201D She went.");
    const QPair<int, int> r1 = FocusRange::sentenceRange(q, 3);
    check(q.mid(r1.first, r1.second - r1.first) == QLatin1String("He waited... then left."),
          "F ellipsis before lower case does not split", q.mid(r1.first, r1.second - r1.first));
    const QPair<int, int> r2 = FocusRange::sentenceRange(q, int(q.indexOf(QLatin1String("Go"))));
    check(q.mid(r2.first, r2.second - r2.first) == QStringLiteral("\u201CGo.\u201D"),
          "F closing quote belongs to its sentence", q.mid(r2.first, r2.second - r2.first));
    const QString ls = QStringLiteral("line one") + QChar(QChar::LineSeparator) + QStringLiteral("line two");
    const QPair<int, int> r3 = FocusRange::sentenceRange(ls, 12);
    check(ls.mid(r3.first, r3.second - r3.first) == QLatin1String("line two"),
          "F a line separator ends a sentence");
    check(FocusRange::sentenceRange(QString(), 0) == qMakePair(0, 0), "F empty paragraph");
}

// ------------------------------------------------------------ theme, fonts

void testThemeAndFonts()
{
    bool ok = true;
    for (const char *id : {"paper", "dark", "inverse"}) {
        const ThemeColors c = Theme::colors(QString::fromLatin1(id));
        ok = ok && c.focusDim.isValid() && c.markup.isValid() && c.code.isValid() && c.link.isValid()
            && c.quote.isValid() && c.focusDim != c.pageFg && c.focusDim != c.pageBg;
    }
    check(ok, "T every theme defines focusDim / markup / code / link / quote");
    check(Theme::colors(QStringLiteral("paper")).focusDim != Theme::colors(QStringLiteral("dark")).focusDim,
          "T focus dim differs between light and dark themes");
    const QString css = Theme::styleSheet(Theme::colors(QStringLiteral("paper")), false, 13,
                                          StyleOptions{QStringLiteral("'iA Writer Duo S'"), true});
    check(css.contains(QLatin1String("font-family: 'iA Writer Duo S'")) && !css.contains(QLatin1Char('@')),
          "T Markdown stylesheet uses the Duo face and resolves every token");
    const QString odtCss = Theme::styleSheet(Theme::colors(QStringLiteral("paper")), true, 12);
    check(odtCss.contains(QLatin1String("font-family: 'Courier New'")), "T ODT stylesheet keeps Courier");

    QStringList families;
    for (const char *name : {"iAWriterDuoS-Regular", "iAWriterDuoS-Bold", "iAWriterDuoS-Italic",
                             "iAWriterDuoS-BoldItalic", "iAWriterMonoS-Regular"}) {
        const int id = QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/%1.ttf").arg(QLatin1String(name)));
        if (id >= 0) {
            families += QFontDatabase::applicationFontFamilies(id);
        }
    }
    check(families.count(QStringLiteral("iA Writer Duo S")) == 4, "T four iA Writer Duo S styles register",
          families.join(QLatin1String(", ")));
    check(families.contains(QStringLiteral("iA Writer Mono S")), "T iA Writer Mono S registers");
    check(QFile::exists(QStringLiteral(":/fonts/LICENSE.md"))
              && readFile(QStringLiteral(":/fonts/LICENSE.md")).contains("SIL Open Font License"),
          "T the OFL licence is embedded with the fonts");
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("FAIL  temp dir\n");
        return 1;
    }
    testRoundTrip(dir.path());
    testScanner();
    testHighlighter();
    testFocus();
    testThemeAndFonts();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
