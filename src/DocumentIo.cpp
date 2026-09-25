#include "DocumentIo.hpp"

#include <QStringView>

#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QFileInfo>
#include <QProcess>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentWriter>

#include <memory>
#include <QList>
#include <QTextBlock>
#include <QStringList>
#include <QBrush>
#include <QColor>
#include <QTextTableFormat>
#include <QTextTable>
#include <QTextFrame>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QSet>
#include <QTextList>
#include <QTextFragment>
#include <functional>
#include <QtGlobal>

namespace DocumentIo {
namespace {

QString extensionOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

// ---------------------------------------------------------------- RTF

// Windows-1252 bytes 0x80..0x9F (the rest of the code page is Latin-1).
QChar cp1252(int byte)
{
    static const char16_t high[32] = {
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
        0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    if (byte >= 0x80 && byte <= 0x9F) {
        return QChar(high[byte - 0x80]);
    }
    return QChar(byte & 0xFF);
}

QString htmlEscaped(QStringView text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        if (ch == QLatin1Char('<')) {
            out += QStringLiteral("&lt;");
        } else if (ch == QLatin1Char('>')) {
            out += QStringLiteral("&gt;");
        } else if (ch == QLatin1Char('&')) {
            out += QStringLiteral("&amp;");
        } else if (ch == QLatin1Char('"')) {
            out += QStringLiteral("&quot;");
        } else {
            out += ch;
        }
    }
    return out;
}

constexpr char16_t kRtfBlankParagraphMark = 0xE000; // same as kBlankParagraphMark below

// RTF -> simple HTML for QTextDocument: paragraphs, bold / italic / underline,
// large font sizes (headings), tables, page breaks and Unicode text. Font,
// colour and style tables, document info and every other destination are
// skipped. Body font and size follow zwriter's defaults.
QString rtfToHtml(const QByteArray &rtf)
{
    struct State {
        bool skip = false;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        int halfPoints = 24;
        int uc = 1; // chars to skip after \uN
    };
    struct Inline {
        bool bold = false;
        bool italic = false;
        bool underline = false;
        int halfPoints = 24;
        bool operator==(const Inline &o) const
        {
            return bold == o.bold && italic == o.italic && underline == o.underline
                && halfPoints == o.halfPoints;
        }
    };
    static const QSet<QByteArray> skipDestinations = {
        "fonttbl", "colortbl", "stylesheet", "info", "pict", "header", "headerl",
        "headerr", "headerf", "footer", "footerl", "footerr", "footerf", "footnote",
        "listtable", "listoverridetable", "rsidtbl", "generator", "xmlnstbl",
        "themedata", "colorschememapping", "datastore", "latentstyles", "pgdsctbl",
        "object", "fldinst", "revtbl", "filetbl", "pntxta", "pntxtb", "userprops", "bkmkstart", "bkmkend", "ftnsep", "ftnsepc",
        "aftnsep", "aftnsepc", "operator", "author", "title", "subject", "company"};

    QList<State> stack{State{}};
    QString html = QStringLiteral("<html><body>");
    QString para;           // inline HTML of the current paragraph
    bool paraHasText = false;
    Inline emitted;         // style of the open <span>, if any
    bool spanOpen = false;
    bool inTable = false;   // paragraph properties: \intbl
    bool pendingBreak = false;
    QString cellHtml;
    QStringList rowCells;
    bool tableOpen = false;
    int skipChars = 0;      // fallback characters after \uN

    auto closeSpan = [&]() {
        if (spanOpen) {
            para += QStringLiteral("</span>");
            spanOpen = false;
        }
    };
    auto appendText = [&](const QString &text) {
        const State &st = stack.last();
        if (st.skip || text.isEmpty()) {
            return;
        }
        const Inline want{st.bold, st.italic, st.underline, st.halfPoints};
        const bool plain = !want.bold && !want.italic && !want.underline && want.halfPoints < 26;
        if (spanOpen && !(emitted == want)) {
            closeSpan();
        }
        if (!spanOpen && !plain) {
            QString css;
            if (want.bold) {
                css += QStringLiteral("font-weight:700;");
            }
            if (want.italic) {
                css += QStringLiteral("font-style:italic;");
            }
            if (want.underline) {
                css += QStringLiteral("text-decoration:underline;");
            }
            if (want.halfPoints >= 26) {
                // Only headings-sized text keeps its size; body text follows
                // zwriter's 12 pt default rather than Word's 11 pt.
                css += QStringLiteral("font-size:%1pt;").arg(want.halfPoints / 2.0);
            }
            para += QStringLiteral("<span style=\"%1\">").arg(css);
            spanOpen = true;
            emitted = want;
        }
        para += htmlEscaped(text);
        paraHasText = true;
    };
    auto wrapParagraph = [&]() -> QString {
        closeSpan();
        QString css = QStringLiteral("margin:0px;white-space:pre-wrap;");
        if (pendingBreak) {
            css += QStringLiteral("page-break-before:always;");
            pendingBreak = false;
        }
        const QString body = paraHasText ? para : QString(QChar(kRtfBlankParagraphMark));
        para.clear();
        paraHasText = false;
        return QStringLiteral("<p style=\"%1\">%2</p>").arg(css, body);
    };
    auto closeTable = [&]() {
        if (tableOpen) {
            html += QStringLiteral("</table>");
            tableOpen = false;
        }
    };
    auto endParagraph = [&]() {
        if (inTable) {
            cellHtml += wrapParagraph();
        } else {
            closeTable();
            html += wrapParagraph();
        }
    };
    auto endCell = [&]() {
        if (paraHasText || cellHtml.isEmpty()) {
            cellHtml += wrapParagraph();
        } else {
            closeSpan();
            para.clear();
        }
        rowCells.append(cellHtml);
        cellHtml.clear();
    };
    auto endRow = [&]() {
        if (paraHasText) {
            endCell();
        }
        if (rowCells.isEmpty()) {
            return;
        }
        if (!tableOpen) {
            html += QStringLiteral("<table>");
            tableOpen = true;
        }
        html += QStringLiteral("<tr>");
        for (const QString &cell : std::as_const(rowCells)) {
            html += QStringLiteral("<td>") + cell + QStringLiteral("</td>");
        }
        html += QStringLiteral("</tr>");
        rowCells.clear();
    };

    const int n = int(rtf.size());
    int i = 0;
    bool groupStart = false; // the next control word is the first in its group
    while (i < n) {
        const char c = rtf.at(i);
        if (c == '{') {
            State st = stack.last();
            stack.append(st);
            groupStart = true;
            skipChars = 0;
            ++i;
            continue;
        }
        if (c == '}') {
            if (stack.size() > 1) {
                stack.removeLast();
            }
            groupStart = false;
            skipChars = 0;
            ++i;
            continue;
        }
        if (c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        if (c != '\\') {
            // Plain text run (bytes are ANSI; zwriter writes ASCII + \uN).
            QString run;
            while (i < n) {
                const char t = rtf.at(i);
                if (t == '\\' || t == '{' || t == '}') {
                    break;
                }
                ++i;
                if (t == '\r' || t == '\n') {
                    continue;
                }
                if (skipChars > 0) {
                    --skipChars;
                    continue;
                }
                run += cp1252(static_cast<unsigned char>(t));
            }
            appendText(run);
            groupStart = false;
            continue;
        }
        // Backslash: control symbol or control word.
        ++i;
        if (i >= n) {
            break;
        }
        const char s = rtf.at(i);
        if (!((s >= 'a' && s <= 'z') || (s >= 'A' && s <= 'Z'))) {
            ++i;
            const bool first = groupStart;
            groupStart = false;
            if (s == '\'' && i + 1 < n) {
                bool ok = false;
                const int v = QByteArray(rtf.constData() + i, 2).toInt(&ok, 16);
                i += 2;
                if (skipChars > 0) {
                    --skipChars;
                } else if (ok) {
                    appendText(QString(cp1252(v)));
                }
            } else if (s == '*') {
                if (first) {
                    stack.last().skip = true; // unknown-destination marker
                }
            } else if (s == '~') {
                appendText(QString(QChar(0x00A0)));
            } else if (s == '_') {
                appendText(QString(QChar(0x2011)));
            } else if (s == '-') {
                // optional hyphen: nothing to show
            } else if (s == '\n' || s == '\r') {
                if (!stack.last().skip) {
                    endParagraph();
                }
            } else if (s == '\\' || s == '{' || s == '}') {
                appendText(QString(QLatin1Char(s)));
            }
            continue;
        }
        const int wordStart = i;
        while (i < n && ((rtf.at(i) >= 'a' && rtf.at(i) <= 'z') || (rtf.at(i) >= 'A' && rtf.at(i) <= 'Z'))) {
            ++i;
        }
        const QByteArray word = rtf.mid(wordStart, i - wordStart);
        bool hasParam = false;
        int param = 0;
        if (i < n && (rtf.at(i) == '-' || (rtf.at(i) >= '0' && rtf.at(i) <= '9'))) {
            const int numStart = i;
            ++i;
            while (i < n && rtf.at(i) >= '0' && rtf.at(i) <= '9') {
                ++i;
            }
            param = rtf.mid(numStart, i - numStart).toInt(&hasParam);
        }
        if (i < n && rtf.at(i) == ' ') {
            ++i; // delimiter
        }
        const bool first = groupStart;
        groupStart = false;
        State &st = stack.last();
        if (first && skipDestinations.contains(word)) {
            st.skip = true;
            continue;
        }
        if (word == "u" && hasParam) {
            if (skipChars > 0) {
                --skipChars;
                continue;
            }
            appendText(QString(QChar(char16_t(param < 0 ? param + 65536 : param))));
            skipChars = st.uc;
            continue;
        }
        skipChars = 0;
        if (st.skip) {
            continue;
        }
        const bool on = !hasParam || param != 0;
        if (word == "par") {
            endParagraph();
        } else if (word == "line") {
            appendText(QString(QChar(QChar::LineSeparator)));
        } else if (word == "tab") {
            appendText(QStringLiteral("\t"));
        } else if (word == "page") {
            endParagraph();
            pendingBreak = true;
        } else if (word == "b") {
            st.bold = on;
        } else if (word == "i") {
            st.italic = on;
        } else if (word == "ul") {
            st.underline = on;
        } else if (word == "ulnone") {
            st.underline = false;
        } else if (word == "fs" && hasParam && param > 0) {
            st.halfPoints = param;
        } else if (word == "plain") {
            st.bold = st.italic = st.underline = false;
            st.halfPoints = 24;
        } else if (word == "uc" && hasParam) {
            st.uc = qMax(0, param);
        } else if (word == "pard") {
            inTable = false;
        } else if (word == "intbl") {
            inTable = true;
        } else if (word == "cell" || word == "nestcell") {
            endCell();
        } else if (word == "row" || word == "nestrow") {
            endRow();
        } else if (word == "emdash") {
            appendText(QString(QChar(0x2014)));
        } else if (word == "endash") {
            appendText(QString(QChar(0x2013)));
        } else if (word == "lquote") {
            appendText(QString(QChar(0x2018)));
        } else if (word == "rquote") {
            appendText(QString(QChar(0x2019)));
        } else if (word == "ldblquote") {
            appendText(QString(QChar(0x201C)));
        } else if (word == "rdblquote") {
            appendText(QString(QChar(0x201D)));
        } else if (word == "bullet") {
            appendText(QString(QChar(0x2022)));
        }
    }
    if (paraHasText) {
        if (inTable) {
            endCell();
            endRow();
        } else {
            endParagraph();
        }
    }
    endRow();
    closeTable();
    html += QStringLiteral("</body></html>");
    return html;
}

// RTF escaping for one run of text.
QString rtfEscaped(QStringView text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar c : text) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('{') || c == QLatin1Char('}')) {
            out += QLatin1Char('\\');
            out += c;
        } else if (c == QLatin1Char('\t')) {
            out += QStringLiteral("\\tab ");
        } else if (c == QChar::LineSeparator || c == QLatin1Char('\n')) {
            out += QStringLiteral("\\line ");
        } else if (c == QChar(0x00A0)) {
            out += QStringLiteral("\\~");
        } else if (c.unicode() > 127) {
            const int v = c.unicode();
            out += QStringLiteral("\\u%1?").arg(v > 32767 ? v - 65536 : v);
        } else if (c.unicode() >= 32) {
            out += c;
        }
    }
    return out;
}

