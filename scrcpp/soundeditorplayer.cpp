#include "soundeditorplayer.h"

#include <QDebug>
#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QAudioDevice>
#include <QtMath>
#include <algorithm>
#include <cstring>

SoundEditorPlayer::AudioDevice::AudioDevice(SoundEditorPlayer* player)
    : QIODevice(player)
    , m_player(player)
{
}

void SoundEditorPlayer::AudioDevice::start()
{
    // Hou het QIODevice open. Qt Multimedia kan na QAudioSink::stop()
    // nog kort readData() oproepen vanuit de backend-thread.
    // Als wij close() doen, krijg je "QIODevice::read: device not open"
    // en kunnen oude callbacks nog rommel veroorzaken.
    if (!isOpen())
        open(QIODevice::ReadOnly);

    m_renderingEnabled = true;
}

void SoundEditorPlayer::AudioDevice::stop()
{
    // Niet sluiten. Gewoon rendering uitzetten; readData() geeft dan stilte.
    m_renderingEnabled = false;
}

void SoundEditorPlayer::AudioDevice::setRenderingEnabled(bool enabled)
{
    if (!isOpen())
        open(QIODevice::ReadOnly);

    m_renderingEnabled = enabled;
}

qint64 SoundEditorPlayer::AudioDevice::readData(char* data, qint64 maxlen)
{
    if (!m_player || !data || maxlen <= 0)
        return 0;

    const qint64 frameBytes = 4; // stereo signed 16-bit
    const qint64 frames = maxlen / frameBytes;
    if (frames <= 0)
        return 0;

    std::memset(data, 0, static_cast<size_t>(maxlen));

    if (m_renderingEnabled)
        m_player->render(reinterpret_cast<int16_t*>(data), static_cast<int>(frames));

    // Altijd het gevraagde aantal bytes teruggeven.
    // Bij stop is dat dus stilte, geen gesloten device.
    return frames * frameBytes;
}

qint64 SoundEditorPlayer::AudioDevice::bytesAvailable() const
{
    return 4096 + QIODevice::bytesAvailable();
}

SoundEditorPlayer::SoundEditorPlayer(QObject* parent)
    : QObject(parent)
{
    // m_device wordt bewust per play-start nieuw gemaakt.
    // Zo kan geen enkele backend/QIODevice state van een vorige song blijven hangen.

    m_vuTimer = new QTimer(this);
    m_vuTimer->setInterval(45);
    m_vuTimer->setTimerType(Qt::PreciseTimer);
    connect(m_vuTimer, &QTimer::timeout, this, &SoundEditorPlayer::flushVu);
}

SoundEditorPlayer::~SoundEditorPlayer()
{
    stopSongStream();

    if (m_device) {
        m_device->close();
        delete m_device;
        m_device = nullptr;
    }
}

