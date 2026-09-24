#pragma once

#include <QObject>
#include <QString>

class QSoundEffect;

// Optional FocusWriter-style key sounds. Default OFF.
// Bundled samples under assets/sounds/: key.wav (per-char click) and
// return.wav (carriage return). Prefer QSoundEffect for low latency.
// No-op when disabled, when samples are absent, or when built without
// Qt6 Multimedia (ZWRITER_HAS_MULTIMEDIA).
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
    void playReturn();

private:
    void ensureEffects();
    static QString findSample(const QString &fileName);

    bool m_enabled = false; // default OFF
    QString m_keyPath;
    QString m_returnPath;
#ifdef ZWRITER_HAS_MULTIMEDIA
    QSoundEffect *m_keyEffect = nullptr;
    QSoundEffect *m_returnEffect = nullptr;
#endif
};
