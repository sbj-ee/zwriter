#include "TypewriterSounds.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#ifdef ZWRITER_HAS_MULTIMEDIA
#  include <QAudioDevice>
#  include <QMediaDevices>
#  include <QSoundEffect>
#endif

QUrl TypewriterSounds::findSample(const QString &fileName)
{
    // On-disk copies win so a user or packager can swap in recorded samples;
    // otherwise fall back to the copies embedded in the binary.
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/sounds/") + fileName,
        QStringLiteral("assets/sounds/") + fileName,
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            QStringLiteral("../assets/sounds/") + fileName),
    };
    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
        }
    }
    if (QFile::exists(QStringLiteral(":/sounds/") + fileName)) {
        return QUrl(QStringLiteral("qrc:/sounds/") + fileName);
    }
    return {};
}

TypewriterSounds::TypewriterSounds(QObject *parent)
    : QObject(parent)
{
    for (int i = 1; i <= 4; ++i) {
        const QUrl url = findSample(QStringLiteral("key-%1.wav").arg(i));
        if (!url.isEmpty()) {
            m_keyUrls.append(url);
        }
    }
    m_returnUrl = findSample(QStringLiteral("return.wav"));
}

TypewriterSounds::~TypewriterSounds() = default;

bool TypewriterSounds::isAvailable() const
{
#ifdef ZWRITER_HAS_MULTIMEDIA
    return !m_keyUrls.isEmpty();
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
        for (const QUrl &url : std::as_const(m_keyUrls)) {
            auto *effect = new QSoundEffect(this);
            effect->setAudioDevice(QMediaDevices::defaultAudioOutput());
            effect->setSource(url);
            effect->setVolume(0.80f);
            m_keyEffects.append(effect);
        }
    }
    if (!m_returnEffect && !m_returnUrl.isEmpty()) {
        m_returnEffect = new QSoundEffect(this);
        m_returnEffect->setAudioDevice(QMediaDevices::defaultAudioOutput());
        m_returnEffect->setSource(m_returnUrl);
        m_returnEffect->setVolume(0.80f);
    }
#else
    Q_UNUSED(m_keyUrls);
    Q_UNUSED(m_returnUrl);
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