void SoundEditorPlayer::startSongStream(const QVariantList& rows, int rowMs, bool loop)
{
    qDebug().noquote() << "\n========== [ADAMP PLAYER] startSongStream BEGIN ==========";
    qDebug().noquote() << "[ADAMP PLAYER] incoming rows=" << rows.size()
                       << "rowMs=" << rowMs
                       << "loop=" << loop;

    for (int i = 0; i < qMin(12, rows.size()); ++i)
        qDebug().noquote() << "[ADAMP PLAYER] INPUTROW" << i << rows.at(i).toList();

    QVector<StreamRow> parsed;
    parsed.reserve(rows.size());

    for (const QVariant& v : rows) {
        const QVariantList rowList = v.toList();
        if (rowList.size() < 8)
            continue;

        StreamRow row;

        if (rowList.size() >= 20) {
            for (int ch = 0; ch < 4; ++ch) {
                row.period[ch] = rowList.value(ch * 5 + 0).toInt();
                row.volume[ch] = rowList.value(ch * 5 + 1).toInt();

                bool envOk = false;
                int envValue = rowList.value(ch * 5 + 2).toInt(&envOk);
                if (!envOk)
                    envValue = 3;
                row.env[ch] = qBound(0, envValue, 15);
                row.waveX[ch] = qBound(0, rowList.value(ch * 5 + 3).toInt(), 100);
                row.waveY[ch] = qBound(0, rowList.value(ch * 5 + 4).toInt(), 100);
            }
        } else if (rowList.size() >= 12) {
            for (int ch = 0; ch < 4; ++ch) {
                row.period[ch] = rowList.value(ch * 3 + 0).toInt();
                row.volume[ch] = rowList.value(ch * 3 + 1).toInt();

                bool envOk = false;
                int envValue = rowList.value(ch * 3 + 2).toInt(&envOk);
                if (!envOk)
                    envValue = 3;
                row.env[ch] = qBound(0, envValue, 15);
                row.waveX[ch] = 50;
                row.waveY[ch] = 50;
            }
        } else {
            // Backwards compatible with older 8-int stream rows.
            for (int ch = 0; ch < 4; ++ch) {
                row.period[ch] = rowList.value(ch * 2 + 0).toInt();
                row.volume[ch] = rowList.value(ch * 2 + 1).toInt();
                row.env[ch] = 3;
                row.waveX[ch] = 50;
                row.waveY[ch] = 50;
            }
        }

        parsed.append(row);
    }

    stopSongStream();
    ++m_generation;

    if (parsed.isEmpty()) {
        qDebug().noquote() << "[ADAMP PLAYER] parsed rows empty, abort start";
        qDebug().noquote() << "========== [ADAMP PLAYER] startSongStream END(empty) ==========\n";
        return;
    }

    qDebug().noquote() << "[ADAMP PLAYER] parsed rows=" << parsed.size();

    for (int i = 0; i < qMin(12, parsed.size()); ++i) {
        const StreamRow& r = parsed.at(i);
        qDebug().noquote()
            << "[ADAMP PLAYER] PARSEDROW" << i
            << "CH1" << r.period[0] << r.volume[0] << r.env[0]
            << "CH2" << r.period[1] << r.volume[1] << r.env[1]
            << "CH3" << r.period[2] << r.volume[2] << r.env[2]
            << "NOISE" << r.period[3] << r.volume[3] << r.env[3];
    }

    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (!device.isFormatSupported(format)) {
        qWarning() << "[SoundEditorPlayer] Requested audio format not supported, using nearest/default.";
        // Qt6 usually supports this format; keep it anyway because most backends accept it.
    }

    {
        QMutexLocker lock(&m_mutex);

        m_rows = parsed;
        m_loop = loop;
        m_rowIndex = 0;
        m_samplesUntilNextRow = 0;
        m_samplesPerRow = qMax(1, qRound((44100.0 * qMax(1, rowMs)) / 1000.0));
        m_sampleRate = 44100;
        m_playing = true;
        m_debugAppliedRows = 0;
        clearStateLocked();

        for (int i = 0; i < 4; ++i) {
            m_vuLevels[i] = 0;
            m_pendingVuLevels[i] = 0;
        }
    }

    // V9: volledige nieuwe QAudioSink + AudioDevice per song-start.
    // Geen enkele QIODevice/backend-state wordt hergebruikt tussen songs.
    m_device = new AudioDevice(this);
    m_sink = new QAudioSink(device, format, this);

    // Niet te groot: een grote buffer kan oude song-audio hoorbaar laten nalopen.
    // 8192 bytes is ruim genoeg voor QAudioSink, maar veel sneller leeg/resetbaar.
    m_sink->setBufferSize(8192);

    m_device->start();
    m_device->setRenderingEnabled(true);
    m_sink->start(m_device);

    if (m_vuTimer && !m_vuTimer->isActive())
        m_vuTimer->start();

    qDebug().noquote() << "[ADAMP PLAYER] QAudioSink started"
                         << "newAudioDevice=" << static_cast<void*>(m_device)
                         << "rows=" << parsed.size()
                         << "rowMs=" << rowMs
                         << "samplesPerRow=" << m_samplesPerRow
                         << "loop=" << loop
                         << "bufferSize=" << (m_sink ? m_sink->bufferSize() : -1);
    qDebug().noquote() << "========== [ADAMP PLAYER] startSongStream END ==========\n";
}

void SoundEditorPlayer::stopSongStream()
{
    qDebug().noquote() << "[ADAMP PLAYER] stopSongStream generation=" << m_generation
                       << "rows=" << m_rows.size()
                       << "playing=" << m_playing;

    ++m_generation;

    // Eerst intern op stil zetten, zodat eventuele late backend reads stilte krijgen.
    {
        QMutexLocker lock(&m_mutex);
        m_playing = false;
        m_loop = false;
    }

    if (m_device)
        m_device->setRenderingEnabled(false);

    if (m_sink) {
        // reset() gooit de backend-buffer weg.
        // stop() kan reeds gebufferde oude audio laten uitlekken.
        m_sink->reset();
        delete m_sink;
        m_sink = nullptr;
    }

    // V9: het QIODevice wordt nu ook volledig vernietigd bij stop.
    // We maken bij de volgende Play een volledig nieuw AudioDevice.
    // Dit is strenger dan V7/V8 en voorkomt dat backend/device state
    // tussen songs blijft hangen.
    if (m_device) {
        m_device->close();
        delete m_device;
        m_device = nullptr;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_playing = false;
        m_loop = false;
        m_rows.clear();
        m_rowIndex = 0;
        m_samplesUntilNextRow = 0;
        clearStateLocked();

        for (int i = 0; i < 4; ++i) {
            m_vuLevels[i] = 0;
            m_pendingVuLevels[i] = 0;
        }
    }

    emit previewVuMetersChanged(0, 0, 0, 0);
    emit previewVuMeterChanged(0, 0);
    emit previewVuMeterChanged(1, 0);
    emit previewVuMeterChanged(2, 0);
    emit previewVuMeterChanged(3, 0);

    if (m_vuTimer)
        m_vuTimer->stop();
}

