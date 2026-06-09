#include "soundmanager.h"
#include "colecocontroller.h"

#include <QDebug>
#include <QMetaObject>
#include <cstring> // memset, memcpy
#include <QMutexLocker>
#include <QDateTime>
#include <QVector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#if defined(Q_OS_LINUX)
#include <alsa/asoundlib.h>
#endif

// debug counters

SoundManager::SoundManager(QObject *parent)
    : QObject(parent)
{
    m_inited    = false;
    m_suspended = false;
    m_running   = false;

    m_previewTimer = new QTimer(this);
    m_previewVuDecayTimer = new QTimer(this);

    // kChunkFrames = 735 frames at 44100 Hz = 16.666 ms.
    // De vorige 10 ms timer schreef sneller dan de DirectSound/ALSA playback
    // consumeerde. Dat gaf een tremolo/warble effect.
    // 735 frames at 44100 Hz = 16.666 ms.
    // Gebruik 17ms, anders schrijven we sneller dan DirectSound afspeelt.
    m_previewTimer->setInterval(17);
    m_previewTimer->setTimerType(Qt::PreciseTimer);

    connect(m_previewTimer, &QTimer::timeout, this, [this]() {
        if (hasActivePreview()) {
            writePreviewOnlyChunk();
            return;
        }

        // Als een toon stopt, moeten we nog enkele stilteblokken naar de
        // DirectSound/ALSA buffer schrijven. Anders blijft de laatste toon
        // in de looping buffer hangen.
        if (m_previewSilenceChunksRemaining > 0) {
            writeSilencePreviewChunk();
            --m_previewSilenceChunksRemaining;
            return;
        }

        stopPreviewTimerIfIdle();
    });

    // VU meters voor de Sound Editor borders. Dit staat los van de audio-buffer:
    // Windows gebruikt one-shot preview buffers, Linux streaming, maar de UI krijgt
    // in beide gevallen dezelfde 0..15 activiteit met een zachte falloff.
    m_previewVuDecayTimer->setInterval(45);
    m_previewVuDecayTimer->setTimerType(Qt::PreciseTimer);
    connect(m_previewVuDecayTimer, &QTimer::timeout,
            this, &SoundManager::decayPreviewVuMeters);
}


SoundManager::~SoundManager()
{
    end();
}

void SoundManager::attachController(ColecoController *ctrl)
{
    m_controller = ctrl;
}

void SoundManager::previewPsgNote(int channel, int psgPeriod, int volume, int instrumentEnv)
{
    channel = qBound(0, channel, 3);
    volume = qBound(0, volume, 15);
    instrumentEnv = qBound(0, instrumentEnv, 15);

    {
        QMutexLocker lock(&m_previewMutex);

        PreviewChannel& ch = m_previewChannels[channel];

        if (volume <= 0 || psgPeriod <= 0) {
            ch.active = false;
            ch.volume = 0;
            ch.psgPeriod = 0;
            ch.frequency = 0.0;
            ch.previewSamplesRemaining = 0;

            // Minstens enkele chunks stilte schrijven zodat de oude toon
            // uit de DirectSound/ALSA buffer verdwijnt.
            m_previewSilenceChunksRemaining = qMax(m_previewSilenceChunksRemaining, 1);
        } else {
            ch.active = true;
            ch.volume = volume;
            ch.psgPeriod = psgPeriod;
            ch.instrumentEnv = instrumentEnv;
            ch.smoothSample = 0.0;
            ch.noteTime = 0.0;
            ch.transitionSamples = 96;

            // Manual keyboard/cell preview is a short one-shot.
            // Song playback uses startSoundEditorStream() and sets -1 internally.
            ch.previewSamplesRemaining = qRound(44100.0 * 0.35);

            if (channel < 3) {
                // SN76489/TMS9919 tone formula:
                // frequency = clock / 32 / period
                ch.frequency = 3579545.0 / 32.0 / qMax(1, psgPeriod);
            } else {
                // Noise preview. psgPeriod is used as noise code 0..7.
                ch.noiseCode = psgPeriod & 0x07;

                // Approximate SN76489 noise rates for editor preview.
                // 0=N/512, 1=N/1024, 2=N/2048, 3=tone3-derived.
                // Codes 4..7 are treated as brighter white-noise variants.
                switch (ch.noiseCode & 0x03) {
                case 0: ch.frequency = 7000.0; break;
                case 1: ch.frequency = 3500.0; break;
                case 2: ch.frequency = 1750.0; break;
                default: ch.frequency = 900.0; break;
                }

                if (ch.noiseCode & 0x04)
                    ch.frequency *= 1.25;
            }
        }
    }

    // Laat de editor-UI weten hoe hard dit kanaal actief is.
    // De editor-volumes zijn 0..15 waarbij 15 het luidst is.
    setPreviewVuLevel(channel, (volume > 0 && psgPeriod > 0) ? volume : 0);

#if defined(Q_OS_WIN)
    // Voor de Sound Editor muziek-playback gebruiken we opnieuw streaming.
    // De vroegere one-shot preview-buffer startte bij elke nieuwe noot opnieuw
    // en veroorzaakte hoorbare gaten/stotters tegenover de CVBasic WAIT-versie.
    releaseDirectSoundPreviewBuffer();
#endif

#if defined(Q_OS_WIN)
    startDirectSoundPreviewIfNeeded();
#endif

    if (hasActivePreview() || m_previewSilenceChunksRemaining > 0)
        startPreviewTimerIfNeeded();
    else
        stopPreviewTimerIfIdle();
}


void SoundManager::setPreviewVuLevel(int channel, int level)
{
    channel = qBound(0, channel, 3);
    level = qBound(0, level, 15);

    bool anyActive = false;

    {
        QMutexLocker lock(&m_previewVuMutex);

        if (m_previewVuPendingLevels[channel] < level)
            m_previewVuPendingLevels[channel] = level;
        else if (level == 0)
            m_previewVuPendingLevels[channel] = 0;

        m_previewVuDirty = true;

        for (int i = 0; i < 4; ++i) {
            if (m_previewVuLevels[i] > 0 || m_previewVuPendingLevels[i] > 0) {
                anyActive = true;
                break;
            }
        }
    }

    // VU updates gaan via aparte timer, niet direct vanuit de audio-render.
    // Dat voorkomt GUI-signals tijdens audiomix en vermindert haperingen.
    if (m_previewVuDecayTimer && !m_previewVuDecayTimer->isActive())
        m_previewVuDecayTimer->start();

    Q_UNUSED(anyActive);
}

