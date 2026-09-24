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

// Convert a slice of ODT content.xml into simple HTML for QTextDocument.
// Parses automatic styles for font-family / size / weight / style so ODT
// round-trip preserves the hide-away toolbar font choices.
QString odtXmlToHtml(const QByteArray &xml)
{
    struct StyleInfo {
        QString fontFamily;
        QString fontSize; // e.g. "12pt"
        bool bold = false;
        bool italic = false;
        bool underline = false;
        QString align;          // CSS text-align value, empty = default
        bool breakBefore = false; // manual page break before the paragraph
    };

    struct ListInfo {
        bool ordered = false;
        QString type; // HTML list type: disc/circle/square or 1/a/A/i/I
    };

    QHash<QString, StyleInfo> styles;
    QHash<QString, ListInfo> listStyles;
    {
        QXmlStreamReader pass1(xml);
        QString currentStyle;
        QString currentListStyle;
        while (!pass1.atEnd()) {
            const auto token = pass1.readNext();
            if (token == QXmlStreamReader::StartElement) {
                const QStringView name = pass1.name();
                if (name == QLatin1String("style")) {
                    currentStyle = pass1.attributes().value(QStringLiteral("style:name")).toString();
                } else if (name == QLatin1String("list-style")) {
                    currentListStyle = pass1.attributes().value(QStringLiteral("style:name")).toString();
                } else if ((name == QLatin1String("list-level-style-bullet")
                            || name == QLatin1String("list-level-style-number"))
                           && !currentListStyle.isEmpty() && !listStyles.contains(currentListStyle)) {
                    // First (outermost) level decides the list's look.
                    ListInfo info;
                    const auto attrs = pass1.attributes();
                    if (name == QLatin1String("list-level-style-number")) {
                        info.ordered = true;
                        const QString fmt = attrs.value(QStringLiteral("style:num-format")).toString();
                        info.type = (fmt == QLatin1String("a") || fmt == QLatin1String("A")
                                     || fmt == QLatin1String("i") || fmt == QLatin1String("I"))
                            ? fmt : QStringLiteral("1");
                    } else {
                        const QString bullet = attrs.value(QStringLiteral("text:bullet-char")).toString();
                        info.type = (bullet == QStringLiteral("○")) ? QStringLiteral("circle")
                            : (bullet == QStringLiteral("■") || bullet == QStringLiteral("▪"))
                                ? QStringLiteral("square") : QStringLiteral("disc");
                    }
                    listStyles.insert(currentListStyle, info);
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

    QXmlStreamReader reader(xml);
    QString html = QStringLiteral(
        "<html><body style=\"font-family: 'Courier New', 'Liberation Mono', 'Noto Sans Mono', "
        "Courier, Menlo, Monaco, 'DejaVu Sans Mono', monospace; font-size: 12pt; color: #1a1a1a;\">");

    QStringList listClosers; // closing tags for the currently open lists
    int headingLevel = 0;
    bool inBold = false;
    bool inItalic = false;
    bool inUnderline = false;
    bool inP = false;
    bool pHasContent = false; // Qt's HTML import drops paragraphs with no content
    bool inStyleSpan = false;

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
        return css;
    };

    // Block-level properties only make sense on <p>/<h*>, not on inline spans.
    auto blockCss = [&](const StyleInfo &info) -> QString {
        QString css = cssFromStyle(info);
        if (!info.align.isEmpty()) {
            css += QStringLiteral("text-align:%1;").arg(info.align);
        }
        if (info.breakBefore) {
            css += QStringLiteral("page-break-before:always;");
        }
        return css;
    };

    auto openParagraph = [&](int level, const QString &styleName) {
        if (inP) {
            closeInline();
            html += (headingLevel > 0)
                ? QStringLiteral("</h%1>").arg(headingLevel)
                : QStringLiteral("</p>");
        }
        headingLevel = level;
        inP = true;
        pHasContent = false;
        const StyleInfo info = styles.value(styleName);
        const QString css = blockCss(info);
        const QString styleAttr = css.isEmpty()
            ? QString()
            : QStringLiteral(" style=\"%1\"").arg(css);
        if (level == 1) {
            html += QStringLiteral("<h1%1>").arg(styleAttr);
        } else if (level == 2) {
            html += QStringLiteral("<h2%1>").arg(styleAttr);
        } else if (level >= 3) {
            html += QStringLiteral("<h3%1>").arg(styleAttr);
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

    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();
            if (name == QLatin1String("h")) {
                const QStringView lvl = reader.attributes().value(QStringLiteral("text:outline-level"));
                bool ok = false;
                int level = lvl.toInt(&ok);
                if (!ok || level < 1) {
                    level = 1;
                }
                if (level > 3) {
                    level = 3;
                }
                const QString styleName =
                    reader.attributes().value(QStringLiteral("text:style-name")).toString();
                openParagraph(level, styleName);
            } else if (name == QLatin1String("p")) {
                const QString styleName =
                    reader.attributes().value(QStringLiteral("text:style-name")).toString();
                openParagraph(0, styleName);
            } else if (name == QLatin1String("span")) {
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
                const ListInfo info = listStyles.value(
                    reader.attributes().value(QStringLiteral("text:style-name")).toString());
                if (info.ordered) {
                    html += QStringLiteral("<ol type=\"%1\">").arg(info.type);
                    listClosers.append(QStringLiteral("</ol>"));
                } else {
                    html += QStringLiteral("<ul type=\"%1\">")
                                .arg(info.type.isEmpty() ? QStringLiteral("disc") : info.type);
                    listClosers.append(QStringLiteral("</ul>"));
                }
            } else if (name == QLatin1String("list-item")) {
                html += QStringLiteral("<li>");
            } else if (name == QLatin1String("table")) {
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
            } else if (name == QLatin1String("s")) {
                html += QLatin1Char(' ');
                pHasContent = true;
            } else if (name == QLatin1String("tab")) {
                html += QLatin1Char('\t');
                pHasContent = true;
            } else if (name == QLatin1String("line-break")) {
                html += QStringLiteral("<br/>");
                pHasContent = true;
            } else if (name == QLatin1String("a")) {
                // keep text only
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QStringView name = reader.name();
            if (name == QLatin1String("span")) {
                closeInline();
            } else if (name == QLatin1String("list-item")) {
                html += QStringLiteral("</li>");
            } else if (name == QLatin1String("list")) {
                if (!listClosers.isEmpty()) {
                    html += listClosers.takeLast();
                }
            } else if (name == QLatin1String("table-cell")) {
                html += QStringLiteral("</td>");
            } else if (name == QLatin1String("table-row")) {
                html += QStringLiteral("</tr>");
            } else if (name == QLatin1String("table")) {
                html += QStringLiteral("</table>");
            } else if (name == QLatin1String("p") || name == QLatin1String("h")) {
                if (inP && !pHasContent) {
                    html += QStringLiteral("<br/>"); // keep blank paragraphs (blank lines)
                }
                closeInline();
                if (inP) {
                    html += (headingLevel > 0)
                        ? QStringLiteral("</h%1>").arg(headingLevel)
                        : QStringLiteral("</p>");
                    inP = false;
                    headingLevel = 0;
                }
            }
        } else if (token == QXmlStreamReader::Characters && !reader.isWhitespace()) {
            html += reader.text().toString().toHtmlEscaped();
            pHasContent = true;
        }
    }

    if (inP) {
        closeInline();
        html += (headingLevel > 0) ? QStringLiteral("</h%1>").arg(headingLevel)
                                   : QStringLiteral("</p>");
    }
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
//  * an empty paragraph arrives as a block holding a lone U+2028 line separator
//    (from the <br/> that keeps it alive) — make it truly empty;
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
    // 1) lone line separators -> empty blocks
    QTextCursor c(doc);
    c.beginEditBlock();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.text() == QString(QChar(QChar::LineSeparator))) {
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
    doc->setModified(false);
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
        return true;
    }

    // TXT (and unknown): plain UTF-8
    doc->setPlainText(QString::fromUtf8(bytes));
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
        return saveWithWriter(doc, path, QByteArrayLiteral("odf"), error);
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