// QTextDocument -> RTF: paragraphs with alignment, bold / italic / underline,
// font family and size, lists (as literal bullets/numbers), page breaks and
// tables. Readable by LibreOffice, Word, TextEdit and zwriter itself.
QString documentToRtf(const QTextDocument *doc)
{
    const QFont base = doc->defaultFont();
    const QString baseFamily = base.family().isEmpty() ? QStringLiteral("Courier New") : base.family();
    const qreal basePt = base.pointSizeF() > 0 ? base.pointSizeF() : 12.0;

    QStringList fonts{baseFamily};
    auto fontIndex = [&fonts](const QString &family) {
        int idx = int(fonts.indexOf(family));
        if (idx < 0) {
            fonts.append(family);
            idx = int(fonts.size()) - 1;
        }
        return idx;
    };

    auto blockRtf = [&](const QTextBlock &block, bool inCell) -> QString {
        QString out = QStringLiteral("\\pard\\plain");
        if (inCell) {
            out += QStringLiteral("\\intbl");
        }
        const QTextBlockFormat bf = block.blockFormat();
        if (bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) {
            out.prepend(QStringLiteral("\\page"));
        }
        const Qt::Alignment al = bf.alignment();
        if (al & Qt::AlignHCenter) {
            out += QStringLiteral("\\qc");
        } else if (al & Qt::AlignRight) {
            out += QStringLiteral("\\qr");
        } else if (al & Qt::AlignJustify) {
            out += QStringLiteral("\\qj");
        }
        out += QStringLiteral("\\f0\\fs%1 ").arg(qRound(basePt * 2));
        if (const QTextList *list = block.textList()) {
            const auto style = list->format().style();
            const bool bullet = style == QTextListFormat::ListDisc
                                || style == QTextListFormat::ListCircle
                                || style == QTextListFormat::ListSquare;
            // Qt has no marker text for bullets; RTF's \bullet is U+2022.
            out += bullet ? QStringLiteral("{\\bullet\\tab }")
                          : QStringLiteral("{%1\\tab }").arg(rtfEscaped(list->itemText(block)));
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) {
                continue;
            }
            const QTextCharFormat cf = frag.charFormat();
            QString ctl;
            const QStringList fams = cf.fontFamilies().toStringList();
            const QString fam = fams.isEmpty() ? baseFamily : fams.first();
            const int fi = fontIndex(fam);
            if (fi != 0) {
                ctl += QStringLiteral("\\f%1").arg(fi);
            }
            const qreal pt = cf.fontPointSize() > 0 ? cf.fontPointSize() : basePt;
            if (!qFuzzyCompare(pt, basePt)) {
                ctl += QStringLiteral("\\fs%1").arg(qRound(pt * 2));
            }
            if (cf.fontWeight() >= QFont::DemiBold) {
                ctl += QStringLiteral("\\b");
            }
            if (cf.fontItalic()) {
                ctl += QStringLiteral("\\i");
            }
            if (cf.fontUnderline()) {
                ctl += QStringLiteral("\\ul");
            }
            if (cf.fontStrikeOut()) {
                ctl += QStringLiteral("\\strike");
            }
            QString text = frag.text();
            text.remove(QChar::ObjectReplacementCharacter);
            if (ctl.isEmpty()) {
                out += rtfEscaped(text);
            } else {
                out += QStringLiteral("{%1 %2}").arg(ctl, rtfEscaped(text));
            }
        }
        return out;
    };

    QString body;
    std::function<void(QTextFrame *)> writeFrame = [&](QTextFrame *frame) {
        for (auto it = frame->begin(); !it.atEnd(); ++it) {
            if (QTextFrame *child = it.currentFrame()) {
                if (auto *table = qobject_cast<QTextTable *>(child)) {
                    const int cols = qMax(1, table->columns());
                    const int textTwips = 9360; // 6.5 in text width
                    for (int r = 0; r < table->rows(); ++r) {
                        body += QStringLiteral("\\trowd\\trgaph108\\trleft0");
                        for (int col = 0; col < cols; ++col) {
                            body += QStringLiteral(
                                        "\\clbrdrt\\brdrs\\brdrw10\\brdrcf1\\clbrdrl\\brdrs\\brdrw10\\brdrcf1"
                                        "\\clbrdrb\\brdrs\\brdrw10\\brdrcf1\\clbrdrr\\brdrs\\brdrw10\\brdrcf1"
                                        "\\cellx%1")
                                        .arg(textTwips * (col + 1) / cols);
                        }
                        body += QLatin1Char('\n');
                        for (int col = 0; col < cols; ++col) {
                            const QTextTableCell cell = table->cellAt(r, col);
                            bool firstBlock = true;
                            if (cell.isValid() && cell.row() == r && cell.column() == col) {
                                for (auto ci = cell.begin(); !ci.atEnd(); ++ci) {
                                    const QTextBlock b = ci.currentBlock();
                                    if (!b.isValid()) {
                                        continue;
                                    }
                                    if (!firstBlock) {
                                        body += QStringLiteral("\\par\n");
                                    }
                                    body += blockRtf(b, true);
                                    firstBlock = false;
                                }
                            }
                            if (firstBlock) {
                                body += QStringLiteral("\\pard\\plain\\intbl ");
                            }
                            body += QStringLiteral("\\cell\n");
                        }
                        body += QStringLiteral("\\row\n");
                    }
                    body += QStringLiteral("\\pard\n");
                } else {
                    writeFrame(child);
                }
            } else {
                const QTextBlock b = it.currentBlock();
                if (b.isValid()) {
                    body += blockRtf(b, false);
                    body += QStringLiteral("\\par\n");
                }
            }
        }
    };
    writeFrame(doc->rootFrame());

    QString fontTable;
    for (int f = 0; f < fonts.size(); ++f) {
        const bool mono = f == 0 ? base.fixedPitch() || baseFamily.contains(QLatin1String("Courier"))
                                 : fonts.at(f).contains(QLatin1String("Mono"))
                || fonts.at(f).contains(QLatin1String("Courier"));
        fontTable += QStringLiteral("{\\f%1\\f%2\\fcharset0 %3;}")
                         .arg(f)
                         .arg(mono ? QStringLiteral("modern") : QStringLiteral("nil"))
                         .arg(rtfEscaped(fonts.at(f)));
    }
    return QStringLiteral("{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl%1}\n"
                          "{\\colortbl;\\red176\\green176\\blue176;}\n"
                          "\\paperw11906\\paperh16838\\margl1440\\margr1440\\margt1440\\margb1440\n%2}\n")
        .arg(fontTable, body);
}

