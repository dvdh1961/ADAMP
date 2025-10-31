#ifndef COLECOCONTROLLER_H
#define COLECOCONTROLLER_H

#include <QObject>
#include <QImage>
#include <QAudioSink>
#include <QIODevice>
#include <QElapsedTimer> // <-- Nodig voor FPS-berekening

class ColecoController : public QObject
{
    Q_OBJECT

public:
    explicit ColecoController(QObject *parent = nullptr);
    ~ColecoController();

private:
    QImage m_frameBuffer;
    bool m_paused = false;

    // === AUDIO LEDEN ===
    QAudioSink* m_audioSink = nullptr;
    QIODevice* m_audioDevice = nullptr;

    int16_t* m_monoBuf = nullptr;
    int16_t* m_stereoBuf = nullptr;

private slots:
    // Geen slots nodig voor de loop

public slots:
    // == COMMANDOS (aangeroepen vanuit UI-thread) ==
    void startEmulation();
    void stopEmulation();
    void loadRom(const QString &romPath);
    void resetMachine();
    void resethMachine();
    void setSGMEnabled(bool enabled);
    void setVideoStandard(bool isNTSC);

    // ===== Debugger control =====
    void pauseEmulation();
    void resumeEmulation();
    void stepOnce();
    bool isPaused() const { return m_paused; }

signals:
    // == NOTIFICATIES (verzonden naar UI-thread) ==
    void frameReady(const QImage &frame);
    void emulationStopped();
    void emuPausedChanged(bool paused);
    void videoStandardChanged(const QString &standard);
    void fpsUpdated(int fps); // <-- NIEUW SIGNAAL

private:
    bool     m_running = false;
    int      m_realFrames = 0;

    // === TIMING LEDEN ===
    bool   m_isNTSC;
    int    m_Clock;
    int    m_SampleRate;
    int    m_AudioChunkFrames;
    int    m_AudioChunkBytes;
    double m_tstates_per_sample;
    int    m_BytesPerSampleStereo;

    // === FPS BEREKENING LEDEN ===
    QElapsedTimer m_fpsCalcTimer;
    int m_fpsFrameCount;

    // interne helper
    QImage frameFromBridge();
};

#endif // COLECOCONTROLLER_H
