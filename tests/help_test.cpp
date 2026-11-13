// Help dialog tests (ctest, headless):
//   R  the embedded Markdown resource loads and renders non-empty content
//   A  in-page #anchor links scroll the guide
//   L  https links are offered to the open-url handler; other schemes are ignored
//   T  applyTheme restyles the browser in the new palette
//   G  the guide documents the real menu labels and shortcuts
// Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "HelpDialog.hpp"
#include "Theme.hpp"

#include <QApplication>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>

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

void process()
{
    QApplication::processEvents();
}

bool documentHas(const QTextDocument *doc, const QString &needle)
{
    return doc && doc->toPlainText().contains(needle);
}

void testResourceAndRender()
{
    const QString md = HelpDialog::loadGuideMarkdown();
    check(!md.trimmed().isEmpty(), "R resource is non-empty", HelpDialog::resourcePath());
    check(md.contains(QStringLiteral("Documents & Modes")), "R source has Documents & Modes");
    check(md.contains(QStringLiteral("Keyboard Shortcuts")), "R source has Keyboard Shortcuts");

    HelpDialog dlg;
    process();
    check(dlg.objectName() == QStringLiteral("helpDialog"), "R objectName is helpDialog");
    check(dlg.windowTitle() == QStringLiteral("zwriter Help"), "R window title");
    check(dlg.sourceMarkdown() == md, "R sourceMarkdown matches the resource");
    QTextBrowser *b = dlg.browser();
    check(b && b->objectName() == QStringLiteral("helpBrowser"), "R browser objectName");
    check(b && b->isReadOnly() && !b->openExternalLinks() && !b->openLinks(),
          "R browser is read-only and does not open links itself");
    check(b && !b->document()->toPlainText().trimmed().isEmpty(), "R rendered text is non-empty");
    check(b && documentHas(b->document(), QStringLiteral("Documents & Modes")),
          "R rendered Documents & Modes");
    check(b && documentHas(b->document(), QStringLiteral("zwriter Help")), "R rendered title");
}

void testAnchors()
{
    HelpDialog dlg;
    dlg.resize(720, 640);
    dlg.show();
    process();
    QTextBrowser *b = dlg.browser();
    QScrollBar *vs = b->verticalScrollBar();
    vs->setValue(vs->maximum());
    process();
    const int atBottom = vs->value();
    check(atBottom > 0, "A guide is long enough to scroll", QString::number(atBottom));

    check(dlg.handleAnchor(QUrl(QStringLiteral("#documents-modes"))), "A documents-modes is handled");
    process();
    const int atDocs = vs->value();
    check(atDocs < atBottom, "A jumping to documents-modes leaves the bottom",
          QStringLiteral("bottom=%1 docs=%2").arg(atBottom).arg(atDocs));

    check(dlg.handleAnchor(QUrl(QStringLiteral("#keyboard-shortcuts"))), "A keyboard-shortcuts is handled");
    process();
    const int atKeys = vs->value();
    check(atKeys > atDocs, "A keyboard-shortcuts is further down than documents-modes",
          QStringLiteral("docs=%1 keys=%2").arg(atDocs).arg(atKeys));
}

void testLinks()
{
    HelpDialog dlg;
    QUrl captured;
    dlg.setOpenUrlHandler([&captured](const QUrl &u) { captured = u; });

    const QUrl https(QStringLiteral("https://github.com/sbj-ee/zwriter"));
    check(dlg.handleAnchor(https), "L https is allowed");
    check(dlg.lastOpenedUrl() == https && captured == https, "L https is handed to the opener",
          dlg.lastOpenedUrl().toString());

    captured = QUrl();
    check(!dlg.handleAnchor(QUrl(QStringLiteral("http://example.com/insecure"))), "L http is blocked");
    check(dlg.lastOpenedUrl().isEmpty() && captured.isEmpty(), "L http does not open anything");

    check(!dlg.handleAnchor(QUrl(QStringLiteral("file:///etc/passwd"))), "L file is blocked");
    check(dlg.lastOpenedUrl().isEmpty(), "L file does not open anything");

    check(!dlg.handleAnchor(QUrl(QStringLiteral("javascript:alert(1)"))), "L javascript is blocked");
    check(!dlg.handleAnchor(QUrl(QStringLiteral("mailto:user@example.com"))), "L mailto is blocked");
    check(captured.isEmpty(), "L no blocked scheme reached the opener");
}

void testTheme()
{
    HelpDialog dlg;
    const ThemeColors paper = Theme::colors(QStringLiteral("paper"));
    const ThemeColors dark = Theme::colors(QStringLiteral("dark"));
    dlg.applyTheme(paper);
    process();
    const QString paperCss = dlg.browser()->styleSheet();
    check(paperCss.contains(paper.pageBg.name()), "T paper theme sets the page background",
          paper.pageBg.name());
    dlg.applyTheme(dark);
    process();
    const QString darkCss = dlg.browser()->styleSheet();
    check(darkCss.contains(dark.pageBg.name()), "T dark theme sets the page background", dark.pageBg.name());
    check(darkCss != paperCss, "T changing theme updates the stylesheet");
    check(documentHas(dlg.browser()->document(), QStringLiteral("Documents & Modes")),
          "T restyle keeps the rendered guide");
}

void testGuideMatchesApp()
{
    const QString md = HelpDialog::loadGuideMarkdown();
    const char *needles[] = {
        "Help \u2192 zwriter Help",
        "`F1`",
        "File \u2192 New Markdown Document",
        "Ctrl+Alt+N",
        "View \u2192 Markdown Preview",
        "Ctrl+Alt+P",
        "formatting toolbar is hidden",
        "View \u2192 Always Show Toolbar",
        "**Esc**",
        "top or bottom 10 pixels",
        "Alt+letter",
        "View \u2192 Focus Mode",
        "Ctrl+Shift+F",
        "View \u2192 Typewriter Scroll",
        "Ctrl+Shift+T",
        "View \u2192 Full Screen",
        "`F11`",
        "File \u2192 Insert/Update Table of Contents",
        "00 Contents.odt",
        "does not expand placeholders",
        "{{ TOC }}",
        "[TOC]",
        "View \u2192 Outline",
        "Ctrl+Alt+O",
        "Format \u2192 Styles\u2026",
        "File \u2192 Export as PDF\u2026",
        "Ctrl+Shift+E",
        "File \u2192 Export Book",
        "nav.xhtml",
        "Help \u2192 Check for Updates\u2026",
        "Ctrl+Alt+0",
        "Ctrl+Shift+Y",
        "Ctrl+Alt+B",
        "Ctrl+Shift+L",
        nullptr,
    };
    for (int i = 0; needles[i]; ++i) {
        const QString n = QString::fromUtf8(needles[i]);
        check(md.contains(n), "G guide mentions expected text", n);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("sbj-ee-test"));
    QApplication::setApplicationName(QStringLiteral("zwriter-help-test"));

    testResourceAndRender();
    testAnchors();
    testLinks();
    testTheme();
    testGuideMatchesApp();

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
