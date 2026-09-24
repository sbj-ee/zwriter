#include "TypewriterSounds.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QUrl>

#ifdef ZWRITER_HAS_MULTIMEDIA
#  include <QSoundEffect>
#endif

TypewriterSounds::TypewriterSounds(QObject *parent)
    : QObject(parent)
{
    // Prefer shipped sample; fall back to a few conventional names.
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/sounds/key.wav"),
        QStringLiteral("assets/sounds/key.wav"),
        QStringLiteral(":/sounds/key.wav"),
    };
    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            m_samplePath = path;
            break;
        }
    }
}

TypewriterSounds::~TypewriterSounds() = default;

void TypewriterSounds::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (m_enabled) {
        ensureEffect();
    }
}

void TypewriterSounds::ensureEffect()
{
#ifdef ZWRITER_HAS_MULTIMEDIA
    if (m_effect || m_samplePath.isEmpty()) {
        return;
    }
    m_effect = new QSoundEffect(this);
    m_effect->setSource(QUrl::fromLocalFile(m_samplePath));
    m_effect->setVolume(0.35f); // tasteful, not cheesy
#else
    Q_UNUSED(m_samplePath);
#endif
}

void TypewriterSounds::playKey()
{
    if (!m_enabled) {
        return;
    }
#ifdef ZWRITER_HAS_MULTIMEDIA
    ensureEffect();
    if (m_effect && m_effect->isLoaded()) {
        m_effect->play();
    }
#endif
}