void SoundEditorPlayer::hardReset()
{
    stopSongStream();

    {
        QMutexLocker lock(&m_mutex);
        m_rows.squeeze();
        clearStateLocked();

        for (int i = 0; i < 4; ++i) {
            m_vuLevels[i] = 0;
            m_pendingVuLevels[i] = 0;
        }
    }

    emit previewVuMetersChanged(0, 0, 0, 0);
}

void SoundEditorPlayer::clearStateLocked()
{
    for (ChannelState& ch : m_channels) {
        ch.active = false;
        ch.period = 0;
        ch.volume = 0;
        ch.env = 3;
        ch.frequency = 0.0;
        ch.phase = 0.0;
        ch.noteTime = 0.0;
        ch.smoothSample = 0.0;
        ch.duty = 0.50;
        ch.waveX = 50;
        ch.waveY = 50;
        ch.noiseCode = 0;
        ch.noiseRng = 0xACE1u;
        ch.noisePhase = 0.0;
        ch.noiseValue = 0.0;
        ch.transitionSamples = 0;
    }
}

void SoundEditorPlayer::applyNextRowLocked()
{
    if (!m_playing)
        return;

    if (m_rowIndex >= m_rows.size()) {
        if (m_loop && !m_rows.isEmpty()) {
            qDebug().noquote() << "[ADAMP PLAYER] LOOP back to row 0";
            m_rowIndex = 0;
        } else {
            qDebug().noquote() << "[ADAMP PLAYER] END stream";
            m_playing = false;
            clearStateLocked();
            for (int ch = 0; ch < 4; ++ch)
                setPendingVuLevelLocked(ch, 0);
            return;
        }
    }

    if (!m_playing || m_rowIndex >= m_rows.size())
        return;

    const int appliedIndex = m_rowIndex;
    const StreamRow row = m_rows.at(m_rowIndex++);

    if (m_debugAppliedRows < 24) {
        qDebug().noquote()
            << "[ADAMP PLAYER] APPLYROW" << appliedIndex
            << "CH1" << row.period[0] << row.volume[0] << row.env[0]
            << "CH2" << row.period[1] << row.volume[1] << row.env[1]
            << "CH3" << row.period[2] << row.volume[2] << row.env[2]
            << "NOISE" << row.period[3] << row.volume[3] << row.env[3];
        ++m_debugAppliedRows;
    }

    for (int ch = 0; ch < 4; ++ch) {
        const int p = row.period[ch];
        const int v = row.volume[ch];
        const int e = qBound(0, row.env[ch], 15);
        const int wx = qBound(0, row.waveX[ch], 100);
        const int wy = qBound(0, row.waveY[ch], 100);

        if (p < 0) {
            if (m_channels[ch].active && m_channels[ch].volume > 0)
                setPendingVuLevelLocked(ch, m_channels[ch].volume);
            continue;
        }

        ChannelState& cs = m_channels[ch];

        if (p == 0 || v <= 0) {
            cs.active = false;
            cs.period = 0;
            cs.volume = 0;
            cs.frequency = 0.0;
            cs.smoothSample *= 0.25;
            cs.transitionSamples = 0;
            setPendingVuLevelLocked(ch, 0);
            continue;
        }

        const bool changed = (!cs.active || cs.period != p || cs.volume != v || cs.env != e || cs.waveX != wx || cs.waveY != wy);

        cs.active = true;
        cs.period = p;
        cs.volume = qBound(0, v, 15);
        cs.env = e;
        cs.waveX = wx;
        cs.waveY = wy;

        if (changed) {
            cs.noteTime = 0.0;
            cs.transitionSamples = 64;
        }

        if (ch < 3) {
            const double f = 3579545.0 / (32.0 * qMax(1, p));
            cs.frequency = qBound(20.0, f, 20000.0);
            cs.duty = dutyForEnvelope(cs.env);
        } else {
            cs.noiseCode = qBound(0, p, 7);
            cs.frequency = 180.0 + cs.noiseCode * 100.0 + (cs.waveX * 2.2);
        }

        setPendingVuLevelLocked(ch, cs.volume);
    }

    m_samplesUntilNextRow += m_samplesPerRow;
}

