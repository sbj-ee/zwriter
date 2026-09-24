#include "TypewriterSounds.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <cstring>

#ifdef ZWRITER_HAS_MULTIMEDIA
#  include <QAudioDevice>
#  include <QAudioFormat>
#  include <QAudioSink>
#  include <QIODevice>
#  include <QThread>
#  include <functional>
#  include <QMediaDevices>
#  include <QMutex>
#  include <QMutexLocker>
#  include <QRandomGenerator>
#  include <QVector>
#  include <algorithm>
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

#ifdef ZWRITER_HAS_MULTIMEDIA

namespace {

struct Clip
{
    QVector<float> mono; // at the mixer's sample rate
};

QByteArray readSampleFile(const QUrl &url)
{
    const QString path = url.scheme() == QLatin1String("qrc")
        ? QStringLiteral(":") + url.path()
        : url.toLocalFile();
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

quint32 le32(const uchar *p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint16 le16(const uchar *p)
{
    return quint16(p[0] | (p[1] << 8));
}

// Minimal RIFF/WAVE reader: 16-bit PCM, any channel count (downmixed to mono),
// linearly resampled to `targetRate`. Returns false for anything else.
bool decodeWav(const QByteArray &raw, int targetRate, QVector<float> &out)
{
    const auto *b = reinterpret_cast<const uchar *>(raw.constData());
    const qsizetype size = raw.size();
    if (size < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) {
        return false;
    }
    int channels = 0;
    int rate = 0;
    int bits = 0;
    int format = 0;
    const uchar *data = nullptr;
    qsizetype dataLen = 0;
    qsizetype pos = 12;
    while (pos + 8 <= size) {
        const quint32 chunkLen = le32(b + pos + 4);
        const uchar *body = b + pos + 8;
        const qsizetype avail = std::min<qsizetype>(chunkLen, size - pos - 8);
        if (memcmp(b + pos, "fmt ", 4) == 0 && avail >= 16) {
            format = le16(body);
            channels = le16(body + 2);
            rate = int(le32(body + 4));
            bits = le16(body + 14);
        } else if (memcmp(b + pos, "data", 4) == 0) {
            data = body;
            dataLen = avail;
            break;
        }
        pos += 8 + qsizetype(chunkLen) + (chunkLen & 1);
    }
    if (!data || format != 1 || bits != 16 || channels < 1 || rate < 8000) {
        return false;
    }

    const qsizetype frames = dataLen / (2 * channels);
    QVector<float> mono(frames);
    for (qsizetype i = 0; i < frames; ++i) {
        float acc = 0.0f;
        for (int c = 0; c < channels; ++c) {
            acc += float(qint16(le16(data + (i * channels + c) * 2))) / 32768.0f;
        }
        mono[i] = acc / channels;
    }
    if (rate == targetRate) {
        out = mono;
        return true;
    }
    const qsizetype outFrames = qsizetype(double(frames) * targetRate / rate);
    out.resize(outFrames);
    for (qsizetype i = 0; i < outFrames; ++i) {
        const double src = double(i) * rate / targetRate;
        const qsizetype i0 = qsizetype(src);
        const qsizetype i1 = std::min(i0 + 1, frames - 1);
        const float frac = float(src - double(i0));
        out[i] = mono[i0] * (1.0f - frac) + mono[i1] * frac;
    }
    return true;
}

// Software mixer: sums any triggered clips into a continuous stream (silence
// when idle). Thread-safe: trigger() comes from the GUI thread, render() from
// the audio thread. Clips are immutable once the audio thread starts.
class SampleMixer
{
public:
    explicit SampleMixer(const QAudioFormat &format) : m_format(format) {}

    int frameBytes() const { return m_format.channelCount() * m_format.bytesPerSample(); }

    int addClip(QVector<float> mono)
    {
        m_clips.append(Clip{std::move(mono)});
        return int(m_clips.size()) - 1;
    }

    void trigger(int clip, float gain)
    {
        if (clip < 0 || clip >= m_clips.size()) {
            return;
        }
        QMutexLocker lock(&m_mutex);
        if (m_voices.size() >= kMaxVoices) {
            m_voices.removeFirst(); // steal the oldest
        }
        m_voices.append(Voice{clip, 0, gain});
    }

    void render(char *dst, qint64 frames)
    {
        const int channels = m_format.channelCount();
        const qint64 frameBytes = this->frameBytes();
        const bool isFloat = m_format.sampleFormat() == QAudioFormat::Float;

        QMutexLocker lock(&m_mutex);
        for (qint64 f = 0; f < frames; ++f) {
            float acc = 0.0f;
            for (Voice &v : m_voices) {
                const Clip &c = m_clips.at(v.clip);
                if (v.pos < c.mono.size()) {
                    acc += c.mono.at(v.pos++) * v.gain;
                }
            }
            acc = std::clamp(acc, -1.0f, 1.0f);
            char *frame = dst + f * frameBytes;
            for (int ch = 0; ch < channels; ++ch) {
                if (isFloat) {
                    reinterpret_cast<float *>(frame)[ch] = acc;
                } else {
                    reinterpret_cast<qint16 *>(frame)[ch] = qint16(acc * 32767.0f);
                }
            }
        }
        m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                                      [this](const Voice &v) {
                                          return v.pos >= m_clips.at(v.clip).mono.size();
                                      }),
                       m_voices.end());
    }

private:
    struct Voice
    {
        int clip;
        qsizetype pos;
        float gain;
    };

    static constexpr int kMaxVoices = 24;

    QAudioFormat m_format;
    QList<Clip> m_clips;
    QList<Voice> m_voices;
    QMutex m_mutex;
};

// Lives on its own thread: owns the QAudioSink in push mode and tops it up
// from the mixer on a short timer. Keeping the sink fed with (mostly silent)
// audio means the stream never idles or suspends, so a keypress is audible
// within one small buffer — and a busy GUI thread can't starve it.
class AudioEngine : public QObject
{
public:
    AudioEngine(const QAudioDevice &device, const QAudioFormat &format,
                SampleMixer *mixer, std::function<void()> onFailed)
        : m_device(device), m_format(format), m_mixer(mixer), m_onFailed(std::move(onFailed))
    {
    }

