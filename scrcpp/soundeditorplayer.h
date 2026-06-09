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
    void stopSongStream();
    void hardReset();

signals:
    void previewVuMeterChanged(int channel, int level);
    void previewVuMetersChanged(int ch1, int ch2, int ch3, int noise);

private:
    struct StreamRow {
        int period[4] = {-1, -1, -1, -1};
        int volume[4] = {-1, -1, -1, -1};
        int env[4]    = { 3,  3,  3,  3};
        int waveX[4]  = {50, 50, 50, 50};
        int waveY[4]  = {50, 50, 50, 50};
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

    void setPendingVuLevelLocked(int channel, int level);
    void flushVu();

private:
    QAudioSink* m_sink = nullptr;
    AudioDevice* m_device = nullptr;
    QTimer* m_vuTimer = nullptr;

    QMutex m_mutex;

    QVector<StreamRow> m_rows;
    ChannelState m_channels[4];

    bool m_playing = false;
    bool m_loop = false;
    int m_rowIndex = 0;
    int m_samplesUntilNextRow = 0;
    int m_samplesPerRow = 7350;
    int m_sampleRate = 44100;
    quint64 m_generation = 0;
    int m_debugAppliedRows = 0;

    int m_vuLevels[4] = {0, 0, 0, 0};
    int m_pendingVuLevels[4] = {0, 0, 0, 0};
};

#endif // SOUNDEDITORPLAYER_H
