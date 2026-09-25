#pragma once

#include <QDateTime>
#include <QMarginsF>
#include <QSizeF>
#include <QString>

// Lean document metadata (not a full Word properties dump).
struct DocumentMeta {
    QString author;
    QDateTime created;
    QDateTime lastEdited;

    // Header / footer bands (plain text). Tokens: {page}, {pages}.
    QString headerLeft;
    QString headerCenter;
    QString headerRight;
    QString footerLeft;
    QString footerCenter; // empty by default; Format -> Page Numbers sets "{page}"
    QString footerRight;

    static QString defaultAuthor();
    void ensureDefaults();
    void touchEdited();

    bool hasHeaderFooter() const;

    // False until defaults or ODT load seeds header/footer fields.
    bool headerFooterSeeded = false;
};

// Best-effort ODT meta.xml round-trip via unzip/zip. QTextDocumentWriter
// does not expose all Dublin Core fields, so we patch the package after write.
//
// writeToOdt() also writes styles.xml (page size, margins and the header /
// footer as an ODF master page, so LibreOffice & co. show them) and lists
// meta.xml and styles.xml in META-INF/manifest.xml; strict ODF readers such
// as LibreOffice refuse a package whose members are not in the manifest.
struct OdtPageStyle {
    QSizeF sheetMm{210.0, 297.0}; // oriented sheet size
    QMarginsF marginsMm{25.4, 25.4, 25.4, 25.4};
    bool landscape = false;
    QString fontFamily = QStringLiteral("Courier New"); // header/footer font
    qreal fontPointSize = 10.0;
};

namespace OdtMeta {
bool readFromOdt(const QString &odtPath, DocumentMeta *meta, QString *error = nullptr);
bool writeToOdt(const QString &odtPath, const DocumentMeta &meta, QString *error = nullptr,
                const OdtPageStyle &page = OdtPageStyle());
// Pieces of the package, exposed for the regression tests.
QString buildStylesXml(const DocumentMeta &meta, const OdtPageStyle &page);
QString manifestWithEntries(const QString &manifest, const QStringList &paths);
}