// Marks a paragraph that is empty in the ODT. Qt's HTML importer drops empty
// paragraphs, so the loader emits this private-use character and
// normaliseImportedBlocks() removes it again after import.
constexpr char16_t kBlankParagraphMark = 0xE000;

const QString kTextNs = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:text:1.0");

// "Heading_20_2" (LibreOffice / ODF common style) or "Heading 2" -> 2; else 0.
int headingLevelFromStyleName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral(R"(^Heading(?:_20_| )([1-9])$)"));
    const QRegularExpressionMatch m = re.match(name);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

// CSS font-family list for a stored family. zwriter's Courier default is saved
// as its first choice, "Courier New"; if that face isn't installed, fontconfig
// would substitute something unrelated (e.g. Cousine) on reopen, so a Courier
// family gets the same fallback chain the editor uses for new documents.
QString cssFontFamilies(const QString &family)
{
    auto quoted = [](const QString &f) {
        if (f == QLatin1String("monospace") || f == QLatin1String("serif")
            || f == QLatin1String("sans-serif")) {
            return f;
        }
        QString e = f.toHtmlEscaped();
        e.remove(QLatin1Char('\''));
        return QStringLiteral("'%1'").arg(e);
    };
    QStringList out{quoted(family)};
    if (family.compare(QLatin1String("Courier New"), Qt::CaseInsensitive) == 0
        || family.compare(QLatin1String("Courier"), Qt::CaseInsensitive) == 0) {
        for (const QString &f : defaultFontFamilies()) {
            if (f.compare(family, Qt::CaseInsensitive) != 0) {
                out.append(quoted(f));
            }
        }
    }
    return out.join(QLatin1Char(','));
}

