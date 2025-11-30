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

private:
#if defined(Q_OS_WIN)
    bool initDirectSound(HWND hwnd, int fpsHint);
    void releaseDirectSound();

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

private:
    QPointer<ColecoController> m_controller;

#if defined(Q_OS_WIN)
    LPDIRECTSOUND8       m_ds            = nullptr;
    LPDIRECTSOUNDBUFFER  m_primaryBuffer = nullptr;
    LPDIRECTSOUNDBUFFER  m_secondaryBuf  = nullptr;
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
};

#endif
