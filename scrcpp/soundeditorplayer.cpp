#include "soundeditorplayer.h"

#include <QDebug>
#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QAudioDevice>
#include <QtMath>
#include <algorithm>
#include <cstring>
#include <QStringList>

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

void SoundEditorPlayer::setChannelAudible(int channel, bool audible)
{
    channel = qBound(0, channel, 6);
    QMutexLocker lock(&m_mutex);
    m_channelAudible[channel] = audible;

    if (!audible) {
        m_pendingVuLevels[channel] = 0;
        m_vuLevels[channel] = 0;
    } else if (m_channels[channel].active) {
        setPendingVuLevelLocked(channel, qBound(0, m_channels[channel].volume, 15));
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

        // V8.45 fixed ROM-parity stream. Never infer this format from row length:
        // muting a channel must not alter how following channels are decoded.
        if (rowList.size() >= 112 && rowList.value(0).toInt() == 841) {
            row.finalRenderedFrame = true;
            int off = 1;

            // V8.58: marker 841 rows are FINAL PSG register frames.
            // No Furnace/editor macro may be interpreted again by SoundEditorPlayer.

            for (int ch = 0; ch < 4; ++ch) {
                const int b = off + ch * 9;
                row.period[ch] = rowList.value(b + 0).toInt();
                row.volume[ch] = rowList.value(b + 1).toInt();
                row.env[ch] = qBound(0, rowList.value(b + 2).toInt(), 15);
                row.waveX[ch] = qBound(0, rowList.value(b + 3).toInt(), 100);
                row.waveY[ch] = qBound(0, rowList.value(b + 4).toInt(), 100);
                row.snVolumeMacro[ch].clear();
                row.snPitchMacro[ch].clear();
                row.snArpMacro[ch].clear();
                row.snNoiseMacro[ch].clear();
            }

            int b = off + 36;
            for (int ch = 4; ch < 7; ++ch, b += 25) {
                row.period[ch] = rowList.value(b + 0).toInt();
                row.volume[ch] = rowList.value(b + 1).toInt();
                row.env[ch] = qBound(0, rowList.value(b + 2).toInt(), 15);
                row.waveX[ch] = qBound(0, rowList.value(b + 3).toInt(), 100);
                row.waveY[ch] = qBound(0, rowList.value(b + 4).toInt(), 100);
                row.ayTone[ch] = rowList.value(b + 5).toInt() ? 1 : 0;
                row.ayNoise[ch] = rowList.value(b + 6).toInt() ? 1 : 0;
                row.ayHwEnv[ch] = rowList.value(b + 7).toInt() ? 1 : 0;
                row.ayShape[ch] = qBound(0, rowList.value(b + 8).toInt(), 15);
                row.ayEnvPeriod[ch] = qBound(1, rowList.value(b + 9).toInt(), 0xFFFF);
                row.ayNoisePeriod[ch] = qBound(0, rowList.value(b + 10).toInt(), 31);
                row.ayAttack[ch] = qBound(0, rowList.value(b + 11).toInt(), 15);
                row.ayDecay[ch] = qBound(0, rowList.value(b + 12).toInt(), 15);
                row.aySustain[ch] = qBound(0, rowList.value(b + 13).toInt(), 15);
                row.ayRelease[ch] = qBound(0, rowList.value(b + 14).toInt(), 15);
                row.ayVibrato[ch] = qBound(0, rowList.value(b + 15).toInt(), 15);
                row.ayArp[ch].clear();
                row.ayVolumeMacro[ch].clear();
                row.ayPitchMacro[ch].clear();
                row.ayNoiseMacro[ch].clear();
                row.ayAutoEnv[ch].clear();
                row.ayWaveMacro[ch].clear();
                row.ayEnvShapeMacro[ch].clear();
                row.ayEnvPeriodMacro[ch].clear();
                row.ayPhaseResetMacro[ch] = rowList.value(b + 24).toString();
            }
        } else if (rowList.size() >= 111) {
            // V8.18 Furnace-style stream: 4 SN channels x 9 fields, then 3 AY channels x 25 fields.
            for (int ch = 0; ch < 4; ++ch) {
                const int b = ch * 9;
                row.period[ch] = rowList.value(b + 0).toInt();
                row.volume[ch] = rowList.value(b + 1).toInt();
                row.env[ch] = qBound(0, rowList.value(b + 2).toInt(), 15);
                row.waveX[ch] = qBound(0, rowList.value(b + 3).toInt(), 100);
                row.waveY[ch] = qBound(0, rowList.value(b + 4).toInt(), 100);
                row.snVolumeMacro[ch] = rowList.value(b + 5).toString();
                row.snPitchMacro[ch] = rowList.value(b + 6).toString();
                row.snArpMacro[ch] = rowList.value(b + 7).toString();
                row.snNoiseMacro[ch] = rowList.value(b + 8).toString();
            }
            int b = 36;
            for (int ch = 4; ch < 7; ++ch, b += 25) {
                row.period[ch] = rowList.value(b + 0).toInt();
                row.volume[ch] = rowList.value(b + 1).toInt();
                row.env[ch] = qBound(0, rowList.value(b + 2).toInt(), 15);
                row.waveX[ch] = qBound(0, rowList.value(b + 3).toInt(), 100);
                row.waveY[ch] = qBound(0, rowList.value(b + 4).toInt(), 100);
                row.ayTone[ch] = rowList.value(b + 5).toInt() ? 1 : 0;
                row.ayNoise[ch] = rowList.value(b + 6).toInt() ? 1 : 0;
                row.ayHwEnv[ch] = rowList.value(b + 7).toInt() ? 1 : 0;
                row.ayShape[ch] = qBound(0, rowList.value(b + 8).toInt(), 15);
                row.ayEnvPeriod[ch] = qBound(1, rowList.value(b + 9).toInt(), 0xFFFF);
                row.ayNoisePeriod[ch] = qBound(0, rowList.value(b + 10).toInt(), 31);
                row.ayAttack[ch] = qBound(0, rowList.value(b + 11).toInt(), 15);
                row.ayDecay[ch] = qBound(0, rowList.value(b + 12).toInt(), 15);
                row.aySustain[ch] = qBound(0, rowList.value(b + 13).toInt(), 15);
                row.ayRelease[ch] = qBound(0, rowList.value(b + 14).toInt(), 15);
                row.ayVibrato[ch] = qBound(0, rowList.value(b + 15).toInt(), 15);
                row.ayArp[ch] = rowList.value(b + 16).toString();
                row.ayVolumeMacro[ch] = rowList.value(b + 17).toString();
                row.ayPitchMacro[ch] = rowList.value(b + 18).toString();
                row.ayNoiseMacro[ch] = rowList.value(b + 19).toString();
                row.ayAutoEnv[ch] = rowList.value(b + 20).toString();
                row.ayWaveMacro[ch] = rowList.value(b + 21).toString();
                row.ayEnvShapeMacro[ch] = rowList.value(b + 22).toString();
                row.ayEnvPeriodMacro[ch] = rowList.value(b + 23).toString();
                row.ayPhaseResetMacro[ch] = rowList.value(b + 24).toString();
            }
        } else if (rowList.size() >= 99) {
            // V8.17 backward-compatible stream: 4 SN x 9 + 3 AY x 21.
            for (int ch = 0; ch < 4; ++ch) {
                const int b = ch * 9;
                row.period[ch]=rowList.value(b+0).toInt(); row.volume[ch]=rowList.value(b+1).toInt();
                row.env[ch]=qBound(0,rowList.value(b+2).toInt(),15); row.waveX[ch]=qBound(0,rowList.value(b+3).toInt(),100); row.waveY[ch]=qBound(0,rowList.value(b+4).toInt(),100);
                row.snVolumeMacro[ch]=rowList.value(b+5).toString(); row.snPitchMacro[ch]=rowList.value(b+6).toString(); row.snArpMacro[ch]=rowList.value(b+7).toString(); row.snNoiseMacro[ch]=rowList.value(b+8).toString();
            }
            int b=36;
            for (int ch=4; ch<7; ++ch,b+=21) {
                row.period[ch]=rowList.value(b+0).toInt(); row.volume[ch]=rowList.value(b+1).toInt(); row.env[ch]=qBound(0,rowList.value(b+2).toInt(),15); row.waveX[ch]=qBound(0,rowList.value(b+3).toInt(),100); row.waveY[ch]=qBound(0,rowList.value(b+4).toInt(),100);
                row.ayTone[ch]=rowList.value(b+5).toInt()?1:0; row.ayNoise[ch]=rowList.value(b+6).toInt()?1:0; row.ayHwEnv[ch]=rowList.value(b+7).toInt()?1:0; row.ayShape[ch]=qBound(0,rowList.value(b+8).toInt(),15); row.ayEnvPeriod[ch]=qBound(1,rowList.value(b+9).toInt(),0xFFFF); row.ayNoisePeriod[ch]=qBound(0,rowList.value(b+10).toInt(),31);
                row.ayAttack[ch]=qBound(0,rowList.value(b+11).toInt(),15); row.ayDecay[ch]=qBound(0,rowList.value(b+12).toInt(),15); row.aySustain[ch]=qBound(0,rowList.value(b+13).toInt(),15); row.ayRelease[ch]=qBound(0,rowList.value(b+14).toInt(),15); row.ayVibrato[ch]=qBound(0,rowList.value(b+15).toInt(),15); row.ayArp[ch]=rowList.value(b+16).toString();
                row.ayVolumeMacro[ch]=rowList.value(b+17).toString(); row.ayPitchMacro[ch]=rowList.value(b+18).toString(); row.ayNoiseMacro[ch]=rowList.value(b+19).toString(); row.ayAutoEnv[ch]=rowList.value(b+20).toString();
            }
        } else if (rowList.size() >= 71) {
            // V4 SGM stream: 4 SN channels x 5 fields, followed by
            // 3 AY channels x 17 fields (5 base + 12 synthesis parameters).
            for (int ch = 0; ch < 4; ++ch) {
                const int b = ch * 5;
                row.period[ch] = rowList.value(b + 0).toInt();
                row.volume[ch] = rowList.value(b + 1).toInt();
                row.env[ch] = qBound(0, rowList.value(b + 2).toInt(), 15);
                row.waveX[ch] = qBound(0, rowList.value(b + 3).toInt(), 100);
                row.waveY[ch] = qBound(0, rowList.value(b + 4).toInt(), 100);
            }
            int b = 20;
            for (int ch = 4; ch < 7; ++ch, b += 17) {
                row.period[ch] = rowList.value(b + 0).toInt();
                row.volume[ch] = rowList.value(b + 1).toInt();
                row.env[ch] = qBound(0, rowList.value(b + 2).toInt(), 15);
                row.waveX[ch] = qBound(0, rowList.value(b + 3).toInt(), 100);
                row.waveY[ch] = qBound(0, rowList.value(b + 4).toInt(), 100);
                row.ayTone[ch] = rowList.value(b + 5).toInt() ? 1 : 0;
                row.ayNoise[ch] = rowList.value(b + 6).toInt() ? 1 : 0;
                row.ayHwEnv[ch] = rowList.value(b + 7).toInt() ? 1 : 0;
                row.ayShape[ch] = qBound(0, rowList.value(b + 8).toInt(), 15);
                row.ayEnvPeriod[ch] = qBound(1, rowList.value(b + 9).toInt(), 0xFFFF);
                row.ayNoisePeriod[ch] = qBound(0, rowList.value(b + 10).toInt(), 31);
                row.ayAttack[ch] = qBound(0, rowList.value(b + 11).toInt(), 15);
                row.ayDecay[ch] = qBound(0, rowList.value(b + 12).toInt(), 15);
                row.aySustain[ch] = qBound(0, rowList.value(b + 13).toInt(), 15);
                row.ayRelease[ch] = qBound(0, rowList.value(b + 14).toInt(), 15);
                row.ayVibrato[ch] = qBound(0, rowList.value(b + 15).toInt(), 15);
                row.ayArp[ch] = rowList.value(b + 16).toString();
            }
        } else if (rowList.size() == 36) {
            // V8.57: SN-only legacy stream from buildLegacySoundEditorStreamRows().
            // Four SN channels x 9 fields = exactly 36 fields.
            //
            // IMPORTANT: this must be checked BEFORE the historical >=35
            // 7-channels-x-5-fields parser.  Without this branch, a normal
            // non-SGM song was decoded as seven 5-field channels and the tail
            // of CH2/CH3/Noise became bogus AY channel data, causing the
            // continuous high-pitched tone.
            for (int ch = 0; ch < 4; ++ch) {
                const int b = ch * 9;
                row.period[ch] = rowList.value(b + 0).toInt();
                row.volume[ch] = rowList.value(b + 1).toInt();

                bool envOk = false;
                int envValue = rowList.value(b + 2).toInt(&envOk);
                if (!envOk)
                    envValue = 3;

                row.env[ch] = qBound(0, envValue, 15);
                row.waveX[ch] = qBound(0, rowList.value(b + 3).toInt(), 100);
                row.waveY[ch] = qBound(0, rowList.value(b + 4).toInt(), 100);

                row.snVolumeMacro[ch] = rowList.value(b + 5).toString();
                row.snPitchMacro[ch] = rowList.value(b + 6).toString();
                row.snArpMacro[ch] = rowList.value(b + 7).toString();
                row.snNoiseMacro[ch] = rowList.value(b + 8).toString();
            }

            // Explicitly keep AY channels fully inactive for a non-SGM row.
            for (int ch = 4; ch < 7; ++ch) {
                row.period[ch] = -1;
                row.volume[ch] = -1;
                row.ayTone[ch] = 0;
                row.ayNoise[ch] = 0;
                row.ayHwEnv[ch] = 0;
            }

        } else if (rowList.size() >= 35) {
            // Historical 7-channel x 5-field stream.
            for (int ch = 0; ch < 7; ++ch) {
                row.period[ch] = rowList.value(ch * 5 + 0).toInt();
                row.volume[ch] = rowList.value(ch * 5 + 1).toInt();
                bool envOk = false;
                int envValue = rowList.value(ch * 5 + 2).toInt(&envOk);
                if (!envOk) envValue = 3;
                row.env[ch] = qBound(0, envValue, 15);
                row.waveX[ch] = qBound(0, rowList.value(ch * 5 + 3).toInt(), 100);
                row.waveY[ch] = qBound(0, rowList.value(ch * 5 + 4).toInt(), 100);
            }
        } else if (rowList.size() >= 20) {
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

    if (parsed.isEmpty()) {
        qDebug().noquote() << "[ADAMP PLAYER] parsed rows empty, abort start";
        qDebug().noquote() << "========== [ADAMP PLAYER] startSongStream END(empty) ==========\n";
        return;
    }

    qDebug().noquote() << "[ADAMP PLAYER] parsed rows=" << parsed.size();

    // V8.82: IMPORTANT — test for active playback BEFORE stopSongStream().
    // V8.74 placed stopSongStream() above this block, which made this live
    // path impossible to reach and restarted every song after an instrument edit.
    // When already playing, only replace future row data and preserve all
    // current timing/oscillator/envelope state.
    // V8.74 live-edit path: when a stream is already playing, replace the
    // stream data in-place and preserve the current playback position.
    // Crucially: do NOT stop/recreate QAudioSink and do NOT reset rowIndex,
    // oscillators, envelopes or note state.
    {
        QMutexLocker lock(&m_mutex);
        if (m_playing && m_sink && m_device) {
            const int oldRowCount = m_rows.size();
            const int oldRowIndex = m_rowIndex;
            m_rows = parsed;
            m_loop = loop;
            m_samplesPerRow = qMax(1, qRound((44100.0 * qMax(1, rowMs)) / 1000.0));
            if (!m_rows.isEmpty()) {
                if (m_loop)
                    m_rowIndex = oldRowIndex % m_rows.size();
                else
                    m_rowIndex = qMin(oldRowIndex, m_rows.size());
            } else {
                m_rowIndex = 0;
            }
            // Apply the edited instrument parameters immediately to the
            // currently sounding voices.  Keep period/phase/noteTime intact so
            // music never restarts or clicks back to the pattern start.
            if (!m_rows.isEmpty()) {
                const int currentRow = qBound(0, m_rowIndex > 0 ? m_rowIndex - 1 : 0,
                                              m_rows.size() - 1);
                const StreamRow& live = m_rows.at(currentRow);

                for (int ch = 0; ch < 7; ++ch) {
                    ChannelState& st = m_channels[ch];
                    if (!st.active)
                        continue;

                    st.finalRenderedFrame = live.finalRenderedFrame;
                    if (live.finalRenderedFrame)
                        continue;

                    st.env = qBound(0, live.env[ch], 15);
                    st.waveX = qBound(0, live.waveX[ch], 100);
                    st.waveY = qBound(0, live.waveY[ch], 100);

                    if (ch < 4) {
                        st.snVolumeMacro = parseMacro(live.snVolumeMacro[ch], 0, 15);
                        st.snPitchMacro = parseMacro(live.snPitchMacro[ch], -36, 36);
                        st.snArpMacro = parseMacro(live.snArpMacro[ch], -36, 36);
                        st.snNoiseMacro = parseMacro(live.snNoiseMacro[ch], 0, 7);
                        st.duty = dutyForEnvelope(st.env);
                    } else {
                        st.ayTone = live.ayTone[ch] != 0;
                        st.ayNoise = live.ayNoise[ch] != 0;
                        st.ayHwEnv = live.ayHwEnv[ch] != 0;
                        st.ayShape = live.ayShape[ch] & 0x0F;
                        st.ayEnvPeriod = qMax(1, live.ayEnvPeriod[ch]);
                        st.ayNoisePeriod = qBound(0, live.ayNoisePeriod[ch], 31);
                        st.ayAttack = qBound(0, live.ayAttack[ch], 15);
                        st.ayDecay = qBound(0, live.ayDecay[ch], 15);
                        st.aySustain = qBound(0, live.aySustain[ch], 15);
                        st.ayRelease = qBound(0, live.ayRelease[ch], 15);
                        st.ayVibrato = qBound(0, live.ayVibrato[ch], 15);
                        st.ayArp = parseArpeggio(live.ayArp[ch]);
                        st.ayVolumeMacro = parseMacro(live.ayVolumeMacro[ch], 0, 15);
                        st.ayPitchMacro = parseMacro(live.ayPitchMacro[ch], -36, 36);
                        st.ayNoiseMacro = parseMacro(live.ayNoiseMacro[ch], 0, 31, 0);
                        st.ayWaveMacro = parseMacro(live.ayWaveMacro[ch], 0, 7);
                        st.ayEnvShapeMacro = parseMacro(live.ayEnvShapeMacro[ch], 0, 15, 0);
                        st.ayEnvPeriodMacro = parseMacro(live.ayEnvPeriodMacro[ch], 1, 65535, 0);
                        st.ayPhaseResetMacro = parseMacro(live.ayPhaseResetMacro[ch], 0, 1);
                        st.ayAutoEnvRatio = parseRatio(live.ayAutoEnv[ch]);
                    }
                }
            }

            qDebug().noquote() << "[ADAMP PLAYER] LIVE stream update"
                               << "oldRows=" << oldRowCount
                               << "newRows=" << m_rows.size()
                               << "rowIndex preserved=" << m_rowIndex;
            return;
        }
    }

    // This is a genuine new Play operation, not a live instrument refresh.
    stopSongStream();
    ++m_generation;

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

        for (int i = 0; i < 7; ++i) {
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

        for (int i = 0; i < 7; ++i) {
            m_vuLevels[i] = 0;
            m_pendingVuLevels[i] = 0;
        }
    }

    emit previewVuMetersChanged(0, 0, 0, 0);
    for (int ch = 0; ch < 7; ++ch)
        emit previewVuMeterChanged(ch, 0);

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

        for (int i = 0; i < 7; ++i) {
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
        ch.ayTone = true;
        ch.ayNoise = false;
        ch.ayHwEnv = false;
        ch.ayShape = 0;
        ch.ayEnvPeriod = 0x100;
        ch.ayNoisePeriod = 0;
        ch.ayAttack = 0;
        ch.ayDecay = 0;
        ch.aySustain = 15;
        ch.ayRelease = 0;
        ch.ayVibrato = 0;
        ch.ayArp.clear();
        ch.snVolumeMacro.clear();
        ch.snPitchMacro.clear();
        ch.snArpMacro.clear();
        ch.snNoiseMacro.clear();
        ch.ayVolumeMacro.clear();
        ch.ayPitchMacro.clear();
        ch.ayNoiseMacro.clear();
        ch.ayWaveMacro.clear();
        ch.ayEnvShapeMacro.clear();
        ch.ayEnvPeriodMacro.clear();
        ch.ayPhaseResetMacro.clear();
        ch.ayAutoEnvRatio = 0.0;
        ch.ayNoisePhase = 0.0;
        ch.ayNoiseValue = 0.0;
        ch.ayNoiseRng = 0xB400u;
        ch.releasing = false;
        ch.releaseStartTime = 0.0;
        ch.releaseStartLevel = 1.0;
    }
    m_aySharedNoisePhase = 0.0;
    m_aySharedNoiseValue = 1.0;
    m_aySharedNoiseRng = 0x1FFFFu;
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
            for (int ch = 0; ch < 7; ++ch)
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

    for (int ch = 0; ch < 7; ++ch) {
        const int p = row.period[ch];
        const int v = row.volume[ch];
        const int e = qBound(0, row.env[ch], 15);
        const int wx = qBound(0, row.waveX[ch], 100);
        const int wy = qBound(0, row.waveY[ch], 100);

        if (p < 0) {
            ChannelState& hold = m_channels[ch];

            if (v >= 0 && hold.active)
                hold.volume = qBound(0, v, 15);

            // Live instrument editing must also affect a currently HELD note.
            // Update timbre/macros in place, but never reset noteTime/phase.
            if (hold.active) {
                hold.finalRenderedFrame = row.finalRenderedFrame;
            }

            if (hold.active && !row.finalRenderedFrame) {
                hold.env = qBound(0, row.env[ch], 15);
                hold.waveX = qBound(0, row.waveX[ch], 100);
                hold.waveY = qBound(0, row.waveY[ch], 100);

                if (ch < 4) {
                    hold.snVolumeMacro = parseMacro(row.snVolumeMacro[ch], 0, 15);
                    hold.snPitchMacro = parseMacro(row.snPitchMacro[ch], -36, 36);
                    hold.snArpMacro = parseMacro(row.snArpMacro[ch], -36, 36);
                    hold.snNoiseMacro = parseMacro(row.snNoiseMacro[ch], 0, 7);
                    hold.duty = dutyForEnvelope(hold.env);
                } else {
                    hold.ayTone = row.ayTone[ch] != 0;
                    hold.ayNoise = row.ayNoise[ch] != 0;
                    hold.ayHwEnv = row.ayHwEnv[ch] != 0;
                    hold.ayShape = row.ayShape[ch] & 0x0F;
                    hold.ayEnvPeriod = qMax(1, row.ayEnvPeriod[ch]);
                    hold.ayNoisePeriod = qBound(0, row.ayNoisePeriod[ch], 31);
                    hold.ayAttack = qBound(0, row.ayAttack[ch], 15);
                    hold.ayDecay = qBound(0, row.ayDecay[ch], 15);
                    hold.aySustain = qBound(0, row.aySustain[ch], 15);
                    hold.ayRelease = qBound(0, row.ayRelease[ch], 15);
                    hold.ayVibrato = qBound(0, row.ayVibrato[ch], 15);
                    hold.ayArp = parseArpeggio(row.ayArp[ch]);
                    hold.ayVolumeMacro = parseMacro(row.ayVolumeMacro[ch], 0, 15);
                    hold.ayPitchMacro = parseMacro(row.ayPitchMacro[ch], -36, 36);
                    hold.ayNoiseMacro = parseMacro(row.ayNoiseMacro[ch], 0, 31, 0);
                    hold.ayWaveMacro = parseMacro(row.ayWaveMacro[ch], 0, 7);
                    hold.ayEnvShapeMacro = parseMacro(row.ayEnvShapeMacro[ch], 0, 15, 0);
                    hold.ayEnvPeriodMacro = parseMacro(row.ayEnvPeriodMacro[ch], 1, 65535, 0);
                    hold.ayPhaseResetMacro = parseMacro(row.ayPhaseResetMacro[ch], 0, 1);
                    hold.ayAutoEnvRatio = parseRatio(row.ayAutoEnv[ch]);
                }
            }

            if (hold.active) {
                // V9.06: AY release rows must drive the VU from the actual
                // remaining envelope level, not from the stale base volume.
                // Otherwise an AY channel can be audibly silent while its VU
                // keeps reporting the previous note volume forever on HOLD rows.
                if (ch >= 4 && hold.releasing) {
                    const double releaseFactor = ayAdsrFactor(hold, hold.noteTime);
                    const int releaseVu = qBound(0, qRound(hold.volume * releaseFactor), 15);

                    if (releaseFactor <= 0.0001 || releaseVu <= 0) {
                        hold.active = false;
                        hold.releasing = false;
                        hold.period = 0;
                        hold.volume = 0;
                        hold.frequency = 0.0;
                        hold.transitionSamples = 0;
                        setPendingVuLevelLocked(ch, 0);
                    } else {
                        setPendingVuLevelLocked(ch, releaseVu);
                    }
                } else if (hold.volume > 0) {
                    setPendingVuLevelLocked(ch, hold.volume);
                } else {
                    setPendingVuLevelLocked(ch, 0);
                }
            } else {
                setPendingVuLevelLocked(ch, 0);
            }
            continue;
        }

        ChannelState& cs = m_channels[ch];

        if (p == 0 || v <= 0) {
            if (ch >= 4 && cs.active && cs.ayRelease > 0) {
                cs.releasing = true;
                cs.releaseStartTime = cs.noteTime;
                cs.releaseStartLevel = ayAdsrFactor(cs, cs.noteTime);
                setPendingVuLevelLocked(ch, qBound(0, cs.volume, 15));
            } else {
                cs.active = false;
                cs.period = 0;
                cs.volume = 0;
                cs.frequency = 0.0;
                cs.smoothSample *= 0.25;
                cs.transitionSamples = 0;
                setPendingVuLevelLocked(ch, 0);
            }
            continue;
        }

        // Song playback receives already-rendered CVBasic register frames.
        // Only actual register-visible state changes matter here.
        bool changed = (!cs.active || cs.period != p || cs.volume != v);
        if (ch >= 4) {
            changed = changed
                || cs.ayTone != (row.ayTone[ch] != 0)
                || cs.ayNoise != (row.ayNoise[ch] != 0)
                || cs.ayHwEnv != (row.ayHwEnv[ch] != 0)
                || cs.ayShape != (row.ayShape[ch] & 0x0F)
                || cs.ayEnvPeriod != qMax(1, row.ayEnvPeriod[ch])
                || cs.ayNoisePeriod != qBound(0, row.ayNoisePeriod[ch], 31);
        }

        cs.active = true;
        cs.finalRenderedFrame = row.finalRenderedFrame;
        cs.period = p;
        cs.volume = qBound(0, v, 15);
        cs.env = e;
        cs.waveX = wx;
        cs.waveY = wy;

        if (ch < 4) {
            if (row.finalRenderedFrame) {
                cs.snVolumeMacro.clear();
                cs.snPitchMacro.clear();
                cs.snArpMacro.clear();
                cs.snArpTime = 0.0;
                cs.snNoiseMacro.clear();
            } else {
                cs.snVolumeMacro = parseMacro(row.snVolumeMacro[ch], 0, 15);
                cs.snPitchMacro = parseMacro(row.snPitchMacro[ch], -36, 36);
                cs.snArpMacro = parseMacro(row.snArpMacro[ch], -36, 36);
                cs.snArpTime = 0.0;
                cs.snNoiseMacro = parseMacro(row.snNoiseMacro[ch], 0, 7);
            }
        }

        if (ch >= 4) {
            cs.ayTone = row.ayTone[ch] != 0;
            cs.ayNoise = row.ayNoise[ch] != 0;
            cs.ayHwEnv = row.ayHwEnv[ch] != 0;
            cs.ayShape = row.ayShape[ch] & 0x0F;
            cs.ayEnvPeriod = qMax(1, row.ayEnvPeriod[ch]);
            cs.ayNoisePeriod = qBound(0, row.ayNoisePeriod[ch], 31);
            cs.ayAttack = qBound(0, row.ayAttack[ch], 15);
            cs.ayDecay = qBound(0, row.ayDecay[ch], 15);
            cs.aySustain = qBound(0, row.aySustain[ch], 15);
            cs.ayRelease = qBound(0, row.ayRelease[ch], 15);
            cs.ayVibrato = qBound(0, row.ayVibrato[ch], 15);
            cs.releasing = false;

            if (row.finalRenderedFrame) {
                // Already-rendered CVBasic/PSG frame: never synthesize macros twice.
                cs.ayArp.clear();
                cs.ayVolumeMacro.clear();
                cs.ayPitchMacro.clear();
                cs.ayNoiseMacro.clear();
                cs.ayWaveMacro.clear();
                cs.ayEnvShapeMacro.clear();
                cs.ayEnvPeriodMacro.clear();
                cs.ayPhaseResetMacro.clear();
                cs.ayAutoEnvRatio = 0.0;
            } else {
                // Normal Sound Editor song row: instrument synthesis is active.
                cs.ayArp = parseArpeggio(row.ayArp[ch]);
                cs.ayVolumeMacro = parseMacro(row.ayVolumeMacro[ch], 0, 15);
                cs.ayPitchMacro = parseMacro(row.ayPitchMacro[ch], -36, 36);
                cs.ayNoiseMacro = parseMacro(row.ayNoiseMacro[ch], 0, 31, 0);
                cs.ayWaveMacro = parseMacro(row.ayWaveMacro[ch], 0, 7);
                cs.ayEnvShapeMacro = parseMacro(row.ayEnvShapeMacro[ch], 0, 15, 0);
                cs.ayEnvPeriodMacro = parseMacro(row.ayEnvPeriodMacro[ch], 1, 65535, 0);
                cs.ayPhaseResetMacro = parseMacro(row.ayPhaseResetMacro[ch], 0, 1);
                cs.ayAutoEnvRatio = parseRatio(row.ayAutoEnv[ch]);
            }
        }

        if (changed) {
            cs.noteTime = 0.0;
            cs.transitionSamples = 64;
        }

        if (ch < 3) {
            const double f = 3579545.0 / (32.0 * qMax(1, p));
            cs.frequency = qBound(20.0, f, 20000.0);
            cs.duty = dutyForEnvelope(cs.env);
        } else if (ch == 3) {
            cs.noiseCode = qBound(0, p, 7);

            // SN76489 noise control:
            // bits 0..1 = clock select:
            //   0 -> PSG clock / 512
            //   1 -> PSG clock / 1024
            //   2 -> PSG clock / 2048
            //   3 -> tone channel 3 frequency
            // bit 2 selects white vs periodic feedback.
            const int rate = cs.noiseCode & 0x03;
            if (rate == 0)
                cs.frequency = 3579545.0 / 512.0;
            else if (rate == 1)
                cs.frequency = 3579545.0 / 1024.0;
            else if (rate == 2)
                cs.frequency = 3579545.0 / 2048.0;
            else
                cs.frequency = qMax(1.0, m_channels[2].frequency);
        } else {
            const double f = 3579545.0 / (16.0 * qMax(1, p));
            cs.frequency = qBound(20.0, f, 20000.0);
            cs.duty = 0.50;
        }

        setPendingVuLevelLocked(ch, cs.volume);
    }

    m_samplesUntilNextRow += m_samplesPerRow;
}

QVector<int> SoundEditorPlayer::parseArpeggio(const QString& text)
{
    QVector<int> result;
    const QString cleaned = text.trimmed();
    if (cleaned.isEmpty() || cleaned == "---")
        return result;

    const QStringList parts = cleaned.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const int semitones = part.trimmed().toInt(&ok, 10);
        if (ok)
            result.append(qBound(-36, semitones, 36));
    }
    return result;
}

QVector<int> SoundEditorPlayer::parseMacro(const QString& text, int minValue, int maxValue, int base)
{
    QVector<int> result;
    const QString cleaned = text.trimmed();
    if (cleaned.isEmpty() || cleaned == "---") return result;
    const QStringList parts = cleaned.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString token = part.trimmed();
        bool ok = false;
        int value = token.toInt(&ok, base);
        if (!ok && base == 0) value = token.toInt(&ok, 16);
        if (ok) result.append(qBound(minValue, value, maxValue));
    }
    return result;
}

