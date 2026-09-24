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
    };

    QHash<QString, StyleInfo> styles;
    {
        QXmlStreamReader pass1(xml);
        QString currentStyle;
        while (!pass1.atEnd()) {
            const auto token = pass1.readNext();
            if (token == QXmlStreamReader::StartElement) {
                const QStringView name = pass1.name();
                if (name == QLatin1String("style")) {
                    currentStyle = pass1.attributes().value(QStringLiteral("style:name")).toString();
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
                    styles.insert(currentStyle, info);
                }
            } else if (token == QXmlStreamReader::EndElement) {
                if (pass1.name() == QLatin1String("style")) {
                    currentStyle.clear();
                }
            }
        }
    }

    QXmlStreamReader reader(xml);
    QString html = QStringLiteral(
        "<html><body style=\"font-family: 'Courier New', 'Liberation Mono', 'Noto Sans Mono', "
        "Courier, Menlo, Monaco, 'DejaVu Sans Mono', monospace; font-size: 12pt; color: #1a1a1a;\">");

    int headingLevel = 0;
    bool inBold = false;
    bool inItalic = false;
    bool inP = false;
    bool inStyleSpan = false;

    auto closeInline = [&]() {
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

    auto openParagraph = [&](int level, const QString &styleName) {
        if (inP) {
            closeInline();
            html += (headingLevel > 0)
                ? QStringLiteral("</h%1>").arg(headingLevel)
                : QStringLiteral("</p>");
        }
        headingLevel = level;
        inP = true;
        const StyleInfo info = styles.value(styleName);
        const QString css = cssFromStyle(info);
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
            } else if (name == QLatin1String("s")) {
                html += QLatin1Char(' ');
            } else if (name == QLatin1String("tab")) {
                html += QLatin1Char('\t');
            } else if (name == QLatin1String("line-break")) {
                html += QStringLiteral("<br/>");
            } else if (name == QLatin1String("a")) {
                // keep text only
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QStringView name = reader.name();
            if (name == QLatin1String("span")) {
                closeInline();
            } else if (name == QLatin1String("p") || name == QLatin1String("h")) {
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
