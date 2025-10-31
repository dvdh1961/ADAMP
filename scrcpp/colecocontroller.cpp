#include "colecocontroller.h"
#include <QThread>
#include <QDebug>
#include <QFileInfo>
#include <QCoreApplication> // Nodig voor processEvents
#include <cstring>
#include <QAudioFormat>

// ==== C++-core header ====
#include "coleco.h"
#include "psg_bridge.h"

// ==== C-core headers ====
extern "C" {
#include "emu.h"
#include "tms9928a.h"
#include "video_bridge.h"
}

// ==== Palet-symbolen ====
extern unsigned char cv_palette[];
extern int           cv_pal32[];
void RenderCalcPalette(unsigned char *dst, int nbcolors);

// =====================================================================================

ColecoController::ColecoController(QObject *parent)
    : QObject(parent)
    , m_realFrames(0)
    , m_paused(false)
    , m_audioSink(nullptr)
    , m_audioDevice(nullptr)
    , m_running(false)
    , m_fpsFrameCount(0) // <-- Init
{
    // Alloceer buffers groot genoeg voor PAL (882 samples)
    m_monoBuf = new int16_t[882];
    m_stereoBuf = new int16_t[882 * 2];

    m_BytesPerSampleStereo = 4; // 16-bit stereo
    m_SampleRate = 44100;

    // Default op NTSC
    setVideoStandard(true);
}

ColecoController::~ColecoController()
{
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
    }
    delete[] m_monoBuf;
    delete[] m_stereoBuf;
}

// --- De video-standaard slot ---
void ColecoController::setVideoStandard(bool isNTSC)
{
    m_isNTSC = isNTSC;

    if (m_isNTSC) {
        // NTSC Timings
        m_Clock = 3579545;
        m_AudioChunkFrames = 735; // 44100 / 60
        machine.tperscanline = 228;
        tms.ScanLines = 262;
    } else {
        // PAL Timings
        m_Clock = 3546893;
        m_AudioChunkFrames = 882; // 44100 / 50
        machine.tperscanline = 227;
        tms.ScanLines = 313;
    }

    m_AudioChunkBytes = m_AudioChunkFrames * m_BytesPerSampleStereo;
    m_tstates_per_sample = (double)m_Clock / (double)m_SampleRate;

    emit videoStandardChanged(m_isNTSC ? "NTSC" : "PAL");

    if (m_running) {
        qDebug() << "[Controller] Resetting core for new video standard.";
        PsgBridge::reset(m_Clock, m_SampleRate);
        coleco_reset();
    }
}


void ColecoController::startEmulation()
{
    qDebug() << "[Controller] startEmulation()";

    // 1. Setup Core Emulatie
    m_realFrames = 0;
    coleco_initialise();
    PsgBridge::init(m_Clock, m_SampleRate);
    PsgBridge::reset(m_Clock, m_SampleRate);
    machine.interrupt = 0;

    emit videoStandardChanged(m_isNTSC ? "NTSC" : "PAL");

    // Palet setup
    static const int TMS9918_PAL16[16] = {
        0x000000, 0x000000, 0x21C842, 0x5EDC78,
        0x5455ED, 0x7D76FC, 0xD4524D, 0x42EBF5,
        0xFC5554, 0xFF7978, 0xD4C154, 0xE6CE80,
        0x21B03B, 0xC95BBA, 0xCCCCCC, 0xFFFFFF
    };
    extern int cv_pal32[];
    for (int bank = 0; bank < 4; ++bank)
        for (int i = 0; i < 16; ++i)
            cv_pal32[bank*16 + i] = TMS9918_PAL16[i];

    // 2. Setup Qt Audio
    QAudioFormat format;
    format.setSampleRate(m_SampleRate);
    format.setChannelCount(2); // Stereo
    format.setSampleFormat(QAudioFormat::Int16);

    m_audioSink = new QAudioSink(format, this);

    int bufferSizeBytes = 16384;
    m_audioSink->setBufferSize(bufferSizeBytes);

    m_audioDevice = m_audioSink->start();
    if (!m_audioDevice) {
        qWarning() << "!!! FATAL: Kon QAudioSink niet starten!";
        emit emulationStopped();
        return;
    }

    // 3. De Hoofd-Loop
    m_running = true;
    m_paused = false;
    emit emuPausedChanged(false);

    qDebug() << "[Controller] Emulation loop running met QAudioSink (met throttle)...";

    double tstate_accumulator = 0.0;

    // Start de FPS-timer
    m_fpsFrameCount = 0;
    m_fpsCalcTimer.start();

    while (m_running)
    {
        // === A: Verwerk events (pause, stop, etc.) ===
        QCoreApplication::processEvents();

        // === B: Pauze-afhandeling ===
        if (m_paused) {
            // Reset de FPS-timer tijdens pauze
            m_fpsCalcTimer.restart();
            m_fpsFrameCount = 0;
            QThread::msleep(50);
            continue;
        }

        // === C: DE KLOK / THROTTLE ===
        while (m_audioSink->bytesFree() < m_AudioChunkBytes) {
            QThread::msleep(5);
            QCoreApplication::processEvents();
            if (!m_running || m_paused) break;
        }
        if (!m_running || m_paused) continue;

        // === D: Emuleer 1 frame (audio per scanline) ===
        int samples_generated = 0;
        tstate_accumulator = 0.0;

        for (int i = 0; i < tms.ScanLines; ++i)
        {
            int tstates_this_line = coleco_do_scanline();
            tstate_accumulator += tstates_this_line;

            while (tstate_accumulator >= m_tstates_per_sample)
            {
                if (samples_generated < m_AudioChunkFrames) {
                    PsgBridge::getSamples(&m_monoBuf[samples_generated], 1);
                    samples_generated++;
                }
                tstate_accumulator -= m_tstates_per_sample;
            }
        }

        int samples_to_fill = m_AudioChunkFrames - samples_generated;
        if (samples_to_fill > 0 && samples_to_fill < m_AudioChunkFrames) {
            PsgBridge::getSamples(&m_monoBuf[samples_generated], samples_to_fill);
        }

        // === E: Converteer naar Stereo ===
        int16_t* d = m_stereoBuf;
        for (int i = 0; i < m_AudioChunkFrames; ++i) {
            int32_t v = m_monoBuf[i];
            v /= 4; // Volume/clipping fix
            int16_t s16 = (int16_t)v;
            d[0] = s16;
            d[1] = s16;
            d += 2;
        }

        // === F: SCHRIJF NAAR AUDIOKAART ===
        m_audioDevice->write(
            reinterpret_cast<const char*>(m_stereoBuf),
            m_AudioChunkBytes
            );

        // === G: Stuur videoframe uit ===
        if (g_video_dirty) {
            g_video_dirty = 0;
            QImage real = frameFromBridge();
            emit frameReady(real);
        }

        // === H: FPS BEREKENING ===
        m_fpsFrameCount++;
        qint64 elapsed = m_fpsCalcTimer.elapsed();
        if (elapsed >= 1000) {
            emit fpsUpdated(m_fpsFrameCount); // Stuur de FPS naar de GUI
            m_fpsFrameCount = 0;             // Reset de teller
            m_fpsCalcTimer.restart();        // Herstart de 1-seconde timer
        }
    }

    // 5. Opruimen
    qDebug() << "[Controller] Emulation loop finished.";
    if (m_audioSink) {
        m_audioSink->stop();
    }
    emit emulationStopped();
}

