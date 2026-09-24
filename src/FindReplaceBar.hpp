#pragma once

#include <QWidget>

class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;
class QKeyEvent;
class QShowEvent;

// Keyboard-first find / replace bar (Ctrl+F / Ctrl+H). Esc hides via parent.
class FindReplaceBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindReplaceBar(QWidget *parent = nullptr);

    void showFind();
    void showReplace();
    void setFindText(const QString &text);
    QString findText() const;
    QString replaceText() const;
    bool caseSensitive() const;

signals:
    void findNext();
    void findPrev();
    void replaceOne();
    void replaceAll();
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void setReplaceVisible(bool visible);

    QLineEdit *m_findEdit = nullptr;
    QLineEdit *m_replaceEdit = nullptr;
    QCheckBox *m_caseBox = nullptr;
    QWidget *m_replaceRow = nullptr;
    QPushButton *m_findNextBtn = nullptr;
    QPushButton *m_findPrevBtn = nullptr;
    QPushButton *m_replaceBtn = nullptr;
    QPushButton *m_replaceAllBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};
