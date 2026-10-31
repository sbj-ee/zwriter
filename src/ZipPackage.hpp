#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// In-process zip packages (ODT, EPUB) via the vendored miniz: no `zip` /
// `unzip` child processes, so nothing blocks the GUI thread waiting on one.
namespace ZipPackage {

struct Member {
    QString name;
    QByteArray data;
};

// A whole package in memory, members in archive order.
class Package {
public:
    bool read(const QString &path, QString *error = nullptr);
    bool readData(const QByteArray &zip, QString *error = nullptr);

    bool contains(const QString &name) const;
    QByteArray member(const QString &name, bool *found = nullptr) const;
    // Replaces the member, or appends it.
    void setMember(const QString &name, const QByteArray &data);
    QStringList names() const;

    // Writes the package with "mimetype" (when present) first and stored,
    // as ODF and EPUB require; everything else deflated.
    bool write(const QString &path, QString *error = nullptr) const;
    QByteArray toData(bool *ok = nullptr, QString *error = nullptr) const;

private:
    QList<Member> m_members;
};

// One member of the zip at `path` (empty + *ok false when missing/unreadable).
QByteArray readMember(const QString &path, const QString &name, bool *ok = nullptr, QString *error = nullptr);
QStringList memberNames(const QString &path, bool *ok = nullptr);

// Zips every file under `dir` into `out`; `first` (e.g. "mimetype") goes
// first and stored, then the rest in sorted path order.
bool writeDirectory(const QString &dir, const QString &out, const QString &first, QString *error = nullptr);

namespace Testing {
// While set, every write fails as if the disk were full (tests only).
void setFailWrites(bool fail);
} // namespace Testing

} // namespace ZipPackage
