#include "PreviewPane.hpp"

#include <QDesktopServices>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <vector>

namespace {
constexpr qreal kPreviewMargin = 28.0; // px around the rendered text

QString cssFamilies(const QFont &font)
{
    QStringList families = font.families();
    if (families.isEmpty()) {
        families << font.family();
    }
    QStringList quoted;
    for (const QString &f : std::as_const(families)) {
        if (!f.isEmpty()) {
            quoted << QLatin1Char('\'') + f + QLatin1Char('\'');
        }
    }
    quoted << QStringLiteral("monospace");
    return quoted.join(QStringLiteral(", "));
}
} // namespace

PreviewPane::PreviewPane(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("markdownPreviewPane"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_browser = new QTextBrowser(this);
    m_browser->setObjectName(QStringLiteral("markdownPreview"));
    m_browser->setReadOnly(true);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->setUndoRedoEnabled(false);
    // Links never navigate the pane away from the preview: web and mail links
    // go to the desktop, anything else (relative .md links, anchors) is ignored.
    m_browser->setOpenLinks(false);
    connect(m_browser, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        const QString scheme = url.scheme().toLower();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https")
            || scheme == QLatin1String("mailto")) {
            QDesktopServices::openUrl(url);
        }
    });
    m_browser->document()->setDocumentMargin(kPreviewMargin);
    layout->addWidget(m_browser);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &PreviewPane::renderNow);

    // The rendered height is only known after layout; keep the scroll position
    // as the preview's range changes (re-render, resize).
    connect(m_browser->verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this](int, int) { applyScrollFraction(); });
}

void PreviewPane::attachEditor(QTextEdit *editor)
{
    if (m_editor == editor) {
        return;
    }
    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
        disconnect(m_editor->verticalScrollBar(), nullptr, this, nullptr);
    }
    m_editor = editor;
    if (m_editor) {
        connect(m_editor, &QTextEdit::textChanged, this, &PreviewPane::scheduleRender);
        QScrollBar *vs = m_editor->verticalScrollBar();
        connect(vs, &QScrollBar::valueChanged, this, [this](int) { syncFromScrollBar(); });
        connect(vs, &QScrollBar::rangeChanged, this, [this](int, int) { syncFromScrollBar(); });
    }
    scheduleRender();
}

void PreviewPane::setBaseDirectory(const QString &dir)
{
    const QStringList paths = dir.isEmpty() ? QStringList() : QStringList{dir};
    if (m_browser->searchPaths() == paths) {
        return;
    }
    m_browser->setSearchPaths(paths);
    scheduleRender();
}

void PreviewPane::applyTheme(const ThemeColors &c, const QFont &bodyFont)
{
    m_colors = c;
    m_haveColors = true;
    const int pt = bodyFont.pointSize() > 0 ? bodyFont.pointSize() : 13;
    m_browser->setStyleSheet(
        QStringLiteral("QTextBrowser#markdownPreview {"
                       " background-color: %1; color: %2;"
                       " border: none;"
                       " selection-background-color: %3; selection-color: %4;"
                       " font-family: %5; font-size: %6pt; padding: 0px; }")
            .arg(c.pageBg.name(), c.pageFg.name(), c.selBg.name(), c.selFg.name(),
                 cssFamilies(bodyFont), QString::number(pt)));
    QPalette pal = m_browser->palette();
    pal.setColor(QPalette::Link, c.link);
    pal.setColor(QPalette::LinkVisited, c.link);
    m_browser->setPalette(pal);
    // Rendered spans carry explicit colours: render again in the new ones.
    if (isVisible()) {
        renderNow();
    } else {
        m_stale = true;
    }
}

void PreviewPane::scheduleRender()
{
    m_stale = true;
    if (!isVisible()) {
        m_debounce->stop(); // caught up in showEvent()
        return;
    }
    m_debounce->start(); // restarts: renders kDebounceMs after the LAST edit
}

