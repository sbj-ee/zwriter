#pragma once

#include <QDateTime>
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
namespace OdtMeta {
bool readFromOdt(const QString &odtPath, DocumentMeta *meta, QString *error = nullptr);
bool writeToOdt(const QString &odtPath, const DocumentMeta &meta, QString *error = nullptr);
}