void SoundManager::decayPreviewVuMeters()
{
    bool anyActive = false;

    {
        QMutexLocker lock(&m_previewVuMutex);

        for (int i = 0; i < 4; ++i) {
            if (m_previewVuPendingLevels[i] > m_previewVuLevels[i])
                m_previewVuLevels[i] = m_previewVuPendingLevels[i];
            else if (m_previewVuPendingLevels[i] == 0 && m_previewVuLevels[i] > 0)
                m_previewVuLevels[i] = qMax(0, m_previewVuLevels[i] - 2);
            else if (m_previewVuLevels[i] > 0)
                m_previewVuLevels[i] = qMax(0, m_previewVuLevels[i] - 1);

            m_previewVuPendingLevels[i] = 0;

            if (m_previewVuLevels[i] > 0)
                anyActive = true;
        }

        emitPreviewVuMetersLocked();
        m_previewVuDirty = false;
    }

    if (!anyActive && m_previewVuDecayTimer)
        m_previewVuDecayTimer->stop();
}

void SoundManager::emitPreviewVuMetersLocked()
{
    // Caller houdt m_previewVuMutex vast. We emitteren enkel simpele int-waarden;
    // Qt queued/direct delivery naar de GUI-thread blijft veilig zolang de slot zelf
    // geen SoundManager-locks terug probeert te nemen.
    emit previewVuMeterChanged(0, m_previewVuLevels[0]);
    emit previewVuMeterChanged(1, m_previewVuLevels[1]);
    emit previewVuMeterChanged(2, m_previewVuLevels[2]);
    emit previewVuMeterChanged(3, m_previewVuLevels[3]);
    emit previewVuMetersChanged(m_previewVuLevels[0],
                                m_previewVuLevels[1],
                                m_previewVuLevels[2],
                                m_previewVuLevels[3]);
}



void SoundManager::startSoundEditorStream(const QVariantList& rows, int rowMs, bool loop)
{
    QVector<StreamRow> parsed;
    parsed.reserve(rows.size());

    for (const QVariant& v : rows) {
        const QVariantList rowList = v.toList();
        if (rowList.size() < 8)
            continue;

        StreamRow row;

        if (rowList.size() >= 12) {
            for (int ch = 0; ch < 4; ++ch) {
                row.period[ch] = rowList.value(ch * 3 + 0).toInt();
                row.volume[ch] = rowList.value(ch * 3 + 1).toInt();
                bool envOk = false;
                int envValue = rowList.value(ch * 3 + 2).toInt(&envOk);
                if (!envOk)
                    envValue = 3;
                row.env[ch] = qBound(0, envValue, 15);
            }
        } else {
            // Backwards compatible with old 8-int stream rows.
            for (int ch = 0; ch < 4; ++ch) {
                row.period[ch] = rowList.value(ch * 2 + 0).toInt();
                row.volume[ch] = rowList.value(ch * 2 + 1).toInt();
                row.env[ch]    = 3;
            }
        }

        parsed.append(row);
    }

    if (parsed.isEmpty()) {
        hardStopPreviewAudio();
        return;
    }

    clearPreviewStateAndBuffer();

    {
        QMutexLocker lock(&m_previewMutex);

        m_streamRows = parsed;
        m_streamRowIndex = 0;
        m_streamSamplesPerRow = qMax(1, qRound((44100.0 * qMax(1, rowMs)) / 1000.0));
        m_streamSamplesUntilNextRow = 0;
        m_streamPlaying = true;
        m_streamLoop = loop;

        for (int i = 0; i < 4; ++i) {
            m_previewChannels[i].active = false;
            m_previewChannels[i].volume = 0;
            m_previewChannels[i].psgPeriod = 0;
            m_previewChannels[i].phase = 0.0;
            m_previewChannels[i].noisePhase = 0.0;
            m_previewChannels[i].noiseValue = 0.0;
            m_previewChannels[i].smoothSample = 0.0;
            m_previewChannels[i].noteTime = 0.0;
            m_previewChannels[i].transitionSamples = 0;
            m_previewChannels[i].previewSamplesRemaining = 0;
        }
    }

#if defined(Q_OS_WIN)
    startDirectSoundPreviewIfNeeded();
#endif

    startPreviewTimerIfNeeded();
}

void SoundManager::stopSoundEditorStream()
{
    {
        QMutexLocker lock(&m_previewMutex);
        m_streamRows.clear();
        m_streamRowIndex = 0;
        m_streamSamplesUntilNextRow = 0;
        m_streamPlaying = false;
        m_streamLoop = false;
    }

    clearPreviewStateAndBuffer();
}


void SoundManager::hardStopPreviewAudio()
{
    {
        QMutexLocker lock(&m_previewMutex);
        m_streamRows.clear();
        m_streamRowIndex = 0;
        m_streamSamplesUntilNextRow = 0;
        m_streamPlaying = false;
        m_streamLoop = false;
    }

    clearPreviewStateAndBuffer();

    {
        QMutexLocker vuLock(&m_previewVuMutex);
        for (int i = 0; i < 4; ++i) {
            m_previewVuLevels[i] = 0;
            m_previewVuPendingLevels[i] = 0;
        }
    }

    emit previewVuMetersChanged(0, 0, 0, 0);
}

bool SoundManager::hasActivePreview() const
{
    // Deze functie wordt ook op de GUI-thread gebruikt.
    // Cast is nodig omdat we in een const method toch de mutex willen pakken.
    QMutexLocker lock(const_cast<QMutex*>(&m_previewMutex));

    for (int i = 0; i < 4; ++i) {
        if (m_previewChannels[i].active && m_previewChannels[i].volume > 0)
            return true;
    }

    if (m_streamPlaying)
        return true;

    return false;
}

void SoundManager::startPreviewTimerIfNeeded()
{
    if (!m_previewTimer)
        return;

    if (!m_previewTimer->isActive()) {
        // DirectSound gebruikt een ringbuffer. Als we pas na de eerste timer tick
        // beginnen schrijven, of maar net op tijd schrijven, hoor je warble/tremolo.
        // Daarom vullen we enkele chunks vooraf.
        for (int i = 0; i < 4; ++i)
            writePreviewOnlyChunk();

        m_previewTimer->start();
    }
}

void SoundManager::stopPreviewTimerIfIdle()
{
    if (!m_previewTimer)
        return;

    if (!hasActivePreview() && m_previewSilenceChunksRemaining <= 0 && m_previewTimer->isActive()) {
        m_previewTimer->stop();

#if defined(Q_OS_WIN)
        // Manual key preview auto-release: stop and clear the looping buffer.
        // Otherwise the last waveform can keep coming back as tremolo.
        stopAndClearDirectSoundPreview();
#endif
    }
}



