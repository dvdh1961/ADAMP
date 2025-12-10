#include "colecocontroller.h"
#include <QThread>
#include <QDebug>
#include <QFileInfo>
#include <QCoreApplication>
#include <cstring>
#include <QAudioFormat>
#include <QTimer>
#include <QFile>

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

extern "C" void adamnet_inject_scancode(uint8_t sc);
extern "C" void adamnet_block_ascii_fkeys(int count);
extern "C" void PutKBD(unsigned int Key);
extern "C" void adamnet_host_prn_write_ascii(const char* s);

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
    m_monoBuf = new int16_t[882];
    m_stereoBuf = new int16_t[882 * 2];

    m_BytesPerSampleStereo = 4;
    m_SampleRate = 44100;

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

extern "C" {
extern tTMS9981A tms;
}

/*
    Key0 = 0x1E;        // !
    Key0 = 0x1F;        // @
    Key0 = 0x20;        // #
    Key0 = 0x21;        // $
    Key0 = 0x22;        // %
    Key0 = 0x2D;        // _
    Key0 = 0x24;        // &
    Key0 = 0x25;        // *
    Key0 = 0x26;        // (
    Key0 = 0x27;        // )
    Key0 = F1;          // F1
    Key0 = F2;          // F2
    Key0 = F3;          // F3
    Key0 = F4;          // F4
    Key0 = F5;          // F5
    Key0 = F6;          // F6
    Key0 = F7;          // F7
    Key0 = F8;          // F8
    Key0 = F9;          // F9
    Key0 = F10; 		// F10
    Key0 = F11; 		// F11
    Key0 = F12; 		// F12
    Key0 = HOME;        // home
    Key0 = LEFT;        // left arrow
    Key0 = UP;          // up arrow
    Key0 = RIGHT;       // right arrow
    Key0 = DOWN;        // down arrow

    Key0 = 0x27;        // 0
    Key0 = 0x35;        // `
    Key0 = 0x2D;        // -
    Key0 = 0x2E;        // +
    Key0 = 0x2F;        // {
    Key0 = 0x30;        // }
    Key0 = 0x33;        // :
    Key0 = 0x34;        // "
    Key0 = 0x36;        // <
    Key0 = 0x37;        // >
    Key0 = 0x38;        // ?
    Key0 = 0x31;        // '\'
    Key0 = SPACE;       // space
    Key0 = 0x2A;        // backspace
    Key0 = 0x49;        // insert
    Key0 = 0x2B;        // tab

    Key0 = 0x04;        // control a
    Key0 = 0x06;        // control c
    Key0 = 0x07;        // control d
    Key0 = 0x19;        // control v

    Key0 = 0x2E;        // =
    Key0 = 0x2F;        // [
    Key0 = 0x30;        // ]
    Key0 = 0x33;        // ;
    Key0 = 0x34;        // '
    Key0 = 0x36;        // ,
    Key0 = 0x37;        // .
    Key0 = 0x38;        // /
*/

static uint8_t s_fgActiveMask = 0;

// void ColecoController::onAdamKeyEvent(int code)
// {
//     // ====== AdamNet scancode pad (marker >= 0x100) ======
//     if (code >= 0x100) {
//         qDebug() << "[code]:" << Qt::hex << code;
//         uint8_t sc = uint8_t(code & 0xFF); // bv. 0x54 of 0xD4
//         // FG1..FG6 mask
//         if ((sc & 0x7F) >= 0x54 && (sc & 0x7F) <= 0x5D) {
//             const int bit = (sc & 0x7F) - 0x54;  // 0..5
//             if (sc & 0x80) s_fgActiveMask &= uint8_t(~(1 << bit));  // break
//             else           s_fgActiveMask |=  uint8_t( 1 << bit);   // make
//         }

//         // Stuur de RAUWE PC-scancode (0x54, 0xD4)
//         // De C-laag (adamnet_queue_key) doet de remap
//         qDebug() << "[Controller] FUNCTIETOETSEN:" << Qt::hex << sc;

//         adamnet_inject_scancode(sc);
//         return;
//     }

//     // --- ASCII pad (PutKBD) ---
//     const uint8_t ascii = uint8_t(code & 0xFF);


