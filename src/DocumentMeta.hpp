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

    // Style fonts (1.15.0): StyleFonts::serialize() JSON, stored in meta.xml as
    // meta:user-defined "zwriter:style-fonts". Empty = not stored (pre-1.15 file).
    QString styleFonts;

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
// Sheet size (oriented, mm) and orientation stored in the package's
// styles.xml page layout (1.12.0 reads it back; zwriter has always written it).
// False when the file has no usable page size (then the caller uses A4).
bool readPageSizeFromOdt(const QString &odtPath, QSizeF *sheetMm, bool *landscape = nullptr);
bool parsePageSizeXml(const QByteArray &stylesXml, QSizeF *sheetMm, bool *landscape = nullptr);
// "21cm", "8.5in", "215.9mm", "612pt" -> mm; < 0 when unparsable.
qreal odfLengthToMm(const QString &length);
// Pieces of the package, exposed for the regression tests.
QString buildStylesXml(const DocumentMeta &meta, const OdtPageStyle &page);
QString manifestWithEntries(const QString &manifest, const QStringList &paths);
}