int SoundEditorPlayer::macroValueAt(const QVector<int>& macro, double t, int fallback)
{
    if (macro.isEmpty()) return fallback;
    const int step = qMax(0, static_cast<int>(std::floor(t * 60.0)));
    return macro.at(qMin(step, macro.size() - 1));
}

double SoundEditorPlayer::parseRatio(const QString& text)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || t == "---") return 0.0;
    const QStringList parts = t.split('/');
    bool okA = false, okB = false;
    if (parts.size() == 2) {
        const double a = parts.at(0).trimmed().toDouble(&okA);
        const double b = parts.at(1).trimmed().toDouble(&okB);
        if (okA && okB && b != 0.0) return qBound(0.01, a / b, 64.0);
    }
    const double v = t.toDouble(&okA);
    return okA ? qBound(0.01, v, 64.0) : 0.0;
}

double SoundEditorPlayer::ayAdsrFactor(const ChannelState& ch, double t)
{
    const double attack = (ch.ayAttack <= 0) ? 0.0 : (0.008 + ch.ayAttack * 0.018);
    const double decay = (ch.ayDecay <= 0) ? 0.0 : (0.012 + ch.ayDecay * 0.026);
    const double sustain = qBound(0.0, ch.aySustain / 15.0, 1.0);

    double level = 1.0;
    if (attack > 0.0 && t < attack)
        level = t / attack;
    else if (decay > 0.0 && t < attack + decay) {
        const double x = (t - attack) / decay;
        level = 1.0 + (sustain - 1.0) * qBound(0.0, x, 1.0);
    } else
        level = sustain;

    if (ch.releasing) {
        const double release = (ch.ayRelease <= 0) ? 0.008 : (0.015 + ch.ayRelease * 0.035);
        const double rt = qMax(0.0, t - ch.releaseStartTime);
        level = ch.releaseStartLevel * qMax(0.0, 1.0 - rt / release);
    }

    return qBound(0.0, level, 1.0);
}