//     // Blokkeer ASCII T..Y (0x54..0x59) én '4'..'9' (0x34..0x39)
//     // zolang de overeenkomstige F-key (F1..F6) actief is.
//     if ((ascii >= 0x54 && ascii <= 0x59) || (ascii >= 0x34 && ascii <= 0x39)) {
//         int bit = (ascii >= 0x54) ? (ascii - 0x54) : (ascii - 0x34); // 0..5
//         if (s_fgActiveMask & (1 << bit)) {
//             qDebug() << "[Controller] DROP ASCII (T..Y of '4'..'9') omdat FG actief is:" << Qt::hex << ascii;
//             return; // NIET naar PutKBD
//         }
//     }

//     qDebug() << "[Controller] Doorgeven aan C-laag:" << Qt::hex << ascii;
//     PutKBD(unsigned(ascii));


//     // --- ASCII pad ---
//     bool is_make = !(code & 0x80); // True als het een press is (geen 0x80 bit)

//     uint8_t scancode = 0;

//     // ADAMNet Scancodes voor Cijfers: '1' t/m '9' mappen naar 0x01 t/m 0x09
//     if (ascii >= '1' && ascii <= '9') {
//         scancode = ascii - '0'; // '1' (0x31) wordt 0x01, '9' (0x39) wordt 0x09
//     } else if (ascii == '0') {
//         scancode = 0x0A; // '0' (0x30) wordt 0x0A
//     }
//     // Opmerking: Voor Letters A-Z zou dit 0x11 t/m 0x2A zijn, maar '1' is hier het probleem.


//     if (scancode != 0) {
//         // ADAMNet MAKE-scancode = Scancode + 0x80
//         if (is_make) {
//            // scancode |= 0x80;
//             adamnet_queue_key(scancode | 0x80);

//         } else { PutKBD(unsigned(ascii));}

//        // adamnet_queue_key(scancode);
//         qDebug() << "[Controller] ADAMNet Queue Scancode:" << Qt::hex << scancode;
//     }

//}

void ColecoController::onAdamKeyEvent(int code)
{
    // extern Z80_Regs Z80;
    // if (Z80.pc.w.l != 0x2F)
    // {
    // Z80.pc.w.l = 0x2F; // Low byte van 0xE02F
    // Z80.pc.w.h = 0xE0 >> 8; // High byte van 0xE02F
    // }

    // ====== AdamNet scancode pad (marker >= 0x100) ======
    if (code >= 0x100) {
        qDebug() << "[code]:" << Qt::hex << code;
        uint8_t sc = uint8_t(code & 0xFF); // bv. 0x54 of 0xD4
        // FG1..FG6 mask
        if ((sc & 0x7F) >= 0x54 && (sc & 0x7F) <= 0x5D) {
            const int bit = (sc & 0x7F) - 0x54;  // 0..5
            if (sc & 0x80) s_fgActiveMask &= uint8_t(~(1 << bit));  // break
            else           s_fgActiveMask |=  uint8_t( 1 << bit);   // make
        }

        // Stuur de RAUWE PC-scancode (0x54, 0xD4)
        // De C-laag (adamnet_queue_key) doet de remap
        qDebug() << "[Controller] FUNCTIETOETSEN:" << Qt::hex << sc;

        adamnet_inject_scancode(sc);
        return;
    }

    // --- ASCII pad (PutKBD) ---
    const uint8_t ascii = uint8_t(code & 0xFF);

    // Blokkeer ASCII T..Y (0x54..0x59) én '4'..'9' (0x34..0x39)
    // zolang de overeenkomstige F-key (F1..F6) actief is.
    if ((ascii >= 0x54 && ascii <= 0x59) || (ascii >= 0x34 && ascii <= 0x39)) {
        int bit = (ascii >= 0x54) ? (ascii - 0x54) : (ascii - 0x34); // 0..5
        if (s_fgActiveMask & (1 << bit)) {
            qDebug() << "[Controller] DROP ASCII (T..Y of '4'..'9') omdat FG actief is:" << Qt::hex << ascii;
            return; // NIET naar PutKBD
        }
    }

    qDebug() << "[Controller] Doorgeven aan C-laag:" << Qt::hex << ascii;

    PutKBD(unsigned(ascii));

}

