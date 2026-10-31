#include "ZipPackage.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <atomic>

#include "miniz.h"

namespace ZipPackage {
namespace {

std::atomic_bool g_failWrites{false};

bool setError(QString *error, const QString &msg)
{
    if (error) {
        *error = msg;
    }
    return false;
}

bool readFile(const QString &path, QByteArray *out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return setError(error, QStringLiteral("Could not open %1.").arg(QFileInfo(path).fileName()));
    }
    *out = f.readAll();
    return true;
}

} // namespace

bool Package::readData(const QByteArray &zip, QString *error)
{
    m_members.clear();
    mz_zip_archive z{};
    if (!mz_zip_reader_init_mem(&z, zip.constData(), size_t(zip.size()), 0)) {
        return setError(error, QStringLiteral("Not a valid zip package."));
    }
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    bool ok = true;
    for (mz_uint i = 0; i < n && ok; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) {
            ok = false;
            break;
        }
        if (mz_zip_reader_is_file_a_directory(&z, i)) {
            continue;
        }
        size_t size = 0;
        void *p = mz_zip_reader_extract_to_heap(&z, i, &size, 0);
        if (!p) {
            ok = false;
            break;
        }
        m_members.append({QString::fromUtf8(st.m_filename), QByteArray(static_cast<const char *>(p), qsizetype(size))});
        mz_free(p);
    }
    mz_zip_reader_end(&z);
    if (!ok) {
        m_members.clear();
        return setError(error, QStringLiteral("The zip package is damaged."));
    }
    return true;
}

bool Package::read(const QString &path, QString *error)
{
    QByteArray data;
    return readFile(path, &data, error) && readData(data, error);
}

bool Package::contains(const QString &name) const
{
    for (const Member &m : m_members) {
        if (m.name == name) {
            return true;
        }
    }
    return false;
}

QByteArray Package::member(const QString &name, bool *found) const
{
    for (const Member &m : m_members) {
        if (m.name == name) {
            if (found) {
                *found = true;
            }
            return m.data;
        }
    }
    if (found) {
        *found = false;
    }
    return {};
}

void Package::setMember(const QString &name, const QByteArray &data)
{
    for (Member &m : m_members) {
        if (m.name == name) {
            m.data = data;
            return;
        }
    }
    m_members.append({name, data});
}

QStringList Package::names() const
{
    QStringList out;
    for (const Member &m : m_members) {
        out << m.name;
    }
    return out;
}

QByteArray Package::toData(bool *ok, QString *error) const
{
    if (ok) {
        *ok = false;
    }
    if (g_failWrites) {
        setError(error, QStringLiteral("Could not write the zip package (simulated failure)."));
        return {};
    }
    mz_zip_archive z{};
    if (!mz_zip_writer_init_heap(&z, 0, 64 * 1024)) {
        setError(error, QStringLiteral("Could not start the zip package."));
        return {};
    }
    QList<const Member *> order;
    for (const Member &m : m_members) {
        if (m.name == QLatin1String("mimetype")) {
            order.prepend(&m);
        } else {
            order.append(&m);
        }
    }
    bool good = true;
    for (const Member *m : order) {
        const QByteArray name = m->name.toUtf8();
        const mz_uint level = m->name == QLatin1String("mimetype") ? MZ_NO_COMPRESSION : MZ_DEFAULT_LEVEL;
        if (!mz_zip_writer_add_mem(&z, name.constData(), m->data.constData(), size_t(m->data.size()), level)) {
            good = false;
            break;
        }
    }
    void *buf = nullptr;
    size_t size = 0;
    if (good && !mz_zip_writer_finalize_heap_archive(&z, &buf, &size)) {
        good = false;
    }
    QByteArray out;
    if (good) {
        out = QByteArray(static_cast<const char *>(buf), qsizetype(size));
    }
    mz_zip_writer_end(&z); // frees buf
    if (!good) {
        setError(error, QStringLiteral("Could not build the zip package."));
        return {};
    }
    if (ok) {
        *ok = true;
    }
    return out;
}

bool Package::write(const QString &path, QString *error) const
{
    bool ok = false;
    const QByteArray data = toData(&ok, error);
    if (!ok) {
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size() || !f.flush()) {
        return setError(error, QStringLiteral("Could not write %1.").arg(QFileInfo(path).fileName()));
    }
    return true;
}

QByteArray readMember(const QString &path, const QString &name, bool *ok, QString *error)
{
    if (ok) {
        *ok = false;
    }
    QByteArray zip;
    if (!readFile(path, &zip, error)) {
        return {};
    }
    mz_zip_archive z{};
    if (!mz_zip_reader_init_mem(&z, zip.constData(), size_t(zip.size()), 0)) {
        setError(error, QStringLiteral("Not a valid zip package."));
        return {};
    }
    QByteArray out;
    const QByteArray n = name.toUtf8();
    const int index = mz_zip_reader_locate_file(&z, n.constData(), nullptr, 0);
    bool good = false;
    if (index >= 0) {
        size_t size = 0;
        if (void *p = mz_zip_reader_extract_to_heap(&z, mz_uint(index), &size, 0)) {
            out = QByteArray(static_cast<const char *>(p), qsizetype(size));
            mz_free(p);
            good = true;
        }
    }
    mz_zip_reader_end(&z);
    if (!good) {
        setError(error, QStringLiteral("%1 has no readable %2.").arg(QFileInfo(path).fileName(), name));
        return {};
    }
    if (ok) {
        *ok = true;
    }
    return out;
}

QStringList memberNames(const QString &path, bool *ok)
{
    if (ok) {
        *ok = false;
    }
    QByteArray zip;
    if (!readFile(path, &zip, nullptr)) {
        return {};
    }
    mz_zip_archive z{};
    if (!mz_zip_reader_init_mem(&z, zip.constData(), size_t(zip.size()), 0)) {
        return {};
    }
    QStringList out;
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&z, i, &st) && !mz_zip_reader_is_file_a_directory(&z, i)) {
            out << QString::fromUtf8(st.m_filename);
        }
    }
    mz_zip_reader_end(&z);
    if (ok) {
        *ok = true;
    }
    return out;
}

bool writeDirectory(const QString &dir, const QString &out, const QString &first, QString *error)
{
    const QDir root(dir);
    QStringList files;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files << root.relativeFilePath(it.next());
    }
    files.sort();
    Package p;
    if (!first.isEmpty() && files.removeAll(first) > 0) {
        files.prepend(first);
    }
    for (const QString &rel : std::as_const(files)) {
        QByteArray data;
        if (!readFile(root.filePath(rel), &data, error)) {
            return false;
        }
        p.setMember(rel, data);
    }
    return p.write(out, error);
}

namespace Testing {
void setFailWrites(bool fail)
{
    g_failWrites = fail;
}
} // namespace Testing

} // namespace ZipPackage
