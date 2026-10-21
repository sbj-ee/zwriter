#include "BookChapterDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QVBoxLayout>

BookChapterDialog::BookChapterDialog(const BookChapter &chapter, int currentWords, QWidget *parent)
    : QDialog(parent)
    , m_original(chapter)
    , m_words(currentWords)
{
    setObjectName(QStringLiteral("bookChapterDialog"));
    setWindowTitle(QStringLiteral("Chapter Details"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto *file = new QLabel(chapter.path, this);
    file->setObjectName(QStringLiteral("bookChapterFile"));
    file->setTextFormat(Qt::PlainText);
    file->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("File:"), file);

    m_title = new QLineEdit(chapter.title, this);
    m_title->setPlaceholderText(QFileInfo(chapter.path).completeBaseName());
    form->addRow(QStringLiteral("&Title:"), m_title);

    m_status = new QComboBox(this);
    m_status->addItems(BookManifest::statuses());
    if (!chapter.status.isEmpty() && !BookManifest::isKnownStatus(chapter.status)) {
        // Keep a hand-edited status selectable so OK doesn't silently change it.
        m_status->addItem(chapter.status);
        m_status->setToolTip(QStringLiteral("\"%1\" is not one of zwriter's statuses; it is kept as written.")
                                 .arg(chapter.status));
    }
    m_status->setCurrentText(chapter.status.isEmpty() ? QStringLiteral("Draft") : chapter.status);
    form->addRow(QStringLiteral("&Status:"), m_status);

    m_pov = new QLineEdit(chapter.pov, this);
    m_pov->setPlaceholderText(QStringLiteral("Point-of-view character"));
    form->addRow(QStringLiteral("&POV:"), m_pov);

    m_tags = new QLineEdit(chapter.tags.join(QStringLiteral(", ")), this);
    m_tags->setPlaceholderText(QStringLiteral("Comma-separated"));
    form->addRow(QStringLiteral("T&ags:"), m_tags);

    m_target = new QSpinBox(this);
    m_target->setRange(0, 1000000);
    m_target->setSingleStep(500);
    m_target->setSpecialValueText(QStringLiteral("None"));
    m_target->setSuffix(QStringLiteral(" words"));
    m_target->setValue(chapter.wordTarget);
    form->addRow(QStringLiteral("Word ta&rget:"), m_target);

    m_progress = new QLabel(this);
    m_progress->setObjectName(QStringLiteral("bookChapterProgress"));
    form->addRow(QStringLiteral("Current:"), m_progress);

    m_synopsis = new QPlainTextEdit(chapter.synopsis, this);
    m_synopsis->setPlaceholderText(QStringLiteral("What happens in this chapter"));
    m_synopsis->setTabChangesFocus(true);
    m_synopsis->setFixedHeight(80);
    form->addRow(QStringLiteral("S&ynopsis:"), m_synopsis);

    m_notes = new QPlainTextEdit(chapter.notes, this);
    m_notes->setPlaceholderText(QStringLiteral("Anything to remember while revising"));
    m_notes->setTabChangesFocus(true);
    m_notes->setFixedHeight(80);
    form->addRow(QStringLiteral("&Notes:"), m_notes);

    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_target, &QSpinBox::valueChanged, this, &BookChapterDialog::updateProgress);
    updateProgress();
    // Typing goes to the title first, and Tab walks the fields in the order shown.
    setTabOrder(m_title, m_status);
    setTabOrder(m_status, m_pov);
    setTabOrder(m_pov, m_tags);
    setTabOrder(m_tags, m_target);
    setTabOrder(m_target, m_synopsis);
    setTabOrder(m_synopsis, m_notes);
    m_title->setFocus(Qt::OtherFocusReason);
    resize(460, sizeHint().height());
}

QString BookChapterDialog::progressText(int words, int target)
{
    if (words < 0) {
        return QStringLiteral("File missing");
    }
    const QLocale loc;
    const QString w = QStringLiteral("%1 %2").arg(loc.toString(words),
                                                  words == 1 ? QStringLiteral("word")
                                                             : QStringLiteral("words"));
    if (target <= 0) {
        return w;
    }
    const int pct = int(qint64(words) * 100 / target);
    return QStringLiteral("%1 of %2 (%3%)").arg(w, loc.toString(target)).arg(pct);
}

void BookChapterDialog::updateProgress()
{
    m_progress->setText(progressText(m_words, m_target->value()));
}

BookChapter BookChapterDialog::chapter() const
{
    BookChapter c = m_original;
    c.title = m_title->text().trimmed();
    c.status = m_status->currentText();
    c.pov = m_pov->text().trimmed();
    c.tags.clear();
    for (const QString &t : m_tags->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString s = t.trimmed();
        if (!s.isEmpty() && !c.tags.contains(s)) {
            c.tags << s;
        }
    }
    c.wordTarget = m_target->value();
    c.synopsis = m_synopsis->toPlainText().trimmed();
    c.notes = m_notes->toPlainText().trimmed();
    return c;
}