// Convert a slice of ODT content.xml into simple HTML for QTextDocument.
// Parses automatic styles for font-family / size / weight / style so ODT
// round-trip preserves the hide-away toolbar font choices.
//
// White space follows the ODF rules (a run of spaces/tabs/newlines in the XML
// text collapses to one space, leading space in a paragraph is dropped;
// text:s / text:tab / text:line-break are explicit) and every paragraph is
// emitted with white-space:pre-wrap so Qt keeps exactly what we produce.
QString odtXmlToHtml(const QByteArray &xml)
{
    struct StyleInfo {
        QString fontFamily;
        QString fontSize; // e.g. "12pt"
        QString fontWeight; // CSS font-weight, empty = inherit
        bool bold = false;
        bool italic = false;
        bool underline = false;
        QString align;          // CSS text-align value, empty = default
        bool breakBefore = false; // manual page break before the paragraph
        int headingLevel = 0;     // from a Heading_20_N (parent) style
    };

    struct ListInfo {
        bool ordered = false;
        QString type; // HTML list type: disc/circle/square or 1/a/A/i/I
    };

    QHash<QString, StyleInfo> styles;
    QHash<QString, QHash<int, ListInfo>> listStyles; // list style -> level -> look
    {
        QXmlStreamReader pass1(xml);
        QString currentStyle;
        QString currentListStyle;
        while (!pass1.atEnd()) {
            const auto token = pass1.readNext();
            if (token == QXmlStreamReader::StartElement) {
                const QStringView name = pass1.name();
                if (name == QLatin1String("style")) {
                    const auto attrs = pass1.attributes();
                    currentStyle = attrs.value(QStringLiteral("style:name")).toString();
                    StyleInfo info = styles.value(currentStyle);
                    info.headingLevel = headingLevelFromStyleName(currentStyle);
                    if (info.headingLevel == 0) {
                        info.headingLevel = headingLevelFromStyleName(
                            attrs.value(QStringLiteral("style:parent-style-name")).toString());
                    }
                    if (info.headingLevel == 0) {
                        info.headingLevel =
                            attrs.value(QStringLiteral("style:default-outline-level")).toInt();
                    }
                    styles.insert(currentStyle, info);
                } else if (name == QLatin1String("list-style")) {
                    currentListStyle = pass1.attributes().value(QStringLiteral("style:name")).toString();
                } else if ((name == QLatin1String("list-level-style-bullet")
                            || name == QLatin1String("list-level-style-number"))
                           && !currentListStyle.isEmpty()) {
                    ListInfo info;
                    const auto attrs = pass1.attributes();
                    int level = attrs.value(QStringLiteral("text:level")).toInt();
                    if (level < 1) {
                        level = 1;
                    }
                    if (name == QLatin1String("list-level-style-number")) {
                        info.ordered = true;
                        const QString fmt = attrs.value(QStringLiteral("style:num-format")).toString();
                        info.type = (fmt == QLatin1String("a") || fmt == QLatin1String("A")
                                     || fmt == QLatin1String("i") || fmt == QLatin1String("I"))
                            ? fmt : QStringLiteral("1");
                    } else {
                        const QString bullet = attrs.value(QStringLiteral("text:bullet-char")).toString();
                        info.type = (bullet == QStringLiteral("○") || bullet == QStringLiteral("◦"))
                            ? QStringLiteral("circle")
                            : (bullet == QStringLiteral("■") || bullet == QStringLiteral("▪"))
                                ? QStringLiteral("square") : QStringLiteral("disc");
                    }
                    listStyles[currentListStyle].insert(level, info);
                } else if (name == QLatin1String("text-properties") && !currentStyle.isEmpty()) {
                    StyleInfo info = styles.value(currentStyle);
                    const auto attrs = pass1.attributes();
                    QString family = attrs.value(QStringLiteral("fo:font-family")).toString();
                    if (family.isEmpty()) {
                        family = attrs.value(QStringLiteral("style:font-name")).toString();
                    }
                    family.remove(QLatin1Char('\''));
                    if (!family.isEmpty()) {
                        info.fontFamily = family;
                    }
                    const QString size = attrs.value(QStringLiteral("fo:font-size")).toString();
                    if (!size.isEmpty()) {
                        info.fontSize = size;
                    }
                    const QString weight = attrs.value(QStringLiteral("fo:font-weight")).toString().toLower();
                    if (weight == QLatin1String("bold") || weight == QLatin1String("700")
                        || weight == QLatin1String("800") || weight == QLatin1String("900")) {
                        info.bold = true;
                    }
                    static const QRegularExpression weightRe(QStringLiteral("^(normal|bold|[1-9]00)$"));
                    if (weightRe.match(weight).hasMatch()) {
                        info.fontWeight = weight;
                    }
                    const QString fstyle = attrs.value(QStringLiteral("fo:font-style")).toString().toLower();
                    if (fstyle == QLatin1String("italic") || fstyle == QLatin1String("oblique")) {
                        info.italic = true;
                    }
                    const QString underline =
                        attrs.value(QStringLiteral("style:text-underline-style")).toString().toLower();
                    if (!underline.isEmpty() && underline != QLatin1String("none")) {
                        info.underline = true;
                    }
                    styles.insert(currentStyle, info);
                } else if (name == QLatin1String("paragraph-properties") && !currentStyle.isEmpty()) {
                    StyleInfo info = styles.value(currentStyle);
                    const auto attrs = pass1.attributes();
                    const QString align = attrs.value(QStringLiteral("fo:text-align")).toString().toLower();
                    if (align == QLatin1String("center") || align == QLatin1String("justify")) {
                        info.align = align;
                    } else if (align == QLatin1String("end") || align == QLatin1String("right")) {
                        info.align = QStringLiteral("right");
                    }
                    if (attrs.value(QStringLiteral("fo:break-before")).toString().toLower()
                        == QLatin1String("page")) {
                        info.breakBefore = true;
                    }
                    styles.insert(currentStyle, info);
                }
            } else if (token == QXmlStreamReader::EndElement) {
                if (pass1.name() == QLatin1String("style")) {
                    currentStyle.clear();
                } else if (pass1.name() == QLatin1String("list-style")) {
                    currentListStyle.clear();
                }
            }
        }
    }

    // Look of a list at a given nesting depth: exact level of its style, else
    // the nearest defined level above it, else plain bullets.
    auto listLook = [&](const QString &styleName, int depth) -> ListInfo {
        const auto levels = listStyles.value(styleName);
        for (int l = depth; l >= 1; --l) {
            const auto it = levels.constFind(l);
            if (it != levels.constEnd()) {
                return *it;
            }
        }
        if (!levels.isEmpty()) {
            return levels.constBegin().value();
        }
        return ListInfo{};
    };

    QXmlStreamReader reader(xml);
    // No colour here: text must follow the theme (Paper / Dark room).
    QString html = QStringLiteral("<html><body style=\"font-family:%1; font-size: 12pt;\">")
                       .arg(cssFontFamilies(defaultFontFamilies().first()));

    struct OpenList {
        QString styleName; // effective (inherited when the element has none)
        QString closer;
    };
    QList<OpenList> lists; // currently open text:list elements, outermost first
    int headingLevel = 0;
    bool inBold = false;
    bool inItalic = false;
    bool inUnderline = false;
    bool inP = false;
    bool pHasContent = false; // Qt's HTML import drops paragraphs with no content
    bool pendingSpace = false; // ODF: whitespace run seen, emit one space before next text
    bool atLineStart = true;   // ODF: leading whitespace of a paragraph/line is dropped
    bool inStyleSpan = false;
    bool tabJustWritten = false; // last thing emitted was a text:tab element

    auto closeInline = [&]() {
        if (inUnderline) {
            html += QStringLiteral("</u>");
            inUnderline = false;
        }
        if (inItalic) {
            html += QStringLiteral("</i>");
            inItalic = false;
        }
        if (inBold) {
            html += QStringLiteral("</b>");
            inBold = false;
        }
        if (inStyleSpan) {
            html += QStringLiteral("</span>");
            inStyleSpan = false;
        }
    };

    auto cssFromStyle = [&](const StyleInfo &info) -> QString {
        QString css;
        if (!info.fontFamily.isEmpty()) {
            // One family here: some Qt versions (6.4) mis-parse a quoted list
            // on spans. The Courier fallback chain is restored after import.
            css += QStringLiteral("font-family:'%1';")
                       .arg(info.fontFamily.toHtmlEscaped().remove(QLatin1Char('\'')));
        }
        if (!info.fontSize.isEmpty()) {
            css += QStringLiteral("font-size:%1;").arg(info.fontSize.toHtmlEscaped());
        }
        if (!info.fontWeight.isEmpty()) {
            css += QStringLiteral("font-weight:%1;").arg(info.fontWeight);
        }
        return css;
    };

    // Block-level properties only make sense on <p>/<h*>, not on inline spans.
    auto blockCss = [&](const StyleInfo &info, int level) -> QString {
        // zwriter paragraphs, headings and list items have no extra spacing;
        // don't let Qt's HTML defaults (12 px around <p>/<hN>) add some on
        // every reopen.
        QString css = QStringLiteral("white-space:pre-wrap;margin-top:0px;margin-bottom:0px;");
        css += cssFromStyle(info);
        if (level > 0) {
            // Same look as Format > Paragraph Style when the file doesn't say
            // (e.g. LibreOffice headings styled in styles.xml).
            static const char *const sizes[] = {"22pt", "18pt", "14pt"};
            static const char *const weights[] = {"700", "700", "600"};
            if (info.fontSize.isEmpty()) {
                css += QStringLiteral("font-size:%1;").arg(QLatin1String(sizes[level - 1]));
            }
            if (info.fontWeight.isEmpty()) {
                css += QStringLiteral("font-weight:%1;").arg(QLatin1String(weights[level - 1]));
            }
        }
        if (!info.align.isEmpty()) {
            css += QStringLiteral("text-align:%1;").arg(info.align);
        }
        if (info.breakBefore) {
            css += QStringLiteral("page-break-before:always;");
        }
        return css;
    };

    auto closeParagraph = [&]() {
        if (!inP) {
            return;
        }
        if (pendingSpace && !atLineStart) {
            html += QLatin1Char(' '); // a single trailing space is still content
        }
        pendingSpace = false;
        if (!pHasContent) {
            html += QChar(kBlankParagraphMark); // keep blank paragraphs (blank lines)
        }
        closeInline();
        html += (headingLevel > 0)
            ? QStringLiteral("</h%1>").arg(headingLevel)
            : QStringLiteral("</p>");
        inP = false;
        headingLevel = 0;
    };

    auto openParagraph = [&](int level, const QString &styleName) {
        closeParagraph();
        const StyleInfo info = styles.value(styleName);
        if (level == 0) {
            level = info.headingLevel;
        }
        level = qBound(0, level, 3);
        headingLevel = level;
        inP = true;
        pHasContent = false;
        pendingSpace = false;
        atLineStart = true;
        const QString styleAttr = QStringLiteral(" style=\"%1\"").arg(blockCss(info, level));
        if (level > 0) {
            html += QStringLiteral("<h%1%2>").arg(level).arg(styleAttr);
        } else {
            html += QStringLiteral("<p%1>").arg(styleAttr);
        }
        if (info.bold) {
            html += QStringLiteral("<b>");
            inBold = true;
        }
        if (info.italic) {
            html += QStringLiteral("<i>");
            inItalic = true;
        }
        if (info.underline) {
            html += QStringLiteral("<u>");
            inUnderline = true;
        }
    };

    // Flush a collapsed whitespace run as a single space (not at line start).
    auto flushSpace = [&]() {
        if (pendingSpace && !atLineStart) {
            html += QLatin1Char(' ');
        }
        pendingSpace = false;
    };

    auto appendText = [&](QStringView text) {
        for (const QChar ch : text) {
            if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t') || ch == QLatin1Char('\n')
                || ch == QLatin1Char('\r')) {
                pendingSpace = true;
                continue;
            }
            flushSpace();
            if (ch == QLatin1Char('<')) {
                html += QStringLiteral("&lt;");
            } else if (ch == QLatin1Char('>')) {
                html += QStringLiteral("&gt;");
            } else if (ch == QLatin1Char('&')) {
                html += QStringLiteral("&amp;");
            } else if (ch == QLatin1Char('"')) {
                html += QStringLiteral("&quot;");
            } else {
                html += ch;
            }
            atLineStart = false;
            pHasContent = true;
        }
    };

    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();
            const bool wasTab = tabJustWritten;
            tabJustWritten = false;
            if (name == QLatin1String("h")) {
                const QStringView lvl = reader.attributes().value(QStringLiteral("text:outline-level"));
                bool ok = false;
                int level = lvl.toInt(&ok);
                if (!ok || level < 1) {
                    level = 1;
                }
                const QString styleName =
                    reader.attributes().value(QStringLiteral("text:style-name")).toString();
                openParagraph(level, styleName);
            } else if (name == QLatin1String("p")) {
                const QString styleName =
                    reader.attributes().value(QStringLiteral("text:style-name")).toString();
                openParagraph(0, styleName);
            } else if (name == QLatin1String("span")) {
                flushSpace(); // the space belongs to the text before the span
                const QString style = reader.attributes().value(QStringLiteral("text:style-name")).toString();
                const StyleInfo info = styles.value(style);
                const QString css = cssFromStyle(info);
                if (!css.isEmpty()) {
                    html += QStringLiteral("<span style=\"%1\">").arg(css);
                    inStyleSpan = true;
                }
                const QString lower = style.toLower();
                const bool bold = info.bold
                    || lower.contains(QLatin1String("bold"))
                    || lower.contains(QLatin1String("strong"));
                const bool italic = info.italic
                    || lower.contains(QLatin1String("italic"))
                    || lower.contains(QLatin1String("emphas"));
                if (bold) {
                    html += QStringLiteral("<b>");
                    inBold = true;
                }
                if (italic) {
                    html += QStringLiteral("<i>");
                    inItalic = true;
                }
                if (info.underline) {
                    html += QStringLiteral("<u>");
                    inUnderline = true;
                }
            } else if (name == QLatin1String("list")) {
                closeParagraph(); // a nested list follows its item's paragraph
                // A nested list usually has no style of its own (LibreOffice):
                // it continues the enclosing list's style one level deeper.
                QString styleName = reader.attributes().value(QStringLiteral("text:style-name")).toString();
                if (styleName.isEmpty() && !lists.isEmpty()) {
                    styleName = lists.last().styleName;
                }
                const ListInfo info = listLook(styleName, int(lists.size()) + 1);
                OpenList open;
                open.styleName = styleName;
                if (info.ordered) {
                    html += QStringLiteral("<ol type=\"%1\" style=\"margin-top:0px;margin-bottom:0px;\">").arg(info.type);
                    open.closer = QStringLiteral("</ol>");
                } else {
                    html += QStringLiteral("<ul type=\"%1\" style=\"margin-top:0px;margin-bottom:0px;\">")
                                .arg(info.type.isEmpty() ? QStringLiteral("disc") : info.type);
                    open.closer = QStringLiteral("</ul>");
                }
                lists.append(open);
            } else if (name == QLatin1String("list-item") || name == QLatin1String("list-header")) {
                closeParagraph();
                html += QStringLiteral("<li>");
            } else if (name == QLatin1String("table")) {
                closeParagraph();
                html += QStringLiteral("<table>");
            } else if (name == QLatin1String("table-row")) {
                html += QStringLiteral("<tr>");
            } else if (name == QLatin1String("table-cell")) {
                const auto attrs = reader.attributes();
                const int colspan = attrs.value(QStringLiteral("table:number-columns-spanned")).toInt();
                const int rowspan = attrs.value(QStringLiteral("table:number-rows-spanned")).toInt();
                html += QStringLiteral("<td");
                if (colspan > 1) {
                    html += QStringLiteral(" colspan=\"%1\"").arg(colspan);
                }
                if (rowspan > 1) {
                    html += QStringLiteral(" rowspan=\"%1\"").arg(rowspan);
                }
                html += QLatin1Char('>');
            } else if (inP && name == QLatin1String("s")) {
                flushSpace();
                int count = reader.attributes().value(QStringLiteral("text:c")).toInt();
                if (count < 1) {
                    count = 1;
                }
                html += QString(qMin(count, 10000), QLatin1Char(' '));
                atLineStart = false;
                pHasContent = true;
            } else if (inP && name == QLatin1String("tab")) {
                flushSpace();
                html += QLatin1Char('\t');
                atLineStart = false;
                pHasContent = true;
                tabJustWritten = true;
                continue;
            } else if (inP && name == QLatin1String("line-break")) {
                // Qt's ODF writer puts a text:tab in front of every line break
                // (so justified lines don't stretch); it is not user content.
                if (wasTab && html.endsWith(QLatin1Char('\t'))) {
                    html.chop(1);
                }
                flushSpace();
                html += QStringLiteral("<br/>");
                atLineStart = false;
                pHasContent = true;
            } else if (name == QLatin1String("soft-page-break")) {
                // Layout hint only; not content.
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QStringView name = reader.name();
            if (name == QLatin1String("span")) {
                closeInline();
            } else if (name == QLatin1String("list-item") || name == QLatin1String("list-header")) {
                closeParagraph();
                html += QStringLiteral("</li>");
            } else if (name == QLatin1String("list")) {
                closeParagraph();
                if (!lists.isEmpty()) {
                    html += lists.takeLast().closer;
                }
            } else if (name == QLatin1String("table-cell")) {
                closeParagraph();
                html += QStringLiteral("</td>");
            } else if (name == QLatin1String("table-row")) {
                html += QStringLiteral("</tr>");
            } else if (name == QLatin1String("table")) {
                html += QStringLiteral("</table>");
            } else if (name == QLatin1String("p") || name == QLatin1String("h")) {
                closeParagraph();
            }
        } else if (token == QXmlStreamReader::Characters && inP) {
            if (!reader.isWhitespace()) {
                tabJustWritten = false;
            }
            // Pretty-printing indentation between elements is not text.
            if (reader.isWhitespace() && reader.text().contains(QLatin1Char('\n'))) {
                continue;
            }
            appendText(reader.text());
        }
    }

    closeParagraph();
    html += QStringLiteral("</body></html>");
    return html;
}