#if defined(Q_OS_WIN)
void SoundManager::stopAndClearDirectSoundPreview()
{
    releaseDirectSoundPreviewBuffer();

    if (!m_secondaryBuf || m_bufferBytes == 0) {
        m_previewPlaybackStarted = false;
        return;
    }

    m_secondaryBuf->Stop();

    BYTE* p1 = nullptr;
    BYTE* p2 = nullptr;
    DWORD len1 = 0;
    DWORD len2 = 0;

    HRESULT hr = m_secondaryBuf->Lock(
        0,
        m_bufferBytes,
        reinterpret_cast<LPVOID*>(&p1), &len1,
        reinterpret_cast<LPVOID*>(&p2), &len2,
        0
    );

    if (SUCCEEDED(hr)) {
        if (p1 && len1)
            std::memset(p1, 0, len1);
        if (p2 && len2)
            std::memset(p2, 0, len2);

        m_secondaryBuf->Unlock(p1, len1, p2, len2);
    }

    m_secondaryBuf->SetCurrentPosition(0);
    m_lastWritePos = 0;
    m_previewPlaybackStarted = false;
}

void SoundManager::startDirectSoundPreviewIfNeeded()
{
    if (!m_secondaryBuf || m_bufferBytes == 0)
        return;

    DWORD status = 0;
    if (SUCCEEDED(m_secondaryBuf->GetStatus(&status)) &&
        (status & DSBSTATUS_PLAYING) &&
        m_previewPlaybackStarted) {
        return;
    }

    // Start altijd vanuit stilte en prime enkele chunks zodat DirectSound
    // nooit oude data begint te loopen.
    stopAndClearDirectSoundPreview();

    for (int i = 0; i < 8; ++i)
        writePreviewOnlyChunk();

    m_secondaryBuf->SetCurrentPosition(0);
    m_secondaryBuf->Play(0, 0, DSBPLAY_LOOPING);
    m_previewPlaybackStarted = true;
}
#endif


void SoundManager::clearPreviewStateAndBuffer()
{
    {
        QMutexLocker lock(&m_previewMutex);

        for (int i = 0; i < 4; ++i) {
            m_previewChannels[i].active = false;
            m_previewChannels[i].volume = 0;
            m_previewChannels[i].psgPeriod = 0;
            m_previewChannels[i].frequency = 0.0;
            m_previewChannels[i].phase = 0.0;
            m_previewChannels[i].noisePhase = 0.0;
            m_previewChannels[i].noiseValue = 0.0;
            m_previewChannels[i].smoothSample = 0.0;
            m_previewChannels[i].noteTime = 0.0;
            m_previewChannels[i].transitionSamples = 0;
            m_previewChannels[i].previewSamplesRemaining = 0;
        }

        m_previewSilenceChunksRemaining = 0;
    }

    if (m_previewTimer)
        m_previewTimer->stop();

#if defined(Q_OS_WIN)
    stopAndClearDirectSoundPreview();
#endif

#if defined(Q_OS_LINUX)
    if (m_pcmHandle)
        snd_pcm_drop(m_pcmHandle);
#endif
}


void SoundManager::writeSilencePreviewChunk()
{
    if (!m_inited || m_suspended)
        return;

    QVector<int16_t> silence(kChunkFrames * 2);
    std::fill(silence.begin(), silence.end(), 0);

#if defined(Q_OS_WIN)
    if (!m_secondaryBuf || m_bufferBytes == 0)
        return;

    const DWORD blockBytes = kChunkFrames * m_bytesPerFrame;
    if (blockBytes == 0 || blockBytes > m_bufferBytes)
        return;

    BYTE* p1 = nullptr;
    BYTE* p2 = nullptr;
    DWORD len1 = 0;
    DWORD len2 = 0;

    HRESULT hr = m_secondaryBuf->Lock(
        m_lastWritePos,
        blockBytes,
        reinterpret_cast<LPVOID*>(&p1), &len1,
        reinterpret_cast<LPVOID*>(&p2), &len2,
        0
    );

    if (FAILED(hr))
        return;

    if (p1 && len1)
        std::memset(p1, 0, len1);

    if (p2 && len2)
        std::memset(p2, 0, len2);

    m_secondaryBuf->Unlock(p1, len1, p2, len2);
    m_lastWritePos = (m_lastWritePos + blockBytes) % m_bufferBytes;
#endif

#if defined(Q_OS_LINUX)
    if (!m_pcmHandle)
        return;

    int err = snd_pcm_writei(m_pcmHandle, silence.constData(), kChunkFrames);
    if (err < 0)
        qWarning() << "[SoundManager] preview ALSA silence write failed:" << snd_strerror(err);
#endif
}

void SoundManager::writePreviewOnlyChunk()
{
    if (!m_inited || m_suspended)
        return;

    if (!hasActivePreview()) {
        if (m_previewSilenceChunksRemaining > 0)
            writeSilencePreviewChunk();
        return;
    }

    QVector<int16_t> preview(kChunkFrames * 2);
    std::fill(preview.begin(), preview.end(), 0);

    mixPreviewIntoBuffer(preview.data(), kChunkFrames);

#if defined(Q_OS_WIN)
    if (!m_secondaryBuf || m_bufferBytes == 0)
        return;

    const DWORD blockBytes = kChunkFrames * m_bytesPerFrame;
    if (blockBytes == 0 || blockBytes > m_bufferBytes)
        return;

    BYTE* p1 = nullptr;
    BYTE* p2 = nullptr;
    DWORD len1 = 0;
    DWORD len2 = 0;

    HRESULT hr = m_secondaryBuf->Lock(
        m_lastWritePos,
        blockBytes,
        reinterpret_cast<LPVOID*>(&p1), &len1,
        reinterpret_cast<LPVOID*>(&p2), &len2,
        0
    );

    if (FAILED(hr))
        return;

    if (p1 && len1)
        std::memcpy(p1, preview.constData(), len1);

    if (p2 && len2) {
        std::memcpy(p2,
                    reinterpret_cast<const BYTE*>(preview.constData()) + len1,
                    len2);
    }

    m_secondaryBuf->Unlock(p1, len1, p2, len2);
    m_lastWritePos = (m_lastWritePos + blockBytes) % m_bufferBytes;
#endif

#if defined(Q_OS_LINUX)
    if (!m_pcmHandle)
        return;

    int err = snd_pcm_writei(m_pcmHandle, preview.constData(), kChunkFrames);
    if (err < 0)
        qWarning() << "[SoundManager] preview ALSA write failed:" << snd_strerror(err);
#endif
}

