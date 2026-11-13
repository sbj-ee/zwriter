#include "HelpDialog.hpp"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QPalette>
#include <QFont>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextDocument>
#include <QVBoxLayout>

#include <vector>

namespace {
constexpr qreal kHelpMargin = 28.0;

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
    quoted << QStringLiteral("sans-serif");
    return quoted.join(QStringLiteral(", "));
}

// "Documents & Modes" -> "documents-modes". Used for in-page links.
QString slugify(const QString &text)
{
    QString s;
    for (const QChar ch : text.trimmed().toLower()) {
        if (ch.isLetterOrNumber()) {
            s += ch;
        } else if (!s.isEmpty() && !s.endsWith(QLatin1Char('-'))) {
            s += QLatin1Char('-');
        }
    }
    while (s.endsWith(QLatin1Char('-'))) {
        s.chop(1);
    }
    return s;
}
} // namespace

HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("helpDialog"));
    setWindowTitle(QStringLiteral("zwriter Help"));
    setModal(false);

    auto *layout = new QVBoxLayout(this);

    m_browser = new QTextBrowser(this);
    m_browser->setObjectName(QStringLiteral("helpBrowser"));
    m_browser->setReadOnly(true);
    m_browser->setOpenLinks(false);
    m_browser->setOpenExternalLinks(false);
    m_browser->setUndoRedoEnabled(false);
    m_browser->document()->setDocumentMargin(kHelpMargin);
    connect(m_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) { handleAnchor(url); });
    layout->addWidget(m_browser, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("helpButtons"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    if (QPushButton *close = buttons->button(QDialogButtonBox::Close)) {
        close->setDefault(true);
    }
    layout->addWidget(buttons);

    loadGuide();
    resize(720, 640);
}

QString HelpDialog::resourcePath()
{
    return QStringLiteral(":/ui/zwriter-help.md");
}

QString HelpDialog::loadGuideMarkdown()
{
    QFile f(resourcePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(f.readAll());
}

void HelpDialog::loadGuide()
{
    m_source = loadGuideMarkdown();
    const QString text = m_source.isEmpty() ? QStringLiteral("*The help guide could not be loaded.*") : m_source;
    m_browser->document()->setMarkdown(text, QTextDocument::MarkdownDialectGitHub);
    m_browser->document()->setDocumentMargin(kHelpMargin);
    assignHeadingAnchors();
    colourRenderedSpans();
}

void HelpDialog::assignHeadingAnchors()
{
    // Give each heading a stable slug so Contents links and tests can jump
    // with #documents-modes, even if Qt's importer does not emit those names.
    QTextDocument *doc = m_browser->document();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() <= 0) {
            continue;
        }
        const QString name = slugify(block.text());
        if (name.isEmpty()) {
            continue;
        }
        const int len = block.length() - 1; // exclude the block separator
        if (len <= 0) {
            continue;
        }
        QTextCursor c(doc);
        c.setPosition(block.position());
        c.setPosition(block.position() + len, QTextCursor::KeepAnchor);
        QTextCharFormat fmt;
        fmt.setAnchor(true);
        fmt.setAnchorNames({name});
        c.mergeCharFormat(fmt);
    }
}

void HelpDialog::applyTheme(const ThemeColors &colors, const QFont &bodyFont)
{
    m_colors = colors;
    m_haveColors = true;
    QFont font = bodyFont;
    if (font.family().isEmpty() && font.families().isEmpty()) {
        font = QFont();
    }
    const int pt = font.pointSize() > 0 ? font.pointSize() : 12;
    m_browser->setStyleSheet(
        QStringLiteral("QTextBrowser#helpBrowser {"
                       " background-color: %1; color: %2;"
                       " border: none;"
                       " selection-background-color: %3; selection-color: %4;"
                       " font-family: %5; font-size: %6pt; padding: 0px; }")
            .arg(colors.pageBg.name(), colors.pageFg.name(), colors.selBg.name(), colors.selFg.name(),
                 cssFamilies(font), QString::number(pt)));
    QPalette pal = m_browser->palette();
    pal.setColor(QPalette::Link, colors.link);
    pal.setColor(QPalette::LinkVisited, colors.link);
    pal.setColor(QPalette::Base, colors.pageBg);
    pal.setColor(QPalette::Text, colors.pageFg);
    pal.setColor(QPalette::Window, colors.windowBg);
    pal.setColor(QPalette::WindowText, colors.fg);
    m_browser->setPalette(pal);
    colourRenderedSpans();
}

bool HelpDialog::handleAnchor(const QUrl &url)
{
    m_lastOpenedUrl = QUrl();
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https")) {
        m_lastOpenedUrl = url;
        if (m_openUrl) {
            m_openUrl(url);
        } else {
            QDesktopServices::openUrl(url);
        }
        return true;
    }
    if (url.hasFragment() && (scheme.isEmpty() || scheme == QLatin1String("qrc"))) {
        m_browser->scrollToAnchor(url.fragment());
        return true;
    }
    return false;
}

void HelpDialog::colourRenderedSpans()
{
    if (!m_haveColors) {
        return;
    }
    // Same approach as PreviewPane: Qt's Markdown importer gives code, links
    // and quotes structure but no colour; apply the theme's inks afterwards.
    struct Span {
        int start;
        int end;
        QTextCharFormat fmt;
    };
    struct CodeBlock {
        int position;
    };
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