// Give reloaded tables the same look as Insert Table (the ODT reader only
// recovers structure, not the cell/border styling).
void restyleTables(QTextFrame *frame)
{
    for (QTextFrame *child : frame->childFrames()) {
        if (auto *table = qobject_cast<QTextTable *>(child)) {
            styleTable(table);
        }
        restyleTables(child);
    }
}

// Qt's HTML importer quirks, normalised after import so a document survives
// repeated save/open cycles unchanged:
//  * an empty paragraph arrives as a block holding only kBlankParagraphMark
//    (which keeps it alive through the importer) — make it truly empty;
//  * an extra empty paragraph is inserted after every table.
void collectTables(QTextFrame *frame, QList<QTextTable *> *out)
{
    for (QTextFrame *child : frame->childFrames()) {
        if (auto *table = qobject_cast<QTextTable *>(child)) {
            out->append(table);
        }
        collectTables(child, out);
    }
}

void normaliseImportedBlocks(QTextDocument *doc)
{
    // 1) blank-paragraph markers -> truly empty blocks
    QTextCursor c(doc);
    c.beginEditBlock();
    const QString mark{QChar(kBlankParagraphMark)};
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.text() == mark) {
            QTextCursor sel(b);
            sel.movePosition(QTextCursor::StartOfBlock);
            sel.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            sel.removeSelectedText();
        }
    }

    // 2) drop the importer's spare empty block after each table (never the last block)
    QList<QTextTable *> tables;
    collectTables(doc->rootFrame(), &tables);
    for (QTextTable *table : std::as_const(tables)) {
        QTextCursor after = table->lastCursorPosition();
        after.movePosition(QTextCursor::NextBlock);
        const QTextBlock spare = after.block();
        const QTextBlock next = spare.next();
        if (!spare.isValid() || !spare.text().isEmpty() || !next.isValid()
            || QTextCursor(spare).currentTable()) {
            continue;
        }
        // Merge the spare block into the following one, keeping that block's format.
        const QTextBlockFormat keep = next.blockFormat();
        QTextCursor merge(spare);
        merge.movePosition(QTextCursor::StartOfBlock);
        merge.deleteChar();
        merge.setBlockFormat(keep);
    }

    // 3) Courier text gets the full Courier-class fallback chain, so a file
    //    saved as "Courier New" still shows a typewriter face where that font
    //    is missing (fontconfig would otherwise pick e.g. Cousine or a sans).
    const QStringList chain = defaultFontFamilies();
    auto isCourier = [](const QString &f) {
        return f.compare(QLatin1String("Courier New"), Qt::CaseInsensitive) == 0
            || f.compare(QLatin1String("Courier"), Qt::CaseInsensitive) == 0;
    };
    QList<QPair<int, int>> ranges;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            const QTextCharFormat cf = frag.charFormat();
            const QFont font = cf.font();
            const QStringList fams = font.families();
            const QString first = fams.isEmpty() ? font.family() : fams.first();
            if (isCourier(first) && fams.size() < 2) {
                ranges.append({frag.position(), frag.length()});
            }
        }
    }
    for (const auto &r : std::as_const(ranges)) {
        QTextCursor sel(doc);
        sel.setPosition(r.first);
        sel.setPosition(r.first + r.second, QTextCursor::KeepAnchor);
        QTextCharFormat f;
        f.setFontFamilies(chain);
        sel.mergeCharFormat(f);
    }
    c.endEditBlock();
}

