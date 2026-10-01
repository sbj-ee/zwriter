#include "LibrarySidebar.hpp"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSettings>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

LibrarySidebar::LibrarySidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("librarySidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("libraryHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 6, 8);
    headerLayout->setSpacing(4);
    m_title = new QLabel(header);
    m_title->setObjectName(QStringLiteral("libraryTitle"));
    m_title->setTextFormat(Qt::PlainText);
    headerLayout->addWidget(m_title, 1);
    m_chooseButton = new QToolButton(header);
    m_chooseButton->setObjectName(QStringLiteral("libraryChoose"));
    m_chooseButton->setText(QStringLiteral("…"));
    m_chooseButton->setToolTip(QStringLiteral("Choose the library folder"));
    m_chooseButton->setAutoRaise(true);
    connect(m_chooseButton, &QToolButton::clicked, this, &LibrarySidebar::chooseRootFolder);
    headerLayout->addWidget(m_chooseButton);
    layout->addWidget(header);

    m_hint = new QLabel(QStringLiteral("Choose a folder of .md and .txt files (…)"), this);
    m_hint->setObjectName(QStringLiteral("libraryHint"));
    m_hint->setWordWrap(true);
    m_hint->setContentsMargins(12, 4, 12, 4);
    layout->addWidget(m_hint);

    m_model = new QFileSystemModel(this);
    // Folders always; files only when they match nameFilters(). No hidden
    // entries, no "." / "..". (Without QDir::CaseSensitive the match ignores
    // case, so NOTES.MD is listed too.)
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_model->setNameFilters(nameFilters());
    m_model->setNameFilterDisables(false); // hide, rather than grey out, the rest
    m_model->setReadOnly(true);

    m_view = new QTreeView(this);
    m_view->setObjectName(QStringLiteral("libraryTree"));
    m_view->setModel(m_model);
    m_view->setHeaderHidden(true);
    for (int col = 1; col < m_model->columnCount(); ++col) {
        m_view->hideColumn(col); // size / type / date
    }
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setUniformRowHeights(true);
    m_view->setAnimated(false);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setDragEnabled(false);
    m_view->setFocusPolicy(Qt::ClickFocus);
    connect(m_view, &QTreeView::clicked, this, &LibrarySidebar::onClicked);
    connect(m_view, &QTreeView::activated, this, &LibrarySidebar::onClicked); // Enter
    layout->addWidget(m_view, 1);

    setRootPath(QString());
}

QSize LibrarySidebar::sizeHint() const
{
    return {240, 600};
}

QStringList LibrarySidebar::nameFilters()
{
    return {QStringLiteral("*.md"), QStringLiteral("*.txt")};
}

bool LibrarySidebar::acceptsFile(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return !QFileInfo(fileName).fileName().startsWith(QLatin1Char('.'))
        && (suffix == QLatin1String("md") || suffix == QLatin1String("txt"));
}

void LibrarySidebar::setRootPath(const QString &path)
{
    const QFileInfo info(path);
    const bool usable = !path.isEmpty() && info.isDir();
    m_rootPath = usable ? QDir::cleanPath(info.absoluteFilePath()) : QString();
    if (usable) {
        m_view->setRootIndex(m_model->setRootPath(m_rootPath));
    } else {
        // The tree is hidden; nothing under it is watched or listed.
        m_view->setRootIndex(m_model->setRootPath(QString()));
    }
    m_view->setVisible(usable);
    m_hint->setVisible(!usable);
    updateHeader();
}

void LibrarySidebar::setCurrentFile(const QString &path)
{
    if (m_rootPath.isEmpty() || path.isEmpty()) {
        m_view->clearSelection();
        return;
    }
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (!clean.startsWith(m_rootPath + QLatin1Char('/'))) {
        m_view->clearSelection();
        return;
    }
    const QModelIndex idx = m_model->index(clean);
    if (idx.isValid()) {
        m_view->setCurrentIndex(idx);
        m_view->scrollTo(idx);
    }
}

void LibrarySidebar::applyTheme(const ThemeColors &c)
{
    setStyleSheet(QStringLiteral(
        "#librarySidebar { background-color: %1; }"
        "#libraryHeader { background-color: %1; }"
        "#libraryTitle { color: %2; font-size: 9pt; font-weight: 600; }"
        "#libraryHint { color: %3; font-size: 9pt; }"
        "#libraryChoose { color: %2; border: none; border-radius: 4px; padding: 1px 6px; }"
        "#libraryChoose:hover { background-color: %4; }"
        "QTreeView#libraryTree { background-color: %1; color: %2; border: none;"
        " outline: 0; font-size: 10pt; selection-background-color: %5; selection-color: %6; }"
        "QTreeView#libraryTree::item { padding: 3px 2px; }"
        "QTreeView#libraryTree::item:hover { background-color: %4; }"
        "QTreeView#libraryTree::item:selected { background-color: %5; color: %6; }")
                      .arg(c.barBg.name(), c.fg.name(), c.muted.name(), c.hover.name(),
                           c.accentSoft.name(), c.accentFg.name()));
}

void LibrarySidebar::chooseRootFolder()
{
    const QString start = m_rootPath.isEmpty() ? QDir::homePath() : m_rootPath;
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose Library Folder"),
                                                          start);
    if (dir.isEmpty()) {
        return;
    }
    setRootPath(dir);
    emit rootPathChosen(m_rootPath);
}

QString LibrarySidebar::rootPathKey()
{
    return QStringLiteral("library/rootPath");
}

QString LibrarySidebar::visibleKey()
{
    return QStringLiteral("library/visible");
}

LibrarySidebar::Settings LibrarySidebar::loadSettings()
{
    QSettings s;
    Settings out;
    out.rootPath = s.value(rootPathKey()).toString();
    out.visible = s.value(visibleKey(), false).toBool();
    return out;
}

void LibrarySidebar::saveSettings(const Settings &settings)
{
    QSettings s;
    s.setValue(rootPathKey(), settings.rootPath);
    s.setValue(visibleKey(), settings.visible);
}

void LibrarySidebar::onClicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    if (m_model->isDir(index)) {
        return; // folders expand with the arrow / double-click, as usual
    }
    const QString path = m_model->filePath(index);
    if (!acceptsFile(path)) {
        return;
    }
    // A double-click is a click and then an activation: open once.
    if (path == m_lastActivated && m_activateClock.isValid()
        && m_activateClock.elapsed() < QApplication::doubleClickInterval()) {
        return;
    }
    m_lastActivated = path;
    m_activateClock.start();
    emit fileActivated(path);
}

void LibrarySidebar::updateHeader()
{
    if (m_rootPath.isEmpty()) {
        m_title->setText(QStringLiteral("LIBRARY"));
        m_title->setToolTip(QString());
        return;
    }
    const QString name = QFileInfo(m_rootPath).fileName();
    m_title->setText((name.isEmpty() ? m_rootPath : name).toUpper());
    m_title->setToolTip(QDir::toNativeSeparators(m_rootPath));
}
