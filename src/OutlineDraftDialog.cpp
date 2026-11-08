#include "OutlineDraftDialog.hpp"

#include "OutlineDraft.hpp"
#include "OutlineEdit.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>

OutlineDraftDialog::OutlineDraftDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("outlineDraftDialog"));
    setWindowTitle(QStringLiteral("New from Outline"));
    resize(520, 460);

    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(QStringLiteral(
        "One heading per line. Indent a line (or start it with ##) to make it a subheading; "
        "bullets and numbers in front are dropped."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_text = new QPlainTextEdit(this);
    m_text->setObjectName(QStringLiteral("outlineDraftText"));
    m_text->setTabStopDistance(32);
    m_text->setPlaceholderText(QStringLiteral("Introduction\nBackground\n  Earlier work\n  Gaps\nMethod\nResults\nDiscussion"));
    layout->addWidget(m_text, 1);

    m_odt = new QRadioButton(QStringLiteral("Document (ODT): Heading 1–3"), this);
    m_odt->setObjectName(QStringLiteral("outlineDraftOdt"));
    m_md = new QRadioButton(QStringLiteral("Markdown: # to ######"), this);
    m_md->setObjectName(QStringLiteral("outlineDraftMarkdown"));
    layout->addWidget(m_odt);
    layout->addWidget(m_md);
    (QSettings().value(settingsKey(), false).toBool() ? m_md : m_odt)->setChecked(true);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("outlineDraftStatus"));
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_create = buttons->addButton(QStringLiteral("Create"), QDialogButtonBox::AcceptRole);
    m_create->setObjectName(QStringLiteral("outlineDraftCreate"));
    m_create->setDefault(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::accepted, this, [this]() { QSettings().setValue(settingsKey(), markdown()); });

    connect(m_text, &QPlainTextEdit::textChanged, this, &OutlineDraftDialog::refresh);
    connect(m_md, &QRadioButton::toggled, this, &OutlineDraftDialog::refresh);
    refresh();
}

QString OutlineDraftDialog::settingsKey()
{
    return QStringLiteral("outline/newFormatMarkdown");
}

QString OutlineDraftDialog::outlineText() const
{
    return m_text->toPlainText();
}

bool OutlineDraftDialog::markdown() const
{
    return m_md->isChecked();
}

void OutlineDraftDialog::setOutlineText(const QString &text)
{
    m_text->setPlainText(text);
}

void OutlineDraftDialog::setMarkdown(bool on)
{
    (on ? m_md : m_odt)->setChecked(true);
}

void OutlineDraftDialog::refresh()
{
    const int maxLevel = markdown() ? OutlineEdit::kMaxMarkdownLevel : OutlineEdit::kMaxRichLevel;
    const OutlineDraft::Parsed p = OutlineDraft::parse(m_text->toPlainText(), maxLevel);
    QString text = p.entries.isEmpty()
                       ? QStringLiteral("Type at least one heading.")
                       : (p.entries.size() == 1 ? QStringLiteral("1 heading") : QStringLiteral("%1 headings").arg(p.entries.size()));
    if (p.clamped > 0) {
        text += QStringLiteral(" · %1 moved up a level to fit").arg(p.clamped);
    }
    m_status->setText(text);
    m_create->setEnabled(!p.entries.isEmpty());
}
