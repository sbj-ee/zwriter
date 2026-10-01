// Side-pane tests (ctest, headless):
//   D  PreviewPane debounce: ~300 ms after the LAST edit, one render per burst,
//      none while hidden (caught up on show)
//   R  render: QTextDocument::setMarkdown output (headings, emphasis, code,
//      links, lists, quotes), read-only, theme colours and font
//   S  rough proportional scroll sync editor -> preview
//   F  LibrarySidebar filter: folders + .md / .txt only (any case), no hidden
//      entries; clicking a file emits fileActivated, a folder does not
//   P  QSettings: markdown/previewVisible, library/rootPath, library/visible
// Run: QT_QPA_PLATFORM=offscreen ctest --output-on-failure
#include "LibrarySidebar.hpp"
#include "PreviewPane.hpp"
#include "Theme.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemModel>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTreeView>

#include <cstdio>
#include <functional>

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

// Run the event loop for ms milliseconds.
void spin(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

// Run the event loop until cond() or the timeout; true if cond() held.
bool spinUntil(const std::function<bool()> &cond, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

void touch(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write("x\n");
    }
}

// Find the text fragment containing `needle`; returns its char format.
QTextCharFormat formatOf(QTextDocument *doc, const QString &needle, bool *found)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.isValid() && f.text().contains(needle)) {
                *found = true;
                return f.charFormat();
            }
        }
    }
    *found = false;
    return {};
}

QTextBlock blockWith(QTextDocument *doc, const QString &needle)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.text().contains(needle)) {
            return b;
        }
    }
    return {};
}

// ---------------------------------------------------------------- debounce

void testDebounce()
{
    QTextEdit editor;
    PreviewPane pane;
    pane.attachEditor(&editor);
    pane.resize(400, 300);
    pane.show();
    spin(20);
    check(pane.debounceInterval() == 300 && PreviewPane::kDebounceMs == 300,
          "D debounce interval is 300 ms");

    const int before = pane.renderCount();
    QElapsedTimer sinceLastEdit;
    for (int i = 0; i < 5; ++i) {
        editor.insertPlainText(QStringLiteral("word "));
        sinceLastEdit.start();
        spin(50); // edits 50 ms apart: shorter than the debounce
    }
    check(pane.renderCount() == before && pane.isRenderPending(),
          "D no render while edits keep coming (5 edits, 50 ms apart)",
          QStringLiteral("renders %1").arg(pane.renderCount() - before));
    const bool rendered = spinUntil([&] { return pane.renderCount() > before; }, 2000);
    const qint64 waited = sinceLastEdit.elapsed();
    check(rendered && waited >= 250, // coarse timers may fire up to 5 % early
          "D renders about 300 ms after the last edit",
          QStringLiteral("%1 ms").arg(waited));
    spin(400);
    check(pane.renderCount() == before + 1, "D the whole burst renders once",
          QStringLiteral("renders %1").arg(pane.renderCount() - before));
    check(pane.renderedDocument()->toPlainText().contains(QLatin1String("word word word word word")),
          "D the render has the latest text");

    // Hidden: edits only mark the preview stale; showing it catches up at once.
    pane.hide();
    const int hiddenBefore = pane.renderCount();
    editor.insertPlainText(QStringLiteral("hidden-edit"));
    spin(450);
    check(pane.renderCount() == hiddenBefore && !pane.isRenderPending(),
          "D a hidden preview does not render");
    pane.show();
    spin(20);
    check(pane.renderCount() == hiddenBefore + 1
              && pane.renderedDocument()->toPlainText().contains(QLatin1String("hidden-edit")),
          "D showing the preview renders the current text right away");
}

// ------------------------------------------------------------------ render