bool loadOdt(QTextDocument *doc, const QString &path, QString *error)
{
    QProcess proc;
    proc.start(QStringLiteral("unzip"),
               {QStringLiteral("-p"), path, QStringLiteral("content.xml")});
    if (!proc.waitForFinished(15000) || proc.exitStatus() != QProcess::NormalExit
        || proc.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("Could not read ODT (is unzip available?): %1")
                         .arg(QString::fromUtf8(proc.readAllStandardError()));
        }
        return false;
    }
    const QByteArray xml = proc.readAllStandardOutput();
    if (xml.isEmpty()) {
        if (error) {
            *error = QStringLiteral("ODT content.xml was empty.");
        }
        return false;
    }
    doc->setHtml(odtXmlToHtml(xml));
    restyleTables(doc->rootFrame());
    normaliseImportedBlocks(doc);
    // The clean-up above is part of loading, not an edit: nothing to undo.
    doc->clearUndoRedoStacks();
    doc->setModified(false);
    return true;
}

// Qt's ODF writer has no notion of headings: every block becomes text:p with
// an automatic paragraph style named "p<blockFormatIndex>". Rewrite the
// paragraphs of heading blocks as text:h with text:outline-level so headings
// survive a reopen (and LibreOffice sees real headings / outline).
QString headingPatchedContent(const QString &src, const QHash<QString, int> &headingStyles)
{
    struct Edit {
        qint64 pos;
        qint64 len;
        QString text;
    };
    QList<Edit> edits;
    QList<QString> openHeadings; // per open text:p: replacement end tag or empty
    QXmlStreamReader r(src);
    while (!r.atEnd()) {
        const auto tok = r.readNext();
        const bool isTextP = (tok == QXmlStreamReader::StartElement || tok == QXmlStreamReader::EndElement)
            && r.name() == QLatin1String("p") && r.namespaceUri() == kTextNs;
        if (!isTextP) {
            continue;
        }
        // characterOffset() is just past the tag; '<' cannot occur inside a
        // tag, so the tag starts at the last '<' before that.
        const qint64 end = r.characterOffset();
        const qint64 start = src.lastIndexOf(QLatin1Char('<'), end - 1);
        if (start < 0) {
            return QString();
        }
        const QString qname = r.qualifiedName().toString(); // e.g. "text:p"
        const QString prefix = qname.left(qname.size() - 1);
        if (tok == QXmlStreamReader::StartElement) {
            const QString style = r.attributes().value(kTextNs, QStringLiteral("style-name")).toString();
            const int level = headingStyles.value(style);
            if (level > 0 && QStringView(src).mid(start).startsWith(QLatin1Char('<') + qname)) {
                edits.append({start, qname.size() + 1,
                              QStringLiteral("<%1h %1outline-level=\"%2\"").arg(prefix).arg(level)});
                openHeadings.append(QStringLiteral("</%1h>").arg(prefix));
            } else {
                openHeadings.append(QString());
            }
        } else {
            const QString closer = openHeadings.isEmpty() ? QString() : openHeadings.takeLast();
            // A self-closing <text:p/> has no end tag of its own: the start-tag
            // edit already renamed it.
            if (!closer.isEmpty() && QStringView(src).mid(start).startsWith(QLatin1String("</"))) {
                edits.append({start, end - start, closer});
            }
        }
    }
    if (r.hasError()) {
        return QString();
    }
    QString out = src;
    for (auto it = edits.crbegin(); it != edits.crend(); ++it) {
        out.replace(it->pos, it->len, it->text);
    }
    return out;
}