void SoundEditorPlayer::render(int16_t* stereo, int frames)
{
    if (!stereo || frames <= 0)
        return;

    QMutexLocker lock(&m_mutex);

    for (int frame = 0; frame < frames; ++frame) {
        if (m_playing && m_samplesUntilNextRow <= 0)
            applyNextRowLocked();

        double mixed = 0.0;

        for (int i = 0; i < 4; ++i) {
            ChannelState& ch = m_channels[i];

            if (!ch.active || ch.volume <= 0)
                continue;

            const int env = ch.env & 0x0F;

            // V10: duidelijkere instrumenten, maar met loudness-normalisatie.
            // Geen instrument mag plots veel stiller zijn enkel door envelope/timbre.
            double envMult = envelopeFactor(env, ch.noteTime, 1.00);
            double loudness = loudnessForEnvelope(env);

            double amp = (static_cast<double>(ch.volume) / 15.0)
                       * (i == 3 ? 420.0 : 760.0)
                       * envMult
                       * loudness;

            if (ch.transitionSamples > 0) {
                const double fade = 1.0 - (static_cast<double>(ch.transitionSamples) / 64.0);
                amp *= qBound(0.0, fade, 1.0);
                --ch.transitionSamples;
            }

            if (i < 3) {
                const double padX = qBound(0, ch.waveX, 100) / 100.0;
                const double padY = qBound(0, ch.waveY, 100) / 100.0;
                const double duty = qBound(0.18, dutyForEnvelope(env) + (padX - 0.5) * 0.20, 0.78);
                const double brightness = 0.75 + padY * 0.45;
                const double raw = (ch.phase < duty ? amp * brightness : -amp);

                double shaped = raw;

                switch (env) {
                case 0x01: // Pluck
                    shaped *= 1.08;
                    break;

                case 0x03: // Bass
                    shaped += (ch.phase < 0.50 ? amp * 0.16 : -amp * 0.16);
                    break;

                case 0x04: // Brass/Stab
                    if (ch.noteTime < 0.055)
                        shaped *= 1.22;
                    break;

                case 0x05: // Pad
                    shaped *= 0.92;
                    break;

                case 0x06: // Lead
                    shaped *= 1.08;
                    break;

                case 0x07: // Arp/Pluck
                    if (std::fmod(ch.noteTime * 8.0, 1.0) > 0.55)
                        shaped *= 0.72;
                    break;

                case 0x08: // Bell
                    shaped *= 0.95;
                    break;

                default:
                    break;
                }

                const double smooth = qBound(0.16, smoothingForEnvelope(env, false) + ((ch.waveY - 50) / 100.0) * 0.10, 0.48);
                ch.smoothSample += (shaped - ch.smoothSample) * smooth;
                mixed += ch.smoothSample;

                const double pitchMul = pitchMultiplier(env, ch.noteTime);
                ch.phase += (ch.frequency * pitchMul) / static_cast<double>(m_sampleRate);
                while (ch.phase >= 1.0)
                    ch.phase -= 1.0;
            } else {
                ch.noisePhase += ch.frequency / static_cast<double>(m_sampleRate);

                if (ch.noisePhase >= 1.0) {
                    while (ch.noisePhase >= 1.0)
                        ch.noisePhase -= 1.0;

                    const bool bit = ((ch.noiseRng ^ (ch.noiseRng >> 1)) & 1u) != 0;
                    ch.noiseRng = (ch.noiseRng >> 1) | (bit ? 0x8000u : 0u);
                    ch.noiseValue = (ch.noiseRng & 1u) ? amp : -amp;
                }

                const double smooth = smoothingForEnvelope(env, true);
                ch.smoothSample += (ch.noiseValue - ch.smoothSample) * smooth;
                mixed += ch.smoothSample;
            }

            ch.noteTime += 1.0 / static_cast<double>(m_sampleRate);
        }

        if (m_playing)
            --m_samplesUntilNextRow;

        mixed = std::tanh(mixed / 8200.0) * 8200.0;

        const int l = qBound(-32768, static_cast<int>(stereo[frame * 2 + 0]) + static_cast<int>(mixed), 32767);
        const int r = qBound(-32768, static_cast<int>(stereo[frame * 2 + 1]) + static_cast<int>(mixed), 32767);

        stereo[frame * 2 + 0] = static_cast<int16_t>(l);
        stereo[frame * 2 + 1] = static_cast<int16_t>(r);
    }
}

