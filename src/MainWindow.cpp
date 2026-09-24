#include "MainWindow.hpp"
#include "TypewriterSounds.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QStatusBar>
#include <QTextEdit>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("zwriter"));
    resize(900, 700);
    loadWindowIcon();

    m_editor = new QTextEdit(this);
    m_editor->setAcceptRichText(true);
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setPlaceholderText(QStringLiteral("Start writing…"));
    setCentralWidget(m_editor);

    m_keySounds = new TypewriterSounds(this);

    m_statsLabel = new QLabel(this);
    m_keysLabel = new QLabel(this);
    m_keysLabel->setCursor(Qt::PointingHandCursor);
    m_keysLabel->setToolTip(QStringLiteral("Click or press Ctrl+Shift+K to toggle typewriter key sounds (default off)"));
    statusBar()->addWidget(m_keysLabel);
    statusBar()->addPermanentWidget(m_statsLabel);
    statusBar()->setSizeGripEnabled(false);

    connect(m_editor, &QTextEdit::textChanged, this, &MainWindow::updateStats);
    connect(m_editor, &QTextEdit::textChanged, this, [this]() {
        // Fire a soft click on edits when enabled; no-op if off / no sample.
        if (m_keySounds) {
            m_keySounds->playKey();
        }
    });
    // Click label to toggle.
    m_keysLabel->installEventFilter(this);

    applyDarkTheme();
    setChromeVisible(false);
    updateStats();
    updateKeySoundsLabel();
}

void MainWindow::loadWindowIcon()
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/icons/zwriter-128.png"),
        QStringLiteral("assets/icons/zwriter-128.png"),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            QStringLiteral("../assets/icons/zwriter-128.png")),
        QStringLiteral(":/icons/zwriter-128.png"),
    };
    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            setWindowIcon(QIcon(path));
            return;
        }
    }
}

void MainWindow::applyDarkTheme()
{
    const QString style = QStringLiteral(
        "QMainWindow { background-color: #1e1e1e; }"
        "QTextEdit {"
        "  background-color: #1e1e1e;"
        "  color: #d4d4d4;"
        "  selection-background-color: #264f78;"
        "  selection-color: #ffffff;"
        "  font-family: 'Georgia', 'Times New Roman', serif;"
        "  font-size: 16pt;"
        "  padding: 48px 20%;"
        "}"
        "QStatusBar {"
        "  background-color: #252526;"
        "  color: #a0a0a0;"
        "  border-top: 1px solid #3c3c3c;"
        "  font-family: 'Segoe UI', 'Helvetica Neue', sans-serif;"
        "  font-size: 11pt;"
        "}"
        "QStatusBar QLabel { color: #a0a0a0; padding: 0 8px; }"
    );
    setStyleSheet(style);
}

void MainWindow::setChromeVisible(bool visible)
{
    m_chromeVisible = visible;
    statusBar()->setVisible(visible);
}

void MainWindow::updateStats()
{
    const QString text = m_editor->toPlainText();
    const int chars = text.length();

    int words = 0;
    bool inWord = false;
    for (const QChar c : text) {
        if (c.isSpace()) {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            ++words;
        }
    }

    m_statsLabel->setText(
        QStringLiteral("%1 words  ·  %2 characters").arg(words).arg(chars));
}

void MainWindow::updateKeySoundsLabel()
{
    const bool on = m_keySounds && m_keySounds->isEnabled();
    m_keysLabel->setText(on ? QStringLiteral("Keys: on") : QStringLiteral("Keys: off"));
}

void MainWindow::toggleChrome()
{
    setChromeVisible(!m_chromeVisible);
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::toggleKeySounds()
{
    if (!m_keySounds) {
        return;
    }
    m_keySounds->setEnabled(!m_keySounds->isEnabled());
    updateKeySoundsLabel();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        toggleChrome();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F11) {
        toggleFullscreen();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_K
        && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        toggleKeySounds();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_keysLabel && event->type() == QEvent::MouseButtonRelease) {
        toggleKeySounds();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}