static double previewEnvelopeFactor(int env, double t, double duration);
static double previewPitchMultiplier(int env, double t);

void SoundManager::mixPreviewIntoBuffer(int16_t* stereo, int framesStereo)
{
    if (!stereo || framesStereo <= 0)
        return;

    QMutexLocker lock(&m_previewMutex);

    auto applyStreamRow = [this]() {
        if (!m_streamPlaying)
            return;

        if (m_streamRowIndex >= m_streamRows.size()) {
            if (m_streamLoop && !m_streamRows.isEmpty()) {
                m_streamRowIndex = 0;
            } else {
                m_streamPlaying = false;

                for (int ch = 0; ch < 4; ++ch) {
                    m_previewChannels[ch].active = false;
                    m_previewChannels[ch].volume = 0;
                    m_previewChannels[ch].psgPeriod = 0;
                    m_previewChannels[ch].smoothSample = 0.0;
                    setPreviewVuLevel(ch, 0);
                }

                return;
            }
        }

        if (!m_streamPlaying || m_streamRowIndex >= m_streamRows.size())
            return;

        const StreamRow row = m_streamRows.at(m_streamRowIndex++);

        for (int ch = 0; ch < 4; ++ch) {
            const int p = row.period[ch];
            const int v = row.volume[ch];
            const int e = qBound(0, row.env[ch], 15);

            // p < 0 means HOLD previous PSG state.
            // Geen VU signal op elke HOLD-row; dat kan de audio-timer onnodig belasten.
            if (p < 0)
                continue;

            PreviewChannel& pc = m_previewChannels[ch];

            if (v <= 0 || p == 0) {
                pc.active = false;
                pc.volume = 0;
                pc.psgPeriod = 0;
                pc.frequency = 0.0;
                pc.previewSamplesRemaining = 0;
                pc.noteTime = 0.0;
                pc.transitionSamples = 0;

                // Niet abrupt naar nul springen: laat smoothSample snel uitdoven.
                pc.smoothSample *= 0.25;

                setPreviewVuLevel(ch, 0);
                continue;
            }

            const bool changed = (!pc.active || pc.psgPeriod != p || pc.volume != v || pc.instrumentEnv != e);

            pc.active = true;
            pc.psgPeriod = p;
            pc.volume = qBound(0, v, 15);
            pc.instrumentEnv = e;
            pc.previewSamplesRemaining = -1;

            if (changed) {
                pc.noteTime = 0.0;
                pc.transitionSamples = 64;
            }

            if (ch < 3) {
                const double f = 3579545.0 / (32.0 * qMax(1, p));
                pc.frequency = qBound(20.0, f, 20000.0);
            } else {
                pc.noiseCode = qBound(0, p, 7);
                pc.frequency = 220.0 + pc.noiseCode * 110.0;
            }

            // Bij nieuwe noot niet hard resetten naar 0; dat klikte.
            // Alleen bij eerste activatie een kleine startwaarde.
            if (changed && qAbs(pc.smoothSample) < 0.0001)
                pc.smoothSample = 0.0;

            setPreviewVuLevel(ch, pc.volume);
        }

        m_streamSamplesUntilNextRow += m_streamSamplesPerRow;
    };

    for (int frame = 0; frame < framesStereo; ++frame) {
        if (m_streamPlaying && m_streamSamplesUntilNextRow <= 0)
            applyStreamRow();

        bool anyActive = false;
        double mixed = 0.0;

        for (int i = 0; i < 4; ++i) {
            PreviewChannel& ch = m_previewChannels[i];

            if (!ch.active || ch.volume <= 0)
                continue;

            anyActive = true;

            // Instrument/envelope is now actually used by the stream-player.
            // This makes Bass, Lead, Bell, Pluck, Noise etc. audibly different.
            double envMult = previewEnvelopeFactor(ch.instrumentEnv, ch.noteTime, 0.85);

            // Extra character per envelope type.
            switch (ch.instrumentEnv & 0x0F) {
            case 0x01: envMult *= 0.90; break; // pluck
            case 0x03: envMult *= 1.10; break; // bass sustain
            case 0x05: envMult *= 0.70; break; // soft pad
            case 0x06: envMult *= 1.00; break; // lead
            case 0x08: envMult *= 0.85; break; // bell
            case 0x09:
            case 0x0A:
            case 0x0B: envMult *= 1.15; break; // short noise/percussion
            default: break;
            }

            double amp = (static_cast<double>(ch.volume) / 15.0) * (i == 3 ? 360.0 : 700.0) * envMult;

            // Korte fade-in op nieuwe noot/instrument. Dit voorkomt klik/kraak op harde PSG-overgangen.
            if (ch.transitionSamples > 0) {
                const double fade = 1.0 - (static_cast<double>(ch.transitionSamples) / 96.0);
                amp *= qBound(0.0, fade, 1.0);
            }

            if (i < 3) {
                const double pitchMul = previewPitchMultiplier(ch.instrumentEnv, ch.phase);
                const double raw = (ch.phase < 0.5 ? amp : -amp);

                // Iets zachtere low-pass op square edges.
                ch.smoothSample += (raw - ch.smoothSample) * 0.22;
                mixed += ch.smoothSample;

                ch.phase += (ch.frequency * pitchMul) / 44100.0;
                while (ch.phase >= 1.0)
                    ch.phase -= 1.0;
            } else {
                ch.noisePhase += ch.frequency / 44100.0;

                if (ch.noisePhase >= 1.0) {
                    while (ch.noisePhase >= 1.0)
                        ch.noisePhase -= 1.0;

                    const bool bit = ((ch.noiseRng ^ (ch.noiseRng >> 1)) & 1u) != 0;
                    ch.noiseRng = (ch.noiseRng >> 1) | (bit ? 0x8000u : 0u);
                    ch.noiseValue = (ch.noiseRng & 1u) ? amp : -amp;
                }

                ch.smoothSample += (ch.noiseValue - ch.smoothSample) * 0.18;
                mixed += ch.smoothSample;
            }

            if (ch.transitionSamples > 0)
                --ch.transitionSamples;

            ch.noteTime += 1.0 / 44100.0;
        }

        // Manual key/cell preview auto-release. Song stream uses -1.
        if (!m_streamPlaying) {
            for (int i = 0; i < 4; ++i) {
                PreviewChannel& ch = m_previewChannels[i];

                if (ch.previewSamplesRemaining > 0) {
                    --ch.previewSamplesRemaining;

                    if (ch.previewSamplesRemaining <= 0) {
                        ch.active = false;
                        ch.volume = 0;
                        ch.psgPeriod = 0;
                        ch.frequency = 0.0;
                        ch.smoothSample *= 0.25;
                        setPreviewVuLevel(i, 0);
                        m_previewSilenceChunksRemaining = qMax(m_previewSilenceChunksRemaining, 1);
                    }
                }
            }
        }

        if (m_streamPlaying)
            --m_streamSamplesUntilNextRow;

        if (!anyActive && !m_streamPlaying)
            continue;

        // Eén limiter, niet dubbel. Iets lager plafond.
        mixed = std::tanh(mixed / 7000.0) * 7000.0;

        const int l = qBound(-32768, static_cast<int>(stereo[frame * 2 + 0]) + static_cast<int>(mixed), 32767);
        const int r = qBound(-32768, static_cast<int>(stereo[frame * 2 + 1]) + static_cast<int>(mixed), 32767);

        stereo[frame * 2 + 0] = static_cast<int16_t>(l);
        stereo[frame * 2 + 1] = static_cast<int16_t>(r);
    }
}



