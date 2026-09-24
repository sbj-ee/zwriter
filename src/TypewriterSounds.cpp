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
    m_keyPath = findSample(QStringLiteral("key.wav"));
    m_returnPath = findSample(QStringLiteral("return.wav"));
}

TypewriterSounds::~TypewriterSounds() = default;

bool TypewriterSounds::isAvailable() const
{
#ifdef ZWRITER_HAS_MULTIMEDIA
    return !m_keyPath.isEmpty();
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
    if (!m_keyEffect && !m_keyPath.isEmpty()) {
        m_keyEffect = new QSoundEffect(this);
        // Qt's implicit device can land on a non-default sink (e.g. an HDMI
        // monitor); pin to the system default output explicitly.
        m_keyEffect->setAudioDevice(QMediaDevices::defaultAudioOutput());
        m_keyEffect->setSource(QUrl::fromLocalFile(m_keyPath));
        m_keyEffect->setVolume(0.40f);
    }
    if (!m_returnEffect && !m_returnPath.isEmpty()) {
        m_returnEffect = new QSoundEffect(this);
        m_returnEffect->setAudioDevice(QMediaDevices::defaultAudioOutput());
        m_returnEffect->setSource(QUrl::fromLocalFile(m_returnPath));
        m_returnEffect->setVolume(0.45f);
    }
#else
    Q_UNUSED(m_keyPath);
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
    if (m_keyEffect && m_keyEffect->status() != QSoundEffect::Error) {
        // Restart quickly for rapid typing; QSoundEffect handles overlap poorly
        // on some backends, so stop-then-play keeps latency low.
        if (m_keyEffect->isPlaying()) {
            m_keyEffect->stop();
        }
        m_keyEffect->play();
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
        if (m_returnEffect->isPlaying()) {
            m_returnEffect->stop();
        }
        m_returnEffect->play();
    } else {
        // Fall back to key click if return sample missing.
        playKey();
    }
#endif
}
