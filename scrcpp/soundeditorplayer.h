#ifndef SOUNDEDITORPLAYER_H
#define SOUNDEDITORPLAYER_H

#include <QObject>
#include <QIODevice>
#include <QTimer>
#include <QMutex>
#include <QVector>
#include <QVariantList>

#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSink>
#include <QtMultimedia/QAudioDevice>
#include <QtMultimedia/QMediaDevices>

#include <cstdint>

class SoundEditorPlayer final : public QObject
{
    Q_OBJECT

public:
    explicit SoundEditorPlayer(QObject* parent = nullptr);
    ~SoundEditorPlayer() override;

public slots:
    void startSongStream(const QVariantList& rows, int rowMs, bool loop);
    void setChannelAudible(int channel, bool audible);
    void stopSongStream();
    void hardReset();

signals:
    void previewVuMeterChanged(int channel, int level);
    void previewVuMetersChanged(int ch1, int ch2, int ch3, int noise);

private:
    struct StreamRow {
        int period[7] = {-1, -1, -1, -1, -1, -1, -1};
        int volume[7] = {-1, -1, -1, -1, -1, -1, -1};
        int env[7]    = { 3,  3,  3,  3,  3,  3,  3};
        int waveX[7]  = {50, 50, 50, 50, 50, 50, 50};
        int waveY[7]  = {50, 50, 50, 50, 50, 50, 50};
        // AY instrument synthesis parameters. Used only for channels 4..6.
        int ayTone[7] = {1,1,1,1,1,1,1};
        int ayNoise[7] = {0,0,0,0,0,0,0};
        int ayHwEnv[7] = {0,0,0,0,0,0,0};
        int ayShape[7] = {0,0,0,0,0,0,0};
        int ayEnvPeriod[7] = {0,0,0,0,0,0,0};
        int ayNoisePeriod[7] = {0,0,0,0,0,0,0};
        int ayAttack[7] = {0,0,0,0,0,0,0};
        int ayDecay[7] = {0,0,0,0,0,0,0};
        int aySustain[7] = {15,15,15,15,15,15,15};
        int ayRelease[7] = {0,0,0,0,0,0,0};
        int ayVibrato[7] = {0,0,0,0,0,0,0};
        QString ayArp[7];
        QString snVolumeMacro[7];
        QString snPitchMacro[7];
        QString snArpMacro[7];
        QString snNoiseMacro[7];
        QString ayVolumeMacro[7];
        QString ayPitchMacro[7];
        QString ayNoiseMacro[7];
        QString ayAutoEnv[7];
        QString ayWaveMacro[7];
        QString ayEnvShapeMacro[7];
        QString ayEnvPeriodMacro[7];
        QString ayPhaseResetMacro[7];

        // true only for marker-841 rows that already contain final CVBasic/PSG
        // register-frame values. Normal .adpsnd/MIDI rows remain false and must
        // still use the editor instrument/macros.
        bool finalRenderedFrame = false;
    };

    struct ChannelState {
        bool active = false;
        int period = 0;
        int volume = 0;
        int env = 3;
        double frequency = 0.0;
        double phase = 0.0;
        double noteTime = 0.0;
        double smoothSample = 0.0;
        double duty = 0.50;
        int waveX = 50;
        int waveY = 50;
        int noiseCode = 0;
        quint32 noiseRng = 0xACE1u;
        double noisePhase = 0.0;
        double noiseValue = 0.0;
        int transitionSamples = 0;

        // SGM/AY runtime instrument state.
        bool ayTone = true;
        bool ayNoise = false;
        bool ayHwEnv = false;
        int ayShape = 0;
        int ayEnvPeriod = 0;
        int ayNoisePeriod = 0;
        int ayAttack = 0;
        int ayDecay = 0;
        int aySustain = 15;
        int ayRelease = 0;
        int ayVibrato = 0;
        QVector<int> ayArp;
        QVector<int> snVolumeMacro;
        QVector<int> snPitchMacro;
        QVector<int> snArpMacro;
        double snArpTime = 0.0;
        QVector<int> snNoiseMacro;
        QVector<int> ayVolumeMacro;
        QVector<int> ayPitchMacro;
        QVector<int> ayNoiseMacro;
        QVector<int> ayWaveMacro;
        QVector<int> ayEnvShapeMacro;
        QVector<int> ayEnvPeriodMacro;
        QVector<int> ayPhaseResetMacro;
        double ayAutoEnvRatio = 0.0;
        double ayNoisePhase = 0.0;
        double ayNoiseValue = 0.0;
        quint32 ayNoiseRng = 0xB400u;
        bool releasing = false;
        double releaseStartTime = 0.0;
        double releaseStartLevel = 1.0;

        // false = normal Sound Editor instrument synthesis is active.
        // true  = this row already contains final CVBasic/PSG register values.
        bool finalRenderedFrame = false;
    };

    class AudioDevice final : public QIODevice
    {
    public:
        explicit AudioDevice(SoundEditorPlayer* player);

        void start();
        void stop();
        void setRenderingEnabled(bool enabled);

        qint64 readData(char* data, qint64 maxlen) override;
        qint64 writeData(const char*, qint64) override { return 0; }
        qint64 bytesAvailable() const override;

    private:
        SoundEditorPlayer* m_player = nullptr;
        bool m_renderingEnabled = false;
    };

private:
    void render(int16_t* stereo, int frames);
    void applyNextRowLocked();
    void clearStateLocked();

    static double envelopeFactor(int env, double t, double duration);
    static double pitchMultiplier(int env, double t);
    static double dutyForEnvelope(int env);
    static double loudnessForEnvelope(int env);
    static double smoothingForEnvelope(int env, bool noise);
    static double ayAdsrFactor(const ChannelState& ch, double t);
    static double ayHardwareEnvelopeFactor(const ChannelState& ch, double t);
    static double ayPitchMultiplier(const ChannelState& ch, double t);
    static QVector<int> parseArpeggio(const QString& text);
    static QVector<int> parseMacro(const QString& text, int minValue, int maxValue, int base = 10);
    static int macroValueAt(const QVector<int>& macro, double t, int fallback);
    static double parseRatio(const QString& text);

    void setPendingVuLevelLocked(int channel, int level);
    void flushVu();

private:
    QAudioSink* m_sink = nullptr;
    AudioDevice* m_device = nullptr;
    QTimer* m_vuTimer = nullptr;

    QMutex m_mutex;

    QVector<StreamRow> m_rows;
    ChannelState m_channels[7];
    bool m_channelAudible[7] = {true, true, true, true, true, true, true};

    bool m_playing = false;
    bool m_loop = false;
    int m_rowIndex = 0;
    int m_samplesUntilNextRow = 0;
    int m_samplesPerRow = 7350;
    int m_sampleRate = 44100;
    quint64 m_generation = 0;

    // AY-3-8910 has one shared noise generator and one shared hardware-envelope
    // generator for channels A/B/C. Keep those resources global in the PC preview
    // so the editor behaves like the SGM hardware and the CVBasic SOUND 8/9 stream.
    double m_aySharedNoisePhase = 0.0;
    double m_aySharedNoiseValue = 1.0;
    quint32 m_aySharedNoiseRng = 0x1FFFFu;
    int m_debugAppliedRows = 0;

    int m_vuLevels[7] = {0, 0, 0, 0, 0, 0, 0};
    int m_pendingVuLevels[7] = {0, 0, 0, 0, 0, 0, 0};
};

#endif // SOUNDEDITORPLAYER_H
