#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

#include <memory>

// Optional FocusWriter-style key sounds. Default OFF.
// Bundled samples under assets/sounds/: key-1..6.wav (type-bar strikes,
// rotated so fast typing doesn't sound machine-gunned) and return.wav
// (carriage slide + bell).
//
// Playback uses ONE persistent audio stream fed by a tiny software mixer:
// a keypress just adds a voice to the running stream, so there is no
// per-keystroke stream setup, no dropped clicks during fast typing, and
// overlapping strikes mix instead of cutting each other off.
// No-op when disabled, when samples are absent, or when built without
// Qt6 Multimedia (ZWRITER_HAS_MULTIMEDIA).
class TypewriterSounds : public QObject
{
    Q_OBJECT

public:
    explicit TypewriterSounds(QObject *parent = nullptr);
    ~TypewriterSounds() override;

    bool isEnabled() const { return m_enabled; }
    // True when Multimedia is built in and at least one key sample was found.
    bool isAvailable() const;

public slots:
    void setEnabled(bool enabled);
    void playKey();
    void playSpace(); // space bar / backspace: no type bar fires, so a softer thup
    void playReturn();

private:
    struct Impl;

    static QUrl findSample(const QString &fileName);
    void startStream();
    void stopStream();

    bool m_enabled = false; // default OFF
    QList<QUrl> m_keyUrls;
    QUrl m_spaceUrl;
    QUrl m_returnUrl;
    std::unique_ptr<Impl> d;
};