void testRender()
{
    QTextEdit editor;
    PreviewPane pane;
    pane.attachEditor(&editor);
    const ThemeColors paper = Theme::colors(QStringLiteral("paper"));
    QFont duo;
    duo.setFamilies({QStringLiteral("iA Writer Duo S"), QStringLiteral("Courier New")});
    duo.setPointSize(13);
    pane.applyTheme(paper, duo);
    editor.setPlainText(QStringLiteral(
        "# Title\n\n"
        "Some **bold** and *italic* text with `inline code` and a [link](https://example.com/x).\n\n"
        "- first item\n- second item\n\n"
        "> quoted line\n\n"
        "```\nfenced block\n```\n"));
    pane.show();
    pane.renderNow();
    QTextDocument *doc = pane.renderedDocument();

    check(pane.browser()->isReadOnly(), "R the preview is read-only");
    check(!doc->toPlainText().contains(QLatin1Char('#')) && !doc->toPlainText().contains(QLatin1String("**")),
          "R markup is rendered, not shown");
    const QTextBlock title = blockWith(doc, QStringLiteral("Title"));
    check(title.isValid() && title.blockFormat().headingLevel() == 1, "R '# Title' is a level-1 heading");
    bool found = false;
    QTextCharFormat f = formatOf(doc, QStringLiteral("bold"), &found);
    check(found && f.fontWeight() >= QFont::Bold, "R **bold** is bold");
    f = formatOf(doc, QStringLiteral("italic"), &found);
    check(found && f.fontItalic(), "R *italic* is italic");
    f = formatOf(doc, QStringLiteral("link"), &found);
    check(found && f.isAnchor() && f.anchorHref() == QLatin1String("https://example.com/x"),
          "R [link](url) is an anchor to the url");
    check(found && f.foreground().color() == paper.link, "R links use the theme's link colour");
    f = formatOf(doc, QStringLiteral("inline code"), &found);
    check(found && f.foreground().color() == paper.code, "R inline code uses the theme's code colour");
    const QTextBlock item = blockWith(doc, QStringLiteral("first item"));
    check(item.isValid() && item.textList() != nullptr, "R '- item' is a list");
    f = formatOf(doc, QStringLiteral("quoted line"), &found);
    check(found && f.foreground().color() == paper.quote, "R block quotes use the quote colour");
    const QTextBlock code = blockWith(doc, QStringLiteral("fenced block"));
    check(code.isValid() && code.blockFormat().background().color() == paper.hover,
          "R fenced code gets a code background");

    const QString css = pane.browser()->styleSheet();
    check(css.contains(paper.pageBg.name()) && css.contains(paper.pageFg.name())
              && css.contains(QLatin1String("'iA Writer Duo S'")),
          "R pane uses the theme's page colours and the reading face");

    // Switching theme re-renders in the new inks.
    const ThemeColors dark = Theme::colors(QStringLiteral("dark"));
    pane.applyTheme(dark, duo);
    f = formatOf(pane.renderedDocument(), QStringLiteral("link"), &found);
    check(found && f.foreground().color() == dark.link
              && pane.browser()->styleSheet().contains(dark.pageBg.name()),
          "R theme change recolours the preview");
}

// ------------------------------------------------------------- scroll sync

void testScrollSync()
{
    QTextEdit editor;
    editor.resize(400, 200);
    PreviewPane pane;
    pane.resize(400, 200);
    pane.attachEditor(&editor);
    QString text;
    for (int i = 0; i < 200; ++i) {
        text += QStringLiteral("Paragraph %1 of the long document.\n\n").arg(i);
    }
    editor.setPlainText(text);
    editor.show();
    pane.show();
    pane.renderNow();
    spin(50);
    QScrollBar *es = editor.verticalScrollBar();
    QScrollBar *ps = pane.browser()->verticalScrollBar();
    check(es->maximum() > 0 && ps->maximum() > 0, "S both sides scroll");
    es->setValue(es->maximum() / 2);
    spin(10);
    const qreal frac = qreal(ps->value()) / ps->maximum();
    check(qAbs(frac - 0.5) < 0.02, "S editor halfway -> preview halfway",
          QStringLiteral("%1").arg(frac));
    es->setValue(es->maximum());
    spin(10);
    check(ps->value() == ps->maximum(), "S editor at the end -> preview at the end");
    // A re-render keeps the position.
    editor.moveCursor(QTextCursor::End);
    editor.insertPlainText(QStringLiteral("\n\nOne more line."));
    es->setValue(es->maximum() / 4);
    pane.renderNow();
    spin(20);
    const qreal frac2 = qreal(ps->value()) / ps->maximum();
    check(qAbs(frac2 - 0.25) < 0.03, "S position survives a re-render",
          QStringLiteral("%1").arg(frac2));
}

// ------------------------------------------------------------------ filter

