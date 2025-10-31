#ifndef SOUNDMANAGER_H
#define SOUNDMANAGER_H

#include "audio_sink.h"
#include <QObject>
#include <QTimer>
#include <QPointer>

#include <windows.h>
#include <dsound.h>
#include <cstdint>
#include <QMutex>

static const int kSampleRate  = 44100;
static const int kChunkFrames = 735;   // 44100 / 60
static const int kBytesPerFrame = 4;   // stereo 16-bit

// Forward declare jouw controller
class ColecoController;

/**
 * SoundManager
 *
 * Qt-versie van de EmulTwo audio backend:
 * - initialise(hwnd, fpsHint): DirectSound device + buffers klaarzetten
 * - suspend()/resume(): tijdelijk stilzetten zonder stoppen
 * - end(): netjes opruimen
 *
 * Er draait een QTimer (m_fillTimer) in de GUI-thread.
 * Elke tik vraagt hij verse audio van de emulatorthread
 * via mixAudioInternal() en schrijft dat in de DirectSound
 * secondary buffer.
 */
class SoundManager : public QObject, public IAudioSink
{
    Q_OBJECT
public:
    explicit SoundManager(QObject *parent = nullptr);
    ~SoundManager();

    // hwnd = (HWND)MainWindow::winId()
    // fpsHint = 60 (NTSC) of 50 (PAL). Gebruik voorlopig 60.
    bool initialise(HWND hwnd, int fpsHint);
    bool reInitialise(HWND hwnd, int fpsHint);

    void suspend();
    void resume();

    void end();

    // MainWindow moet de controller doorgeven zodat we audio kunnen vragen.
    void attachController(ColecoController *ctrl);
    void pushAudioFromEmu(const int16_t* srcStereo, int framesStereo);

private:
    // Interne helpers
    bool initDirectSound(HWND hwnd, int fpsHint);
    void releaseDirectSound();

    bool createPrimaryBuffer(const WAVEFORMATEX &wfx);
    bool createSecondaryBuffer(const WAVEFORMATEX &wfx);

    void refillSecondaryBuffer();
    bool fetchSamplesFromEmu(int16_t *dst, int framesStereo);
private:
    // Emu controller (zit in QThread). Wordt gebruikt via invokeMethod(BlockingQueuedConnection)
    QPointer<ColecoController> m_controller;

    // DirectSound
    LPDIRECTSOUND8       m_ds            = nullptr;
    LPDIRECTSOUNDBUFFER  m_primaryBuffer = nullptr;
    LPDIRECTSOUNDBUFFER  m_secondaryBuf  = nullptr;

    // audioformat
    DWORD   m_sampleRate     = 44100; // Hz
    WORD    m_channels       = 2;     // stereo
    WORD    m_bitsPerSample  = 16;    // signed 16-bit
    DWORD   m_bytesPerFrame  = 4;     // stereo16 = 4 bytes per frame

    // ringbuffer in DirectSound
    DWORD   m_bufferBytes    = 0;     // totale lengte van de secondary buffer
    DWORD   m_lastWritePos   = 0;     // waar we laatst geschreven hebben

    bool    m_inited         = false;
    bool    m_suspended      = false; // pauze / mute
    bool    m_running        = false; // timer actief?

    // Grootte van één "chunk" die we per timer-tick schrijven.
    // 512 stereo frames = 512 * 4 = 2048 bytes per keer → prima.
    static const int kChunkFrames = 735;
    int16_t m_mixBufferInterleaved[kChunkFrames * 2]; // L,R,L,R,...

    // laatst opgehaalde stereo mix uit de emu-thread
    int16_t m_lastAudioChunk[kChunkFrames * 2];
    bool    m_lastAudioValid = false;
    QMutex  m_audioMutex;
};

#endif // SOUNDMANAGER_H
