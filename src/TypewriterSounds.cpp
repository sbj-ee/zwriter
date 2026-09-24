#include "TypewriterSounds.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QUrl>

#ifdef ZWRITER_HAS_MULTIMEDIA
#  include <QAudioDevice>
#  include <QMediaDevices>
#  include <QSoundEffect>
#endif

QString TypewriterSounds::findSample(const QString &fileName)
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/sounds/") + fileName,
        QStringLiteral("assets/sounds/") + fileName,
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            QStringLiteral("../assets/sounds/") + fileName),
        QStringLiteral(":/sounds/") + fileName,
    };
    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            return path;
        }
    }
    return {};
}

TypewriterSounds::TypewriterSounds(QObject *parent)
    : QObject(parent)
{
    for (int i = 1; i <= 4; ++i) {
        const QString path = findSample(QStringLiteral("key-%1.wav").arg(i));
        if (!path.isEmpty()) {
            m_keyPaths.append(path);
        }
    }
    m_returnPath = findSample(QStringLiteral("return.wav"));
}

TypewriterSounds::~TypewriterSounds() = default;

bool TypewriterSounds::isAvailable() const
{
#ifdef ZWRITER_HAS_MULTIMEDIA
    return !m_keyPaths.isEmpty();
#else
    return false;
#endif
}

void TypewriterSounds::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (m_enabled) {
        ensureEffects();
    }
}

void TypewriterSounds::ensureEffects()
{
#ifdef ZWRITER_HAS_MULTIMEDIA
    // Qt's implicit device can land on a non-default sink (e.g. an HDMI
    // monitor); pin every effect to the system default output explicitly.
    if (m_keyEffects.isEmpty()) {
        for (const QString &path : std::as_const(m_keyPaths)) {
            auto *effect = new QSoundEffect(this);
            effect->setAudioDevice(QMediaDevices::defaultAudioOutput());
            effect->setSource(QUrl::fromLocalFile(path));
            effect->setVolume(0.80f);
            m_keyEffects.append(effect);
        }
    }
    if (!m_returnEffect && !m_returnPath.isEmpty()) {
        m_returnEffect = new QSoundEffect(this);
        m_returnEffect->setAudioDevice(QMediaDevices::defaultAudioOutput());
        m_returnEffect->setSource(QUrl::fromLocalFile(m_returnPath));
        m_returnEffect->setVolume(0.80f);
    }
#else
    Q_UNUSED(m_keyPaths);
    Q_UNUSED(m_returnPath);
#endif
}

void TypewriterSounds::playKey()
{
    if (!m_enabled) {
        return;
    }
#ifdef ZWRITER_HAS_MULTIMEDIA
    ensureEffects();
    // Round-robin through the variants; each is only reused every Nth key,
    // so a strike is never cut off by the next keystroke.
    for (int tries = 0; tries < m_keyEffects.size(); ++tries) {
        QSoundEffect *effect = m_keyEffects.at(m_nextKey);
        m_nextKey = (m_nextKey + 1) % m_keyEffects.size();
        if (effect->status() != QSoundEffect::Error && !effect->isPlaying()) {
            effect->play();
            return;
        }
    }
#endif
}

void TypewriterSounds::playReturn()
{
    if (!m_enabled) {
        return;
    }
#ifdef ZWRITER_HAS_MULTIMEDIA
    ensureEffects();
    if (m_returnEffect && m_returnEffect->status() != QSoundEffect::Error) {
        m_returnEffect->play();
    } else {
        // Fall back to key click if return sample missing.
        playKey();
    }
#endif
}