    void begin() // audio thread
    {
        m_sink = new QAudioSink(m_device, m_format, this);
        // Small buffer = low click latency (a new voice is heard after at most
        // one buffer of already-queued audio).
        m_sink->setBufferSize(int(m_format.bytesForDuration(40'000)));
        m_out = m_sink->start();
        if (!m_out) {
            m_onFailed();
            return;
        }
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        m_timer->setInterval(5);
        connect(m_timer, &QTimer::timeout, this, [this]() { tick(); });
        m_timer->start();
        tick();
    }

    void end() // audio thread
    {
        if (m_timer) {
            m_timer->stop();
        }
        if (m_sink) {
            m_sink->stop();
        }
        delete m_timer;
        delete m_sink;
        m_timer = nullptr;
        m_sink = nullptr;
        m_out = nullptr;
    }

private:
    void tick()
    {
        if (!m_sink || !m_out) {
            return;
        }
        if (m_sink->state() == QAudio::SuspendedState) {
            m_sink->resume();
        }
        if (m_sink->state() == QAudio::StoppedState && m_sink->error() != QAudio::NoError) {
            m_timer->stop();
            m_onFailed(); // device gone / server restarted: let the owner rebuild us
            return;
        }
        const qint64 frameBytes = m_mixer->frameBytes();
        const qint64 frames = m_sink->bytesFree() / frameBytes;
        if (frames <= 0) {
            return;
        }
        m_scratch.resize(int(frames * frameBytes));
        m_mixer->render(m_scratch.data(), frames);
        m_out->write(m_scratch);
    }

    QAudioDevice m_device;
    QAudioFormat m_format;
    SampleMixer *m_mixer;
    std::function<void()> m_onFailed;
    QAudioSink *m_sink = nullptr;
    QIODevice *m_out = nullptr;
    QTimer *m_timer = nullptr;
    QByteArray m_scratch;
};

} // namespace

struct TypewriterSounds::Impl
{
    QThread *thread = nullptr;
    AudioEngine *engine = nullptr;
    SampleMixer *mixer = nullptr;
    QMediaDevices *devices = nullptr;
    QList<int> keyClips;
    int spaceClip = -1;
    int returnClip = -1;
    int lastKey = -1;
    bool restartPending = false;
};

#else

struct TypewriterSounds::Impl
{
};

#endif

TypewriterSounds::TypewriterSounds(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    for (int i = 1; i <= 6; ++i) {
        const QUrl url = findSample(QStringLiteral("key-%1.wav").arg(i));
        if (!url.isEmpty()) {
            m_keyUrls.append(url);
        }
    }
    m_spaceUrl = findSample(QStringLiteral("space.wav"));
    m_returnUrl = findSample(QStringLiteral("return.wav"));

#ifdef ZWRITER_HAS_MULTIMEDIA
    // Follow the system default output (headphones plugged in, etc.).
    d->devices = new QMediaDevices(this);
    connect(d->devices, &QMediaDevices::audioOutputsChanged, this, [this]() {
        if (m_enabled) {
            startStream();
        }
    });
#endif
}

TypewriterSounds::~TypewriterSounds()
{
    stopStream();
}

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
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (m_enabled) {
        startStream();
    } else {
        stopStream();
    }
}

void TypewriterSounds::stopStream()
{
#ifdef ZWRITER_HAS_MULTIMEDIA
    if (d->thread) {
        if (d->engine) {
            QMetaObject::invokeMethod(d->engine, [e = d->engine]() { e->end(); },
                                      Qt::BlockingQueuedConnection);
        }
        d->thread->quit();
        d->thread->wait();
        delete d->engine;
        d->engine = nullptr;
        delete d->thread;
        d->thread = nullptr;
    }
    delete d->mixer;
    d->mixer = nullptr;
    d->keyClips.clear();
    d->spaceClip = -1;
    d->returnClip = -1;
#endif
}

void TypewriterSounds::startStream()
{
#ifdef ZWRITER_HAS_MULTIMEDIA
    stopStream();
    if (m_keyUrls.isEmpty()) {
        return;
    }

    // Always target the system default output explicitly: Qt's implicit
    // choice can land on a non-default sink (e.g. an HDMI monitor).
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        return;
    }
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        format = device.preferredFormat();
    }
    if (format.sampleFormat() != QAudioFormat::Int16
        && format.sampleFormat() != QAudioFormat::Float) {
        return;
    }

