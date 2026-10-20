#pragma once

#include "BookManifest.hpp"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

// Chapter details (right-click a chapter in the Book sidebar → Edit Details…):
// title, status, POV, tags, word-count target (with the current count), and
// free-form synopsis and notes. Plain Qt form; nothing is written until OK.
class BookChapterDialog : public QDialog
{
    Q_OBJECT

public:
    // currentWords < 0 = file missing / unreadable.
    BookChapterDialog(const BookChapter &chapter, int currentWords, QWidget *parent = nullptr);

    BookChapter chapter() const;

    QLineEdit *titleEdit() const { return m_title; }
    QComboBox *statusBox() const { return m_status; }
    QLineEdit *povEdit() const { return m_pov; }
    QLineEdit *tagsEdit() const { return m_tags; }
    QSpinBox *targetBox() const { return m_target; }
    QPlainTextEdit *synopsisEdit() const { return m_synopsis; }
    QPlainTextEdit *notesEdit() const { return m_notes; }
    QLabel *progressLabel() const { return m_progress; }

    static QString progressText(int words, int target);

private:
    void updateProgress();

    BookChapter m_original;
    int m_words = -1;
    QLineEdit *m_title = nullptr;
    QComboBox *m_status = nullptr;
    QLineEdit *m_pov = nullptr;
    QLineEdit *m_tags = nullptr;
    QSpinBox *m_target = nullptr;
    QLabel *m_progress = nullptr;
    QPlainTextEdit *m_synopsis = nullptr;
    QPlainTextEdit *m_notes = nullptr;
};
