#pragma once
// Insert > Symbol… (1.13.0): a grid of Greek letters and EE symbols with a
// "Recently used" row. Clicking a symbol emits symbolChosen(); the dialog
// stays open so several can be inserted in a row.
#include <QDialog>

class QGridLayout;
class QGroupBox;
class QToolButton;

class SymbolDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SymbolDialog(QWidget *parent = nullptr);

    // Rebuilds the Recently used row from QSettings.
    void refreshRecent();
    // For tests: the button showing `text` in a section ("recent", "greek", "ee").
    QToolButton *buttonFor(const QString &section, const QString &text) const;

signals:
    void symbolChosen(const QString &text);

private:
    QToolButton *makeButton(const QString &text, const QString &tip, const QString &section);
    void choose(const QString &text);

    QGroupBox *m_recentBox = nullptr;
    QGridLayout *m_recentGrid = nullptr;
};
