#pragma once

#include <QMainWindow>

class QTextEdit;
class QLabel;
class TypewriterSounds;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void updateStats();
    void toggleChrome();
    void toggleFullscreen();
    void toggleKeySounds();

private:
    void applyDarkTheme();
    void setChromeVisible(bool visible);
    void loadWindowIcon();
    void updateKeySoundsLabel();

    QTextEdit *m_editor = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_keysLabel = nullptr;
    TypewriterSounds *m_keySounds = nullptr;
    bool m_chromeVisible = false;
};
