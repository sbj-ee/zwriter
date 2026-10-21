#pragma once

#include "BookExport.hpp"

#include <QDialog>

class QCheckBox;

// Options for Export Book (PDF / EPUB) and Print Book: table of contents,
// title page, figure numbering.
class BookExportDialog : public QDialog
{
    Q_OBJECT
public:
    BookExportDialog(const QString &title, const QString &okText, const BookExport::Options &options,
                     QWidget *parent = nullptr);
    BookExport::Options options() const;

private:
    QCheckBox *m_toc = nullptr;
    QCheckBox *m_titlePage = nullptr;
    QCheckBox *m_figures = nullptr;
};