double SoundEditorPlayer::envelopeFactor(int env, double t, double duration)
{
    if (duration <= 0.0)
        return 1.0;

    const double x = qBound(0.0, t / duration, 1.0);

    switch (env & 0x0F) {
    case 0x01: return qMax(0.52, 1.0 - x * 0.82);      // Pluck
    case 0x02: return qMax(0.78, 1.0 - x * 0.20);      // Mild decay
    case 0x03: return (x < 0.018) ? (x / 0.018) : 0.98; // Bass
    case 0x04: return qMax(0.66, 1.0 - x * 0.42);      // Brass/Stab
    case 0x05: return (x < 0.14) ? (x / 0.14) * 0.86 : 0.86; // Pad
    case 0x06: return 0.94 + 0.06 * std::sin(2.0 * M_PI * 5.0 * t); // Lead
    case 0x07: return qMax(0.58, 1.0 - x * 0.50);      // Arp/Pluck
    case 0x08: return qMax(0.44, std::exp(-1.15 * x)); // Bell
    case 0x09: return (x < 0.14) ? qMax(0.0, 1.0 - x * 6.0) : 0.0;
    case 0x0A: return qMax(0.0, 1.0 - x * 1.9);
    case 0x0B: return qMax(0.0, 1.0 - x * 4.0);
    case 0x0C: return qMax(0.0, 1.0 - x * 0.68);
    case 0x0D: return qBound(0.45, 0.45 + 0.65 * x, 1.10);
    default: return 1.0;
    }
}

double SoundEditorPlayer::pitchMultiplier(int env, double t)
{
    switch (env & 0x0F) {
    case 0x06:
        return 1.0 + 0.0035 * std::sin(2.0 * M_PI * 5.0 * t);

    case 0x0D:
        return 1.0 + qMin(0.18, t * 0.55);

    default:
        return 1.0;
    }
}

double SoundEditorPlayer::dutyForEnvelope(int env)
{
    switch (env & 0x0F) {
    case 0x01: return 0.37;
    case 0x03: return 0.50;
    case 0x04: return 0.42;
    case 0x05: return 0.58;
    case 0x06: return 0.47;
    case 0x07: return 0.33;
    case 0x08: return 0.30;
    default:   return 0.50;
    }
}

double SoundEditorPlayer::loudnessForEnvelope(int env)
{
    switch (env & 0x0F) {
    case 0x01: return 1.12;
    case 0x02: return 1.04;
    case 0x03: return 1.15;
    case 0x04: return 1.10;
    case 0x05: return 1.18;
    case 0x06: return 1.08;
    case 0x07: return 1.22;
    case 0x08: return 1.28;
    case 0x09: return 1.10;
    case 0x0A: return 1.12;
    case 0x0B: return 1.16;
    case 0x0C: return 1.05;
    case 0x0D: return 1.08;
    default:   return 1.00;
    }
}

double SoundEditorPlayer::smoothingForEnvelope(int env, bool noise)
{
    if (noise)
        return 0.22;

    switch (env & 0x0F) {
    case 0x03: return 0.24;
    case 0x05: return 0.20;
    case 0x08: return 0.34;
    case 0x01:
    case 0x07: return 0.36;
    default:   return 0.30;
    }
}

void SoundEditorPlayer::setPendingVuLevelLocked(int channel, int level)
{
    if (channel < 0 || channel >= 4)
        return;

    m_pendingVuLevels[channel] = qMax(m_pendingVuLevels[channel], qBound(0, level, 15));
}

void SoundEditorPlayer::flushVu()
{
    int out[4] = {0, 0, 0, 0};
    bool anyActive = false;

    {
        QMutexLocker lock(&m_mutex);

        for (int i = 0; i < 4; ++i) {
            if (m_pendingVuLevels[i] > m_vuLevels[i])
                m_vuLevels[i] = m_pendingVuLevels[i];
            else if (m_vuLevels[i] > 0)
                m_vuLevels[i] = qMax(0, m_vuLevels[i] - 1);

            m_pendingVuLevels[i] = 0;
            out[i] = m_vuLevels[i];

            if (m_vuLevels[i] > 0)
                anyActive = true;
        }
    }

    emit previewVuMeterChanged(0, out[0]);
    emit previewVuMeterChanged(1, out[1]);
    emit previewVuMeterChanged(2, out[2]);
    emit previewVuMeterChanged(3, out[3]);
    emit previewVuMetersChanged(out[0], out[1], out[2], out[3]);

    if (!anyActive && !m_playing && m_vuTimer)
        m_vuTimer->stop();
}
