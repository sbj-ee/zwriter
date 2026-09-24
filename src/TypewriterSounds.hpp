#pragma once

#include <QObject>
#include <QString>

class QSoundEffect;

// Optional FocusWriter-style key sounds. Default OFF. Tasteful sample packs
 // can be swapped later under assets/sounds/. No-op when disabled or when
// the sample file is absent. Requires Qt6 Multimedia at build time when
// ZWRITER_HAS_MULTIMEDIA is defined; otherwise the toggle still works as a
// preference stub with no audio.
class TypewriterSounds : public QObject
{
    Q_OBJECT

public:
    explicit TypewriterSounds(QObject *parent = nullptr);
    ~TypewriterSounds() override;

    bool isEnabled() const { return m_enabled; }

public slots:
    void setEnabled(bool enabled);
    void playKey();

private:
    void ensureEffect();

    bool m_enabled = false; // default OFF
    QString m_samplePath;
#ifdef ZWRITER_HAS_MULTIMEDIA
    QSoundEffect *m_effect = nullptr;
#endif
};