#if defined(Q_OS_WIN)
bool SoundManager::initialise(HWND hwnd, int fpsHint)
{
    if (m_inited) {
        qWarning() << "[SoundManager] initialise() called twice";
        return true;
    }
    if (!hwnd) {
        qWarning() << "[SoundManager] invalid HWND";
        return false;
    }

    if (!initDirectSound(hwnd, fpsHint)) {
        qWarning() << "[SoundManager] initDirectSound failed";
        releaseDirectSound();
        return false;
    }

    m_inited        = true;
    m_suspended     = false;
    m_running       = true;
    m_lastWritePos  = 0;

    qDebug() << "[SoundManager] initialise OK";
    return true;
}

bool SoundManager::reInitialise(HWND hwnd, int fpsHint)
{
    // Simpelste versie: volledig herstarten
    end();
    return initialise(hwnd, fpsHint);
}
#endif
#if defined(Q_OS_LINUX)
bool SoundManager::initialise(int fpsHint)
{
    if (m_inited) {
        qWarning() << "[SoundManager] initialise() called twice";
        return true;
    }

    if (!initALSA(fpsHint)) {
        qWarning() << "[SoundManager] initALSA failed";
        releaseALSA();
        return false;
    }

    m_inited        = true;
    m_suspended     = false;
    m_running       = true;
    m_lastWritePos  = 0;

    qDebug() << "[SoundManager] initialise OK";
    return true;
}

bool SoundManager::reInitialise(int fpsHint)
{
    // Simple version: fully restart
    end();
    return initialise(fpsHint);
}
#endif

void SoundManager::suspend()
{
    m_suspended = true;
}

void SoundManager::resume()
{
    m_suspended = false;
}

void SoundManager::end()
{
    if (m_previewVuDecayTimer)
        m_previewVuDecayTimer->stop();

    {
        QMutexLocker lock(&m_previewVuMutex);
        for (int i = 0; i < 4; ++i) {
            m_previewVuLevels[i] = 0;
            m_previewVuPendingLevels[i] = 0;
        }
        emitPreviewVuMetersLocked();
    }

    // Als alle kanalen stil zijn, buffer volledig leegmaken.
    // Anders blijft DirectSound de laatste ringbufferinhoud loopen: hoorbare tremolo na STOP.
    if (!hasActivePreview()) {
        clearPreviewStateAndBuffer();
        return;
    }

#if defined(Q_OS_WIN)
    releaseDirectSound();
#endif
#if defined(Q_OS_LINUX)
    releaseALSA();
#endif
    m_inited = false;
}

// ----------------------
// Private helpers
// ----------------------
#if defined(Q_OS_WIN)
bool SoundManager::initDirectSound(HWND hwnd, int fpsHint)
{
    m_channels       = 2;
    m_bitsPerSample  = 16;
    m_sampleRate     = 44100;
    m_bytesPerFrame  = (m_channels * m_bitsPerSample) / 8; // 4 bytes/stereo frame

    // 1. DirectSound
    HRESULT hr = DirectSoundCreate8(NULL, &m_ds, NULL);
    if (FAILED(hr) || !m_ds) {
        qWarning() << "[SoundManager] DirectSoundCreate8 failed hr=" << Qt::hex << hr;
        return false;
    }

    hr = m_ds->SetCooperativeLevel(hwnd, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        qWarning() << "[SoundManager] SetCooperativeLevel failed hr=" << Qt::hex << hr;
        return false;
    }

    // 2. WAVEFORMATEX
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = m_channels;
    wfx.nSamplesPerSec  = m_sampleRate;
    wfx.wBitsPerSample  = m_bitsPerSample;
    wfx.nBlockAlign     = (wfx.nChannels * wfx.wBitsPerSample) / 8; // 4
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;     // 44100 * 4
    wfx.cbSize          = 0;

    // 3. bufferlengte gelijk aan EmulTwo (2 chunks van 1/FPS)
    const int fps = (fpsHint > 0 ? fpsHint : 60);

    // size voor 1 frame audio in BYTES
    // (44.1k samples/sec * 4 bytes/stereo-frame) / 60 fps ≈ 2940 bytes
    DWORD bytesPerFrameOfEmu = (wfx.nBlockAlign * wfx.nSamplesPerSec) / fps;

    // force even
    bytesPerFrameOfEmu = (bytesPerFrameOfEmu & ~1u);

    // Preview/editor audio heeft meer veiligheidsbuffer nodig dan 2 chunks.
    // 16 chunks geeft extra reserve tegen korte QTimer/GUI haperingen.
    m_bufferBytes = bytesPerFrameOfEmu * 16;

    m_lastWritePos = 0;

    // 4. primary buffer (format zetten)
    if (!createPrimaryBuffer(wfx)) {
        qWarning() << "[SoundManager] createPrimaryBuffer failed";
        return false;
    }

    // 5. secondary buffer (onze ringbuffer)
    if (!createSecondaryBuffer(wfx)) {
        qWarning() << "[SoundManager] createSecondaryBuffer failed";
        return false;
    }

    // 6. start looping playback
    if (m_secondaryBuf) {
        hr = m_secondaryBuf->Play(0, 0, DSBPLAY_LOOPING);
        if (FAILED(hr)) {
            qWarning() << "[SoundManager] secondaryBuf->Play failed hr=" << Qt::hex << hr;
            return false;
        }
    }

    qDebug() << "[SoundManager] DirectSound init OK. bufferBytes=" << m_bufferBytes;
    return true;
}

