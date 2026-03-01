#include "colecocontroller.h"
#include "printwindow.h"
#include <QThread>
#include <QDebug>
#include <QFileInfo>
#include <QCoreApplication>
#include <cstring>
#include <QAudioFormat>
#include <QTimer>
#include <QFile>
#include <QDir>

// ==== C++-core header ====
#include "coleco.h"
#include "psg_bridge.h"

// ==== C-core headers ====
extern "C" {
#include "emu.h"
#include "tms9928a.h"
#include "video_bridge.h"
#include "adamnet.h"
#include "ay8910.h"
#include "z80.h"
}

#include <stdint.h>

ColecoController *g_controller = nullptr;
static std::atomic<bool> g_dtSoundEnabled{true};


extern "C" void adamnet_inject_scancode(uint8_t sc);
extern "C" void adamnet_block_ascii_fkeys(int count);
extern "C" void PutKBD(unsigned int Key);
extern "C" void adamnet_host_prn_write_ascii(const char* s);
extern "C" void coleco_set_bios_paths(const char* coleco_path, const char* eos_path, const char* writer_path);
extern "C" void sendAsciiSequenceWithDelay(QObject* context,const QByteArray& seq, int delayMs = 80)
{
    int t = 0;
    for (unsigned char ch : seq) {
        QTimer::singleShot(t, context, [ch]() {
            PutKBD(static_cast<unsigned int>(ch));
        });
        t += delayMs;
    }
}

// ==== Palet-symbolen ====
extern unsigned char cv_palette[];
extern int           cv_pal32[];
void RenderCalcPalette(unsigned char *dst, int nbcolors);

// =====================================================================================
ColecoController::ColecoController(QObject *parent)
    : QObject(parent)
    , m_running(false)
    , m_realFrames(0)
    , m_paused(false)
    , m_audioSink(nullptr)
    , m_audioDevice(nullptr)
    , m_currentColecoCartPath()
    , m_currentAdamCartPath()
    , m_fpsFrameCount(0)
{
    g_controller = this;
    m_monoBuf = new int16_t[882];
    m_stereoBuf = new int16_t[882 * 2];

    m_BytesPerSampleStereo = 4;
    m_SampleRate = 44100;

    setVideoStandard(true);

    // In de constructor van ColecoController (colecocontroller.cpp)
    QTimer *heartbeat = new QTimer(this);

    // Aparte statische timers voor onafhankelijke cooldown
    static QElapsedTimer diskCooldown;
    static QElapsedTimer tapeCooldown;
    if (!diskCooldown.isValid()) diskCooldown.start();
    if (!tapeCooldown.isValid()) tapeCooldown.start();

    connect(heartbeat, &QTimer::timeout, this, [this]() {
        if (!m_running || m_paused) return;

        // Globale enable/disable voor disk/tape "activity" sounds


        if (!g_dtSoundEnabled.load()) {
            // Consume triggers zodat ze niet blijven opstapelen terwijl geluid uit staat
            g_diskSoundActive.store(false);
            g_tapeSoundActive.store(false);
            return;
        }

        // Onafhankelijke check voor Disk
        if (g_diskSoundActive.exchange(false)) {
            if (diskCooldown.elapsed() > 60) {
                emit requestPlayDiskSound();
                diskCooldown.restart();
            }
        }

        // Onafhankelijke check voor Tape
        if (g_tapeSoundActive.exchange(false)) {
            if (tapeCooldown.elapsed() > 60) {
                emit requestPlayTapeSound();
                tapeCooldown.restart();
            }
        }

    });
    heartbeat->start(35);
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

extern "C" {
extern tTMS9981A tms;
}

void ColecoController::onAdamKeyEvent(int code)
{
    //qDebug() << "[CTRL] onAdamKeyEvent ontvangen:" << Qt::hex << code << " CODE";
    if (code >= 0x100) {
        uint8_t sc = uint8_t(code & 0xFF);
        //qDebug() << "  -> ROUTE: ADAMNET Scancode:" << Qt::hex << sc << ")";
        adamnet_inject_scancode(sc);
    } else {
        uint8_t ascii = uint8_t(code & 0xFF);
        //qDebug() << "  -> ROUTE: ASCII BUFFER (PutKBD:" << Qt::hex << ascii << ")";
        PutKBD(unsigned(ascii));
    }
}

// --- video-standaard slot ---
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
        qDebug() << "[CTRL] Resetting core for new video standard.";
        PsgBridge::reset(m_Clock, m_SampleRate);
        coleco_reset();
    }
}