// Qt's ODF writer gets tables wrong for zwriter: a 100 % wide table is written
// as style:width="100pt", and the borders come out as the table border width
// on every cell. Write the table as full width (rel-width 100 %, aligned to the
// margins) and the grid as one 0.5 pt light-grey line.
QString tablePatchedContent(QString xml)
{
    static const QRegularExpression tableProps(
        QStringLiteral(R"(<style:table-properties\b[^>]*>)"));
    static const QRegularExpression width(QStringLiteral(R"(\s(?:style:width|style:rel-width|table:align)="[^"]*")"));
    static const QRegularExpression cellProps(
        QStringLiteral(R"(<style:table-cell-properties\b[^>]*>)"));
    static const QRegularExpression border(QStringLiteral(R"(\sfo:border(?:-left|-right|-top|-bottom)?="[^"]*")"));
    const QString rule = QStringLiteral(" fo:border=\"0.5pt solid %1\"").arg(tableBorderColor().name());

    auto rewrite = [&xml](const QRegularExpression &element, const std::function<QString(QString)> &fix) {
        QString out;
        out.reserve(xml.size() + 256);
        qsizetype last = 0;
        auto it = element.globalMatch(xml);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            out += QStringView(xml).mid(last, m.capturedStart() - last);
            out += fix(m.captured());
            last = m.capturedEnd();
        }
        out += QStringView(xml).mid(last);
        xml = out;
    };
    rewrite(tableProps, [&](QString tag) {
        tag.remove(width);
        const qsizetype at = tag.indexOf(QLatin1String("style:table-properties")) + 22;
        tag.insert(at, QStringLiteral(" style:rel-width=\"100%\" table:align=\"margins\""));
        return tag;
    });
    rewrite(cellProps, [&](QString tag) {
        if (!border.match(tag).hasMatch()) {
            return tag; // Qt also writes unused border-less cell styles
        }
        tag.remove(border);
        const qsizetype at = tag.indexOf(QLatin1String("style:table-cell-properties")) + 27;
        tag.insert(at, rule);
        return tag;
    });
    return xml;
}

// Post-process content.xml written by QTextDocumentWriter: headings (text:h)
// and table styling. Best effort: the document itself is already saved.
bool patchOdtContent(QTextDocument *doc, const QString &path, QString *error)
{
    QHash<QString, int> headingStyles;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const int level = b.blockFormat().headingLevel();
        if (level > 0) {
            headingStyles.insert(QStringLiteral("p%1").arg(b.blockFormatIndex()), qMin(level, 6));
        }
    }
    QList<QTextTable *> tables;
    collectTables(doc->rootFrame(), &tables);
    // Qt names each automatic text style "c<format index>". Qt 6.4's writer
    // puts fo:font-family="Sans" into every one of them, whatever the font;
    // put the format's real (first) family back.
    QHash<QString, QString> textStyleFamilies;
    const QList<QTextFormat> formats = doc->allFormats();
    for (int i = 0; i < formats.size(); ++i) {
        if (!formats.at(i).isCharFormat()) {
            continue;
        }
        const QStringList fams = formats.at(i).property(QTextFormat::FontFamilies).toStringList();
        if (!fams.isEmpty() && !fams.first().isEmpty()) {
            textStyleFamilies.insert(QStringLiteral("c%1").arg(i), fams.first());
        }
    }
    if (headingStyles.isEmpty() && tables.isEmpty() && textStyleFamilies.isEmpty()) {
        return true;
    }
    auto fail = [error](const QString &msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };
    QProcess unzip;
    unzip.start(QStringLiteral("unzip"), {QStringLiteral("-p"), path, QStringLiteral("content.xml")});
    if (!unzip.waitForFinished(15000) || unzip.exitStatus() != QProcess::NormalExit
        || unzip.exitCode() != 0) {
        return fail(QStringLiteral("Headings/tables saved unpatched (unzip unavailable)."));
    }
    const QString original = QString::fromUtf8(unzip.readAllStandardOutput());
    QString patched = original;
    if (!headingStyles.isEmpty()) {
        patched = headingPatchedContent(patched, headingStyles);
        if (patched.isEmpty()) {
            return fail(QStringLiteral("Headings saved as plain paragraphs (could not parse content.xml)."));
        }
    }
    if (!tables.isEmpty()) {
        patched = tablePatchedContent(patched);
    }
    if (!textStyleFamilies.isEmpty()) {
        static const QRegularExpression re(QStringLiteral(
            R"re((<style:style style:name="(c\d+)" style:family="text">\s*<style:text-properties[^>]*?fo:font-family=")([^"]*)("))re"));
        QString out;
        qsizetype last = 0;
        auto it = re.globalMatch(patched);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const auto fam = textStyleFamilies.constFind(m.captured(2));
            if (fam == textStyleFamilies.cend()) {
                continue;
            }
            out += QStringView(patched).mid(last, m.capturedStart(3) - last);
            out += fam->toHtmlEscaped();
            last = m.capturedEnd(3);
        }
        out += QStringView(patched).mid(last);
        patched = out;
    }
    if (patched == original) {
        return true;
    }
    QTemporaryDir dir;
    QFile content(dir.filePath(QStringLiteral("content.xml")));
    if (!dir.isValid() || !content.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(QStringLiteral("Headings/tables saved unpatched (temp dir unavailable)."));
    }
    content.write(patched.toUtf8());
    content.close();
    // Replace just that member; the stored "mimetype" member stays first.
    QProcess zip;
    zip.setWorkingDirectory(dir.path());
    zip.start(QStringLiteral("zip"), {QStringLiteral("-X"), QStringLiteral("-q"),
                                      QFileInfo(path).absoluteFilePath(), QStringLiteral("content.xml")});
    if (!zip.waitForFinished(30000) || zip.exitStatus() != QProcess::NormalExit || zip.exitCode() != 0) {
        return fail(QStringLiteral("Headings/tables saved unpatched (zip unavailable)."));
    }
    return true;
}

void spellOutDefaultFamilies(QTextDocument *doc, const QFont &defaultFont)
{
    QStringList families = defaultFont.families();
    if (families.isEmpty() && !defaultFont.family().isEmpty()) {
        families << defaultFont.family();
    }
    if (families.isEmpty()) {
        return;
    }
    QList<QPair<int, int>> ranges;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.charFormat().hasProperty(QTextFormat::FontFamilies)) {
                ranges.append({frag.position(), frag.length()});
            }
        }
    }
    QTextCursor edit(doc);
    edit.beginEditBlock();
    for (const auto &r : std::as_const(ranges)) {
        QTextCursor sel(doc);
        sel.setPosition(r.first);
        sel.setPosition(r.first + r.second, QTextCursor::KeepAnchor);
        QTextCharFormat f;
        f.setFontFamilies(families);
        sel.mergeCharFormat(f);
    }
    edit.endEditBlock();
}

