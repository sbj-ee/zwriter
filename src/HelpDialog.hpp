#pragma once

#include "Theme.hpp"

#include <QDialog>
#include <QFont>
#include <QUrl>

#include <functional>

class QTextBrowser;

// Help → zwriter Help (F1): a single scrollable guide, loaded from the
// embedded Markdown resource :/ui/zwriter-help.md and rendered with the same
// QTextBrowser Markdown styling as the preview pane and the update dialog.
// External links open only if they are https; in-page #anchors scroll the
// guide. The window is modeless: opening Help again raises the existing one.
class HelpDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HelpDialog(QWidget *parent = nullptr);

    QTextBrowser *browser() const { return m_browser; }
    QString sourceMarkdown() const { return m_source; }

    // Colours and body face (PreviewPane-style). Does not reload the guide,
    // so the scroll position is kept when the theme changes.
    void applyTheme(const ThemeColors &colors, const QFont &bodyFont = QFont());

    // Tests can replace the desktop-open call so https links are not launched.
    void setOpenUrlHandler(const std::function<void(const QUrl &)> &handler) { m_openUrl = handler; }
    QUrl lastOpenedUrl() const { return m_lastOpenedUrl; }

    // Shared by the browser's anchorClicked signal and the tests.
    // Returns true if the URL was an in-page jump or an https link.
    bool handleAnchor(const QUrl &url);

    static QString resourcePath();
    static QString loadGuideMarkdown();

private:
    void loadGuide();
    void assignHeadingAnchors();
    void colourRenderedSpans();

    QTextBrowser *m_browser = nullptr;
    QString m_source;
    ThemeColors m_colors;
    bool m_haveColors = false;
    QUrl m_lastOpenedUrl;
    std::function<void(const QUrl &)> m_openUrl;
};
