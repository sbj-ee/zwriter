#pragma once

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;

// File > New from Outline…: type an outline, pick ODT or Markdown, get a new
// document with those headings (OutlineDraft.hpp says how lines become levels).
class OutlineDraftDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OutlineDraftDialog(QWidget *parent = nullptr);

    QString outlineText() const;
    bool markdown() const;
    void setOutlineText(const QString &text); // tests
    void setMarkdown(bool on);                // tests
    QLabel *statusLabel() const { return m_status; }
    QPushButton *createButton() const { return m_create; }

    static QString settingsKey(); // "outline/newFormatMarkdown"

private:
    void refresh();

    QPlainTextEdit *m_text = nullptr;
    QRadioButton *m_odt = nullptr;
    QRadioButton *m_md = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_create = nullptr;
};