    d->mixer = new SampleMixer(format);
    for (const QUrl &url : std::as_const(m_keyUrls)) {
        QVector<float> mono;
        if (decodeWav(readSampleFile(url), format.sampleRate(), mono)) {
            d->keyClips.append(d->mixer->addClip(std::move(mono)));
        }
    }
    QVector<float> space;
    if (!m_spaceUrl.isEmpty()
        && decodeWav(readSampleFile(m_spaceUrl), format.sampleRate(), space)) {
        d->spaceClip = d->mixer->addClip(std::move(space));
    }
    QVector<float> ret;
    if (!m_returnUrl.isEmpty()
        && decodeWav(readSampleFile(m_returnUrl), format.sampleRate(), ret)) {
        d->returnClip = d->mixer->addClip(std::move(ret));
    }
    if (d->keyClips.isEmpty()) {
        stopStream();
        return;
    }

    auto onFailed = [this]() {
        // Called from the audio thread; recover on the GUI thread.
        QMetaObject::invokeMethod(this, [this]() {
            if (!m_enabled || d->restartPending) {
                return;
            }
            d->restartPending = true;
            QTimer::singleShot(250, this, [this]() {
                d->restartPending = false;
                if (m_enabled) {
                    startStream();
                }
            });
        }, Qt::QueuedConnection);
    };
    d->engine = new AudioEngine(device, format, d->mixer, onFailed);
    d->thread = new QThread(this);
    d->thread->setObjectName(QStringLiteral("zwriter-audio"));
    d->engine->moveToThread(d->thread);
    d->thread->start(QThread::TimeCriticalPriority);
    QMetaObject::invokeMethod(d->engine, [e = d->engine]() { e->begin(); },
                              Qt::QueuedConnection);
#endif
}

void TypewriterSounds::playKey()
{
    if (!m_enabled) {
        return;
    }
#ifdef ZWRITER_HAS_MULTIMEDIA
    if (!d->mixer || d->keyClips.isEmpty()) {
        return;
    }
    // Random variant (never the same twice in a row) with slight level jitter
    // so repeated keys don't sound like one looped sample.
    int idx = 0;
    if (d->keyClips.size() > 1) {
        do {
            idx = int(QRandomGenerator::global()->bounded(d->keyClips.size()));
        } while (idx == d->lastKey);
    }
    d->lastKey = idx;
    const float gain = 0.75f + 0.20f * float(QRandomGenerator::global()->generateDouble());
    d->mixer->trigger(d->keyClips.at(idx), gain);
#endif
}

void TypewriterSounds::playSpace()
{
    if (!m_enabled) {
        return;
    }
#ifdef ZWRITER_HAS_MULTIMEDIA
    if (!d->mixer) {
        return;
    }
    if (d->spaceClip >= 0) {
        d->mixer->trigger(d->spaceClip, 0.85f);
    } else {
        playKey(); // fall back to a normal strike if the sample is missing
    }
#endif
}

void TypewriterSounds::playReturn()
{
    if (!m_enabled) {
        return;
    }
#ifdef ZWRITER_HAS_MULTIMEDIA
    if (!d->mixer) {
        return;
    }
    if (d->returnClip >= 0) {
        d->mixer->trigger(d->returnClip, 0.85f);
    } else {
        // Fall back to a key click if the return sample is missing.
        playKey();
    }
#endif
}
