#include "soundmanager.h"
#include "colecocontroller.h"

#include <QDebug>
#include <QMetaObject>
#include <cstring> // memset, memcpy
#include <QMutexLocker>
#include <QDateTime>

// debug counters

SoundManager::SoundManager(QObject *parent)
    : QObject(parent)
{
    m_inited    = false;
    m_suspended = false;
    m_running   = false;
}

SoundManager::~SoundManager()
{
    end();
}

void SoundManager::attachController(ColecoController *ctrl)
{
    m_controller = ctrl;
}

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
    releaseDirectSound();
    m_inited = false;
}

// ----------------------
// Private helpers
// ----------------------

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

    // dubbelbuffer
    m_bufferBytes = bytesPerFrameOfEmu * 2;  // ≈ 5880 bytes totaal

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

void SoundManager::releaseDirectSound()
{
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
    std::memcpy(p1, srcStereo, len1);
    if (p2 && len2 > 0) {
        std::memcpy(p2,
                    reinterpret_cast<const BYTE*>(srcStereo) + len1,
                    len2);
    }

    m_secondaryBuf->Unlock(p1, len1, p2, len2);

    // schuif write cursor met blockBytes, niet met "full chunkBytes"
    m_lastWritePos = (m_lastWritePos + blockBytes) % m_bufferBytes;
}