void ColecoController::pauseEmulation()
{
    if (m_paused) return;
    qDebug() << "[Controller] pauseEmulation()";
    m_paused = true;
    if (m_audioSink) m_audioSink->suspend();
    emit emuPausedChanged(true);
}

void ColecoController::resumeEmulation()
{
    if (!m_paused) return;
    qDebug() << "[Controller] resumeEmulation()";
    m_paused = false;
    if (m_audioSink) m_audioSink->resume();
    emit emuPausedChanged(false);
}

void ColecoController::stopEmulation()
{
    qDebug() << "[Controller] stopEmulation() requested.";
    m_running = false; // Dit stopt de 'while(m_running)' loop
}

void ColecoController::stepOnce()
{
    if (!m_paused) {
        pauseEmulation();
    }
    qDebug() << "[Controller] stepOnce()";

    for (int i = 0; i < tms.ScanLines; ++i) {
        coleco_do_scanline();
    }

    if (g_video_dirty) {
        g_video_dirty = 0;
        QImage real = frameFromBridge();
        emit frameReady(real);
    }

    QCoreApplication::processEvents();
}

void ColecoController::loadRom(const QString &romPath)
{
    qDebug() << "[Controller] loadRom:" << romPath;
    m_realFrames = 0;

    QByteArray path = QFile::encodeName(romPath);
    BYTE ok = coleco_loadcart(path.data());
    if (ok != 0) {
        qWarning() << "[Controller] ROM laden faalde, code =" << int(ok);
        return;
    }

    coleco_reset();
    resumeEmulation();
}

void ColecoController::resetMachine()
{
    qDebug() << "[Controller] resetMachine()";
    coleco_reset();
    PsgBridge::reset(m_Clock, m_SampleRate);
}

void ColecoController::resethMachine()
{
    qDebug() << "[Controller] hard resetMachine()";
    coleco_hardreset();
    coleco_reset_and_restart_bios();
    PsgBridge::reset(m_Clock, m_SampleRate);
}

void ColecoController::setSGMEnabled(bool enabled)
{
    qDebug() << "[COLECO] SGM set =" << enabled;
    emul2->SGM = enabled ? 1 : 0;

    if (enabled) {
        coleco_writeport(0x60, 0x0F, nullptr);
        coleco_writeport(0x53, 0x01, nullptr);
    } else {
        coleco_writeport(0x53, 0x00, nullptr);
        coleco_writeport(0x60, 0x0F, nullptr);
    }
    coleco_reset_and_restart_bios();
}

QImage ColecoController::frameFromBridge()
{
    QImage img(VB_WIDTH, VB_HEIGHT, QImage::Format_ARGB32);

    for (int y = 0; y < VB_HEIGHT; ++y) {
        void *dstLine = img.scanLine(y);
        const void *srcLine = &g_video_frame[y * VB_WIDTH];
        std::memcpy(dstLine, srcLine, VB_WIDTH * sizeof(uint32_t));
    }
    return img;
}
