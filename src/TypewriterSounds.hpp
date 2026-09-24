#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QUrl>

class QSoundEffect;

// Optional FocusWriter-style key sounds. Default OFF.
// Bundled samples under assets/sounds/: key-1..4.wav (type-bar strikes,
// rotated so fast typing doesn't cut a sample off or sound machine-gunned)
// and return.wav (carriage slide + bell). Uses QSoundEffect for low latency.
// No-op when disabled, when samples are absent, or when built without
// Qt6 Multimedia (ZWRITER_HAS_MULTIMEDIA).
class TypewriterSounds : public QObject
{
    Q_OBJECT

public:
    explicit TypewriterSounds(QObject *parent = nullptr);
    ~TypewriterSounds() override;

    bool isEnabled() const { return m_enabled; }
    // True when Multimedia is built in and at least the key sample was found.
    bool isAvailable() const;

public slots:
    void setEnabled(bool enabled);
    void playKey();
    void playReturn();

private:
    void ensureEffects();
    static QUrl findSample(const QString &fileName);

    bool m_enabled = false; // default OFF
    QList<QUrl> m_keyUrls;
    QUrl m_returnUrl;
#ifdef ZWRITER_HAS_MULTIMEDIA
    QList<QSoundEffect *> m_keyEffects;
    int m_nextKey = 0;
    QSoundEffect *m_returnEffect = nullptr;
#endif
};