void SoundManager::releaseDirectSoundPreviewBuffer()
{
    if (m_previewBuf) {
        m_previewBuf->Stop();
        m_previewBuf->Release();
        m_previewBuf = nullptr;
    }
}

static double previewEnvelopeFactor(int env, double t, double duration)
{
    if (duration <= 0.0)
        return 1.0;

    const double x = qBound(0.0, t / duration, 1.0);

    switch (env & 0x0F) {
    case 0x01:
        // Short pluck.
        return qMax(0.0, 1.0 - x * 1.45);

    case 0x02:
        // Mild decay.
        return 1.0 - (x * 0.35);

    case 0x03:
        // Bass / square sustain: strong attack, medium sustain.
        return (x < 0.04) ? (x / 0.04) : 0.82;

    case 0x04:
        // Brass stab: hard hit and fast fall.
        return qMax(0.0, 1.0 - x * 1.10);

    case 0x05:
        // Soft pad: slow attack, soft sustain.
        return (x < 0.25) ? (x / 0.25) * 0.70 : 0.70;

    case 0x06:
        // Lead synth: slight tremolo, long sustain.
        return 0.78 + 0.22 * std::sin(2.0 * M_PI * 6.0 * t);

    case 0x07:
        // Arp pluck: repeating pulse envelope.
        return (std::fmod(t * 12.0, 1.0) < 0.38) ? (1.0 - x * 0.45) : 0.18;

    case 0x08:
        // Bell: exponential-ish decay.
        return std::exp(-4.0 * x);

    case 0x09:
        // Perc click: extremely short.
        return (x < 0.10) ? (1.0 - x * 9.0) : 0.0;

    case 0x0A:
        // Snare: short noisy body.
        return qMax(0.0, 1.0 - x * 2.8);

    case 0x0B:
        // HiHat: very short bright decay.
        return qMax(0.0, 1.0 - x * 5.5);

    case 0x0C:
        // Explosion: slow noisy fade.
        return qMax(0.0, 1.0 - x * 0.85);

    case 0x0D:
        // PowerUp: rising pulse feeling.
        return 0.35 + 0.65 * x;

    default:
        return 1.0;
    }
}

static double previewPitchMultiplier(int env, double t)
{
    switch (env & 0x0F) {
    case 0x06:
        // Lead vibrato.
        return 1.0 + 0.010 * std::sin(2.0 * M_PI * 5.5 * t);

    case 0x07:
        // Arp feeling: major chord steps.
        switch (static_cast<int>(t * 12.0) % 3) {
        case 0: return 1.0;
        case 1: return 1.2599210499; // +4 semitones
        default: return 1.4983070769; // +7 semitones
        }

    case 0x08:
        // Bell slight detune wobble.
        return 1.0 + 0.006 * std::sin(2.0 * M_PI * 9.0 * t);

    case 0x0D:
        // Power up sweep.
        return 1.0 + qMin(0.65, t * 1.8);

    default:
        return 1.0;
    }
}

bool SoundManager::playDirectSoundPreviewSnapshot()
{
    if (!m_ds)
        return false;

    PreviewChannel channels[4];

    {
        QMutexLocker lock(&m_previewMutex);
        for (int i = 0; i < 4; ++i)
            channels[i] = m_previewChannels[i];
    }

    bool anyActive = false;
    for (const PreviewChannel& ch : channels) {
        if (ch.active && ch.volume > 0) {
            anyActive = true;
            break;
        }
    }

    releaseDirectSoundPreviewBuffer();

    if (!anyActive)
        return true;

    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = 2;
    wfx.nSamplesPerSec  = 44100;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = 4;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize          = 0;

    // One-shot preview buffer. Omdat dit buffer volledig vooraf gevuld wordt,
    // is er geen QTimer-jitter meer en dus geen tremolo/warble.
    const int previewMs = 900; // langer zodat held notes tussen tracker rows niet wegvallen
    const int frames = (wfx.nSamplesPerSec * previewMs) / 1000;
    const DWORD bufferBytes = frames * wfx.nBlockAlign;

    DSBUFFERDESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize        = sizeof(DSBUFFERDESC);
    desc.dwFlags       = DSBCAPS_GLOBALFOCUS;
    desc.dwBufferBytes = bufferBytes;
    desc.lpwfxFormat   = &wfx;

    LPDIRECTSOUNDBUFFER preview = nullptr;
    HRESULT hr = m_ds->CreateSoundBuffer(&desc, &preview, nullptr);
    if (FAILED(hr) || !preview) {
        qWarning() << "[SoundManager] preview CreateSoundBuffer failed hr=" << Qt::hex << hr;
        return false;
    }

    QVector<int16_t> pcm(frames * 2);
    std::fill(pcm.begin(), pcm.end(), 0);

    for (int frame = 0; frame < frames; ++frame) {
        double mixed = 0.0;

        // Kleine fade-in/out tegen clicks.
        double env = 1.0;
        const int fadeFrames = 96;
        if (frame < fadeFrames)
            env = static_cast<double>(frame) / fadeFrames;
        else if (frame > frames - fadeFrames)
            env = static_cast<double>(frames - frame) / fadeFrames;

        for (int i = 0; i < 4; ++i) {
            PreviewChannel& ch = channels[i];

            if (!ch.active || ch.volume <= 0)
                continue;

            const double instEnv = previewEnvelopeFactor(ch.instrumentEnv,
                                                         static_cast<double>(frame) / 44100.0,
                                                         static_cast<double>(frames) / 44100.0);
            const double amp = (static_cast<double>(ch.volume) / 15.0) * 2200.0 * env * instEnv;

            if (i < 3) {
                const double pitchMul = previewPitchMultiplier(ch.instrumentEnv,
                                                                 static_cast<double>(frame) / 44100.0);
                mixed += (ch.phase < 0.5 ? amp : -amp);
                ch.phase += (ch.frequency * pitchMul) / 44100.0;
                while (ch.phase >= 1.0)
                    ch.phase -= 1.0;
            } else {
                ch.noisePhase += ch.frequency / 44100.0;

                if (ch.noisePhase >= 1.0) {
                    while (ch.noisePhase >= 1.0)
                        ch.noisePhase -= 1.0;

                    const bool bit = ((ch.noiseRng ^ (ch.noiseRng >> 1)) & 1u) != 0;
                    ch.noiseRng = (ch.noiseRng >> 1) | (bit ? 0x8000u : 0u);
                    ch.noiseValue = (ch.noiseRng & 1u) ? amp : -amp;
                }

                // Noise is de grootste kraak-veroorzaker: licht smoothen.
                ch.smoothSample += (ch.noiseValue - ch.smoothSample) * 0.25;
                mixed += ch.smoothSample;
            }
        }

        const int sample = qBound(-32768, static_cast<int>(mixed), 32767);
        pcm[frame * 2 + 0] = static_cast<int16_t>(sample);
        pcm[frame * 2 + 1] = static_cast<int16_t>(sample);
    }

    void* p1 = nullptr;
    void* p2 = nullptr;
    DWORD b1 = 0;
    DWORD b2 = 0;

    hr = preview->Lock(0, bufferBytes, &p1, &b1, &p2, &b2, 0);
    if (FAILED(hr)) {
        qWarning() << "[SoundManager] preview Lock failed hr=" << Qt::hex << hr;
        preview->Release();
        return false;
    }

    if (p1 && b1)
        std::memcpy(p1, pcm.constData(), b1);

    if (p2 && b2)
        std::memcpy(p2, reinterpret_cast<const BYTE*>(pcm.constData()) + b1, b2);

    preview->Unlock(p1, b1, p2, b2);

    hr = preview->Play(0, 0, 0);
    if (FAILED(hr)) {
        qWarning() << "[SoundManager] preview Play failed hr=" << Qt::hex << hr;
        preview->Release();
        return false;
    }

    m_previewBuf = preview;
    return true;
}

