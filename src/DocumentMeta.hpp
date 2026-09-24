#pragma once

#include <QDateTime>
#include <QString>

// Lean document metadata (not a full Word properties dump).
struct DocumentMeta {
    QString author;
    QDateTime created;
    QDateTime lastEdited;

    static QString defaultAuthor();
    void ensureDefaults();
    void touchEdited();
};

// Best-effort ODT meta.xml round-trip via unzip/zip. QTextDocumentWriter
// does not expose all Dublin Core fields, so we patch the package after write.
namespace OdtMeta {
bool readFromOdt(const QString &odtPath, DocumentMeta *meta, QString *error = nullptr);
bool writeToOdt(const QString &odtPath, const DocumentMeta &meta, QString *error = nullptr);
}
