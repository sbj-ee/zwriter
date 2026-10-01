#pragma once

#include "Theme.hpp"

#include <QFont>
#include <QString>
#include <QWidget>

class QScrollBar;
class QTextBrowser;
class QTextDocument;
class QTextEdit;
class QTimer;

// Markdown mode: a read-only rendering of the document beside the editor.
// The text is rendered with QTextDocument::setMarkdown (GitHub dialect) about
// 300 ms after the last edit, never on every keystroke, and only while the pane
// is visible -- a hidden pane just remembers that it is out of date and catches
// up when it is shown again. The preview follows the editor's scroll position
// proportionally (a rough sync: the same fraction of the way down).
class PreviewPane : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kDebounceMs = 300;

    explicit PreviewPane(QWidget *parent = nullptr);

    QSize sizeHint() const override;

    // Render this editor's text and follow its vertical scroll bar.
    void attachEditor(QTextEdit *editor);

    // Where relative image links resolve (the document's folder); empty = none.
    void setBaseDirectory(const QString &dir);

    // Colours and reading face; re-renders so the rendered spans recolour too.
    void applyTheme(const ThemeColors &colors, const QFont &bodyFont);

    // Debounced: (re)starts the timer if the pane is visible, otherwise marks
    // the preview stale for the next show.
    void scheduleRender();
    // Render right away (cancels a pending debounce).
    void renderNow();
    bool isRenderPending() const;
    int debounceInterval() const;
    void setDebounceInterval(int ms); // tests only; the app keeps kDebounceMs

    // 0.0 = top, 1.0 = bottom; kept across re-renders.
    void setScrollFraction(qreal fraction);
    qreal scrollFraction() const { return m_fraction; }

    QTextBrowser *browser() const { return m_browser; }
    QTextDocument *renderedDocument() const;
    int renderCount() const { return m_renderCount; }

    // View > Markdown Preview: the user's choice, remembered in QSettings under
    // settingsKey() (default off). Whether the pane is on screen also depends
    // on the document being Markdown.
    static QString settingsKey();
    static bool loadVisibleSetting();
    static void saveVisibleSetting(bool visible);

signals:
    void rendered();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void syncFromScrollBar();
    void colourRenderedSpans();
    void applyScrollFraction();

    QTextBrowser *m_browser = nullptr;
    QTimer *m_debounce = nullptr;
    QTextEdit *m_editor = nullptr;
    ThemeColors m_colors;
    bool m_haveColors = false;
    bool m_stale = true;
    qreal m_fraction = 0.0;
    int m_renderCount = 0;
};