void SoundManager::releaseDirectSound()
{
    releaseDirectSoundPreviewBuffer();

    if (m_secondaryBuf) {
        m_secondaryBuf->Stop();
        m_secondaryBuf->Release();
        m_secondaryBuf = nullptr;
    }
    if (m_primaryBuffer) {
        m_primaryBuffer->Release();
        m_primaryBuffer = nullptr;
    }
    if (m_ds) {
        m_ds->Release();
        m_ds = nullptr;
    }
}

bool SoundManager::createPrimaryBuffer(const WAVEFORMATEX &wfx)
{
    if (!m_ds) return false;

    DSBUFFERDESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize        = sizeof(DSBUFFERDESC);
    desc.dwFlags       = DSBCAPS_PRIMARYBUFFER;
    desc.dwBufferBytes = 0;
    desc.lpwfxFormat   = NULL;

    LPDIRECTSOUNDBUFFER primary = nullptr;
    HRESULT hr = m_ds->CreateSoundBuffer(&desc, &primary, NULL);
    if (FAILED(hr) || !primary) {
        qWarning() << "[SoundManager] CreateSoundBuffer(PRIMARY) failed hr=" << Qt::hex << hr;
        return false;
    }

    hr = primary->SetFormat(&wfx);
    if (FAILED(hr)) {
        qWarning() << "[SoundManager] primary->SetFormat failed hr=" << Qt::hex << hr;
        primary->Release();
        return false;
    }

    m_primaryBuffer = primary;
    return true;
}

bool SoundManager::createSecondaryBuffer(const WAVEFORMATEX &wfx)
{
    if (!m_ds) return false;

    DSBUFFERDESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize        = sizeof(DSBUFFERDESC);
    desc.dwFlags       = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
    desc.dwBufferBytes = m_bufferBytes;        // belangrijk!
    desc.lpwfxFormat   = (WAVEFORMATEX*)&wfx;

    LPDIRECTSOUNDBUFFER secondary = nullptr;
    HRESULT hr = m_ds->CreateSoundBuffer(&desc, &secondary, NULL);
    if (FAILED(hr) || !secondary) {
        qWarning() << "[SoundManager] CreateSoundBuffer(SECONDARY) failed hr=" << Qt::hex << hr;
        return false;
    }

    m_secondaryBuf = secondary;

    // Buffer initieel met stilte vullen
    void* p1 = nullptr; DWORD b1 = 0;
    void* p2 = nullptr; DWORD b2 = 0;

    hr = m_secondaryBuf->Lock(
        0,
        m_bufferBytes,
        &p1, &b1,
        &p2, &b2,
        0
        );
    if (FAILED(hr)) {
        qWarning() << "[SoundManager] secondary->Lock init failed hr=" << Qt::hex << hr;
        return false;
    }

    if (p1 && b1) std::memset(p1, 0, b1);
    if (p2 && b2) std::memset(p2, 0, b2);

    m_secondaryBuf->Unlock(p1, b1, p2, b2);

    return true;
}

// ----------------------
// Periodieke refill
// ----------------------

void SoundManager::refillSecondaryBuffer()
{
    if (!m_secondaryBuf) return;

    // Hoeveel bytes per vul-ronde?
    const DWORD chunkBytes = kChunkFrames * m_bytesPerFrame;
    if (chunkBytes == 0 || chunkBytes > m_bufferBytes)
        return;

    // Haal playback/write cursors op (kan nuttig zijn voor debugging/logica)
    DWORD playPos = 0;
    DWORD writePosDS = 0;
    HRESULT hr = m_secondaryBuf->GetCurrentPosition(&playPos, &writePosDS);
    if (FAILED(hr)) {
        // Als dit faalt (apparaat busy?), skip deze tick
        return;
    }


    // Bepaal de regio die we gaan vullen
    DWORD startPos = m_lastWritePos;
    DWORD endPos   = (m_lastWritePos + chunkBytes) % m_bufferBytes;

    // Vraag NIET meer live aan de emu-thread.
    // Pak gewoon de laatst bekende chunk.
    if (!m_suspended) {
        QMutexLocker lock(&m_audioMutex);
        if (m_lastAudioValid) {
            std::memcpy(m_mixBufferInterleaved,
                        m_lastAudioChunk,
                        chunkBytes);
        } else {
            std::memset(m_mixBufferInterleaved, 0, chunkBytes);
        }
    } else {
        std::memset(m_mixBufferInterleaved, 0, chunkBytes);
    }

    mixPreviewIntoBuffer(m_mixBufferInterleaved, kChunkFrames);

    // Lock en schrijf in de DS ringbuffer
    void* p1=nullptr; DWORD b1=0;
    void* p2=nullptr; DWORD b2=0;

    hr = m_secondaryBuf->Lock(
        startPos,
        chunkBytes,
        &p1, &b1,
        &p2, &b2,
        0
        );

    if (FAILED(hr)) {
        return;
    }

    if (p1 && b1) {
        std::memcpy(p1, m_mixBufferInterleaved, b1);
    }
    if (p2 && b2) {
        std::memcpy(
            p2,
            reinterpret_cast<const uint8_t*>(m_mixBufferInterleaved) + b1,
            b2
            );
    }

    m_secondaryBuf->Unlock(p1,b1,p2,b2);

    // schuif pointer vooruit
    m_lastWritePos = endPos;
}