void testLibrary(const QString &root)
{
    QDir(root).mkpath(QStringLiteral("drafts/old"));
    QDir(root).mkpath(QStringLiteral("images"));
    QDir(root).mkpath(QStringLiteral(".git"));
    for (const char *name : {"notes.md", "todo.txt", "README.MD", "letter.odt", "essay.rtf",
                             "photo.png", "script.sh", "notes.md.bak", ".hidden.md",
                             "drafts/chapter.md", "drafts/cover.pdf"}) {
        touch(root + QLatin1Char('/') + QLatin1String(name));
    }

    check(LibrarySidebar::acceptsFile(QStringLiteral("a.md")) && LibrarySidebar::acceptsFile(QStringLiteral("B.TXT"))
              && LibrarySidebar::acceptsFile(QStringLiteral("/x/y/Notes.Md")),
          "F .md / .txt accepted, any case");
    check(!LibrarySidebar::acceptsFile(QStringLiteral("a.odt")) && !LibrarySidebar::acceptsFile(QStringLiteral("a.rtf"))
              && !LibrarySidebar::acceptsFile(QStringLiteral("a.md.bak"))
              && !LibrarySidebar::acceptsFile(QStringLiteral("a.markdown"))
              && !LibrarySidebar::acceptsFile(QStringLiteral(".hidden.md")),
          "F everything else rejected (odt, rtf, .md.bak, hidden)");

    LibrarySidebar side;
    side.resize(260, 400);
    side.show();
    side.setRootPath(root);
    QFileSystemModel *model = side.model();
    const QModelIndex rootIdx = side.view()->rootIndex();
    check(model->filePath(rootIdx) == QDir::cleanPath(root), "F tree is rooted at the chosen folder");
    const auto listed = [&](const QModelIndex &parent) {
        QStringList names;
        for (int r = 0; r < model->rowCount(parent); ++r) {
            names << model->fileName(model->index(r, 0, parent));
        }
        names.sort();
        return names;
    };
    const QStringList want{QStringLiteral("README.MD"), QStringLiteral("drafts"), QStringLiteral("images"),
                           QStringLiteral("notes.md"), QStringLiteral("todo.txt")};
    spinUntil([&] { return listed(rootIdx) == want; }, 3000);
    check(listed(rootIdx) == want, "F root lists folders + .md/.txt only",
          listed(rootIdx).join(QLatin1String(", ")));

    const QModelIndex drafts = model->index(root + QStringLiteral("/drafts"));
    model->fetchMore(drafts);
    const QStringList wantDrafts{QStringLiteral("chapter.md"), QStringLiteral("old")};
    spinUntil([&] { return listed(drafts) == wantDrafts; }, 3000);
    check(listed(drafts) == wantDrafts, "F sub-folders are filtered the same way",
          listed(drafts).join(QLatin1String(", ")));

    QStringList opened;
    QObject::connect(&side, &LibrarySidebar::fileActivated, [&](const QString &p) { opened << p; });
    emit side.view()->clicked(model->index(root + QStringLiteral("/notes.md")));
    check(opened == QStringList{root + QStringLiteral("/notes.md")}, "F clicking a file asks to open it");
    emit side.view()->activated(model->index(root + QStringLiteral("/notes.md")));
    check(opened.size() == 1, "F double-click (click + activate) opens it once");
    emit side.view()->clicked(drafts);
    check(opened.size() == 1, "F clicking a folder does not open anything");

    side.setCurrentFile(root + QStringLiteral("/todo.txt"));
    check(side.view()->currentIndex() == model->index(root + QStringLiteral("/todo.txt")),
          "F the open document is highlighted");

    side.setRootPath(root + QStringLiteral("/does-not-exist"));
    check(side.rootPath().isEmpty() && !side.view()->isVisibleTo(&side),
          "F a missing folder shows nothing (hint instead)");
}

// ---------------------------------------------------------------- settings

void testSettings()
{
    check(PreviewPane::settingsKey() == QLatin1String("markdown/previewVisible")
              && LibrarySidebar::rootPathKey() == QLatin1String("library/rootPath")
              && LibrarySidebar::visibleKey() == QLatin1String("library/visible"),
          "P settings keys");
    check(!PreviewPane::loadVisibleSetting(), "P preview defaults to off");
    const LibrarySidebar::Settings d = LibrarySidebar::loadSettings();
    check(d.rootPath.isEmpty() && !d.visible, "P library defaults: no folder, hidden");

    PreviewPane::saveVisibleSetting(true);
    LibrarySidebar::saveSettings({QStringLiteral("/home/writer/Library"), true});
    QSettings().sync();
    {
        QSettings fresh; // what the next launch reads
        check(fresh.value(PreviewPane::settingsKey()).toBool(), "P preview on is stored");
        check(fresh.value(LibrarySidebar::rootPathKey()).toString() == QLatin1String("/home/writer/Library")
                  && fresh.value(LibrarySidebar::visibleKey()).toBool(),
              "P library folder and visibility are stored");
    }
    const LibrarySidebar::Settings back = LibrarySidebar::loadSettings();
    check(PreviewPane::loadVisibleSetting() && back.rootPath == QLatin1String("/home/writer/Library")
              && back.visible,
          "P values load back");
    PreviewPane::saveVisibleSetting(false);
    LibrarySidebar::saveSettings({QString(), false});
    check(!PreviewPane::loadVisibleSetting() && !LibrarySidebar::loadSettings().visible,
          "P turning them off is stored too");
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    QTemporaryDir libraryDir;
    if (!settingsDir.isValid() || !libraryDir.isValid()) {
        std::printf("FAIL  temp dir\n");
        return 1;
    }
    // Never touch the real zwriter settings.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("sbj-ee-test"));
    QApplication::setApplicationName(QStringLiteral("zwriter-panes-test"));

    testDebounce();
    testRender();
    testScrollSync();
    testLibrary(libraryDir.path());
    testSettings();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