double SoundEditorPlayer::ayHardwareEnvelopeFactor(const ChannelState& ch, double t)
{
    // AY envelope clock: master / 256 / period. This preview follows the
    // characteristic shapes closely enough to make instrument editing useful.
    const double stepHz = 3579545.0 / (256.0 * qMax(1, ch.ayEnvPeriod));
    double rampHz = stepHz / 16.0;
    if (ch.ayAutoEnvRatio > 0.0 && ch.frequency > 0.0)
        rampHz = ch.frequency * ch.ayAutoEnvRatio;
    const double phase = std::fmod(qMax(0.0, t) * qMax(0.003, rampHz), 1.0);
    const int shape = ch.ayShape & 0x0F;

    // Shapes 0..3 and 8..B are descending families; 4..7 and C..F ascending.
    // Continue/alternate/hold bits change repetition behaviour.
    const bool cont = (shape & 0x08) != 0;
    const bool attack = (shape & 0x04) != 0;
    const bool alternate = (shape & 0x02) != 0;
    const bool hold = (shape & 0x01) != 0;

    const double cycles = qMax(0.0, t) * qMax(0.003, rampHz);
    const int cycleIndex = static_cast<int>(std::floor(cycles));
    double x = phase;
    bool up = attack;

    if (alternate && (cycleIndex & 1))
        up = !up;

    if (!cont && cycleIndex > 0)
        return attack ? 1.0 : 0.0;

    if (hold && cycleIndex > 0) {
        if (alternate)
            up = !attack;
        return up ? 1.0 : 0.0;
    }

    return qBound(0.0, up ? x : (1.0 - x), 1.0);
}