void ColecoController::startEmulation()
{
    qDebug() << "[CTRL] startEmulation()";

    m_frameCCounter = 0;
    m_frameACounter = 0;
    m_realFrames = 0;

    if (emulator->currentMachineType != MACHINEADAM)
    {
        coleco_initialise();
    }
    else
    {
    if (!m_coreInitialized) {
        qDebug() << "[CTRL] Core init (cold start).";
        coleco_initialise();  // doet base_init + BIOS load + reset_bios
        m_coreInitialized = true;
    } else {
        // géén coleco_initialise() meer! enkel herstart mapping/CPU/VDP
        qDebug() << "[CTRL] Core restart (no initialise).";
        coleco_reset_and_restart_bios();
    }
    }

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

    //qDebug() << "[CTRL] Emulation loop running met QAudioSink (met throttle)...";

    double tstate_accumulator = 0.0;

    // Start de FPS-timer
    m_fpsFrameCount = 0;
    m_fpsCalcTimer.start();

    while (m_running)
    {
        if (m_waitForCBiosFrames)
            m_frameCCounter++;
        if (m_waitForCBiosFrames && m_frameCCounter >= 10) {
            m_waitForCBiosFrames = false;

            emit biosCFramesDone();
        }

        if (m_waitForABiosFrames)
            m_frameACounter++;
        if (m_waitForABiosFrames && m_frameACounter >= 10) {
            m_waitForABiosFrames = false;

            emit biosAFramesDone();
        }

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

            // Optioneel: stop scanlines verder verwerken zodra de core gestopt is
            if (emulator->stop) {
                break;
            }
        }

        if (emulator->stop) {
            // CPU staat stil door breakpoint → audio stil maken
            for (int i = 0; i < m_AudioChunkFrames; ++i) {
                m_monoBuf[i] = 0;
            }
        } else {
            int samples_to_fill = m_AudioChunkFrames - samples_generated;
            if (samples_to_fill > 0 && samples_to_fill < m_AudioChunkFrames) {
                PsgBridge::getSamples(&m_monoBuf[samples_generated], samples_to_fill);
            }
        }

        // === E: Converteer naar Stereo ===
        int16_t* d = m_stereoBuf;
        for (int i = 0; i < m_AudioChunkFrames; ++i) {
            int32_t v = m_monoBuf[i];
            v /= 2; // Volume/clipping fix
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
        if (video_get_dirty()) {
            // redraw
            video_set_dirty(0);
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

        // === I: Deferred disk mount (used for CP/M tape-boot) ===
        if (m_deferredMountFramesRemaining > 0) {
            m_deferredMountFramesRemaining--;
            if (m_deferredMountFramesRemaining == 0 && !m_deferredMountDisk0Path.isEmpty()) {
                qDebug() << "[CTRL][CPM] Mount deferred disk as A: now:" << m_deferredMountDisk0Path;
                if (coleco_load_disk(0, QFile::encodeName(m_deferredMountDisk0Path).constData()) != 0) {
                    qWarning() << "[CTRL][CPM] Deferred mount failed for A:";
                }
                m_deferredMountDisk0Path.clear();
            }
        }
    }

    // 5. Opruimen
    qDebug() << "[CTRL] Emulation loop finished.";

    if (m_audioSink) {
        m_audioSink->stop();
    }

    if (!m_preserveMediaOnStop) {
        for (int i = 0; i < MAX_DISKS; ++i)
            ejectDisk(i);
        for (int i = 0; i < MAX_TAPES; ++i)
            ejectTape(i);
    } else {
        qDebug() << "[CTRL] Preserving media on stop (reboot/bootCPM).";
    }
    emit emulationStopped();
}

void ColecoController::pauseEmulation()
{
    if (m_paused) return;
    qDebug() << "[CTRL] pauseEmulation()";
    m_paused = true;
    if (m_audioSink) m_audioSink->suspend();
    emit emuPausedChanged(true);
}

void ColecoController::resumeEmulation()
{
    if (!m_paused) return;
    qDebug() << "[CTRL] resumeEmulation()";

    if (emulator) {
        emulator->stop = 0;
        emulator->singlestep = 0;
    }

    m_paused = false;
    if (m_audioSink) m_audioSink->resume();
    emit emuPausedChanged(false);
}

void ColecoController::stopEmulation()
{
    qDebug() << "[CTRL] stopEmulation() requested.";

    m_running = false;
}

void ColecoController::stepOnce()
{
    if (!m_paused) {
        pauseEmulation();
    }
    qDebug() << "[CTRL] stepOnce()";

    if (emulator) {
        emulator->stop = 0;
    }

    for (int i = 0; i < tms.ScanLines; ++i) {
        coleco_do_scanline();
    }

    if (video_get_dirty()) {
        // redraw
        video_set_dirty(0);
        QImage real = frameFromBridge();
        emit frameReady(real);
    }

    QCoreApplication::processEvents();
}

void ColecoController::sstepOnce()
{
    if (!m_paused) {
        pauseEmulation();
    }

    qDebug() << "[CTRL] sstepOnce()";

    if (!emulator) {
        return;
    }

    emulator->stop = 0;

    z80_do_opcode();

    QCoreApplication::processEvents();
}

void ColecoController::stepOver(uint16_t returnAddress)
{
    //qDebug() << "[CTRL] stepOver() requested to return to PC:" << Qt::hex << returnAddress;

    // HIER: De gebruiker moet de code toevoegen die het externe C-mechanisme activeert.
    // Bijvoorbeeld: temp_step_over_addr = returnAddress;

    if (returnAddress != 0) {
        // Zodra het tijdelijke breakpoint is gezet in de C-core, hervatten we de emulator.
        // De core draait dan totdat het retouradres wordt geraakt.
        resumeEmulation();
    } else {
        // Als er geen CALL was (returnAddress=0), doen we een simpele stap.
        stepOnce();
    }
}

void ColecoController::gotoAddr(uint16_t newPC)
{
    if (!emulator) return;

    pauseEmulation();

    Z80.pc.w.l = newPC;

    qDebug() << "[CTRL] PC manually set to" << Qt::hex << newPC;
}

void ColecoController::loadRom(const QString &romPath)
{
    //qDebug() << "[CTRL] loadRom:" << romPath;
    m_realFrames = 0;

    QByteArray path = QFile::encodeName(romPath);
    BYTE ok = coleco_loadcart(path.data());
    if (ok != 0) {
        qWarning() << "[CTRL] ROM failed loading, code =" << int(ok);
        return;
    }

    coleco_reset();
    resumeEmulation();
}

void ColecoController::AdamCartridge(const QString &romPath)
{
    //qDebug() << "[CTRL] load Adam Rom:" << romPath;
    m_realFrames = 0;

    QByteArray path = QFile::encodeName(romPath);

    // Schakel Adam ROMs uit en zet Coleco-modus aan (dit maakt de bank vrij)
    //setMachineType(Machine_Coleco);
    // Laad de cartridge data

    BYTE retload = coleco_loadcart(path.data());

    if (retload == ROM_LOAD_PASS)
    {
        //coleco_reset();
        m_currentColecoCartPath.clear();
        m_currentAdamCartPath = QFileInfo(romPath).fileName();
    }
    else if (retload == ROM_VERIFY_FAIL)
    {
        qDebug() << "[CTRL] Can't verify the rom file";
        m_currentAdamCartPath.clear();
        m_currentColecoCartPath.clear();
    }
    else {
        qWarning() << "[CTRL] ADAM ROM laden faalde, code =" << int(retload);
        m_currentAdamCartPath.clear();
        m_currentColecoCartPath.clear();
    }

    g_adamCartridgeMode = true;
    coleco_reset_and_restart_bios();   // of coleco_reset()
    resumeEmulation();

    emit cartridgeStatusChanged(m_currentColecoCartPath, m_currentAdamCartPath);
}

void ColecoController::ejectAdamCartridge()
{
   g_adamCartridgeMode = false;
    if (!m_currentAdamCartPath.isEmpty()) {
        m_currentAdamCartPath.clear();
        emit cartridgeStatusChanged(m_currentColecoCartPath, m_currentAdamCartPath);
        qDebug() << "[CTRL] ADAM Cartridge ejected (GUI updated).";
    }
}
void ColecoController::ColecoCartridge(const QString &romPath)
{
    //qDebug() << "[CTRL] load Coleco Rom:" << romPath;
    m_realFrames = 0;

    QByteArray path = QFile::encodeName(romPath);
    // Laad de cartridge data
    BYTE retload = coleco_loadcart(path.data());

    //setMachineType(Machine_Adam);
    if (retload != 0) {
        qWarning() << "[CTRL] COLECO ROM failed loading, code =" << int(retload);
        m_currentColecoCartPath.clear();
        m_currentAdamCartPath.clear();
    } else {
        m_currentAdamCartPath.clear();
        m_currentColecoCartPath = QFileInfo(romPath).fileName();
    }
    emit cartridgeStatusChanged(m_currentColecoCartPath, m_currentAdamCartPath);
}

void ColecoController::ejectColecoCartridge()
{
    if (!m_currentColecoCartPath.isEmpty()) {
        m_currentColecoCartPath.clear();
        emit cartridgeStatusChanged(m_currentColecoCartPath, m_currentAdamCartPath);
        qDebug() << "[CTRL] Coleco Cartridge ejected (GUI updated).";
    }
}

void ColecoController::resetMachine() // SOFT
{
    if (AlreadyReset) return;
    AlreadyReset = true;

    qDebug() << "[CTRL] resetMachine()";
    coleco_reset();
    PsgBridge::reset(m_Clock, m_SampleRate);
}

void ColecoController::resethMachine() // HARD
{
    // Hard reset button / user reset: execute prepared boot plan.
    if (AlreadyReset) return;
    AlreadyReset = true;

    qDebug() << "[CTRL] resethMachine()";
    // Make the core deterministic first
    coleco_hardreset();

    // Allow next reset press
    AlreadyReset = false;

    // Reset smartwriter  printer
    g_prn_line_counter = 0;
    g_prn_in_wp = false;
    PrintWindow* w = PrintWindow::instance();
    if (w) {
        QMetaObject::invokeMethod(w, "updatePrinterMode", Qt::QueuedConnection, Q_ARG(bool, g_prn_in_wp));
    }

}

void ColecoController::powerOffMachine()
{
    qDebug() << "[CTRL] power off Machine()";

    // Reset smartwriter  printer
    g_prn_line_counter = 0;
    g_prn_in_wp = false;
    PrintWindow* w = PrintWindow::instance();
    if (w) {
        QMetaObject::invokeMethod(w, "updatePrinterMode", Qt::QueuedConnection, Q_ARG(bool, g_prn_in_wp));
    }

    // Stop eventueel lopende CP/M deferred mount
    m_deferredMountFramesRemaining = 0;
    m_deferredMountDisk0Path.clear();

    // Power-cycle naar Writer/EOS
    // 1️⃣ First reset (direct)
    coleco_initialise();
    coleco_reset_and_restart_bios();

    // 2️⃣ second reset when cp/m loaded
    if (m_cpm_enabled || emulator->currentMachineType == MACHINEADAM)
    {
        QTimer::singleShot(500, QCoreApplication::instance(), []() {
            qDebug() << "[CTRL] doubleResetToWriter() SECOND reset";

            coleco_initialise();
            coleco_reset_and_restart_bios();
        });

    }

    m_cpm_enabled = false;

    PsgBridge::reset(m_Clock, m_SampleRate);
    AlreadyReset = false;
}

void ColecoController::setSGMEnabled(bool enabled)
{

    if (emulator->currentMachineType == MACHINEADAM)
    {
        // ADAM Modus
        if (enabled) {
            qDebug() << "[COLECO] Ignoring SGM *enable* in ADAM mode.";
            emit sgmStatusChanged(false);
            return;
        }

        // Forceer SGM 'uit' in de core
        qDebug() << "[COLECO] SGM set = false (Forced for ADAM)";
        emulator->SGM = 0;
    }
    else
    {
        // COLECO Modus
        qDebug() << "[COLECO] SGM set =" << enabled;
        emulator->SGM = enabled ? 1 : 0;

        if (enabled) {
            coleco_writeport(0x60, 0x0F, nullptr);
            coleco_writeport(0x53, 0x01, nullptr);
        } else {
            coleco_writeport(0x53, 0x00, nullptr);
            coleco_writeport(0x60, 0x0F, nullptr);
        }
    }

    coleco_reset_and_restart_bios();
    emit sgmStatusChanged(enabled);

    PsgBridge::init(m_Clock, m_SampleRate);
    ay8910_init(m_Clock, m_SampleRate);
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

void ColecoController::loadDisk(int drive, const QString& path)
{
    if (drive >= MAX_DISKS) return;
    // ✅ Als dezelfde disk al ingestoken is: NIET ejecten/opslaan/herladen
    if (!m_currentDiskPath[drive].isEmpty() &&
        QDir::cleanPath(m_currentDiskPath[drive]) == QDir::cleanPath(path))
    {
        //qDebug() << "[CTRL] Disk" << drive << "already loaded, skipping reload:" << path;
        return;
    }

    // Anders pas ejecten
    ejectDisk(drive);

    QByteArray cPath = QFile::encodeName(path);
    qDebug() << "[CTRL] ========================================";
    qDebug() << "[CTRL] Loading Disk" << drive << ":" << path;
    qDebug() << "[CTRL] ========================================";

    const QString p = QFileInfo(path).fileName().toLower();

    // Check if CP/M or T-DOS disk
    bool isCpmDisk = p.contains("cpm") || p.contains("cp-m");
    bool isTdosDisk = p.contains("tdos") || p.contains("t-dos");
    ::m_tdos_enabled = isTdosDisk;
    ::m_cpm_enabled = isCpmDisk || isTdosDisk;

    if (::m_cpm_enabled)
    {
        qDebug() << "[CTRL] *** CP/M DISK DETECTED ***";
        qDebug() << "[CTRL] Setting CP/M mode and preparing boot sequence";
    }
    else
    {
        qDebug() << "[CTRL] Normal ADAM disk detected";
    }

    BYTE ok = coleco_load_disk(drive, cPath.constData());

    if (ok == 0) {
        m_currentDiskPath[drive] = path;
        qDebug() << "[CTRL] Disk" << drive << "loaded successfully";

        if (emulator->currentMachineType != MACHINEADAM) {
            qDebug() << "[CTRL] Switching to ADAM mode for disk boot";
            setMachineType(Machine_Adam);
            emit machineTypeChanged(Machine_Adam);
            adamnet_block_ascii_fkeys(100);
        }

        if (::m_cpm_enabled) {
            qDebug() << "[CTRL] CP/M disk loaded - system ready for reset/boot";
            qDebug() << "[CTRL] User must press RESET to boot CP/M";
        }

    } else {
        qWarning() << "[CTRL] *** FAILED TO LOAD DISK ***";
        m_currentDiskPath[drive].clear();
    }

    emit diskStatusChanged(drive, QFileInfo(m_currentDiskPath[drive]).fileName());
}

void ColecoController::ejectDisk(int drive)
{
    if (drive >= MAX_DISKS) return;
    // Save state and eject C-kern
    if (!m_currentDiskPath[drive].isEmpty()) {
        QByteArray cOldPath = QFile::encodeName(m_currentDiskPath[drive]);
        qDebug() << "[CTRL] Saving and Ejecting Disk" << drive << ":" << m_currentDiskPath[drive];
        int result = coleco_save_disk(drive, cOldPath.constData());
        qDebug() << "DISK SAVE RESULT:" << result;
        coleco_eject_disk(drive);
        m_currentDiskPath[drive].clear(); // Clear status
    }
    emit diskStatusChanged(drive, "");
}

void ColecoController::loadTape(int drive, const QString& path)
{
    if (drive >= MAX_TAPES) return;
    ejectTape(drive);

    // cPath full path, needed by C-core function coleco_load_tape
    QByteArray cPath = QFile::encodeName(path);

    const QString p = QFileInfo(path).fileName().toLower();

    // Check if CP/M or T-DOS tape
    bool isCpmTape = p.contains("cpm") || p.contains("cp-m");
    bool isTdosTape = p.contains("tdos") || p.contains("t-dos");
    ::m_tdos_enabled = isTdosTape;
    ::m_cpm_enabled = isCpmTape || isTdosTape;

    qDebug() << "[CTRL] Loading Tape" << drive << ":" << path;
    BYTE ok = coleco_load_tape(drive, cPath.constData());

    if (ok == 0) {
        // save full path
        m_currentTapePath[drive] = path;
    } else {
        qWarning() << "Failed to load disk image.";
        m_currentTapePath[drive].clear();
    }

    // Sent filename to GUI
    emit tapeStatusChanged(drive, QFileInfo(m_currentTapePath[drive]).fileName());
}

void ColecoController::ejectTape(int drive)
{
    if (drive >= MAX_TAPES) return;

    if (!m_currentTapePath[drive].isEmpty()) {
        QByteArray cOldPath = QFile::encodeName(m_currentTapePath[drive]);
        //qDebug() << "[CTRL] Saving and Ejecting Tape" << drive << ":" << m_currentTapePath[drive];
        int result = coleco_save_tape(drive, cOldPath.constData());
        //qDebug() << "[CTRL] TAPE SAVE RESULT:" << result;
        coleco_eject_tape(drive);
        m_currentTapePath[drive].clear();
    }

    emit tapeStatusChanged(drive, "");
}

void ColecoController::bootCpmDisk()
{
    stopEmulation();

    // 3. Stel geheugenpoorten in voor RAM-dominantie
    coleco_port60 = 0x01; // Map RAM naar lower 32K
    coleco_setadammemory(true);

    // 4. Start de emulatie opnieuw
    resethMachine();
    startEmulation();
}

void ColecoController::saveState(const QString& filePath)
{
    if (filePath.isEmpty()) {
        qWarning() << "[CTRL] saveState(): leeg pad, niets te doen";
        return;
    }

    QByteArray cPath = QFile::encodeName(filePath);
    qDebug() << "[CTRL] saveState ->" << filePath;

    BYTE ok = coleco_savestate(cPath.data());
    if (!ok) {
        qWarning() << "[CTRL] saveState FAILED for" << filePath;
    } else {
        qDebug() << "[CTRL] saveState OK";
    }
}

void ColecoController::loadState(const QString& filePath)
{
    if (filePath.isEmpty()) {
        qWarning() << "[CTRL] loadState(): emty path, nothing to do";
        return;
    }

    // if (m_currentColecoCartPath.isEmpty() && m_currentAdamCartPath.isEmpty()) {
    //     qWarning() << "[CTRL] loadState ABORTED: No cartridge loaded. Please load a game first.";
    //     return;
    // }

    try {
        QByteArray cPath = QFile::encodeName(filePath);

        // Voer de core-functie uit
        BYTE ok = coleco_loadstate(cPath.data());

        if (!ok) {
            throw std::runtime_error("C-core kon de state niet verwerken (mismatch?)");
        }

        // Hersynchroniseer hardware na succesvolle load
//        PsgBridge::init(m_Clock, m_SampleRate);
 //       PsgBridge::reset(m_Clock, m_SampleRate);
  //      ay8910_init(m_Clock, m_SampleRate);

        qDebug() << "[CTRL] LoadState succesvol uitgevoerd.";
    }
    catch (const std::exception& e) {
        qCritical() << "[CTRL] CRASH VOORKOMEN in loadState:" << e.what();
        // Zorg dat de emulator niet in een corrupte staat blijft hangen
        coleco_reset();
    }
}

void ColecoController::resetAdam()
{
    setMachineType(Machine_Adam);
}

void ColecoController::resetColeco()
{
    setMachineType(Machine_Coleco);

}

void ColecoController::doHardReset()
{
    resethMachine();
}

void ColecoController::setMachineType(ColecoController::MachineType machineType)
 {
     const int isAdam = (machineType == 1) ? 1 : 0; // 0=COLECO 1=ADAM
     coleco_set_machine_type(isAdam);
     qDebug() << "[CTRL] Machine switched to "
              << (isAdam ? "ADAM" : "COLECO");

     // Audio sync
     PsgBridge::init(m_Clock, m_SampleRate);
     ay8910_init(m_Clock, m_SampleRate);
     machine.interrupt = 0;

     if (isAdam) {
         emulator->SGM = 0;
         emit sgmStatusChanged(false);
         if (!m_cpm_enabled)
            resethMachine();
         else
             bootCpmDisk();

     }
     else
     {
         coleco_initialise();
         resetMachine();
     }

     emit machineTypeChanged(machineType);
 }

extern "C" void coleco_set_bios_paths(const char* coleco_path, const char* eos_path, const char* writer_path);

void ColecoController::loadBiosRoms(const QString& colecoPath, const QString& eosPath, const QString& writerPath)
{
    m_colecoPathBytes = colecoPath.isEmpty() ? QByteArray() : QFile::encodeName(colecoPath);
    m_eosPathBytes    = eosPath.isEmpty()    ? QByteArray() : QFile::encodeName(eosPath);
    m_writerPathBytes = writerPath.isEmpty() ? QByteArray() : QFile::encodeName(writerPath);

    coleco_set_bios_paths(
        m_colecoPathBytes.isEmpty() ? nullptr : m_colecoPathBytes.constData(),
        m_eosPathBytes.isEmpty()    ? nullptr : m_eosPathBytes.constData(),
        m_writerPathBytes.isEmpty() ? nullptr : m_writerPathBytes.constData()
        );

    qDebug() << "[CTRL] BIOS reloads --> with media inserted";

    coleco_probe_bios_status_all();
    updateBiosStatus();
}

void ColecoController::startWithBios(const QString& colecoPath,
                                     const QString& eosPath,
                                     const QString& writerPath)
{
    loadBiosRoms(colecoPath, eosPath, writerPath); // set paths
    startEmulation();                              // then start thread
}

void ColecoController::updateBiosStatus()
{
    emit onBiosStatusUpdated(
        coleco_get_bios_status(0),
        coleco_get_bios_status(1),
        coleco_get_bios_status(2)
        );
}

void ColecoController::prepareForNewCRomAndPauseOnBios()
{   // COLECO CARTRIDGE
    stopEmulation();
    coleco_reset_and_restart_bios();
    m_waitForCBiosFrames=true;
    startEmulation();
}

void ColecoController::prepareForNewARomAndPauseOnBios()
{   // ADAM CARTRIDGE
    stopEmulation();
    coleco_reset_and_restart_bios();
    m_waitForABiosFrames=true;
    startEmulation();
}

void ColecoController::setDTsoundEnabled(bool enabled)
{
    g_dtSoundEnabled.store(enabled, std::memory_order_relaxed);
}
