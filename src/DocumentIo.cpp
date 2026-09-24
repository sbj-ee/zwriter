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
#include <QList>
#include <QTextBlock>
#include <QStringList>
#include <QColor>
#include <QTextTableFormat>
#include <QTextTable>
#include <QTextFrame>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtGlobal>

namespace DocumentIo {
namespace {

QString extensionOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

// Best-effort: strip common RTF control words to plain-ish text.
QString rtfToPlain(const QString &rtf)
{
    QString out;
    out.reserve(rtf.size());
    bool inCtrl = false;
    bool inGroupIgnore = false;
    int ignoreDepth = 0;
    QString ctrl;

    for (int i = 0; i < rtf.size(); ++i) {
        const QChar c = rtf.at(i);
        if (inGroupIgnore) {
            if (c == QLatin1Char('{')) {
                ++ignoreDepth;
            } else if (c == QLatin1Char('}')) {
                --ignoreDepth;
                if (ignoreDepth <= 0) {
                    inGroupIgnore = false;
                    ignoreDepth = 0;
                }
            }
            continue;
        }
        if (c == QLatin1Char('{')) {
            // Skip font/color tables etc. heuristically when immediately followed by \*
            if (i + 2 < rtf.size() && rtf.at(i + 1) == QLatin1Char('\\')
                && rtf.at(i + 2) == QLatin1Char('*')) {
                inGroupIgnore = true;
                ignoreDepth = 1;
            }
            continue;
        }
        if (c == QLatin1Char('}')) {
            continue;
        }
        if (c == QLatin1Char('\\')) {
            if (i + 1 < rtf.size() && rtf.at(i + 1) == QLatin1Char('\\')) {
                out.append(QLatin1Char('\\'));
                ++i;
                continue;
            }
            if (i + 1 < rtf.size() && rtf.at(i + 1) == QLatin1Char('{')) {
                out.append(QLatin1Char('{'));
                ++i;
                continue;
            }
            if (i + 1 < rtf.size() && rtf.at(i + 1) == QLatin1Char('}')) {
                out.append(QLatin1Char('}'));
                ++i;
                continue;
            }
            // \'hh hex char
            if (i + 3 < rtf.size() && rtf.at(i + 1) == QLatin1Char('\'')) {
                const QString hex = rtf.mid(i + 2, 2);
                bool ok = false;
                const int v = hex.toInt(&ok, 16);
                if (ok) {
                    out.append(QChar(v));
                }
                i += 3;
                continue;
            }
            inCtrl = true;
            ctrl.clear();
            continue;
        }
        if (inCtrl) {
            if (c.isLetter()) {
                ctrl.append(c);
                continue;
            }
            // End of control word
            if (ctrl == QLatin1String("par") || ctrl == QLatin1String("line")) {
                out.append(QLatin1Char('\n'));
            } else if (ctrl == QLatin1String("tab")) {
                out.append(QLatin1Char('\t'));
            }
            inCtrl = false;
            if (c == QLatin1Char(' ')) {
                continue; // delimiter space after control word
            }
            if (c.isDigit() || c == QLatin1Char('-')) {
                // skip numeric arg
                while (i + 1 < rtf.size()
                       && (rtf.at(i + 1).isDigit() || rtf.at(i + 1) == QLatin1Char('-'))) {
                    ++i;
                }
                if (i + 1 < rtf.size() && rtf.at(i + 1) == QLatin1Char(' ')) {
                    ++i;
                }
                continue;
            }
            // fall through to emit non-delimiter char
        }
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            continue;
        }
        out.append(c);
    }
    return out;
}

QString plainToRtf(const QString &plain)
{
    QString body;
    body.reserve(plain.size() * 2);
    for (const QChar c : plain) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('{') || c == QLatin1Char('}')) {
            body.append(QLatin1Char('\\'));
            body.append(c);
        } else if (c == QLatin1Char('\n')) {
            body.append(QStringLiteral("\\par\n"));
        } else if (c.unicode() > 127) {
            body.append(QStringLiteral("\\u%1?").arg(static_cast<int>(c.unicode())));
        } else {
            body.append(c);
        }
    }
    return QStringLiteral("{\\rtf1\\ansi\\deff0\n{\\fonttbl{\\f0 Times New Roman;}}\n\\f0\\fs24\n%1\n}\n")
        .arg(body);
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
    QString html = QStringLiteral(
        "<html><body style=\"font-family: 'Courier New', 'Liberation Mono', 'Noto Sans Mono', "
        "Courier, Menlo, Monaco, 'DejaVu Sans Mono', monospace; font-size: 12pt;\">");

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
            css += QStringLiteral("font-family:'%1';").arg(info.fontFamily.toHtmlEscaped());
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
        QString css = QStringLiteral("white-space:pre-wrap;");
        if (level > 0) {
            // Headings made in zwriter have no extra paragraph spacing; don't
            // let Qt's <hN> defaults add some on every reopen.
            css += QStringLiteral("margin-top:0px;margin-bottom:0px;");
        }
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
                    html += QStringLiteral("<ol type=\"%1\">").arg(info.type);
                    open.closer = QStringLiteral("</ol>");
                } else {
                    html += QStringLiteral("<ul type=\"%1\">")
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
            QTextTableFormat fmt = table->format();
            fmt.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            // Qt 6.8+ defaults to collapsed borders, which draws no grid with this format.
            fmt.setBorderCollapse(false);
            fmt.setBorder(1.5);
            fmt.setBorderBrush(QColor(QStringLiteral("#a0a0a0")));
            fmt.setCellPadding(8);
            fmt.setCellSpacing(0);
            fmt.setWidth(QTextLength(QTextLength::PercentageLength, 100));
            table->setFormat(fmt);
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

bool patchOdtHeadings(QTextDocument *doc, const QString &path, QString *error)
{
    QHash<QString, int> headingStyles;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const int level = b.blockFormat().headingLevel();
        if (level > 0) {
            headingStyles.insert(QStringLiteral("p%1").arg(b.blockFormatIndex()), qMin(level, 6));
        }
    }
    if (headingStyles.isEmpty()) {
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
        return fail(QStringLiteral("Headings saved as plain paragraphs (unzip unavailable)."));
    }
    const QString patched =
        headingPatchedContent(QString::fromUtf8(unzip.readAllStandardOutput()), headingStyles);
    if (patched.isEmpty()) {
        return fail(QStringLiteral("Headings saved as plain paragraphs (could not parse content.xml)."));
    }
    QTemporaryDir dir;
    QFile content(dir.filePath(QStringLiteral("content.xml")));
    if (!dir.isValid() || !content.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(QStringLiteral("Headings saved as plain paragraphs (temp dir unavailable)."));
    }
    content.write(patched.toUtf8());
    content.close();
    // Replace just that member; the stored "mimetype" member stays first.
    QProcess zip;
    zip.setWorkingDirectory(dir.path());
    zip.start(QStringLiteral("zip"), {QStringLiteral("-X"), QStringLiteral("-q"),
                                      QFileInfo(path).absoluteFilePath(), QStringLiteral("content.xml")});
    if (!zip.waitForFinished(30000) || zip.exitStatus() != QProcess::NormalExit || zip.exitCode() != 0) {
        return fail(QStringLiteral("Headings saved as plain paragraphs (zip unavailable)."));
    }
    return true;
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
        doc->setPlainText(rtfToPlain(QString::fromUtf8(bytes)));
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
        if (!saveWithWriter(doc, path, QByteArrayLiteral("odf"), error)) {
            return false;
        }
        // Best effort, like the meta.xml patch: the body is already saved.
        QString headingError;
        if (!patchOdtHeadings(doc, path, &headingError)) {
            qWarning("zwriter: %s", qPrintable(headingError));
        }
        return true;
    }
    if (format == Format::Txt) {
        return saveWithWriter(doc, path, QByteArrayLiteral("plaintext"), error);
    }
    if (format == Format::Rtf) {
        // Best-effort RTF: plain text body with escapes (bold/italic round-trip
        // via ODT; RTF keeps readable interchange).
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) {
                *error = QStringLiteral("Could not write %1.").arg(path);
            }
            return false;
        }
        const QByteArray data = plainToRtf(doc->toPlainText()).toUtf8();
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