void ColecoController::setMachineType(int machineType)
{
    const int isAdam = (machineType == 1) ? 1 : 0;

    coleco_set_machine_type(isAdam);

    if (isAdam) {
        emulator->SGM = 0;
        emit sgmStatusChanged(false);
    }

    // BIOS-blobs laden (Writer/EOS/OS7 of Coleco)
    coleco_initialise();

    // ADAM-poorten/mapping + CPU/VDP resetten
    coleco_reset_and_restart_bios();

    // Audio syncen
    PsgBridge::init(m_Clock, m_SampleRate);
    ay8910_init(m_Clock, m_SampleRate);
    machine.interrupt = 0;

    qDebug() << "[Controller] Machine switched to"
             << (isAdam ? "ADAM" : "COLECO")
             << "with fresh initialise() + reset_bios()";
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

    for (int i = 0; i < MAX_DISKS; ++i)
        ejectDisk(i);
    for (int i = 0; i < MAX_TAPES; ++i)
        ejectTape(i);

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
    qDebug() << "[Controller] stopEmulation() requested.";


    m_running = false;
}

void ColecoController::stepOnce()
{
    if (!m_paused) {
        pauseEmulation();
    }
    qDebug() << "[Controller] stepOnce()";

    if (emulator) {
        emulator->stop = 0;
    }

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

void ColecoController::sstepOnce()
{
    if (!m_paused) {
        pauseEmulation();
    }

    qDebug() << "[Controller] sstepOnce()";

    if (!emulator) {
        return;
    }

    emulator->stop = 0;

    z80_do_opcode();

    QCoreApplication::processEvents();
}

void ColecoController::stepOver(uint16_t returnAddress)
{
    // *** Dit slot draait op de emulator-thread ***

    qDebug() << "[Controller] stepOver() requested to return to PC:" << Qt::hex << returnAddress;

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

    // Z80 heeft PC in de union:
    // Z80.pc.w.l = low byte
    // Z80.pc.w.h = high byte
    Z80.pc.w.l = newPC & 0xFF;
    Z80.pc.w.h = (newPC >> 8) & 0xFF;

    qDebug() << "[Controller] PC manually set to" << Qt::hex << newPC;
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

void ColecoController::AdamCartridge(const QString &romPath)
{
    qDebug() << "[Controller] load Adam Rom:" << romPath;
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
        qDebug() << "[Controller] Can't verify the rom file";
        m_currentAdamCartPath.clear();
        m_currentColecoCartPath.clear();
    }
    else {
        qWarning() << "[Controller] ADAM ROM laden faalde, code =" << int(retload);
        m_currentAdamCartPath.clear();
        m_currentColecoCartPath.clear();
    }

    emit cartridgeStatusChanged(m_currentColecoCartPath, m_currentAdamCartPath);
}

void ColecoController::ejectAdamCartridge()
{
    if (!m_currentAdamCartPath.isEmpty()) {
        m_currentAdamCartPath.clear();
        emit cartridgeStatusChanged(m_currentColecoCartPath, m_currentAdamCartPath);
        qDebug() << "[Controller] ADAM Cartridge ejected (GUI updated).";
    }
}
void ColecoController::ColecoCartridge(const QString &romPath)
{
    qDebug() << "[Controller] load Coleco Rom:" << romPath;
    m_realFrames = 0;

    QByteArray path = QFile::encodeName(romPath);
    // Laad de cartridge data
    BYTE retload = coleco_loadcart(path.data());

    //setMachineType(Machine_Adam);
    if (retload != 0) {
        qWarning() << "[Controller] COLECO ROM laden faalde, code =" << int(retload);
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
        qDebug() << "[Controller] Coleco Cartridge ejected (GUI updated).";
    }
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
    ejectDisk(drive);

    // cPath bevat het VOLLEDIGE PAD, nodig voor de C-core functie coleco_load_disk
    QByteArray cPath = QFile::encodeName(path);
    qDebug() << "[Controller] Loading Disk" << drive << ":" << path;
    BYTE ok = coleco_load_disk(drive, cPath.constData());

    if (ok == 0) {
        // [FIX] Sla het VOLLEDIGE PAD op
        m_currentDiskPath[drive] = path;

        if (emulator->currentMachineType != MACHINEADAM) {
            setMachineType(Machine_Adam);
            emit machineTypeChanged(Machine_Adam);
            qDebug() << "[Controller] Forced machine type to ADAM for disk boot.";
            adamnet_block_ascii_fkeys(100);
        }

    } else {
        qWarning() << "Failed to load disk image.";
        m_currentDiskPath[drive].clear();
    }

    // Stuur bestandsnaam naar de GUI
    emit diskStatusChanged(drive, QFileInfo(m_currentDiskPath[drive]).fileName());
}

void ColecoController::ejectDisk(int drive)
{
    if (drive >= MAX_DISKS) return;

    // Save state en eject in de C-kern
    if (!m_currentDiskPath[drive].isEmpty()) {
        QByteArray cOldPath = QFile::encodeName(m_currentDiskPath[drive]);
        qDebug() << "[Controller] Saving and Ejecting Disk" << drive << ":" << m_currentDiskPath[drive];
        int result = coleco_save_disk(drive, cOldPath.constData());
        qDebug() << "DISK SAVE RESULT:" << result;
        coleco_eject_disk(drive);
        m_currentDiskPath[drive].clear(); // Status in de controller wissen
    }

    // Stuur een lege string om de GUI terug op default te zetten
    emit diskStatusChanged(drive, "");
}

void ColecoController::loadTape(int drive, const QString& path)
{
    if (drive >= MAX_TAPES) return;
    ejectTape(drive);

    // cPath bevat het VOLLEDIGE PAD, nodig voor de C-core functie coleco_load_tape
    QByteArray cPath = QFile::encodeName(path);
    qDebug() << "[Controller] Loading Tape" << drive << ":" << path;
    BYTE ok = coleco_load_tape(drive, cPath.constData());

    if (ok == 0) {
        // [FIX] Sla het VOLLEDIGE PAD op
        m_currentTapePath[drive] = path;
    } else {
        qWarning() << "Failed to load disk image.";
        m_currentTapePath[drive].clear();
    }

    // Stuur bestandsnaam naar de GUI
    emit tapeStatusChanged(drive, QFileInfo(m_currentTapePath[drive]).fileName());
}

void ColecoController::ejectTape(int drive)
{
    if (drive >= MAX_TAPES) return;

    if (!m_currentTapePath[drive].isEmpty()) {
        QByteArray cOldPath = QFile::encodeName(m_currentTapePath[drive]);
        qDebug() << "[Controller] Saving and Ejecting Tape" << drive << ":" << m_currentTapePath[drive];
        int result = coleco_save_tape(drive, cOldPath.constData());
        qDebug() << "TAPE SAVE RESULT:" << result;
        coleco_eject_tape(drive);
        m_currentTapePath[drive].clear();
    }

    emit tapeStatusChanged(drive, "");
}

void ColecoController::saveState(const QString& filePath)
{
    if (filePath.isEmpty()) {
        qWarning() << "[Controller] saveState(): leeg pad, niets te doen";
        return;
    }

    QByteArray cPath = QFile::encodeName(filePath);
    qDebug() << "[Controller] saveState ->" << filePath;

    BYTE ok = coleco_savestate(cPath.data());
    if (!ok) {
        qWarning() << "[Controller] saveState FAILED for" << filePath;
    } else {
        qDebug() << "[Controller] saveState OK";
    }
}

void ColecoController::loadState(const QString& filePath)
{
    if (filePath.isEmpty()) {
        qWarning() << "[Controller] loadState(): leeg pad, niets te doen";
        return;
    }

    QByteArray cPath = QFile::encodeName(filePath);
    qDebug() << "[Controller] loadState <-" << filePath;

    BYTE ok = coleco_loadstate(cPath.data());
    if (!ok) {
        qWarning() << "[Controller] loadState FAILED for" << filePath;
        return;
    }

    // Na een loadstate audio/PSG opnieuw syncen
    PsgBridge::init(m_Clock, m_SampleRate);
    PsgBridge::reset(m_Clock, m_SampleRate);
    ay8910_init(m_Clock, m_SampleRate);

    qDebug() << "[Controller] loadState OK";
}

void ColecoController::resetAdam()
{
    setMachineType(Machine_Adam);  // hier stel je ADAM-mode in
    doHardReset();                 // daarna pas reset (ROM/RAM/VDP/CPU, etc.)
    emit machineTypeChanged(Machine_Adam);
}

void ColecoController::resetColeco()
{
    setMachineType(Machine_Coleco); // naar Coleco-mode
    doHardReset();                  // en dan resetten
    emit machineTypeChanged(Machine_Coleco);
}

void ColecoController::doHardReset()
{
    resethMachine();
}

void ColecoController::setMachineType(ColecoController::MachineType machineType)
{
    // Forward naar de int-versie (0 = Coleco, 1 = ADAM)
    setMachineType(static_cast<int>(machineType));
}