bool saveWithWriter(QTextDocument *doc, const QString &path, const QByteArray &fmt, QString *error)
{
    QTextDocumentWriter writer(path, fmt);
    if (!writer.write(doc)) {
        if (error) {
            *error = QStringLiteral("Failed to write %1.").arg(path);
        }
        return false;
    }
    return true;
}

} // namespace

QStringList defaultFontFamilies()
{
    // Courier first, then Courier-class monospace faces.
    return {
        QStringLiteral("Courier New"),
        QStringLiteral("Courier"),
        QStringLiteral("Courier Prime"),
        QStringLiteral("Nimbus Mono PS"),
        QStringLiteral("Liberation Mono"),
        QStringLiteral("Noto Sans Mono"),
        QStringLiteral("Menlo"),
        QStringLiteral("Monaco"),
        QStringLiteral("DejaVu Sans Mono"),
        QStringLiteral("monospace"),
    };
}

const QColor &tableBorderColor()
{
    static const QColor c(QStringLiteral("#b8b8b8"));
    return c;
}

void styleTable(QTextTable *table)
{
    if (!table) {
        return;
    }
    // One thin light-grey grid line between cells, like a word processor:
    // collapsed borders, a 1 px table edge and 1 px on every cell (with
    // collapsed borders Qt draws the inner grid from the cell formats only).
    // 1 layout px is 0.75 pt on paper; the ODT gets 0.5 pt.
    // One edit block so the (paginated) layout runs once, not once per cell:
    // a large table otherwise takes minutes to open.
    QTextCursor batch(table->document());
    batch.beginEditBlock();
    QTextTableFormat fmt = table->format();
    fmt.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    fmt.setBorderCollapse(true);
    fmt.setBorder(kTableBorderPx);
    fmt.setBorderBrush(tableBorderColor());
    fmt.setCellPadding(8);
    fmt.setCellSpacing(0);
    fmt.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    table->setFormat(fmt);
    for (int r = 0; r < table->rows(); ++r) {
        for (int c = 0; c < table->columns(); ++c) {
            QTextTableCell cell = table->cellAt(r, c);
            if (!cell.isValid() || cell.row() != r || cell.column() != c) {
                continue; // covered by a span
            }
            QTextTableCellFormat cf = cell.format().toTableCellFormat();
            if (qFuzzyCompare(cf.topBorder(), kTableBorderPx)
                && qFuzzyCompare(cf.leftBorder(), kTableBorderPx)
                && cf.topBorderBrush() == QBrush(tableBorderColor())) {
                continue;
            }
            cf.setBorder(kTableBorderPx);
            cf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            cf.setBorderBrush(tableBorderColor());
            cell.setFormat(cf);
        }
    }
    batch.endEditBlock();
}

Format formatFromPath(const QString &path)
{
    const QString ext = extensionOf(path);
    if (ext == QLatin1String("odt")) {
        return Format::Odt;
    }
    if (ext == QLatin1String("txt") || ext == QLatin1String("text")) {
        return Format::Txt;
    }
    if (ext == QLatin1String("rtf")) {
        return Format::Rtf;
    }
    return Format::Unknown;
}

QString formatName(Format format)
{
    switch (format) {
    case Format::Odt:
        return QStringLiteral("odt");
    case Format::Txt:
        return QStringLiteral("txt");
    case Format::Rtf:
        return QStringLiteral("rtf");
    case Format::Unknown:
        break;
    }
    return QStringLiteral("odt");
}

QByteArray writerFormat(Format format)
{
    switch (format) {
    case Format::Odt:
        return QByteArrayLiteral("odf");
    case Format::Txt:
        return QByteArrayLiteral("plaintext");
    case Format::Rtf:
        return QByteArray(); // custom writer
    case Format::Unknown:
        break;
    }
    return QByteArrayLiteral("odf");
}

QString openFilter()
{
    return QStringLiteral(
        "OpenDocument Text (*.odt);;"
        "Plain Text (*.txt);;"
        "Rich Text Format (*.rtf);;"
        "All Files (*)");
}

QString saveFilter()
{
    // ODT first — native default.
    return QStringLiteral(
        "OpenDocument Text (*.odt);;"
        "Plain Text (*.txt);;"
        "Rich Text Format (*.rtf)");
}

Format formatFromFilter(const QString &selectedFilter, const QString &pathHint)
{
    const QString f = selectedFilter.toLower();
    if (f.contains(QLatin1String("*.odt")) || f.contains(QLatin1String("opendocument"))) {
        return Format::Odt;
    }
    if (f.contains(QLatin1String("*.txt")) || f.contains(QLatin1String("plain"))) {
        return Format::Txt;
    }
    if (f.contains(QLatin1String("*.rtf")) || f.contains(QLatin1String("rich text"))) {
        return Format::Rtf;
    }
    const Format fromPath = formatFromPath(pathHint);
    return fromPath == Format::Unknown ? Format::Odt : fromPath;
}

bool load(QTextDocument *doc, const QString &path, QString *error)
{
    if (!doc) {
        return false;
    }
    const Format fmt = formatFromPath(path);
    if (fmt == Format::Odt) {
        return loadOdt(doc, path, error);
    }


    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Could not open %1.").arg(path);
        }
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    if (fmt == Format::Rtf
        || QString::fromUtf8(bytes.left(5)).startsWith(QLatin1String("{\\rtf"))) {
        doc->setHtml(rtfToHtml(bytes));
        restyleTables(doc->rootFrame());
        normaliseImportedBlocks(doc);
        doc->clearUndoRedoStacks();
        doc->setModified(false);
        return true;
    }

    // TXT (and unknown): plain UTF-8
    doc->setPlainText(QString::fromUtf8(bytes));
    doc->clearUndoRedoStacks();
    doc->setModified(false);
    return true;
}

bool save(QTextDocument *doc, const QString &path, Format format, QString *error)
{
    if (!doc) {
        return false;
    }
    if (format == Format::Unknown) {
        format = Format::Odt;
    }

    if (format == Format::Odt) {
        // Text typed with the document's default font carries no family of
        // its own; some Qt versions (6.4) then write the application default
        // ("Sans") into the ODT. Save a copy with the default families spelled
        // out so the file records the face actually shown.
        std::unique_ptr<QTextDocument> copy(doc->clone());
        spellOutDefaultFamilies(copy.get(), doc->defaultFont());
        if (!saveWithWriter(copy.get(), path, QByteArrayLiteral("odf"), error)) {
            return false;
        }
        // Best effort, like the meta.xml patch: the body is already saved.
        QString headingError;
        if (!patchOdtContent(copy.get(), path, &headingError)) {
            qWarning("zwriter: %s", qPrintable(headingError));
        }
        return true;
    }
    if (format == Format::Txt) {
        return saveWithWriter(doc, path, QByteArrayLiteral("plaintext"), error);
    }
    if (format == Format::Rtf) {
        // Basic RTF: paragraphs, character formatting, lists and tables.
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) {
                *error = QStringLiteral("Could not write %1.").arg(path);
            }
            return false;
        }
        const QByteArray data = documentToRtf(doc).toLatin1(); // ASCII only: non-ASCII is escaped
        if (file.write(data) != data.size()) {
            if (error) {
                *error = QStringLiteral("Short write to %1.").arg(path);
            }
            return false;
        }
        return true;
    }
    if (error) {
        *error = QStringLiteral("Unsupported format.");
    }
    return false;
}

} // namespace DocumentIo
