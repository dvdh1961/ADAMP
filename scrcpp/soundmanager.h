#ifndef SOUNDMANAGER_H
#define SOUNDMANAGER_H

#include "audio_sink.h"
#include <QObject>
#include <QTimer>
#include <QPointer>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dsound.h>
#endif
#if defined(Q_OS_LINUX)
#include <alsa/asoundlib.h>  // ALSA for Linux
#endif

#include <cstdint>
#include <QMutex>
#include <QVariantList>

static const int kSampleRate  = 44100;
static const int kChunkFrames = 735;
static const int kBytesPerFrame = 4;

class ColecoController;

class SoundManager : public QObject, public IAudioSink
{
    Q_OBJECT
public:
    explicit SoundManager(QObject *parent = nullptr);
    ~SoundManager();

#if defined(Q_OS_WIN)
    bool initialise(HWND hwnd, int fpsHint);
    bool reInitialise(HWND hwnd, int fpsHint);
#endif
#if defined(Q_OS_LINUX)
    bool initialise(int fpsHint);
    bool reInitialise(int fpsHint);
#endif

    void suspend();
    void resume();

    void end();

    void attachController(ColecoController *ctrl);
    void pushAudioFromEmu(const int16_t* srcStereo, int framesStereo);

signals:
    // VU meter feedback for the CVBasic Sound Editor.
    // channel: 0=CH1, 1=CH2, 2=CH3, 3=NOISE
    // level:   0=silent, 15=maximum activity
    void previewVuMeterChanged(int channel, int level);
    void previewVuMetersChanged(int ch1, int ch2, int ch3, int noise);

public slots:
    // Preview tone/noise from CVBasic Sound Editor.
    // channel: 0=CH1, 1=CH2, 2=CH3, 3=NOISE
    // psgPeriod: tone period 1..1023 for CH1-CH3, noise code 0..7 for NOISE
    // volume: editor volume 0..15, where 0=silent, 15=loudest
    void previewPsgNote(int channel, int psgPeriod, int volume, int instrumentEnv = 3);

    // Hard stop all preview audio and clear playback buffers.
    void hardStopPreviewAudio();

    // Real Sound Editor stream playback. This is not one-shot preview.
    void startSoundEditorStream(const QVariantList& rows, int rowMs, bool loop);
    void stopSoundEditorStream();

private:
#if defined(Q_OS_WIN)
    bool initDirectSound(HWND hwnd, int fpsHint);
    void releaseDirectSound();

    bool playDirectSoundPreviewSnapshot();
    void releaseDirectSoundPreviewBuffer();

    bool createPrimaryBuffer(const WAVEFORMATEX &wfx);
    bool createSecondaryBuffer(const WAVEFORMATEX &wfx);

    void refillSecondaryBuffer();
    bool fetchSamplesFromEmu(int16_t *dst, int framesStereo);
#endif
#if defined(Q_OS_LINUX)
    bool initALSA(int fpsHint);  // ALSA initialization
    void releaseALSA();          // Release ALSA resources

    bool createPCMBuffer();      // Create ALSA PCM buffer
    void refillPCMBuffer();      // Refill ALSA PCM buffer
    bool fetchSamplesFromEmu(int16_t *dst, int framesStereo);
#endif

    void mixPreviewIntoBuffer(int16_t* stereo, int framesStereo);
    bool hasActivePreview() const;
    void startPreviewTimerIfNeeded();
    void stopPreviewTimerIfIdle();
    void writePreviewOnlyChunk();
    void writeSilencePreviewChunk();
    void clearPreviewStateAndBuffer();
#if defined(Q_OS_WIN)
    void stopAndClearDirectSoundPreview();
    void startDirectSoundPreviewIfNeeded();
#endif
    void setPreviewVuLevel(int channel, int level);
    void decayPreviewVuMeters();
    void emitPreviewVuMetersLocked();

private:
    struct PreviewChannel {
        bool active = false;
        int psgPeriod = 0;
        int volume = 0;
        double phase = 0.0;
        double frequency = 0.0;
        int noiseCode = 0;
        int instrumentEnv = 3;
        quint32 noiseRng = 0xACE1u;
        double noisePhase = 0.0;
        double noiseValue = 0.0;

        // Kleine smoothing tegen klik/kraak bij harde square-wave overgangen.
        double smoothSample = 0.0;

        // Tijd sinds laatste nootstart, nodig voor instrument/envelope verschillen.
        double noteTime = 0.0;

        // Kleine fade-in na een nieuwe noot/instrument-wissel.
        int transitionSamples = 0;

        // >0 = manual keyboard preview auto-release counter.
        // -1 = stream/song playback, do not auto-release here.
        int previewSamplesRemaining = 0;
    };

    struct StreamRow {
        int period[4] = {-1, -1, -1, -1};
        int volume[4] = {-1, -1, -1, -1};
        int env[4]    = { 3,  3,  3,  3};
    };

private:
    QPointer<ColecoController> m_controller;

#if defined(Q_OS_WIN)
    LPDIRECTSOUND8       m_ds            = nullptr;
    LPDIRECTSOUNDBUFFER  m_primaryBuffer = nullptr;
    LPDIRECTSOUNDBUFFER  m_secondaryBuf  = nullptr;
    LPDIRECTSOUNDBUFFER  m_previewBuf    = nullptr; // one-shot Sound Editor preview buffer
    DWORD   m_sampleRate     = 44100; // Hz
    WORD    m_channels       = 2;     // stereo
    WORD    m_bitsPerSample  = 16;    // signed 16-bit
    DWORD   m_bytesPerFrame  = 4;     // stereo16 = 4 bytes per frame

    DWORD   m_bufferBytes    = 0;     // totale lengte van de secondary buffer
    DWORD   m_lastWritePos   = 0;     // waar we laatst geschreven hebben
#endif
#if defined(Q_OS_LINUX)
    // ALSA variables
    snd_pcm_t *m_pcmHandle = nullptr;
    snd_pcm_hw_params_t *m_params = nullptr;
    unsigned int m_sampleRate     = 44100; // Hz
    unsigned int m_channels       = 2;     // stereo
    unsigned int m_bitsPerSample  = 16;    // signed 16-bit
    unsigned int m_bytesPerFrame  = 4;     // stereo16 = 4 bytes per frame

    unsigned int m_bufferBytes    = 0;     // totale lengte van de secondary buffer
    unsigned int m_lastWritePos   = 0;     // waar we laatst geschreven hebben
#endif


    bool    m_inited         = false;
    bool    m_suspended      = false;
    bool    m_running        = false;

    static const int kChunkFrames = 735;
    int16_t m_mixBufferInterleaved[kChunkFrames * 2];

    int16_t m_lastAudioChunk[kChunkFrames * 2];
    bool    m_lastAudioValid = false;
    QMutex  m_audioMutex;

    PreviewChannel m_previewChannels[4];
    QMutex m_previewMutex;

    QVector<StreamRow> m_streamRows;
    int m_streamRowIndex = 0;
    int m_streamSamplesUntilNextRow = 0;
    int m_streamSamplesPerRow = 7350;
    bool m_streamPlaying = false;
    bool m_streamLoop = false;
    QTimer* m_previewTimer = nullptr;
    QTimer* m_previewVuDecayTimer = nullptr;
    int m_previewSilenceChunksRemaining = 0;
    bool m_previewPlaybackStarted = false;
    int m_previewVuLevels[4] = {0, 0, 0, 0};
    int m_previewVuPendingLevels[4] = {0, 0, 0, 0};
    bool m_previewVuDirty = false;
    QMutex m_previewVuMutex;
};

#endif
