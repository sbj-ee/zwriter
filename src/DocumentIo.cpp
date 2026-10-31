#include "DocumentIo.hpp"
#include "Images.hpp"
#include "ParagraphStyles.hpp"
#include "Alignment.hpp"

#include <QStringView>

#include <QDir>
#include <cctype>
#include <QImage>
#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QFileInfo>
#include <QProcess>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentWriter>
#include <QGuiApplication>
#include <QSaveFile>
#include <QStringDecoder>
#include <QVariant>

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
#include <QUrl>
#include <cmath>
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

constexpr int kNamedStyleState = 0x5A000; // + index into ParagraphStyles::named()
constexpr char16_t kRtfBlankParagraphMark = 0xE000; // same as kBlankParagraphMark below

// RTF -> simple HTML for QTextDocument: paragraphs, bold / italic / underline,
// large font sizes (headings), tables, page breaks and Unicode text. Font,
// colour and style tables, document info and every other destination are
// skipped. Body font and size follow zwriter's defaults.
QString rtfToHtml(const QByteArray &rtf, QHash<QString, QByteArray> *pictures = nullptr)
{
    struct State {
        bool skip = false;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        int vpos = 0; // 1 \super, -1 \sub (1.13.0)
        int halfPoints = 24;
        int uc = 1; // chars to skip after \uN
        char align = 'l'; // paragraph alignment: \ql \qc \qr \qj (reset by \pard)
        int li = 0, ri = 0, fi = 0, sb = 0, sa = 0, sl = 0; // twips; sl with \slmult1 = 240 per line
        bool slmult = false;
        bool pict = false; // inside a \pict group: text is hex picture data
    };
    // The picture being read (\pict ... hex ... }).
    struct Pict {
        QByteArray type; // "png" / "jpeg" / other (ignored)
        int picw = 0, pich = 0, wgoal = 0, hgoal = 0, scalex = 100, scaley = 100;
        QByteArray hex;
        int level = 0; // stack depth of the \pict group
    } pict;
    int pictureCount = 0;
    struct Inline {
        bool bold = false;
        bool italic = false;
        bool underline = false;
        int halfPoints = 24;
        int vpos = 0;
        bool operator==(const Inline &o) const
        {
            return bold == o.bold && italic == o.italic && underline == o.underline
                && halfPoints == o.halfPoints && vpos == o.vpos;
        }
    };
    static const QSet<QByteArray> skipDestinations = {
        "fonttbl", "colortbl", "stylesheet", "info", "nonshppict", "header", "headerl",
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
        const Inline want{st.bold, st.italic, st.underline, st.halfPoints, st.vpos};
        const bool plain = !want.bold && !want.italic && !want.underline && want.halfPoints < 26
            && want.vpos == 0;
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
            if (want.vpos != 0) {
                css += want.vpos > 0 ? QStringLiteral("vertical-align:super;") : QStringLiteral("vertical-align:sub;");
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
        // The align attribute, not CSS text-align: Qt's HTML importer ignores
        // "text-align:justify".
        QString alignAttr;
        switch (stack.last().align) {
        case 'c':
            alignAttr = QStringLiteral(" align=\"center\"");
            break;
        case 'r':
            alignAttr = QStringLiteral(" align=\"right\"");
            break;
        case 'j':
            alignAttr = QStringLiteral(" align=\"justify\"");
            break;
        default:
            break;
        }
        if (pendingBreak) {
            css += QStringLiteral("page-break-before:always;");
            pendingBreak = false;
        }
        {
            const State &ps = stack.last();
            auto px = [](int twips) { return QString::number(twips / 15.0, 'f', 2) + QStringLiteral("px"); };
            if (ps.li) css += QStringLiteral("margin-left:%1;").arg(px(ps.li));
            if (ps.ri) css += QStringLiteral("margin-right:%1;").arg(px(ps.ri));
            if (ps.fi) css += QStringLiteral("text-indent:%1;").arg(px(ps.fi));
            if (ps.sb) css += QStringLiteral("margin-top:%1;").arg(px(ps.sb));
            if (ps.sa) css += QStringLiteral("margin-bottom:%1;").arg(px(ps.sa));
            if (ps.slmult && ps.sl > 0 && ps.sl != 240) {
                css += QStringLiteral("line-height:%1%;").arg(qRound(ps.sl * 100.0 / 240));
            }
        }
        const QString body = paraHasText ? para : QString(QChar(kRtfBlankParagraphMark));
        para.clear();
        paraHasText = false;
        return QStringLiteral("<p style=\"%1\"%2>%3</p>").arg(css, alignAttr, body);
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
            if (pict.level > 0 && int(stack.size()) == pict.level) {
                const QByteArray data = QByteArray::fromHex(pict.hex);
                const QImage img = QImage::fromData(data);
                if ((pict.type == "png" || pict.type == "jpeg") && !img.isNull() && pictures
                    && !stack.at(stack.size() - 2).skip) {
                    const QString name = QStringLiteral("rtf-picture-%1.%2")
                                             .arg(++pictureCount)
                                             .arg(QString::fromLatin1(pict.type));
                    pictures->insert(name, data);
                    const qreal w = pict.wgoal > 0 ? pict.wgoal / 15.0 * pict.scalex / 100.0 : img.width();
                    const qreal h = pict.hgoal > 0 ? pict.hgoal / 15.0 * pict.scaley / 100.0 : img.height();
                    closeSpan();
                    para += QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\" />")
                                .arg(name).arg(qRound(w)).arg(qRound(h));
                    paraHasText = true;
                }
                pict = Pict{};
            }
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
        if (c != '\\' && stack.last().pict && !stack.last().skip) {
            while (i < n && rtf.at(i) != '\\' && rtf.at(i) != '{' && rtf.at(i) != '}') {
                const char t = rtf.at(i++);
                if (isxdigit(static_cast<unsigned char>(t))) {
                    pict.hex += t;
                }
            }
            groupStart = false;
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
        if (word == "shppict") {
            st.skip = false; // {\*\shppict{\pict ...}}: Word's picture, keep it
            continue;
        }
        if (st.skip) {
            continue;
        }
        if (word == "pict" && pict.level == 0) {
            pict = Pict{};
            pict.level = int(stack.size());
            st.pict = true;
            continue;
        }
        if (st.pict) {
            if (word == "pngblip") {
                pict.type = "png";
            } else if (word == "jpegblip") {
                pict.type = "jpeg";
            } else if (word == "picw") {
                pict.picw = param;
            } else if (word == "pich") {
                pict.pich = param;
            } else if (word == "picwgoal") {
                pict.wgoal = param;
            } else if (word == "pichgoal") {
                pict.hgoal = param;
            } else if (word == "picscalex" && param > 0) {
                pict.scalex = param;
            } else if (word == "picscaley" && param > 0) {
                pict.scaley = param;
            }
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
        } else if (word == "super") {
            st.vpos = on ? 1 : 0;
        } else if (word == "sub") {
            st.vpos = on ? -1 : 0;
        } else if (word == "nosupersub") {
            st.vpos = 0;
        } else if (word == "fs" && hasParam && param > 0) {
            st.halfPoints = param;
        } else if (word == "plain") {
            st.bold = st.italic = st.underline = false;
            st.vpos = 0;
            st.halfPoints = 24;
        } else if (word == "uc" && hasParam) {
            st.uc = qMax(0, param);
        } else if (word == "pard") {
            inTable = false;
            st.align = 'l';
            st.li = st.ri = st.fi = st.sb = st.sa = st.sl = 0;
            st.slmult = false;
        } else if (word == "li" && hasParam) {
            st.li = param;
        } else if (word == "ri" && hasParam) {
            st.ri = param;
        } else if (word == "fi" && hasParam) {
            st.fi = param;
        } else if (word == "sb" && hasParam) {
            st.sb = param;
        } else if (word == "sa" && hasParam) {
            st.sa = param;
        } else if (word == "sl" && hasParam) {
            st.sl = qAbs(param);
        } else if (word == "slmult") {
            st.slmult = on;
        } else if (word == "ql" || word == "qc" || word == "qr" || word == "qj") {
            st.align = word.at(1);
        } else if (word == "qd") {
            st.align = 'j'; // distributed: closest is justified
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
// One picture as an RTF \pict group: PNG (or the original JPEG bytes) in hex,
// with its display size in twips (15 per CSS px). Wrap is not kept in RTF.
QString rtfPicture(const QTextDocument *doc, const QTextImageFormat &f)
{
    const QVariant v = doc->resource(QTextDocument::ImageResource, QUrl(f.name()));
    QByteArray bytes = v.typeId() == QMetaType::QImage ? QByteArray() : v.toByteArray();
    QImage img = v.typeId() == QMetaType::QImage ? v.value<QImage>() : QImage::fromData(bytes);
    if (img.isNull()) {
        return QString();
    }
    const bool jpeg = bytes.startsWith("\xFF\xD8\xFF");
    if (!jpeg && !bytes.startsWith("\x89PNG")) {
        bytes.clear();
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
    }
    const qreal w = f.width() > 0 ? f.width() : img.width();
    const qreal h = f.height() > 0 ? f.height() : img.height();
    QString out = QStringLiteral("{\\pict%1\\picw%2\\pich%3\\picwgoal%4\\pichgoal%5\n")
                      .arg(jpeg ? QStringLiteral("\\jpegblip") : QStringLiteral("\\pngblip"))
                      .arg(img.width())
                      .arg(img.height())
                      .arg(qRound(w * 15.0))
                      .arg(qRound(h * 15.0));
    const QByteArray hex = bytes.toHex();
    for (qsizetype i = 0; i < hex.size(); i += 128) {
        out += QString::fromLatin1(hex.mid(i, 128));
        out += QLatin1Char('\n');
    }
    out += QLatin1Char('}');
    return out;
}

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
        switch (Alignment::kindOf(bf.alignment())) {
        case Alignment::Kind::Center:
            out += QStringLiteral("\\qc");
            break;
        case Alignment::Kind::Right:
            out += QStringLiteral("\\qr");
            break;
        case Alignment::Kind::Justify:
            out += QStringLiteral("\\qj");
            break;
        case Alignment::Kind::Left:
            out += QStringLiteral("\\ql");
            break;
        }
        // Indents and spacing in twips (15 per px at 96 dpi); line spacing as
        // a multiple of single (\sl240\slmult1 = single).
        auto tw = [](qreal px) { return qRound(px * 15.0); };
        if (bf.leftMargin() != 0) {
            out += QStringLiteral("\\li%1").arg(tw(bf.leftMargin()));
        }
        if (bf.rightMargin() != 0) {
            out += QStringLiteral("\\ri%1").arg(tw(bf.rightMargin()));
        }
        if (bf.textIndent() != 0) {
            out += QStringLiteral("\\fi%1").arg(tw(bf.textIndent()));
        }
        if (bf.topMargin() != 0) {
            out += QStringLiteral("\\sb%1").arg(tw(bf.topMargin()));
        }
        if (bf.bottomMargin() != 0) {
            out += QStringLiteral("\\sa%1").arg(tw(bf.bottomMargin()));
        }
        if (bf.lineHeightType() == QTextBlockFormat::ProportionalHeight && bf.lineHeight() > 0
            && bf.lineHeight() != 100) {
            out += QStringLiteral("\\sl%1\\slmult1").arg(qRound(240 * bf.lineHeight() / 100.0));
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
            if (cf.isImageFormat()) {
                for (int k = 0; k < frag.length(); ++k) {
                    out += rtfPicture(doc, cf.toImageFormat());
                }
                continue;
            }
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
            if (cf.verticalAlignment() == QTextCharFormat::AlignSuperScript) {
                ctl += QStringLiteral("\\super");
            } else if (cf.verticalAlignment() == QTextCharFormat::AlignSubScript) {
                ctl += QStringLiteral("\\sub");
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
// -qt-user-state base that carries "this paragraph is in ODT table of
// contents n" through the HTML import (turned into TocProperty after it).
constexpr int kTocUserState = 0x5B000;
constexpr int kGeneratedTocState = 0x5C000; // + level (0 = the title)

// The parts of an ODT needed to write its tables of contents back unchanged.
struct OdtTocs {
    QStringList raw;    // each text:table-of-content element, verbatim
    QString styles;     // style:style definitions they use (and their parents)
    QStringList ns;     // xmlns declarations of the original content.xml root
};

OdtTocs extractOdtTocs(const QString &content, const QString &stylesXml)
{
    OdtTocs out;
    static const QRegularExpression tocRe(QStringLiteral(R"(<text:table-of-content\b.*?</text:table-of-content>)"),
                                          QRegularExpression::DotMatchesEverythingOption);
    auto it = tocRe.globalMatch(content);
    while (it.hasNext()) {
        const QString raw = it.next().captured(0);
        // zwriter's own generated ToC is rebuilt from the document, not kept.
        out.raw << (raw.contains(QLatin1String("text:name=\"zwriter-toc\"")) ? QString() : raw);
    }
    bool anyKept = false;
    for (const QString &r : std::as_const(out.raw)) {
        anyKept = anyKept || !r.isEmpty();
    }
    if (!anyKept) {
        out.raw.clear();
        return out;
    }
    static const QRegularExpression rootRe(QStringLiteral(R"(<office:document-content\b[^>]*>)"));
    static const QRegularExpression nsRe(QStringLiteral(R"re(xmlns:[\w.-]+="[^"]*")re"));
    const QString root = rootRe.match(content).captured(0);
    auto ns = nsRe.globalMatch(root);
    while (ns.hasNext()) {
        out.ns << ns.next().captured(0);
    }
    // Every style the tables name, then the styles those inherit from.
    static const QRegularExpression refRe(QStringLiteral(R"re(\b[\w-]+:(?:style-name|parent-style-name)="([^"]*)")re"));
    QStringList pending;
    for (const QString &raw : std::as_const(out.raw)) {
        auto r = refRe.globalMatch(raw);
        while (r.hasNext()) {
            pending << r.next().captured(1);
        }
    }
    QSet<QString> done;
    while (!pending.isEmpty()) {
        const QString name = pending.takeFirst();
        if (name.isEmpty() || done.contains(name)) {
            continue;
        }
        done.insert(name);
        const QRegularExpression defRe(
            QStringLiteral(R"re(<style:style\b[^>]*\bstyle:name="%1"[^>]*?(?:/>|>.*?</style:style>))re")
                .arg(QRegularExpression::escape(name)),
            QRegularExpression::DotMatchesEverythingOption);
        for (const QString *src : {&content, &stylesXml}) {
            const QRegularExpressionMatch m = defRe.match(*src);
            if (m.hasMatch()) {
                out.styles += m.captured(0);
                auto r = refRe.globalMatch(m.captured(0));
                while (r.hasNext()) {
                    pending << r.next().captured(1);
                }
                break;
            }
        }
    }
    return out;
}

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

// Paragraph alignment of the common (named) styles in styles.xml, e.g.
// LibreOffice's centred "Title" or a justified "Text_20_body", with each
// style's parent so automatic styles in content.xml can inherit it.
struct OdtAlignStyle {
    QString align;  // "left" / "center" / "right" / "justify"; empty = inherit
    QString parent;
};

// fo:text-align -> the CSS value zwriter uses; empty for missing/unknown.
// start/end are taken as left/right (zwriter documents are left-to-right).
QString odtAlignToCss(QStringView value)
{
    const QString v = value.toString().trimmed().toLower();
    if (v == QLatin1String("center") || v == QLatin1String("justify")) {
        return v;
    }
    if (v == QLatin1String("end") || v == QLatin1String("right")) {
        return QStringLiteral("right");
    }
    if (v == QLatin1String("start") || v == QLatin1String("left")) {
        return QStringLiteral("left");
    }
    return QString();
}

QHash<QString, OdtAlignStyle> odtCommonAlignStyles(const QByteArray &stylesXml)
{
    QHash<QString, OdtAlignStyle> out;
    if (stylesXml.isEmpty()) {
        return out;
    }
    QXmlStreamReader r(stylesXml);
    bool inCommon = false; // office:styles only: styles.xml's automatic styles
                           // (header/footer paragraphs) reuse names like "P1"
    QString current;
    while (!r.atEnd()) {
        const auto token = r.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (r.name() == QLatin1String("styles") && r.prefix() == QLatin1String("office")) {
                inCommon = true;
            } else if (inCommon && r.name() == QLatin1String("style")) {
                const auto attrs = r.attributes();
                current = attrs.value(QStringLiteral("style:name")).toString();
                OdtAlignStyle st;
                st.parent = attrs.value(QStringLiteral("style:parent-style-name")).toString();
                out.insert(current, st);
            } else if (inCommon && !current.isEmpty() && r.name() == QLatin1String("paragraph-properties")) {
                const QString css = odtAlignToCss(r.attributes().value(QStringLiteral("fo:text-align")));
                if (!css.isEmpty()) {
                    out[current].align = css;
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (r.name() == QLatin1String("styles") && r.prefix() == QLatin1String("office")) {
                inCommon = false;
            } else if (r.name() == QLatin1String("style")) {
                current.clear();
            }
        }
    }
    return out;
}

// Convert a slice of ODT content.xml into simple HTML for QTextDocument.
// Parses automatic styles for font-family / size / weight / style so ODT
// round-trip preserves the hide-away toolbar font choices.
//
// White space follows the ODF rules (a run of spaces/tabs/newlines in the XML
// text collapses to one space, leading space in a paragraph is dropped;
// text:s / text:tab / text:line-break are explicit) and every paragraph is
// emitted with white-space:pre-wrap so Qt keeps exactly what we produce.
// `stylesXml` (optional) supplies the alignment of named styles.
// `images`: package members (e.g. "Pictures/1.png") that hold a decodable
// image; draw:image elements pointing elsewhere are skipped.
QString odtXmlToHtml(const QByteArray &xml, const QByteArray &stylesXml = QByteArray(),
                     const QSet<QString> &images = QSet<QString>())
{
    struct StyleInfo {
        QString fontFamily;
        QString fontSize; // e.g. "12pt"
        QString fontWeight; // CSS font-weight, empty = inherit
        bool bold = false;
        bool italic = false;
        bool underline = false;
        int textPosition = 0;   // style:text-position: 1 super, -1 sub (1.13.0)
        QString align;          // HTML align value ("left" explicit), empty = inherit
        QString parent;         // style:parent-style-name
        bool breakBefore = false; // manual page break before the paragraph
        int headingLevel = 0;     // from a Heading_20_N (parent) style
        QString geometryCss;      // margins, text-indent, line-height (1.7.0)
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
                    info.parent = attrs.value(QStringLiteral("style:parent-style-name")).toString();
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
                    // "super 58%", "sub 58%", or "33% 58%" / "-33% 58%" (1.13.0).
                    const QString pos =
                        attrs.value(QStringLiteral("style:text-position")).toString().trimmed().toLower();
                    if (pos.startsWith(QLatin1String("super"))) {
                        info.textPosition = 1;
                    } else if (pos.startsWith(QLatin1String("sub"))) {
                        info.textPosition = -1;
                    } else if (!pos.isEmpty()) {
                        const double v = pos.section(QLatin1Char(' '), 0, 0).remove(QLatin1Char('%')).toDouble();
                        info.textPosition = v > 0 ? 1 : (v < 0 ? -1 : 0);
                    }
                    styles.insert(currentStyle, info);
                } else if (name == QLatin1String("paragraph-properties") && !currentStyle.isEmpty()) {
                    StyleInfo info = styles.value(currentStyle);
                    const auto attrs = pass1.attributes();
                    const QString align = odtAlignToCss(attrs.value(QStringLiteral("fo:text-align")));
                    if (!align.isEmpty()) {
                        info.align = align;
                    }
                    if (attrs.value(QStringLiteral("fo:break-before")).toString().toLower()
                        == QLatin1String("page")) {
                        info.breakBefore = true;
                    }
                    // Indents and spacing (fo:margin-*, fo:text-indent) and
                    // line spacing as CSS for Qt's HTML importer.
                    static const char *const geo[][2] = {
                        {"fo:margin-left", "margin-left"}, {"fo:margin-right", "margin-right"},
                        {"fo:margin-top", "margin-top"},   {"fo:margin-bottom", "margin-bottom"},
                        {"fo:text-indent", "text-indent"}};
                    for (const auto &g : geo) {
                        const QStringView v = attrs.value(QLatin1String(g[0]));
                        if (!v.isEmpty()) {
                            const bool neg = v.startsWith(QLatin1Char('-'));
                            const double px = Images::odfLengthToPx(neg ? v.mid(1) : v);
                            info.geometryCss += QStringLiteral("%1:%2px;").arg(QLatin1String(g[1]))
                                                    .arg(neg ? -px : px, 0, 'f', 2);
                        }
                    }
                    QString lh = attrs.value(QStringLiteral("fo:line-height")).toString();
                    if (lh.isEmpty()) {
                        lh = attrs.value(QStringLiteral("style:line-height")).toString();
                    }
                    if (lh.endsWith(QLatin1Char('%'))) {
                        const int pct = lh.chopped(1).toInt();
                        if (pct > 0 && pct != 100) {
                            info.geometryCss += QStringLiteral("line-height:%1%;").arg(pct);
                        }
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

    // Alignment is inherited through style:parent-style-name: an automatic
    // style in content.xml, then the common styles of styles.xml. A paragraph
    // may also name a common style directly (text:style-name="Title").
    {
        const QHash<QString, OdtAlignStyle> common = odtCommonAlignStyles(stylesXml);
        auto inherited = [&](QString name) -> QString {
            for (int depth = 0; depth < 16 && !name.isEmpty(); ++depth) {
                const auto own = styles.constFind(name);
                if (own != styles.constEnd()) {
                    if (!own->align.isEmpty()) {
                        return own->align;
                    }
                    name = own->parent;
                    continue;
                }
                const auto c = common.constFind(name);
                if (c == common.constEnd()) {
                    break;
                }
                if (!c->align.isEmpty()) {
                    return c->align;
                }
                name = c->parent;
            }
            return QString();
        };
        for (auto it = common.constBegin(); it != common.constEnd(); ++it) {
            if (!styles.contains(it.key())) {
                StyleInfo info;
                info.parent = it->parent;
                info.align = it->align;
                styles.insert(it.key(), info);
            }
        }
        for (auto it = styles.begin(); it != styles.end(); ++it) {
            if (it->align.isEmpty()) {
                it->align = inherited(it->parent);
            }
        }
        for (auto it = styles.begin(); it != styles.end(); ++it) {
            if (it->align == QLatin1String("left")) {
                it->align.clear(); // the default: nothing to say
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
    bool tabJustWritten = false; // last thing emitted was a text:tab element
    int tocCurrent = -1; // inside the n-th text:table-of-content
    bool generatedToc = false; // inside zwriter's own ToC
    int tocCount = 0;

    // Open text:span elements, innermost last: each entry is the markup that
    // closes it. A span inside a span ends only itself.
    QStringList spanClosers;
    auto closeSpan = [&]() {
        if (!spanClosers.isEmpty()) {
            html += spanClosers.takeLast();
        }
    };
    auto closeInline = [&]() {
        while (!spanClosers.isEmpty()) {
            closeSpan();
        }
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
        if (info.breakBefore) {
            css += QStringLiteral("page-break-before:always;");
        }
        css += info.geometryCss; // after the 0px defaults, so it wins
        // A zwriter named style (Title, Bibliography, ...) rides through the
        // HTML import as the block's user state; load() turns it back into
        // the block property.
        for (int i = 0; i < ParagraphStyles::named().size(); ++i) {
            if (ParagraphStyles::named().at(i).name == info.parent) {
                css += QStringLiteral("-qt-user-state:%1;").arg(kNamedStyleState + i);
            }
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
        QString css = blockCss(info, level);
        if (generatedToc) {
            static const QRegularExpression lvlRe(QStringLiteral(R"(^Contents_20_(\d)$)"));
            const QRegularExpressionMatch m = lvlRe.match(styleName);
            css += QStringLiteral("-qt-user-state:%1;").arg(kGeneratedTocState + (m.hasMatch() ? m.captured(1).toInt() : 0));
        } else if (tocCurrent >= 0) {
            css += QStringLiteral("-qt-user-state:%1;").arg(kTocUserState + tocCurrent);
        }
        QString styleAttr = QStringLiteral(" style=\"%1\"").arg(css);
        if (!info.align.isEmpty()) {
            // The align attribute, not CSS text-align: Qt's HTML importer
            // ignores "text-align:justify".
            styleAttr += QStringLiteral(" align=\"%1\"").arg(info.align);
        }
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

    qreal frameW = 0.0;
    qreal frameH = 0.0;
    bool frameDone = false;
    QString frameFloat; // "left" / "right" / empty = in line
    // Graphic styles (content.xml automatic, styles.xml common): which ones
    // float a frame left or right with text wrapping past it.
    struct GraphicStyle {
        QString wrap;
        QString hpos;
        QString parent;
    };
    QHash<QString, GraphicStyle> graphicStyles;
    for (const QByteArray *src : {&stylesXml, &xml}) {
        QXmlStreamReader g(*src);
        QString current;
        while (!g.atEnd()) {
            const auto tok = g.readNext();
            if (tok == QXmlStreamReader::StartElement && g.name() == QLatin1String("style")
                && g.attributes().value(QStringLiteral("style:family")) == QLatin1String("graphic")) {
                current = g.attributes().value(QStringLiteral("style:name")).toString();
                graphicStyles[current].parent =
                    g.attributes().value(QStringLiteral("style:parent-style-name")).toString();
            } else if (tok == QXmlStreamReader::StartElement && !current.isEmpty()
                       && g.name() == QLatin1String("graphic-properties")) {
                const auto a = g.attributes();
                if (a.hasAttribute(QStringLiteral("style:wrap"))) {
                    graphicStyles[current].wrap = a.value(QStringLiteral("style:wrap")).toString();
                }
                if (a.hasAttribute(QStringLiteral("style:horizontal-pos"))) {
                    graphicStyles[current].hpos = a.value(QStringLiteral("style:horizontal-pos")).toString();
                }
            } else if (tok == QXmlStreamReader::EndElement && g.name() == QLatin1String("style")) {
                current.clear();
            }
        }
    }
    auto floatOf = [&](QString style) -> QString {
        QString wrap;
        QString hpos;
        for (int depth = 0; depth < 8 && !style.isEmpty(); ++depth) {
            const auto it = graphicStyles.constFind(style);
            if (it == graphicStyles.constEnd()) {
                break;
            }
            if (wrap.isEmpty()) {
                wrap = it->wrap;
            }
            if (hpos.isEmpty()) {
                hpos = it->hpos;
            }
            style = it->parent;
        }
        if (wrap.isEmpty() || wrap == QLatin1String("none") || wrap == QLatin1String("run-through")) {
            return QString();
        }
        if (hpos == QLatin1String("left") || hpos == QLatin1String("inside") || hpos == QLatin1String("from-left")
            || hpos == QLatin1String("from-inside")) {
            return QStringLiteral("left");
        }
        if (hpos == QLatin1String("right") || hpos == QLatin1String("outside")) {
            return QStringLiteral("right");
        }
        return QString(); // centred: no float in zwriter, show it in line
    };
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();
            const bool wasTab = tabJustWritten;
            tabJustWritten = false;
            if (name == QLatin1String("table-of-content") && reader.prefix() == QLatin1String("text")) {
                closeParagraph();
                tocCurrent = tocCount++;
                if (reader.attributes().value(QStringLiteral("text:name")) == QLatin1String("zwriter-toc")) {
                    generatedToc = true;
                    tocCurrent = -1;
                }
            } else if (name == QLatin1String("table-of-content-source")) {
                reader.skipCurrentElement(); // templates, not text
                continue;
            } else if (name == QLatin1String("h")) {
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
                QString css = cssFromStyle(info);
                if (info.textPosition != 0) {
                    css += info.textPosition > 0 ? QStringLiteral("vertical-align:super;")
                                                 : QStringLiteral("vertical-align:sub;");
                }
                QString closer;
                if (!css.isEmpty()) {
                    html += QStringLiteral("<span style=\"%1\">").arg(css);
                    closer.prepend(QStringLiteral("</span>"));
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
                    closer.prepend(QStringLiteral("</b>"));
                }
                if (italic) {
                    html += QStringLiteral("<i>");
                    closer.prepend(QStringLiteral("</i>"));
                }
                if (info.underline) {
                    html += QStringLiteral("<u>");
                    closer.prepend(QStringLiteral("</u>"));
                }
                spanClosers << closer;
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
            } else if ((name == QLatin1String("title") || name == QLatin1String("desc"))
                       && reader.prefix() == QLatin1String("svg")) {
                // A frame's hidden title / description (alt text): not body text.
                reader.skipCurrentElement();
                continue;
            } else if (name == QLatin1String("frame") && reader.prefix() == QLatin1String("draw")) {
                const auto attrs = reader.attributes();
                frameW = Images::odfLengthToPx(attrs.value(QStringLiteral("svg:width")));
                frameH = Images::odfLengthToPx(attrs.value(QStringLiteral("svg:height")));
                frameDone = false;
                frameFloat.clear();
                if (attrs.value(QStringLiteral("text:anchor-type")) != QLatin1String("as-char")) {
                    frameFloat = floatOf(attrs.value(QStringLiteral("draw:style-name")).toString());
                }
            } else if (name == QLatin1String("image") && reader.prefix() == QLatin1String("draw")) {
                // A frame may hold several images (LibreOffice: an SVG plus a
                // PNG fallback): use the first one Qt can show.
                QString href = reader.attributes().value(QStringLiteral("xlink:href")).toString();
                if (href.startsWith(QLatin1String("./"))) {
                    href = href.mid(2);
                }
                if (!frameDone && images.contains(href)) {
                    if (!inP) {
                        openParagraph(0, QString());
                    }
                    flushSpace();
                    html += QStringLiteral("<img src=\"%1\"").arg(htmlEscaped(href));
                    if (frameW > 0.0) {
                        html += QStringLiteral(" width=\"%1\"").arg(qRound(frameW));
                    }
                    if (frameH > 0.0) {
                        html += QStringLiteral(" height=\"%1\"").arg(qRound(frameH));
                    }
                    if (!frameFloat.isEmpty()) {
                        html += QStringLiteral(" style=\"float: %1\"").arg(frameFloat);
                    }
                    html += QStringLiteral(" />");
                    atLineStart = false;
                    pHasContent = true;
                    frameDone = true;
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QStringView name = reader.name();
            if (name == QLatin1String("span")) {
                closeSpan();
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
            } else if (name == QLatin1String("table-of-content")) {
                closeParagraph();
                tocCurrent = -1;
                generatedToc = false;
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

// Column widths and header rows of the ODT's tables (1.16.0), in document
// order. Widths come from the table-column styles (number-columns-repeated
// honoured): style:rel-column-width when every column has one (LibreOffice and
// zwriter write it), else the absolute style:column-width (some writers give
// only that), as proportions in percent. Header rows: the rows inside
// table:table-header-rows.
struct OdtTableLayout {
    QList<double> percents;
    int headerRows = 0;
};

double odtLengthPt(QStringView v, bool *percent = nullptr)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*([0-9]*\.?[0-9]+)\s*([a-z%]*)\s*$)"));
    const QRegularExpressionMatch m = re.match(v.toString());
    if (percent) {
        *percent = false;
    }
    if (!m.hasMatch()) {
        return -1;
    }
    const double n = m.captured(1).toDouble();
    const QString u = m.captured(2);
    if (u == QLatin1String("cm")) return n * 72.0 / 2.54;
    if (u == QLatin1String("mm")) return n * 72.0 / 25.4;
    if (u == QLatin1String("in") || u == QLatin1String("inch")) return n * 72.0;
    if (u == QLatin1String("pt") || u.isEmpty()) return n;
    if (u == QLatin1String("pc")) return n * 12.0;
    if (u == QLatin1String("px")) return n * 0.75;
    if (u == QLatin1String("%")) {
        if (percent) {
            *percent = true;
        }
        return n;
    }
    return -1;
}

QList<OdtTableLayout> odtTableLayouts(const QByteArray &xml)
{
    struct ColStyle {
        double rel = -1;
        double abs = -1;
        double pct = -1;
    };
    QHash<QString, ColStyle> colStyles;
    QList<OdtTableLayout> out;
    struct Open {
        int index;
        QList<ColStyle> cols;
        bool inHeader = false;
        int depth = 0; // element depth of the table element
    };
    QList<Open> stack;
    QString currentStyle; // inside a table-column style:style
    int depth = 0;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            ++depth;
            const QStringView name = r.name();
            const QStringView prefix = r.prefix();
            const auto a = r.attributes();
            if (prefix == QLatin1String("style") && name == QLatin1String("style")
                && a.value(QStringLiteral("style:family")) == QLatin1String("table-column")) {
                currentStyle = a.value(QStringLiteral("style:name")).toString();
                colStyles.insert(currentStyle, ColStyle{});
            } else if (prefix == QLatin1String("style") && name == QLatin1String("table-column-properties")
                       && !currentStyle.isEmpty()) {
                ColStyle &cs = colStyles[currentStyle];
                const QStringView rel = a.value(QStringLiteral("style:rel-column-width"));
                if (!rel.isEmpty()) {
                    bool ok = false;
                    const double v = rel.toString().remove(QLatin1Char('*')).trimmed().toDouble(&ok);
                    if (ok && v > 0) {
                        cs.rel = v;
                    }
                }
                bool pct = false;
                const double w = odtLengthPt(a.value(QStringLiteral("style:column-width")), &pct);
                if (w > 0) {
                    (pct ? cs.pct : cs.abs) = w;
                }
            } else if (prefix == QLatin1String("table") && name == QLatin1String("table")) {
                Open o{int(out.size())};
                o.depth = depth;
                out.append(OdtTableLayout{});
                stack << o;
            } else if (!stack.isEmpty() && prefix == QLatin1String("table")
                       && name == QLatin1String("table-column")) {
                const int n = qBound(1, a.value(QStringLiteral("table:number-columns-repeated")).toInt(), 1024);
                const ColStyle cs = colStyles.value(a.value(QStringLiteral("table:style-name")).toString());
                for (int i = 0; i < n; ++i) {
                    stack.last().cols << cs;
                }
            } else if (!stack.isEmpty() && prefix == QLatin1String("table")
                       && name == QLatin1String("table-header-rows") && depth == stack.last().depth + 1) {
                stack.last().inHeader = true;
            } else if (!stack.isEmpty() && prefix == QLatin1String("table") && name == QLatin1String("table-row")
                       && stack.last().inHeader) {
                ++out[stack.last().index].headerRows;
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const QStringView name = r.name();
            const QStringView prefix = r.prefix();
            if (prefix == QLatin1String("style") && name == QLatin1String("style")) {
                currentStyle.clear();
            } else if (!stack.isEmpty() && prefix == QLatin1String("table")
                       && name == QLatin1String("table-header-rows") && depth == stack.last().depth + 1) {
                stack.last().inHeader = false;
            } else if (!stack.isEmpty() && prefix == QLatin1String("table") && name == QLatin1String("table")) {
                const Open o = stack.takeLast();
                auto all = [&](auto get) {
                    if (o.cols.isEmpty()) {
                        return false;
                    }
                    for (const ColStyle &c : o.cols) {
                        if (!(get(c) > 0)) {
                            return false;
                        }
                    }
                    return true;
                };
                std::function<double(const ColStyle &)> pick;
                if (all([](const ColStyle &c) { return c.rel; })) {
                    pick = [](const ColStyle &c) { return c.rel; };
                } else if (all([](const ColStyle &c) { return c.abs; })) {
                    pick = [](const ColStyle &c) { return c.abs; };
                } else if (all([](const ColStyle &c) { return c.pct; })) {
                    pick = [](const ColStyle &c) { return c.pct; };
                }
                if (pick) {
                    double sum = 0;
                    for (const ColStyle &c : o.cols) {
                        sum += pick(c);
                    }
                    QList<double> p;
                    for (const ColStyle &c : o.cols) {
                        p << std::round(pick(c) / sum * 10000.0) / 100.0;
                    }
                    out[o.index].percents = p;
                }
            }
            --depth;
        }
    }
    return out;
}

// Put the widths and header rows back on the loaded tables (Qt's HTML
// importer, which the ODT reader goes through, keeps neither).
void applyOdtTableLayouts(QTextDocument *doc, const QList<OdtTableLayout> &layouts)
{
    QList<QTextTable *> tables;
    collectTables(doc->rootFrame(), &tables);
    if (tables.size() != layouts.size()) {
        return; // the importer merged or dropped a table: do not guess
    }
    QTextCursor batch(doc);
    batch.beginEditBlock();
    for (int i = 0; i < tables.size(); ++i) {
        QTextTable *t = tables.at(i);
        const OdtTableLayout &l = layouts.at(i);
        QTextTableFormat f = t->format();
        bool changed = false;
        if (l.percents.size() == t->columns()) {
            QList<QTextLength> c;
            for (double v : l.percents) {
                c << QTextLength(QTextLength::PercentageLength, v);
            }
            f.setColumnWidthConstraints(c);
            changed = true;
        }
        if (l.headerRows > 0 && l.headerRows < t->rows() + 1) {
            f.setHeaderRowCount(qMin(l.headerRows, t->rows()));
            changed = true;
        }
        if (changed) {
            t->setFormat(f);
        }
    }
    batch.endEditBlock();
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
    // styles.xml is optional: it only adds the alignment of named styles.
    QByteArray stylesXml;
    {
        QProcess styles;
        styles.start(QStringLiteral("unzip"),
                     {QStringLiteral("-p"), path, QStringLiteral("styles.xml")});
        if (styles.waitForFinished(15000) && styles.exitStatus() == QProcess::NormalExit
            && styles.exitCode() == 0) {
            stylesXml = styles.readAllStandardOutput();
        }
    }
    // Pictures: every package member that decodes as an image becomes a
    // document resource named by its path, which the draw:image hrefs use.
    QSet<QString> imageMembers;
    QHash<QString, QByteArray> imageData;
    {
        QProcess list;
        list.start(QStringLiteral("unzip"), {QStringLiteral("-Z1"), path});
        if (list.waitForFinished(15000) && list.exitStatus() == QProcess::NormalExit
            && list.exitCode() == 0) {
            const QStringList members =
                QString::fromUtf8(list.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (QString m : members) {
                m = m.trimmed();
                if (m.isEmpty() || m.endsWith(QLatin1Char('/')) || m.endsWith(QLatin1String(".xml"))
                    || m == QLatin1String("mimetype") || m.startsWith(QLatin1String("META-INF/"))
                    || !xml.contains(m.toUtf8())) {
                    continue;
                }
                QProcess one;
                one.start(QStringLiteral("unzip"), {QStringLiteral("-p"), path, m});
                if (!one.waitForFinished(15000) || one.exitCode() != 0) {
                    continue;
                }
                const QByteArray data = one.readAllStandardOutput();
                if (Images::isDecodable(data)) {
                    doc->addResource(QTextDocument::ImageResource, QUrl(m), data);
                    imageMembers.insert(m);
                    imageData.insert(m, data);
                }
            }
        }
    }
    const OdtTocs tocs = extractOdtTocs(QString::fromUtf8(xml), QString::fromUtf8(stylesXml));
    doc->setProperty("zwriter.odtTocs", tocs.raw);
    doc->setProperty("zwriter.odtTocStyles", tocs.styles);
    doc->setProperty("zwriter.odtTocNs", tocs.ns);
    doc->setHtml(odtXmlToHtml(xml, stylesXml, imageMembers));
    {
        QTextCursor mark(doc);
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            const int st = b.userState();
            if (st >= kGeneratedTocState && st < kGeneratedTocState + 16) {
                b.setUserState(-1);
                mark.setPosition(b.position());
                QTextBlockFormat f;
                if (st == kGeneratedTocState) {
                    f.setProperty(GeneratedTocTitleProperty, true);
                } else {
                    f.setProperty(GeneratedTocProperty, st - kGeneratedTocState);
                }
                mark.mergeBlockFormat(f);
                if (b.text().endsWith(QLatin1Char('\t'))) { // the empty page-number tab
                    QTextCursor tab(b);
                    tab.movePosition(QTextCursor::EndOfBlock);
                    tab.deletePreviousChar();
                }
            }
        }
    }
    if (!tocs.raw.isEmpty()) {
        QTextCursor mark(doc);
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            const int st = b.userState();
            if (st >= kTocUserState && st < kTocUserState + 0x1000) {
                b.setUserState(-1);
                mark.setPosition(b.position());
                QTextBlockFormat f;
                f.setProperty(TocProperty, st - kTocUserState);
                mark.mergeBlockFormat(f);
            }
        }
    }
    for (const QString &m : std::as_const(imageMembers)) {
        // setHtml() may drop resources on some Qt versions; put them back.
        if (doc->resource(QTextDocument::ImageResource, QUrl(m)).isNull()) {
            doc->addResource(QTextDocument::ImageResource, QUrl(m), imageData.value(m));
        }
    }
    restyleTables(doc->rootFrame());
    applyOdtTableLayouts(doc, odtTableLayouts(xml));
    normaliseImportedBlocks(doc);
    {
        QTextCursor c(doc);
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            const int i = b.userState() - kNamedStyleState;
            if (b.userState() >= kNamedStyleState && i < ParagraphStyles::named().size()) {
                QTextBlockFormat bf = b.blockFormat();
                bf.setProperty(ParagraphStyles::StyleNameProperty, ParagraphStyles::named().at(i).name);
                c.setPosition(b.position());
                c.setBlockFormat(bf);
                b.setUserState(-1);
            }
        }
    }
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

// Column widths and header rows of every table (1.16.0), in document order
// (pre-order, the order of both table:table elements and collectTables()).
struct TableSaveInfo {
    QList<double> percents; // empty: Qt sizes the columns itself
    int headerRows = 0;
};

QList<TableSaveInfo> tableSaveInfo(const QList<QTextTable *> &tables)
{
    QList<TableSaveInfo> out;
    for (const QTextTable *t : tables) {
        TableSaveInfo info;
        const QList<QTextLength> c = t->format().columnWidthConstraints();
        if (c.size() == t->columns()) {
            for (const QTextLength &l : c) {
                if (l.type() != QTextLength::PercentageLength || l.rawValue() <= 0) {
                    info.percents.clear();
                    break;
                }
                info.percents << l.rawValue();
            }
        }
        info.headerRows = qBound(0, t->format().headerRowCount(), t->rows());
        out << info;
    }
    return out;
}

// Write each table's columns the way LibreOffice does: one automatic
// table-column style per column with style:rel-column-width (the proportion,
// out of 65535) and an absolute style:column-width (of a 6.5 in text column;
// readers that size by the absolute width keep the proportions). The leading
// header rows go into table:table-header-rows, which LibreOffice repeats on
// every page. Qt's own "TableN.k" column styles (width in "%", not valid ODF)
// are dropped.
QString tableColumnsPatchedContent(const QString &xml, const QList<TableSaveInfo> &info)
{
    bool any = false;
    for (const TableSaveInfo &t : info) {
        any = any || !t.percents.isEmpty() || t.headerRows > 0;
    }
    if (!any) {
        return xml;
    }
    static const QRegularExpression tag(
        QStringLiteral(R"(<(/?)table:(table-row|table-column|table)(?=[\s/>])[^>]*?(/?)>)"));
    struct Open {
        int index;
        int rows = 0;
        bool headerOpen = false;
        bool dropColumns = false;
    };
    QList<Open> stack;
    QString out;
    out.reserve(xml.size() + 4096);
    QString styles;
    qsizetype last = 0;
    int counter = 0;
    constexpr double kTextWidthCm = 16.51; // 6.5 in
    auto it = tag.globalMatch(xml);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const bool closing = !m.captured(1).isEmpty();
        const bool selfClosing = !m.captured(3).isEmpty();
        const QString kind = m.captured(2);
        out += QStringView(xml).mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const TableSaveInfo *ti = stack.isEmpty() || stack.last().index >= info.size()
            ? nullptr : &info.at(stack.last().index);
        if (kind == QLatin1String("table")) {
            if (!closing) {
                Open o{counter++};
                out += m.captured();
                if (o.index < info.size() && !info.at(o.index).percents.isEmpty()) {
                    o.dropColumns = true;
                    const QList<double> &p = info.at(o.index).percents;
                    double sum = 0;
                    for (double v : p) {
                        sum += v;
                    }
                    for (int c = 0; c < p.size(); ++c) {
                        const QString name = QStringLiteral("zwTable%1.C%2").arg(o.index + 1).arg(c + 1);
                        const int rel = qMax(1, qRound(p.at(c) / sum * 65535.0));
                        styles += QStringLiteral("<style:style style:name=\"%1\" style:family=\"table-column\">"
                                                 "<style:table-column-properties style:column-width=\"%2cm\" "
                                                 "style:rel-column-width=\"%3*\"/></style:style>")
                                      .arg(name, QString::number(kTextWidthCm * p.at(c) / sum, 'f', 3))
                                      .arg(rel);
                        out += QStringLiteral("<table:table-column table:style-name=\"%1\"/>").arg(name);
                    }
                }
                if (!selfClosing) {
                    stack << o;
                }
            } else {
                if (!stack.isEmpty()) {
                    if (stack.last().headerOpen) {
                        out += QStringLiteral("</table:table-header-rows>");
                    }
                    stack.removeLast();
                }
                out += m.captured();
            }
        } else if (kind == QLatin1String("table-column")) {
            if (!(stack.isEmpty() || !stack.last().dropColumns)) {
                if (!selfClosing && !closing) {
                    // <table:table-column ...></table:table-column>: drop through its end tag
                    const qsizetype end = xml.indexOf(QLatin1String("</table:table-column>"), last);
                    if (end >= 0) {
                        last = end + 21;
                        // keep the iterator in step
                        while (it.hasNext() && it.peekNext().capturedStart() < last) {
                            it.next();
                        }
                    }
                }
                continue; // ours were written after the table start tag
            }
            out += m.captured();
        } else { // table-row
            if (!closing && !stack.isEmpty() && ti && ti->headerRows > 0 && stack.last().rows == 0) {
                out += QStringLiteral("<table:table-header-rows>");
                stack.last().headerOpen = true;
            }
            out += m.captured();
            if (!stack.isEmpty() && (closing || selfClosing)) {
                Open &o = stack.last();
                ++o.rows;
                if (o.headerOpen && ti && o.rows == ti->headerRows) {
                    out += QStringLiteral("</table:table-header-rows>");
                    o.headerOpen = false;
                }
            }
        }
    }
    out += QStringView(xml).mid(last);
    // Qt's own percentage column styles are no longer referenced.
    static const QRegularExpression qtColumnStyle(
        QStringLiteral(R"(<style:style style:name="[^"]*" style:family="table-column">\s*<style:table-column-properties[^>]*/>\s*</style:style>)"));
    QString cleaned;
    {
        qsizetype from = 0;
        auto ci = qtColumnStyle.globalMatch(out);
        while (ci.hasNext()) {
            const QRegularExpressionMatch m = ci.next();
            const QString name = QRegularExpression(QStringLiteral(R"re(style:name="([^"]*)")re")).match(m.captured()).captured(1);
            if (out.contains(QStringLiteral("table:style-name=\"%1\"").arg(name))) {
                continue; // still used (a table without stored widths)
            }
            cleaned += QStringView(out).mid(from, m.capturedStart() - from);
            from = m.capturedEnd();
        }
        cleaned += QStringView(out).mid(from);
    }
    if (!styles.isEmpty()) {
        const qsizetype close = cleaned.indexOf(QLatin1String("</office:automatic-styles>"));
        if (close >= 0) {
            cleaned.insert(close, styles);
        } else {
            const qsizetype body = cleaned.indexOf(QLatin1String("<office:body"));
            if (body >= 0) {
                cleaned.insert(body, QStringLiteral("<office:automatic-styles>%1</office:automatic-styles>").arg(styles));
            }
        }
    }
    return cleaned;
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

// Text wrap: Qt writes every picture as an as-char frame. A floating image
// becomes a char-anchored frame with a graphic style that puts it at the left
// or right of the paragraph with text running past it (LibreOffice's
// "Wrap: Page/Optimal" look).
QString framePatchedContent(const QString &xml, const QList<Images::Wrap> &wraps)
{
    static const QRegularExpression frame(QStringLiteral(R"(<draw:frame\b[^>]*>)"));
    static const QRegularExpression anchor(QStringLiteral(R"(\stext:anchor-type="[^"]*")"));
    QString out;
    out.reserve(xml.size() + 1024);
    qsizetype last = 0;
    int index = 0;
    auto it = frame.globalMatch(xml);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(xml).mid(last, m.capturedStart() - last);
        QString tag = m.captured();
        const Images::Wrap w = index < wraps.size() ? wraps.at(index) : Images::Wrap::Inline;
        ++index;
        if (w != Images::Wrap::Inline) {
            tag.remove(anchor);
            tag.insert(11, QStringLiteral(" draw:style-name=\"%1\" text:anchor-type=\"char\"")
                               .arg(w == Images::Wrap::Left ? QStringLiteral("zwImageLeft")
                                                            : QStringLiteral("zwImageRight")));
        }
        out += tag;
        last = m.capturedEnd();
    }
    out += QStringView(xml).mid(last);
    const QString styles = QStringLiteral(
        "<style:style style:name=\"zwImageLeft\" style:family=\"graphic\">"
        "<style:graphic-properties style:wrap=\"right\" style:number-wrapped-paragraphs=\"no-limit\" "
        "style:horizontal-pos=\"left\" style:horizontal-rel=\"paragraph\" style:vertical-pos=\"top\" "
        "style:vertical-rel=\"paragraph\" fo:margin-right=\"0.25cm\" fo:margin-bottom=\"0.1cm\"/></style:style>"
        "<style:style style:name=\"zwImageRight\" style:family=\"graphic\">"
        "<style:graphic-properties style:wrap=\"left\" style:number-wrapped-paragraphs=\"no-limit\" "
        "style:horizontal-pos=\"right\" style:horizontal-rel=\"paragraph\" style:vertical-pos=\"top\" "
        "style:vertical-rel=\"paragraph\" fo:margin-left=\"0.25cm\" fo:margin-bottom=\"0.1cm\"/></style:style>");
    const qsizetype close = out.indexOf(QLatin1String("</office:automatic-styles>"));
    if (close >= 0) {
        out.insert(close, styles);
    } else {
        const qsizetype body = out.indexOf(QLatin1String("<office:body"));
        if (body >= 0) {
            out.insert(body, QStringLiteral("<office:automatic-styles>%1</office:automatic-styles>").arg(styles));
        }
    }
    return out;
}

// Put the original tables of contents back over the paragraphs that show
// them (Qt's writer would save them as plain paragraphs).
// zwriter's generated ToC as a real ODF table of contents: LibreOffice shows
// the dot leaders, and its page numbers once updated there.
QString generatedTocContent(const QString &src, const QTextDocument *copy)
{
    QSet<QString> tocStyles;
    QString title = QStringLiteral("Contents");
    QString body;
    int maxLevel = 1;
    for (QTextBlock b = copy->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat f = b.blockFormat();
        if (f.hasProperty(GeneratedTocTitleProperty)) {
            tocStyles.insert(QStringLiteral("p%1").arg(b.blockFormatIndex()));
            title = b.text();
        } else if (f.hasProperty(GeneratedTocProperty)) {
            tocStyles.insert(QStringLiteral("p%1").arg(b.blockFormatIndex()));
            const int level = qBound(1, f.intProperty(GeneratedTocProperty), 9);
            maxLevel = qMax(maxLevel, level);
            // "entry<TAB>page" (zwriter puts the whole-book PDF's page numbers there).
            const QString line = b.text();
            const qsizetype tab = line.lastIndexOf(QLatin1Char('\t'));
            const QString entry = tab >= 0 ? line.left(tab) : line;
            const QString page = tab >= 0 ? line.mid(tab + 1) : QString();
            body += QStringLiteral("<text:p text:style-name=\"Contents_20_%1\">%2<text:tab/>%3</text:p>")
                        .arg(level)
                        .arg(entry.toHtmlEscaped(), page.toHtmlEscaped());
        }
    }
    if (tocStyles.isEmpty()) {
        return src;
    }
    static const QRegularExpression paraRe(QStringLiteral(
        R"re(<text:(p|h)\b[^>]*?text:style-name="(p\d+)"[^>]*?(?:/>|>.*?</text:\1>))re"),
        QRegularExpression::DotMatchesEverythingOption);
    qsizetype start = -1;
    qsizetype end = -1;
    auto it = paraRe.globalMatch(src);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (tocStyles.contains(m.captured(2))) {
            if (start < 0) {
                start = m.capturedStart(0);
            }
            end = m.capturedEnd(0);
        } else if (start >= 0) {
            break; // one contiguous table
        }
    }
    if (start < 0) {
        return src;
    }
    QString templates;
    for (int l = 1; l <= maxLevel; ++l) {
        templates += QStringLiteral(
            "<text:table-of-content-entry-template text:outline-level=\"%1\" text:style-name=\"Contents_20_%1\">"
            "<text:index-entry-text/><text:index-entry-tab-stop style:type=\"right\" style:leader-char=\".\"/>"
            "<text:index-entry-page-number/></text:table-of-content-entry-template>").arg(l);
    }
    const QString toc = QStringLiteral(
        "<text:table-of-content text:style-name=\"Sect_zwToc\" text:protected=\"true\" text:name=\"zwriter-toc\">"
        "<text:table-of-content-source text:outline-level=\"%1\">"
        "<text:index-title-template text:style-name=\"Contents_20_Heading\">%2</text:index-title-template>%3"
        "</text:table-of-content-source><text:index-body>"
        "<text:index-title text:style-name=\"Sect_zwToc\" text:name=\"zwriter-toc_Head\">"
        "<text:p text:style-name=\"Contents_20_Heading\">%2</text:p></text:index-title>%4"
        "</text:index-body></text:table-of-content>")
                            .arg(maxLevel)
                            .arg(title.toHtmlEscaped(), templates, body);
    QString out = src;
    out.replace(start, end - start, toc);
    // Styles: a right tab with dot leaders at the text column's edge.
    QString styles = QStringLiteral(
        "<style:style style:name=\"Sect_zwToc\" style:family=\"section\"><style:section-properties style:editable=\"false\"/></style:style>"
        "<style:style style:name=\"Contents_20_Heading\" style:display-name=\"Contents Heading\" style:family=\"paragraph\">"
        "<style:paragraph-properties fo:margin-bottom=\"0.25cm\"/><style:text-properties fo:font-size=\"16pt\" fo:font-weight=\"bold\"/></style:style>");
    for (int l = 1; l <= maxLevel; ++l) {
        styles += QStringLiteral(
            "<style:style style:name=\"Contents_20_%1\" style:display-name=\"Contents %1\" style:family=\"paragraph\">"
            "<style:paragraph-properties fo:margin-left=\"%2cm\" fo:margin-top=\"%3cm\"><style:tab-stops>"
            "<style:tab-stop style:position=\"%4cm\" style:type=\"right\" style:leader-style=\"dotted\" style:leader-text=\".\"/>"
            "</style:tab-stops></style:paragraph-properties>%5</style:style>")
                      .arg(l)
                      .arg(QString::number(0.6 * (l - 1), 'f', 2))
                      .arg(l == 1 ? QStringLiteral("0.15") : QStringLiteral("0"))
                      .arg(QString::number(16.0 - 0.6 * (l - 1), 'f', 2))
                      .arg(l == 1 ? QStringLiteral("<style:text-properties fo:font-weight=\"600\"/>") : QString());
    }
    const qsizetype at = out.indexOf(QLatin1String("</office:automatic-styles>"));
    if (at >= 0) {
        out.insert(at, styles);
    }
    return out;
}

QString tocRestoredContent(const QString &src, const QTextDocument *copy, const QTextDocument *original)
{
    const QStringList raw = original->property("zwriter.odtTocs").toStringList();
    if (raw.isEmpty()) {
        return src;
    }
    QHash<QString, int> styleToToc; // "p<format index>" -> table
    for (QTextBlock b = copy->begin(); b.isValid(); b = b.next()) {
        const QVariant t = b.blockFormat().property(TocProperty);
        if (t.isValid() && t.toInt() >= 0 && t.toInt() < raw.size()) {
            styleToToc.insert(QStringLiteral("p%1").arg(b.blockFormatIndex()), t.toInt());
        }
    }
    if (styleToToc.isEmpty()) {
        return src; // the table was deleted
    }
    static const QRegularExpression paraRe(QStringLiteral(
        R"re(<text:(p|h)\b[^>]*?text:style-name="(p\d+)"[^>]*?(?:/>|>.*?</text:\1>))re"),
        QRegularExpression::DotMatchesEverythingOption);
    QHash<int, QPair<qsizetype, qsizetype>> spans; // table -> [first start, last end)
    auto it = paraRe.globalMatch(src);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const auto t = styleToToc.constFind(m.captured(2));
        if (t == styleToToc.cend()) {
            continue;
        }
        auto span = spans.find(*t);
        if (span == spans.end()) {
            spans.insert(*t, {m.capturedStart(0), m.capturedEnd(0)});
        } else {
            span->second = m.capturedEnd(0);
        }
    }
    QList<int> order = spans.keys();
    std::sort(order.begin(), order.end(), [&](int a, int b) { return spans[a].first > spans[b].first; });
    QString out = src;
    for (int t : std::as_const(order)) { // back to front keeps offsets valid
        out.replace(spans[t].first, spans[t].second - spans[t].first, raw.at(t));
    }
    // The styles they use, and any namespace the original declared.
    const QString styles = original->property("zwriter.odtTocStyles").toString();
    if (!styles.isEmpty()) {
        const qsizetype at = out.indexOf(QLatin1String("</office:automatic-styles>"));
        if (at >= 0) {
            out.insert(at, styles);
        } else {
            const qsizetype body = out.indexOf(QLatin1String("<office:body"));
            if (body >= 0) {
                out.insert(body, QStringLiteral("<office:automatic-styles>%1</office:automatic-styles>").arg(styles));
            }
        }
    }
    const qsizetype rootStart = out.indexOf(QLatin1String("<office:document-content"));
    const qsizetype rootEnd = rootStart >= 0 ? out.indexOf(QLatin1Char('>'), rootStart) : -1;
    if (rootEnd > 0) {
        const QString root = out.mid(rootStart, rootEnd - rootStart);
        QString add;
        for (const QString &decl : original->property("zwriter.odtTocNs").toStringList()) {
            const QString prefix = decl.section(QLatin1Char('='), 0, 0);
            if (!root.contains(prefix + QLatin1Char('='))) {
                add += QLatin1Char(' ') + decl;
            }
        }
        out.insert(rootEnd, add);
    }
    return out;
}

// Post-process content.xml written by QTextDocumentWriter: headings (text:h)
// and table styling. Best effort: the document itself is already saved.
bool patchOdtContent(QTextDocument *doc, const QString &path, QString *error, const QTextDocument *source = nullptr)
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
    // Wrap (float) of every image, in document order = the order Qt writes
    // the draw:frame elements.
    QList<Images::Wrap> frameWraps;
    bool anyFloat = false;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat f = it.fragment().charFormat();
            if (f.isImageFormat()) {
                for (int k = 0; k < it.fragment().length(); ++k) {
                    frameWraps << Images::wrapOf(doc, f);
                    anyFloat = anyFloat || frameWraps.last() != Images::Wrap::Inline;
                }
            }
        }
    }
    // Named paragraph styles (Title, Bibliography, ...) by automatic style,
    // and whether any paragraph has a line spacing (Qt writes the
    // non-standard style:line-height; ODF readers want fo:line-height).
    QHash<QString, QString> namedStyles;
    bool anyLineHeight = false;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString named = ParagraphStyles::styleOf(b.blockFormat());
        if (!named.isEmpty()) {
            namedStyles.insert(QStringLiteral("p%1").arg(b.blockFormatIndex()), named);
        }
        anyLineHeight = anyLineHeight || b.blockFormat().hasProperty(QTextFormat::LineHeight);
    }
    bool anyGeneratedToc = false;
    for (QTextBlock b = doc->begin(); b.isValid() && !anyGeneratedToc; b = b.next()) {
        anyGeneratedToc = b.blockFormat().hasProperty(GeneratedTocProperty)
            || b.blockFormat().hasProperty(GeneratedTocTitleProperty);
    }
    const bool anyToc = anyGeneratedToc || (source && !source->property("zwriter.odtTocs").toStringList().isEmpty());
    if (headingStyles.isEmpty() && tables.isEmpty() && textStyleFamilies.isEmpty() && !anyFloat
        && namedStyles.isEmpty() && !anyLineHeight && !anyToc) {
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
        patched = tableColumnsPatchedContent(patched, tableSaveInfo(tables));
    }
    if (anyFloat) {
        patched = framePatchedContent(patched, frameWraps);
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
    for (auto it = namedStyles.constBegin(); it != namedStyles.constEnd(); ++it) {
        const QString tag = QStringLiteral("<style:style style:name=\"%1\" style:family=\"paragraph\"").arg(it.key());
        const qsizetype at = patched.indexOf(tag);
        if (at >= 0) {
            patched.insert(at + tag.size(), QStringLiteral(" style:parent-style-name=\"%1\"").arg(it.value()));
        }
    }
    if (anyLineHeight) {
        patched.replace(QLatin1String(" style:line-height=\""), QLatin1String(" fo:line-height=\""));
    }
    if (anyGeneratedToc) {
        patched = generatedTocContent(patched, doc);
    }
    if (anyToc && source) {
        patched = tocRestoredContent(patched, doc, source);
    }
    if (patched == original) {
        return true;
    }
    QStringList members{QStringLiteral("content.xml")};
    QTemporaryDir dir;
    if (!namedStyles.isEmpty() && dir.isValid()) {
        // styles.xml with the named styles (LibreOffice lists them by name),
        // and its manifest entry.
        QString defs;
        for (const ParagraphStyles::Style &st : ParagraphStyles::named()) {
            auto pt = [](qreal px) { return QString::number(ParagraphStyles::pxToPt(px), 'f', 2) + QStringLiteral("pt"); };
            const Qt::Alignment a = st.align & Qt::AlignHorizontal_Mask;
            defs += QStringLiteral("<style:style style:name=\"%1\" style:display-name=\"%2\" style:family=\"paragraph\" "
                                   "style:class=\"text\"><style:paragraph-properties fo:margin-left=\"%3\" "
                                   "fo:margin-right=\"%4\" fo:text-indent=\"%5\" fo:margin-top=\"%6\" "
                                   "fo:margin-bottom=\"%7\" fo:text-align=\"%8\"/><style:text-properties "
                                   "fo:font-size=\"%9pt\" fo:font-weight=\"%10\" fo:font-style=\"%11\"/></style:style>")
                        .arg(st.name, st.display, pt(st.left), pt(st.right), pt(st.indent), pt(st.top), pt(st.bottom),
                             a & Qt::AlignHCenter ? QStringLiteral("center") : a & Qt::AlignRight ? QStringLiteral("end") : QStringLiteral("start"))
                        .arg(st.pointSize)
                        .arg(st.weight >= 600 ? QStringLiteral("bold") : QStringLiteral("normal"),
                             st.italic ? QStringLiteral("italic") : QStringLiteral("normal"));
        }
        QFile styles(dir.filePath(QStringLiteral("styles.xml")));
        if (styles.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            styles.write(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                        "<office:document-styles xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
                                        "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
                                        "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
                                        "office:version=\"1.2\"><office:styles>%1</office:styles></office:document-styles>\n")
                             .arg(defs).toUtf8());
            styles.close();
            members << QStringLiteral("styles.xml");
            QProcess man;
            man.start(QStringLiteral("unzip"), {QStringLiteral("-p"), path, QStringLiteral("META-INF/manifest.xml")});
            if (man.waitForFinished(15000) && man.exitCode() == 0) {
                QString manifest = QString::fromUtf8(man.readAllStandardOutput());
                if (!manifest.contains(QLatin1String("\"styles.xml\""))) {
                    manifest.replace(QLatin1String("</manifest:manifest>"),
                                     QLatin1String("<manifest:file-entry manifest:media-type=\"text/xml\" "
                                                   "manifest:full-path=\"styles.xml\"/></manifest:manifest>"));
                    QDir(dir.path()).mkpath(QStringLiteral("META-INF"));
                    QFile mf(dir.filePath(QStringLiteral("META-INF/manifest.xml")));
                    if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        mf.write(manifest.toUtf8());
                        members << QStringLiteral("META-INF/manifest.xml");
                    }
                }
            }
        }
    }
    QFile content(dir.filePath(QStringLiteral("content.xml")));
    if (!dir.isValid() || !content.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(QStringLiteral("Headings/tables saved unpatched (temp dir unavailable)."));
    }
    content.write(patched.toUtf8());
    content.close();
    // Replace just that member; the stored "mimetype" member stays first.
    QProcess zip;
    zip.setWorkingDirectory(dir.path());
    zip.start(QStringLiteral("zip"), QStringList{QStringLiteral("-X"), QStringLiteral("-q"),
                                                 QFileInfo(path).absoluteFilePath()} + members);
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

// Qt's ODF writer re-encodes every picture as PNG. Put JPEG originals back
// (photos would grow several times over and lose nothing by staying JPEG):
// the frame's draw:name is the image resource name, so map frame -> member.
bool restoreJpegPictures(QTextDocument *doc, const QString &path, QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };
    QProcess unzip;
    unzip.start(QStringLiteral("unzip"), {QStringLiteral("-p"), path, QStringLiteral("content.xml")});
    if (!unzip.waitForFinished(15000) || unzip.exitCode() != 0) {
        return fail(QStringLiteral("Pictures saved as PNG (unzip unavailable)."));
    }
    QHash<QString, QByteArray> jpegs; // member -> original bytes
    QXmlStreamReader r(unzip.readAllStandardOutput());
    QString frameName;
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        if (r.name() == QLatin1String("frame")) {
            frameName = r.attributes().value(QStringLiteral("draw:name")).toString();
        } else if (r.name() == QLatin1String("image") && !frameName.isEmpty()) {
            const QString href = r.attributes().value(QStringLiteral("xlink:href")).toString();
            const QByteArray data = doc->resource(QTextDocument::ImageResource, QUrl(frameName)).toByteArray();
            if (data.startsWith("\xFF\xD8\xFF") && !href.isEmpty()) {
                jpegs.insert(href, data);
            }
            frameName.clear();
        }
    }
    if (jpegs.isEmpty()) {
        return true;
    }
    QTemporaryDir dir;
    if (!dir.isValid()) {
        return fail(QStringLiteral("Pictures saved as PNG (no temp dir)."));
    }
    QProcess man;
    man.start(QStringLiteral("unzip"), {QStringLiteral("-p"), path, QStringLiteral("META-INF/manifest.xml")});
    if (!man.waitForFinished(15000) || man.exitCode() != 0) {
        return fail(QStringLiteral("Pictures saved as PNG (no manifest)."));
    }
    QString manifest = QString::fromUtf8(man.readAllStandardOutput());
    QStringList members;
    for (auto it = jpegs.cbegin(); it != jpegs.cend(); ++it) {
        const QString file = dir.filePath(it.key());
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly) || f.write(it.value()) != it.value().size()) {
            return fail(QStringLiteral("Pictures saved as PNG (temp write failed)."));
        }
        members << it.key();
        static const QRegularExpression entry(QStringLiteral(
            "manifest:media-type=\"image/png\"(\\s+manifest:full-path=\"%1\")"));
        const QRegularExpression one(entry.pattern().arg(QRegularExpression::escape(it.key())));
        manifest.replace(one, QStringLiteral("manifest:media-type=\"image/jpeg\"\\1"));
    }
    QDir().mkpath(dir.filePath(QStringLiteral("META-INF")));
    QFile mf(dir.filePath(QStringLiteral("META-INF/manifest.xml")));
    if (!mf.open(QIODevice::WriteOnly) || mf.write(manifest.toUtf8()) < 0) {
        return fail(QStringLiteral("Pictures saved as PNG (temp write failed)."));
    }
    mf.close();
    members << QStringLiteral("META-INF/manifest.xml");
    QProcess zip;
    zip.setWorkingDirectory(dir.path());
    zip.start(QStringLiteral("zip"), QStringList{QStringLiteral("-X"), QStringLiteral("-q"), path} + members);
    if (!zip.waitForFinished(30000) || zip.exitCode() != 0) {
        return fail(QStringLiteral("Pictures saved as PNG (zip unavailable)."));
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

QStringList headingFontFamilies()
{
    QStringList out{QGuiApplication::font().family(), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"),
                    QStringLiteral("Helvetica Neue"), QStringLiteral("Arial"), QStringLiteral("sans-serif")};
    out.removeAll(QString());
    out.removeDuplicates();
    return out;
}

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
    if (ext == QLatin1String("md") || ext == QLatin1String("markdown")) {
        return Format::Markdown;
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
    case Format::Markdown:
        return QStringLiteral("md");
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
    case Format::Markdown:
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
        "Markdown (*.md *.markdown);;"
        "All Files (*)");
}

QString saveFilter()
{
    // ODT first — native default.
    return QStringLiteral(
        "OpenDocument Text (*.odt);;"
        "Plain Text (*.txt);;"
        "Rich Text Format (*.rtf);;"
        "Markdown (*.md)");
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
    if (f.contains(QLatin1String("*.md")) || f.contains(QLatin1String("markdown"))) {
        return Format::Markdown;
    }
    const Format fromPath = formatFromPath(pathHint);
    return fromPath == Format::Unknown ? Format::Odt : fromPath;
}

namespace {

// Kept on the QTextDocument between load() and save() of a Markdown file.
constexpr const char *kMarkdownEolProperty = "zwriter_markdownEol"; // "\n" or "\r\n"
constexpr const char *kMarkdownBomProperty = "zwriter_markdownBom"; // bool
constexpr const char *kMarkdownLossyProperty = "zwriter_markdownLossy"; // bool

void loadMarkdownBytes(QTextDocument *doc, const QByteArray &bytes)
{
    QByteArray body = bytes;
    const bool bom = body.startsWith("\xEF\xBB\xBF");
    if (bom) {
        body.remove(0, 3);
    }
    QStringDecoder decode(QStringDecoder::Utf8);
    QString text = decode(body);
    const bool invalidUtf8 = decode.hasError();
    // One line-ending style per file, taken from its first line break.
    const qsizetype firstLf = text.indexOf(QLatin1Char('\n'));
    const bool crlf = firstLf > 0 && text.at(firstLf - 1) == QLatin1Char('\r');
    // Lone CRs, or LF and CRLF mixed, come back in the file's one style;
    // invalid UTF-8 comes back as U+FFFD.
    const qsizetype crlfCount = text.count(QStringLiteral("\r\n"));
    const bool mixed = (crlf && crlfCount != text.count(QLatin1Char('\n')))
        || text.count(QLatin1Char('\r')) != crlfCount || (!crlf && crlfCount > 0);
    const bool lossy = invalidUtf8 || mixed;
    if (crlf) {
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    }
    doc->setPlainText(text);
    doc->setProperty(kMarkdownLossyProperty, lossy);
    doc->setProperty(kMarkdownEolProperty, crlf ? QStringLiteral("\r\n") : QStringLiteral("\n"));
    doc->setProperty(kMarkdownBomProperty, bom);
    doc->clearUndoRedoStacks();
    doc->setModified(false);
}

} // namespace

void resetMarkdownFileFormat(QTextDocument *doc)
{
    if (doc) {
        doc->setProperty(kMarkdownEolProperty, QStringLiteral("\n"));
        doc->setProperty(kMarkdownBomProperty, false);
        doc->setProperty(kMarkdownLossyProperty, false);
    }
}

bool markdownLoadWasLossy(const QTextDocument *doc)
{
    return doc && doc->property(kMarkdownLossyProperty).toBool();
}

QString markdownText(const QTextDocument *doc)
{
    if (!doc) {
        return QString();
    }
    QString eol = doc->property(kMarkdownEolProperty).toString();
    if (eol.isEmpty()) {
        eol = QStringLiteral("\n");
    }
    // toRawText(), not toPlainText(): the latter turns no-break spaces into
    // ordinary spaces, which would change the file.
    QString text = doc->toRawText();
    text.remove(QChar(0xFDD0)); // frame markers / objects (only from rich
    text.remove(QChar(0xFDD1)); // documents saved as Markdown)
    text.remove(QChar::ObjectReplacementCharacter);
    text.replace(QChar::ParagraphSeparator, eol);
    text.replace(QChar::LineSeparator, eol);
    return text;
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

    if (fmt == Format::Markdown) {
        loadMarkdownBytes(doc, bytes);
        return true;
    }

    if (fmt == Format::Rtf
        || QString::fromUtf8(bytes.left(5)).startsWith(QLatin1String("{\\rtf"))) {
        QHash<QString, QByteArray> pictures;
        const QString html = rtfToHtml(bytes, &pictures);
        for (auto it = pictures.cbegin(); it != pictures.cend(); ++it) {
            doc->addResource(QTextDocument::ImageResource, QUrl(it.key()), it.value());
        }
        doc->setHtml(html);
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
        if (!patchOdtContent(copy.get(), path, &headingError, doc)) {
            qWarning("zwriter: %s", qPrintable(headingError));
        }
        QString pictureError;
        if (!restoreJpegPictures(copy.get(), path, &pictureError)) {
            qWarning("zwriter: %s", qPrintable(pictureError));
        }
        return true;
    }
    if (format == Format::Txt) {
        return saveWithWriter(doc, path, QByteArrayLiteral("plaintext"), error);
    }
    if (format == Format::Markdown) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) {
                *error = QStringLiteral("Could not write %1.").arg(path);
            }
            return false;
        }
        QByteArray data;
        if (doc->property(kMarkdownBomProperty).toBool()) {
            data = QByteArrayLiteral("\xEF\xBB\xBF");
        }
        data += markdownText(doc).toUtf8();
        if (file.write(data) != data.size() || !file.commit()) {
            if (error) {
                *error = QStringLiteral("Could not write %1.").arg(path);
            }
            return false;
        }
        return true;
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