void PreviewPane::renderNow()
{
    m_debounce->stop();
    m_stale = false;
    const QString text = m_editor ? m_editor->document()->toPlainText() : QString();
    QTextDocument *doc = m_browser->document();
    doc->setMarkdown(text, QTextDocument::MarkdownDialectGitHub);
    doc->setDocumentMargin(kPreviewMargin);
    colourRenderedSpans();
    ++m_renderCount;
    applyScrollFraction();
    emit rendered();
}

bool PreviewPane::isRenderPending() const
{
    return m_debounce->isActive();
}

int PreviewPane::debounceInterval() const
{
    return m_debounce->interval();
}

void PreviewPane::setDebounceInterval(int ms)
{
    m_debounce->setInterval(ms);
}

void PreviewPane::setScrollFraction(qreal fraction)
{
    m_fraction = qBound<qreal>(0.0, fraction, 1.0);
    applyScrollFraction();
}

QSize PreviewPane::sizeHint() const
{
    return {420, 600}; // first shown at about 40 % of a 1024 px window
}

QTextDocument *PreviewPane::renderedDocument() const
{
    return m_browser->document();
}

QString PreviewPane::settingsKey()
{
    return QStringLiteral("markdown/previewVisible");
}

bool PreviewPane::loadVisibleSetting()
{
    return QSettings().value(settingsKey(), false).toBool();
}

void PreviewPane::saveVisibleSetting(bool visible)
{
    QSettings().setValue(settingsKey(), visible);
}

void PreviewPane::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_stale) {
        renderNow();
    }
    syncFromScrollBar();
}

void PreviewPane::syncFromScrollBar()
{
    if (!m_editor) {
        return;
    }
    const QScrollBar *vs = m_editor->verticalScrollBar();
    const int range = vs->maximum() - vs->minimum();
    setScrollFraction(range > 0 ? qreal(vs->value() - vs->minimum()) / range : 0.0);
}

void PreviewPane::applyScrollFraction()
{
    QScrollBar *vb = m_browser->verticalScrollBar();
    const int range = vb->maximum() - vb->minimum();
    vb->setValue(vb->minimum() + qRound(m_fraction * range));
}

void PreviewPane::colourRenderedSpans()
{
    if (!m_haveColors) {
        return;
    }
    // QTextMarkdownImporter gives code, links and quotes structure but no
    // colour; give them the theme's Markdown inks (as the editor's inline
    // styling does). Collected first: merging formats splits fragments.
    struct Span { int start; int end; QTextCharFormat fmt; };
    struct CodeBlock { int position; };
    std::vector<Span> spans;
    std::vector<CodeBlock> codeBlocks;
    QTextDocument *doc = m_browser->document();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat bf = block.blockFormat();
        const bool codeBlock = bf.hasProperty(QTextFormat::BlockCodeFence)
            || bf.hasProperty(QTextFormat::BlockCodeLanguage) || bf.nonBreakableLines();
        const bool quote = bf.property(QTextFormat::BlockQuoteLevel).toInt() > 0;
        if (codeBlock) {
            codeBlocks.push_back({block.position()});
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) {
                continue;
            }
            const QTextCharFormat cf = frag.charFormat();
            QTextCharFormat add;
            if (cf.isAnchor()) {
                add.setForeground(m_colors.link);
                add.setFontUnderline(true);
            } else if (codeBlock || cf.fontFixedPitch() || cf.font().fixedPitch()) {
                add.setForeground(m_colors.code);
            } else if (quote) {
                add.setForeground(m_colors.quote);
            } else {
                continue;
            }
            spans.push_back({frag.position(), frag.position() + frag.length(), add});
        }
    }
    QTextCursor cur(doc);
    for (const Span &s : spans) {
        cur.setPosition(s.start);
        cur.setPosition(s.end, QTextCursor::KeepAnchor);
        cur.mergeCharFormat(s.fmt);
    }
    QTextBlockFormat codeBg;
    codeBg.setBackground(m_colors.hover);
    for (const CodeBlock &b : codeBlocks) {
        cur.setPosition(b.position);
        cur.mergeBlockFormat(codeBg);
    }
}
