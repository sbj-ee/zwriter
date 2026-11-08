#include "OutlineDraft.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace OutlineDraft {

namespace {

// Columns of leading whitespace; a tab counts as 4.
int indentWidth(const QString &line, int *textStart)
{
    int w = 0;
    int i = 0;
    for (; i < line.size() && (line[i] == QLatin1Char(' ') || line[i] == QLatin1Char('\t')); ++i) {
        w += line[i] == QLatin1Char('\t') ? 4 : 1;
    }
    *textStart = i;
    return w;
}

QString stripMarker(QString s)
{
    static const QRegularExpression marker(
        QStringLiteral("^(?:[-*+•]\\s+|(?:\\d+(?:\\.\\d+)*[.)]|\\d+(?:\\.\\d+)+|[IVXLCivxlc]+[.)])\\s+)"));
    const QRegularExpressionMatch m = marker.match(s);
    if (m.hasMatch()) {
        s.remove(0, m.capturedLength());
    }
    return s.trimmed();
}

} // namespace

Parsed parse(const QString &text, int maxLevel)
{
    Parsed out;
    QList<int> indents; // stack of indent widths, one per level in use
    int prev = 0;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (line.trimmed().isEmpty()) {
            continue;
        }
        int start = 0;
        const int width = indentWidth(line, &start);
        QString body = line.mid(start);
        int level = 0;
        if (body.startsWith(QLatin1Char('#'))) {
            int n = 0;
            while (n < body.size() && body[n] == QLatin1Char('#')) {
                ++n;
            }
            if (n < body.size() && body[n].isSpace()) {
                level = n;
                body = stripMarker(body.mid(n).trimmed());
                indents.clear();
            }
        }
        if (level == 0) {
            while (!indents.isEmpty() && indents.back() > width) {
                indents.removeLast();
            }
            if (indents.isEmpty() || indents.back() < width) {
                indents.append(width);
            }
            level = indents.size();
            body = stripMarker(body);
        }
        // Closing "#"s of an ATX heading are not part of the title.
        static const QRegularExpression closing(QStringLiteral("\\s+#+\\s*$"));
        body.remove(closing);
        body = body.trimmed();
        if (body.isEmpty() || body.count(QLatin1Char('#')) == body.size()) {
            continue;
        }
        const int fit = qMax(1, std::min({level, prev + 1, maxLevel}));
        if (fit != level) {
            ++out.clamped;
        }
        out.entries.append({fit, body});
        prev = fit;
    }
    return out;
}

QString toMarkdown(const QList<Entry> &entries)
{
    QString s;
    for (const Entry &e : entries) {
        s += QString(e.level, QLatin1Char('#')) + QLatin1Char(' ') + e.title + QStringLiteral("\n\n");
    }
    return s;
}

} // namespace OutlineDraft