double SoundEditorPlayer::ayPitchMultiplier(const ChannelState& ch, double t)
{
    double semitones = 0.0;
    semitones += macroValueAt(ch.ayPitchMacro, t, 0);

    if (!ch.ayArp.isEmpty()) {
        // AY tracker arpeggio: 50 Hz-ish step rate gives the classic PSG chord sound.
        const int step = static_cast<int>(std::floor(t * 50.0)) % ch.ayArp.size();
        semitones += ch.ayArp.at(step);
    }

    if (ch.ayVibrato > 0) {
        const double depthSemitones = 0.025 * ch.ayVibrato;
        semitones += depthSemitones * std::sin(2.0 * M_PI * 5.5 * t);
    }

    return std::pow(2.0, semitones / 12.0);
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


namespace {

// CVBasic exposes volume as 0=silent .. 15=loudest.
// SN76489 hardware itself stores attenuation, 0=loudest .. 15=silent,
// with approximately 2 dB per step.  This table is the corresponding
// normalized amplitude after reversing CVBasic's volume numbering.
static double adampSnVolumeAmplitude(int cvVolume)
{
    static const double table[16] = {
        0.0,        // CV 0 -> SN attenuation 15 -> silence
        1304.0/32767.0,
        1642.0/32767.0,
        2067.0/32767.0,
        2603.0/32767.0,
        3277.0/32767.0,
        4125.0/32767.0,
        5193.0/32767.0,
        6568.0/32767.0,
        8231.0/32767.0,
        10362.0/32767.0,
        13045.0/32767.0,
        16422.0/32767.0,
        20675.0/32767.0,
        26028.0/32767.0,
        1.0
    };
    return table[qBound(0, cvVolume, 15)];
}

// AY-3-8910 16-level DAC curve used by the high-accuracy Ayumi model.
// CVBasic's AY SOUND 5..7 volume numbering is already 0..15 in this direction.
static double adampAyVolumeAmplitude(int cvVolume)
{
    static const double table[16] = {
        0.0,
        0.00999465934234,
        0.0144502937362,
        0.0210574502174,
        0.0307011520562,
        0.0455481803616,
        0.0644998855573,
        0.107362478065,
        0.126588845655,
        0.20498970016,
        0.292210269322,
        0.372838941024,
        0.492530708782,
        0.635324635691,
        0.805584802014,
        1.0
    };
    return table[qBound(0, cvVolume, 15)];
}

} // namespace

void SoundEditorPlayer::render(int16_t* stereo, int frames)
{
    if (!stereo || frames <= 0)
        return;

    QMutexLocker lock(&m_mutex);

    // V9.07: VU meters are output meters. Measure the effective per-channel
    // amplitude generated by the renderer, not the tracker/note state.
    int renderedVuPeak[7] = {0, 0, 0, 0, 0, 0, 0};

    for (int frame = 0; frame < frames; ++frame) {
        if (m_playing && m_samplesUntilNextRow <= 0)
            applyNextRowLocked();

        double mixed = 0.0;

        // -------------------------------------------------------------
        // CVBasic / ColecoVision reference mix
        // -------------------------------------------------------------
        // CVBasic writes raw PSG registers:
        //   SN76489 through SOUND 0..3
        //   AY-3-8910 through SOUND 5..9
        //
        // Gearcoleco's ColecoVision mixer runs the SN core at volume 0.6
        // and then adds the SGM AY stream at native level.  Preserve that
        // same relative balance here:
        const double snMaster = 0.60;
        const double ayMaster = 1.00;

        // 16-bit PCM is +/-32767.  Worst-case simultaneous hardware mix is
        // 4 SN voices * 0.60 + 3 AY voices * 1.00 = 5.4.
        // 6000 gives ~32400 before the final safety gain and therefore uses
        // the available dynamic range instead of V8.51's tiny 760 amplitude.
        const double baseAmplitude = 6000.0;

        // AY has ONE shared noise generator. Advance it exactly once per
        // output sample, using the shared noise period carried by the
        // CVBasic SOUND 9 state.
        int ayNoisePeriod = 0;
        bool ayNoiseUsed = false;
        for (int i = 4; i < 7; ++i) {
            const ChannelState& ch = m_channels[i];
            if (ch.active && ch.ayNoise) {
                ayNoisePeriod = qBound(0, ch.ayNoisePeriod, 31);
                ayNoiseUsed = true;
                break;
            }
        }

        if (ayNoiseUsed) {
            const double nf = 3579545.0 / (16.0 * qMax(1, ayNoisePeriod + 1));
            m_aySharedNoisePhase += nf / static_cast<double>(m_sampleRate);
            while (m_aySharedNoisePhase >= 1.0) {
                m_aySharedNoisePhase -= 1.0;

                // 17-bit AY noise LFSR: taps 0 and 3.
                const quint32 feedback =
                    ((m_aySharedNoiseRng >> 0) ^ (m_aySharedNoiseRng >> 3)) & 1u;
                m_aySharedNoiseRng =
                    (m_aySharedNoiseRng >> 1) | (feedback << 16);

                m_aySharedNoiseValue =
                    (m_aySharedNoiseRng & 1u) ? 1.0 : -1.0;
            }
        } else {
            // When noise is disabled in the mixer, AY treats the noise gate
            // as logically high for tone-only output.
            m_aySharedNoiseValue = 1.0;
        }

        // Shared AY hardware envelope.  CVBasic selects it by writing
        // volume 16 to SOUND 5/6/7 and programs it with SOUND 8.
        const ChannelState* ayEnvelopeSource = nullptr;
        for (int i = 4; i < 7; ++i) {
            const ChannelState& ch = m_channels[i];
            if (ch.active && ch.ayHwEnv) {
                ayEnvelopeSource = &ch;
                break;
            }
        }

        double ayEnvelopeAmp = 1.0;
        if (ayEnvelopeSource) {
            // ayHardwareEnvelopeFactor() models the shared AY envelope.
            // Quantize it to the real 16 AY DAC levels before applying it.
            const double f = ayHardwareEnvelopeFactor(
                *ayEnvelopeSource, ayEnvelopeSource->noteTime);
            const int envelopeLevel = qBound(0, qRound(f * 15.0), 15);
            ayEnvelopeAmp = adampAyVolumeAmplitude(envelopeLevel);
        }

        for (int i = 0; i < 7; ++i) {
            ChannelState& ch = m_channels[i];

            if (!m_channelAudible[i] || !ch.active || ch.volume <= 0)
                continue;

            const bool raw = ch.finalRenderedFrame;

            if (i < 3) {
                // SN76489 tone channels.
                double volumeLevel = ch.volume;
                double freqMultiplier = 1.0;
                double envAmp = 1.0;
                double duty = 0.50;

                if (!raw) {
                    // Furnace / instrument parameters are editor synthesis.
                    if (!ch.snVolumeMacro.isEmpty())
                        volumeLevel = macroValueAt(ch.snVolumeMacro, ch.noteTime, ch.volume);

                    double semitones = 0.0;
                    semitones += macroValueAt(ch.snPitchMacro, ch.noteTime, 0);
                    if (!ch.snArpMacro.isEmpty()) {
                        const int step = static_cast<int>(std::floor(ch.snArpTime * 50.0))
                                         % ch.snArpMacro.size();
                        semitones += ch.snArpMacro.at(step);
                    }
                    freqMultiplier = std::pow(2.0, semitones / 12.0)
                                     * pitchMultiplier(ch.env, ch.noteTime);

                    // Make ENV/Fade-like behaviour actually audible in song playback.
                    envAmp = envelopeFactor(ch.env, ch.noteTime, 1.0)
                             * loudnessForEnvelope(ch.env);

                    // ENV and Wave X/Y visibly change pulse character.
                    duty = qBound(0.08,
                                  dutyForEnvelope(ch.env)
                                  + (ch.waveX - 50) * 0.006
                                  + (ch.waveY - 50) * 0.002,
                                  0.92);
                }

                const double channelAmplitude =
                    adampSnVolumeAmplitude(qBound(0, qRound(volumeLevel), 15))
                    * qMax(0.0, envAmp);
                const double amp = channelAmplitude * baseAmplitude * snMaster;
                renderedVuPeak[i] = qMax(renderedVuPeak[i],
                                         qBound(0, qRound(channelAmplitude * 15.0), 15));

                mixed += (ch.phase < duty) ? amp : -amp;

                ch.phase += (ch.frequency * freqMultiplier)
                            / static_cast<double>(m_sampleRate);
                while (ch.phase >= 1.0)
                    ch.phase -= 1.0;
            }
            else if (i == 3) {
                // SN noise.
                int noiseCode = ch.noiseCode;
                double volumeLevel = ch.volume;
                double envAmp = 1.0;

                if (!raw) {
                    if (!ch.snVolumeMacro.isEmpty())
                        volumeLevel = macroValueAt(ch.snVolumeMacro, ch.noteTime, ch.volume);
                    if (!ch.snNoiseMacro.isEmpty())
                        noiseCode = macroValueAt(ch.snNoiseMacro, ch.noteTime, ch.noiseCode);
                    envAmp = envelopeFactor(ch.env, ch.noteTime, 1.0)
                             * loudnessForEnvelope(ch.env);
                }

                const double channelAmplitude =
                    adampSnVolumeAmplitude(qBound(0, qRound(volumeLevel), 15))
                    * qMax(0.0, envAmp);
                const double amp = channelAmplitude * baseAmplitude * snMaster;
                renderedVuPeak[i] = qMax(renderedVuPeak[i],
                                         qBound(0, qRound(channelAmplitude * 15.0), 15));

                const int rate = noiseCode & 0x03;
                double noiseFreq = ch.frequency;
                if (!raw) {
                    if (rate == 0) noiseFreq = 3579545.0 / 512.0;
                    else if (rate == 1) noiseFreq = 3579545.0 / 1024.0;
                    else if (rate == 2) noiseFreq = 3579545.0 / 2048.0;
                    else noiseFreq = qMax(1.0, m_channels[2].frequency);
                }

                ch.noisePhase += noiseFreq / static_cast<double>(m_sampleRate);
                while (ch.noisePhase >= 1.0) {
                    ch.noisePhase -= 1.0;
                    const bool whiteNoise = (noiseCode & 0x04) != 0;
                    bool feedback = false;
                    if (whiteNoise)
                        feedback = ((ch.noiseRng ^ (ch.noiseRng >> 1)) & 1u) != 0;
                    else
                        feedback = (ch.noiseRng & 1u) != 0;

                    ch.noiseRng =
                        (ch.noiseRng >> 1) | (feedback ? 0x8000u : 0u);
                    ch.noiseValue =
                        (ch.noiseRng & 1u) ? 1.0 : -1.0;
                }

                mixed += ch.noiseValue * amp;
            }
            else {
                // AY-3-8910 channel.
                bool toneOn = ch.ayTone;
                bool noiseOn = ch.ayNoise;
                bool hwEnv = ch.ayHwEnv;
                int shape = ch.ayShape;
                int envPeriod = ch.ayEnvPeriod;
                int noisePeriod = ch.ayNoisePeriod;
                double volumeLevel = ch.volume;
                double freqMultiplier = 1.0;

                if (!raw) {
                    if (!ch.ayVolumeMacro.isEmpty())
                        volumeLevel = macroValueAt(ch.ayVolumeMacro, ch.noteTime, ch.volume);

                    freqMultiplier = ayPitchMultiplier(ch, ch.noteTime);

                    if (!ch.ayWaveMacro.isEmpty()) {
                        const int wave = macroValueAt(ch.ayWaveMacro, ch.noteTime,
                                                      (toneOn ? 1 : 0) |
                                                      (noiseOn ? 2 : 0) |
                                                      (hwEnv ? 4 : 0));
                        toneOn = (wave & 1) != 0;
                        noiseOn = (wave & 2) != 0;
                        hwEnv = (wave & 4) != 0;
                    }

                    if (!ch.ayEnvShapeMacro.isEmpty())
                        shape = macroValueAt(ch.ayEnvShapeMacro, ch.noteTime, shape);
                    if (!ch.ayEnvPeriodMacro.isEmpty())
                        envPeriod = macroValueAt(ch.ayEnvPeriodMacro, ch.noteTime, envPeriod);
                    if (!ch.ayNoiseMacro.isEmpty())
                        noisePeriod = macroValueAt(ch.ayNoiseMacro, ch.noteTime, noisePeriod);
                }

                // Tone and noise gate, AY-style.
                const bool toneGate = !toneOn || (ch.phase < 0.5);
                const bool noiseGate = !noiseOn || (m_aySharedNoiseValue > 0.0);
                const bool gate = toneGate && noiseGate;

                double amplitude = 0.0;
                if (hwEnv) {
                    if (raw) {
                        amplitude = ayEnvelopeAmp;
                    } else {
                        ChannelState envState = ch;
                        envState.ayShape = shape & 0x0F;
                        envState.ayEnvPeriod = qMax(1, envPeriod);
                        amplitude = ayHardwareEnvelopeFactor(envState, ch.noteTime);
                    }
                } else {
                    amplitude = adampAyVolumeAmplitude(qBound(0, qRound(volumeLevel), 15));
                    if (!raw)
                        amplitude *= ayAdsrFactor(ch, ch.noteTime);
                }

                const double channelAmplitude = qMax(0.0, amplitude);
                const double amp = channelAmplitude * baseAmplitude * ayMaster;
                renderedVuPeak[i] = qMax(renderedVuPeak[i],
                                         qBound(0, qRound(channelAmplitude * 15.0), 15));
                mixed += gate ? amp : -amp;

                ch.phase += (ch.frequency * freqMultiplier)
                            / static_cast<double>(m_sampleRate);
                while (ch.phase >= 1.0)
                    ch.phase -= 1.0;
            }

            ch.noteTime += 1.0 / static_cast<double>(m_sampleRate);
            if (i < 4)
                ch.snArpTime += 1.0 / static_cast<double>(m_sampleRate);
        }

        // Master output only. Relative SN/AY/channel balance above remains intact.
        mixed *= 0.82;
        mixed = qBound(-32767.0, mixed, 32767.0);

        const qint16 sample = static_cast<qint16>(mixed);
        stereo[frame * 2 + 0] = sample;
        stereo[frame * 2 + 1] = sample;

        if (m_playing) {
            --m_samplesUntilNextRow;
            if (m_samplesUntilNextRow <= 0)
                applyNextRowLocked();
        }
    }

    // The render result is authoritative for VU. This deliberately overwrites
    // row/state-driven pending values. A held note that still produces sound
    // remains visible; a channel whose effective output reached silence is 0.
    for (int ch = 0; ch < 7; ++ch) {
        const int level = m_channelAudible[ch] ? renderedVuPeak[ch] : 0;
        m_pendingVuLevels[ch] = level;
        if (level == 0)
            m_vuLevels[ch] = 0;
    }
}

void SoundEditorPlayer::setPendingVuLevelLocked(int channel, int level)
{
    if (channel < 0 || channel >= 7)
        return;

    if (!m_channelAudible[channel]) {
        m_pendingVuLevels[channel] = 0;
        m_vuLevels[channel] = 0;
        return;
    }

    level = qBound(0, level, 15);

    // V9.05: an explicit release/silence must win immediately. Using qMax()
    // unconditionally kept an earlier peak pending even after the pattern had
    // already released the channel in the same VU interval.
    if (level == 0)
        m_pendingVuLevels[channel] = 0;
    else
        m_pendingVuLevels[channel] = qMax(m_pendingVuLevels[channel], level);
}

void SoundEditorPlayer::flushVu()
{
    int out[7] = {0, 0, 0, 0, 0, 0, 0};
    bool anyActive = false;

    {
        QMutexLocker lock(&m_mutex);

        for (int i = 0; i < 7; ++i) {
            if (!m_channelAudible[i]) {
                m_pendingVuLevels[i] = 0;
                m_vuLevels[i] = 0;
                out[i] = 0;
                continue;
            }

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

    for (int ch = 0; ch < 7; ++ch)
        emit previewVuMeterChanged(ch, out[ch]);
    emit previewVuMetersChanged(out[0], out[1], out[2], out[3]);

    if (!anyActive && !m_playing && m_vuTimer)
        m_vuTimer->stop();
}
