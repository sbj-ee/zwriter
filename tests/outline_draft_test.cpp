// Outline draft parser tests (ctest, headless): levels from "#", from indentation,
// markers dropped, clamping, Markdown output.
#include "OutlineDraft.hpp"

#include <QCoreApplication>
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
QString show(const OutlineDraft::Parsed &p)
{
    QStringList l;
    for (const auto &e : p.entries) {
        l << QStringLiteral("%1:%2").arg(e.level).arg(e.title);
    }
    return l.join(QStringLiteral(" | "));
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace OutlineDraft;

    Parsed p = parse(QStringLiteral("# One\n## Two\n### Three\n# Four\n"), 6);
    check(show(p) == "1:One | 2:Two | 3:Three | 1:Four" && p.clamped == 0, "D1 # levels", show(p));

    p = parse(QStringLiteral("Intro\n  Background\n  Aims\n    Detail\nResults\n"), 6);
    check(show(p) == "1:Intro | 2:Background | 2:Aims | 3:Detail | 1:Results", "D2 indentation levels", show(p));

    p = parse(QStringLiteral("A\n\tB\n\t\tC\n\tD\n"), 6);
    check(show(p) == "1:A | 2:B | 3:C | 2:D", "D3 tabs", show(p));

    p = parse(QStringLiteral("- Plan\n  - Budget\n  * Team\n1. Do\n2.1 Sub\nIV) Roman\n"), 6);
    check(show(p) == "1:Plan | 2:Budget | 2:Team | 1:Do | 1:Sub | 1:Roman", "D4 bullets and numbers are dropped", show(p));

    p = parse(QStringLiteral("# A\n#### Deep\n"), 6);
    check(show(p) == "1:A | 2:Deep" && p.clamped == 1, "D5 a level is at most one below the line above", show(p));

    p = parse(QStringLiteral("# A\n## B\n### C\n#### D\n"), 3);
    check(show(p) == "1:A | 2:B | 3:C | 3:D" && p.clamped == 1, "D6 rich documents stop at level 3", show(p));

    p = parse(QStringLiteral("\n  \n## Starts deep\r\n# Closed ##\r\n#hashtag\n#\n"), 6);
    check(show(p) == "1:Starts deep | 1:Closed | 1:#hashtag" && p.clamped == 1,
          "D7 first line is level 1, CRLF, closing #s, blank and empty lines, '#word' is a title", show(p));

    p = parse(QString(), 6);
    check(p.entries.isEmpty(), "D8 empty input");

    check(toMarkdown(parse(QStringLiteral("A\n  B\nC"), 6).entries) == QStringLiteral("# A\n\n## B\n\n# C\n\n"),
          "D9 Markdown output");

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