// ----------------------
// Samples uit emu halen
// ----------------------

bool SoundManager::fetchSamplesFromEmu(int16_t *dst, int framesStereo)
{
    if (!m_controller)
        return false;

    // We roepen ColecoController::mixAudioInternal(void*,int)
    // in de emu-thread aan, blocking zodat het resultaat geldig blijft.
    bool ok = QMetaObject::invokeMethod(
        m_controller,
        "mixAudioInternal",
        Qt::BlockingQueuedConnection,
        Q_ARG(void*, (void*)dst),
        Q_ARG(int, framesStereo)
        );

    return ok;
}

void SoundManager::pushAudioFromEmu(const int16_t* srcStereo, int framesStereo)
{
    if (!m_secondaryBuf) return;
    if (framesStereo <= 0) return;

    QVector<int16_t> mixed(framesStereo * 2);
    std::memcpy(mixed.data(), srcStereo, framesStereo * m_bytesPerFrame);
    mixPreviewIntoBuffer(mixed.data(), framesStereo);

    // bereken bytes voor net DIT blok
    const DWORD blockBytes = framesStereo * m_bytesPerFrame; // frames * 4

    // Lock precies dat blok
    BYTE* p1 = nullptr;
    BYTE* p2 = nullptr;
    DWORD len1 = 0;
    DWORD len2 = 0;

    HRESULT hr = m_secondaryBuf->Lock(
        m_lastWritePos,
        blockBytes,
        (LPVOID*)&p1, &len1,
        (LPVOID*)&p2, &len2,
        0
        );
    if (FAILED(hr)) {
        qWarning() << "[Sound] Lock failed";
        return;
    }

    // Kopieer exact blockBytes
    std::memcpy(p1, mixed.constData(), len1);
    if (p2 && len2 > 0) {
        std::memcpy(p2,
                    reinterpret_cast<const BYTE*>(mixed.constData()) + len1,
                    len2);
    }

    m_secondaryBuf->Unlock(p1, len1, p2, len2);

    // schuif write cursor met blockBytes, niet met "full chunkBytes"
    m_lastWritePos = (m_lastWritePos + blockBytes) % m_bufferBytes;
}
#endif
#if defined(Q_OS_LINUX)
// ----------------------
// Private helpers
// ----------------------
bool SoundManager::initALSA(int fpsHint)
{
    int err;
    // Open PCM device
    err = snd_pcm_open(&m_pcmHandle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        qWarning() << "[SoundManager] Unable to open PCM device: " << snd_strerror(err);
        return false;
    }

    // Allocate hardware parameters object
    snd_pcm_hw_params_alloca(&m_params);

    // Initialize hardware parameters
    snd_pcm_hw_params_any(m_pcmHandle, m_params);
    snd_pcm_hw_params_set_channels(m_pcmHandle, m_params, m_channels);
    snd_pcm_hw_params_set_rate(m_pcmHandle, m_params, m_sampleRate, 0);
    snd_pcm_hw_params_set_format(m_pcmHandle, m_params, SND_PCM_FORMAT_S16_LE); // 16-bit signed little endian

    // Apply hardware parameters
    err = snd_pcm_hw_params(m_pcmHandle, m_params);
    if (err < 0) {
        qWarning() << "[SoundManager] Unable to set HW parameters: " << snd_strerror(err);
        return false;
    }

    // Get the buffer size
    snd_pcm_uframes_t buffer_size;
    snd_pcm_hw_params_get_buffer_size(m_params, &buffer_size);
    m_bufferBytes = buffer_size * m_bytesPerFrame;

    return true;
}

void SoundManager::releaseALSA()
{
    if (m_pcmHandle) {
        snd_pcm_close(m_pcmHandle);
        m_pcmHandle = nullptr;
    }
}

// ----------------------
// Periodic refill
// ----------------------

void SoundManager::refillPCMBuffer()
{
    if (!m_pcmHandle) return;

    const DWORD chunkBytes = kChunkFrames * m_bytesPerFrame;
    if (chunkBytes == 0 || chunkBytes > m_bufferBytes)
        return;

    // Determine where we should write in the buffer
    DWORD startPos = m_lastWritePos;
    DWORD endPos   = (m_lastWritePos + chunkBytes) % m_bufferBytes;

    // Get the latest audio chunk from the emulator
    if (!m_suspended) {
        QMutexLocker lock(&m_audioMutex);
        if (m_lastAudioValid) {
            std::memcpy(m_mixBufferInterleaved, m_lastAudioChunk, chunkBytes);
        } else {
            std::memset(m_mixBufferInterleaved, 0, chunkBytes);
        }
    } else {
        std::memset(m_mixBufferInterleaved, 0, chunkBytes);
    }

    mixPreviewIntoBuffer(m_mixBufferInterleaved, kChunkFrames);

    // Write audio data to the ALSA buffer
    int err = snd_pcm_writei(m_pcmHandle, m_mixBufferInterleaved, chunkBytes / m_bytesPerFrame);
    if (err < 0) {
        qWarning() << "[SoundManager] ALSA write error: " << snd_strerror(err);
    }

    // Update the last write position
    m_lastWritePos = endPos;
}

// ----------------------
// Fetch samples from emulator
// ----------------------

bool SoundManager::fetchSamplesFromEmu(int16_t *dst, int framesStereo)
{
    if (!m_controller)
        return false;

    bool ok = QMetaObject::invokeMethod(
        m_controller,
        "mixAudioInternal",
        Qt::BlockingQueuedConnection,
        Q_ARG(void*, (void*)dst),
        Q_ARG(int, framesStereo)
        );

    return ok;
}

void SoundManager::pushAudioFromEmu(const int16_t* srcStereo, int framesStereo)
{
    if (!m_pcmHandle) return;
    if (framesStereo <= 0) return;

    QVector<int16_t> mixed(framesStereo * 2);
    std::memcpy(mixed.data(), srcStereo, framesStereo * m_bytesPerFrame);
    mixPreviewIntoBuffer(mixed.data(), framesStereo);

    const DWORD blockBytes = framesStereo * m_bytesPerFrame; // frames * 4 bytes

    int err = snd_pcm_writei(m_pcmHandle, mixed.constData(), framesStereo);
    if (err < 0) {
        qWarning() << "[SoundManager] ALSA write failed: " << snd_strerror(err);
        return;
    }

    // Update write position
    m_lastWritePos = (m_lastWritePos + blockBytes) % m_bufferBytes;
}
#endif
