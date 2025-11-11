#include "colecocontroller.h"
#include <QThread>
#include <QDebug>
#include <QFileInfo>
#include <QCoreApplication> // Nodig voor processEvents
#include <cstring>
#include <QAudioFormat>
#include <QTimer>

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
}

#include <stdint.h>
#include "printwindow.h"

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

extern "C" {
extern tTMS9981A tms;
}

// Houd bij welke FG-toets(en) ingedrukt is/zijn.
// bit0 = FG1 (0x54), bit1 = FG2 (0x55), ... bit5 = FG6 (0x59)

/*
case '!':
    Modifier = 0X02; // modifier key
    Key0 = 0x1E; // !
    break;
case '@':
    Modifier = 0X02;
    Key0 = 0x1F; // @
    break;
case '#':
    Modifier = 0X02;
    Key0 = 0x20; // #
    break;
case '$':
    Modifier = 0X02;
    Key0 = 0x21; // $
    break;
case '%':
    Modifier = 0X02;
    Key0 = 0x22; // %
    break;
case '_':
    Modifier = 0X02;
    Key0 = 0x2D; // _
    break;
case '&':
    Modifier = 0X02;
    Key0 = 0x24; // &
    break;
case '*':
    Modifier = 0X02;
    Key0 = 0x25; // *
    break;
case '(':
    Modifier = 0X02;
    Key0 = 0x26; // (
    break;
case ')':
    Modifier = 0X02;
    Key0 = 0x27; // )
    break;
case '0':
    Modifier = 0X00;
    Key0 = 0x27; // 0
    break;
case '`':
    Modifier = 0X00;
    Key0 = 0x35; // `
    break;
case '-':
    Modifier = 0X00;
    Key0 = 0x2D; // -
    break;
case '=':
    Modifier = 0X02;
    Key0 = 0x2E; // =
    break;
case '+':
    Modifier = 0X00;
    Key0 = 0x2E; // +
    break;

case '{':
    Modifier = 0X00;
    Key0 = 0x2F; // {
    break;
case '[':
    Modifier = 0X02;
    Key0 = 0x2F; // [
    break;
case '}':
    Modifier = 0X00;
    Key0 = 0x30; // }
    break;
case ']':
    Modifier = 0X02;
    Key0 = 0x30; // ]
    break;
case ':':
    Modifier = 0X00;
    Key0 = 0x33; // :
    break;
case ';':
    Modifier = 0X02;
    Key0 = 0x33; // ;
    break;
case '"':
    Modifier = 0X00;
    Key0 = 0x34; // "
    break;
case '\'':
    Modifier = 0X02;
    Key0 = 0x34; // '
    break;

case '<':
    Modifier = 0X00;
    Key0 = 0x36; // <
    break;
case ',':
    Modifier = 0X02;
    Key0 = 0x36; // ,
    break;
case '>':
    Modifier = 0X00;
    Key0 = 0x37; // >
    break;
case '.':
    Modifier = 0X02;
    Key0 = 0x37; // .
    break;
case '?':
    Modifier = 0X00;
    Key0 = 0x38; // ?
    break;
case '/':
    Modifier = 0X02;
    Key0 = 0x38; // /
    break;

case '|':
    Modifier = 0X00;
    Key0 = 0x31; // |
    break;
case '\\':
    Modifier = 0X02;
    Key0 = 0x31; // '\'
    break;

case ' ':
    Modifier = 0X00;
    Key0 = SPACE; // space
    break;
case 13 :
    Modifier = 0X00;
    Key0 = ENTER; // enter
    break;
case 8  :
    Modifier = 0X00;
    Key0 = 0x2A;  // backspace
    break;
case 151:
    Modifier = 0X00;
    Key0 = 0x63;  // delete
    break;
case 148:
    Modifier = 0X00;
    Key0 = 0x49;  // insert
    break;
case 9  :
case 0xB9 :
    Modifier = 0X00;
    Key0 = 0x2B;  // tab
    break;
case 1  :
    Modifier = 0X01;
    Key0 = 0x04;  // control a
    break;
case 3  :
    Modifier = 0X01;
    Key0 = 0x06;  // control c
    break;
case 4  :
    Modifier = 0X01;
    Key0 = 0x07;  // control d
    break;
case 22 :
    Modifier = 0X01;
    Key0 = 0x19;  // control v
    break;

case 129:
    Modifier = 0X00;
    Key0 = F1; 		// F1
    break;
case 130:
    Modifier = 0X00;
    Key0 = F2; 		// F2
    break;
case 131:
    Modifier = 0X00;
    Key0 = F3; 		// F3
    break;
case 132:
    Modifier = 0X00;
    Key0 = F4; 		// F4
    break;
case 133:
    Modifier = 0X00;
    Key0 = F5; 		// F5
    break;
case 134:
    Modifier = 0X00;
    Key0 = F6; 		// F6
    break;

case 137: // with shift
    Modifier = 0X00;
    Key0 = F7; 		// F7
    break;
case 138:
    Modifier = 0X00;
    Key0 = F8; 		// F8
    break;
case 139:
    Modifier = 0X00;
    Key0 = F9; 		// F9
    break;
case 140:
    Modifier = 0X00;
    Key0 = F10; 		// F10
    break;
case 141:
    Modifier = 0X00;
    Key0 = F11; 		// F11
    break;
case 142:
    Modifier = 0X00;
    Key0 = F12; 		// F12
    break;

case 27:
    Modifier = 0X00;
    Key0 = F12;    // escape (F12)
    break;

case 128:
    Modifier = 0X00;
    Key0 = HOME; // home
    break;
case 163:
    Modifier = 0X00;
    Key0 = LEFT; // left arrow
    break;
case 160:
    Modifier = 0X00;
    Key0 = UP;   // up arrow
    break;
case 161:
    Modifier = 0X00;
    Key0 = RIGHT;// right arrow
    break;
case 162:
    Modifier = 0X00;
    Key0 = DOWN; // down arrow
    break;
*/







static uint8_t s_fgActiveMask = 0;

void ColecoController::onAdamKeyEvent(int code)
{
    // ====== AdamNet scancode pad (marker >= 0x100) ======
    if (code >= 0x100) {
        qDebug() << "[code]:" << Qt::hex << code;
        uint8_t sc = uint8_t(code & 0xFF); // bv. 0x54 of 0xD4
        // FG1..FG6 mask bijhouden (dit is prima)
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

    // 1) Machine type in de core
    coleco_set_machine_type(isAdam);

    if (isAdam) {
        emul2->SGM = 0;
        emit sgmStatusChanged(false); // Zorg dat de UI ook klopt
    }

    // 2) BIOS-blobs laden (Writer/EOS/OS7 of Coleco)
    coleco_initialise();

    // 3) BELANGRIJK: ADAM-poorten/mapping + CPU/VDP resetten
    coleco_reset_and_restart_bios();

    // 4) Audio netjes syncen
    PsgBridge::init(m_Clock, m_SampleRate);
    ay8910_init(m_Clock, m_SampleRate);
    machine.interrupt = 0;

    qDebug() << "[Controller] Machine switched to"
             << (isAdam ? "ADAM" : "COLECO")
             << "with fresh initialise() + reset_bios()";
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

    for (int i = 0; i < MAX_DISKS; ++i) ejectDisk(i);
    for (int i = 0; i < MAX_TAPES; ++i) ejectTape(i);

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
    // NEW: SGM bestaat niet in ADAM
    if (emul2->currentMachineType == MACHINEADAM)
    {
        // ADAM Modus
        if (enabled) { // Probeer SGM AAN te zetten in ADAM mode
            qDebug() << "[COLECO] Ignoring SGM *enable* in ADAM mode.";
            emit sgmStatusChanged(false); // Blijf 'false' rapporteren
            return; // Sta niet toe
        }

        // Forceer SGM 'uit' in de core, maar doe GEEN port writes
        qDebug() << "[COLECO] SGM set = false (Forced for ADAM)";
        emul2->SGM = 0;
    }
    else
    {
        // COLECO Modus
        qDebug() << "[COLECO] SGM set =" << enabled;
        emul2->SGM = enabled ? 1 : 0;

        // Deze port writes zijn VEILIG in Coleco-modus
        if (enabled) {
            coleco_writeport(0x60, 0x0F, nullptr);
            coleco_writeport(0x53, 0x01, nullptr);
        } else {
            coleco_writeport(0x53, 0x00, nullptr);
            coleco_writeport(0x60, 0x0F, nullptr); // Correct voor Coleco
        }
    }

    // Deze reset draait nu in beide gevallen met de JUISTE SGM-vlag
    // en zonder dat de ADAM-map corrupt is.
    coleco_reset_and_restart_bios();
    emit sgmStatusChanged(enabled);

    // Her-initialiseer audio na de CPU reset
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
    ejectDisk(drive); // Sla vorige disk op en eject

    QByteArray cPath = QFile::encodeName(path);
    qDebug() << "[Controller] Loading Disk" << drive << ":" << path;
    BYTE ok = coleco_load_disk(drive, cPath.constData());

    if (ok == 0) { // 0 = succes
        m_currentDiskPath[drive] = path;
        emit diskStatusChanged(drive, QFileInfo(path).fileName());
    } else {
        qWarning() << "Failed to load disk image.";
        m_currentDiskPath[drive].clear();
        emit diskStatusChanged(drive, ""); // Stuur lege string
    }
    resetMachine();
}

void ColecoController::loadTape(int drive, const QString& path)
{
    if (drive >= MAX_TAPES) return;
    ejectTape(drive); // Sla vorige tape op en eject

    QByteArray cPath = QFile::encodeName(path);
    qDebug() << "[Controller] Loading Tape" << drive << ":" << path;
    BYTE ok = coleco_load_tape(drive, cPath.constData());

    if (ok == 0) {
        m_currentTapePath[drive] = path;
        emit tapeStatusChanged(drive, QFileInfo(path).fileName());
    } else {
        qWarning() << "Failed to load tape image.";
        m_currentTapePath[drive].clear();
        emit tapeStatusChanged(drive, ""); // Stuur lege string
    }
    resetMachine();
}

void ColecoController::ejectDisk(int drive)
{
    if (drive >= MAX_DISKS) return;

    if (!m_currentDiskPath[drive].isEmpty()) {
        QByteArray cOldPath = QFile::encodeName(m_currentDiskPath[drive]);
        qDebug() << "[Controller] Saving and Ejecting Disk" << drive << ":" << m_currentDiskPath[drive];
        coleco_save_disk(drive, cOldPath.constData());
        coleco_eject_disk(drive);
        m_currentDiskPath[drive].clear();
    }
    emit diskStatusChanged(drive, ""); // Stuur lege string
}

void ColecoController::ejectTape(int drive)
{
    if (drive >= MAX_TAPES) return;

    if (!m_currentTapePath[drive].isEmpty()) {
        QByteArray cOldPath = QFile::encodeName(m_currentTapePath[drive]);
        qDebug() << "[Controller] Saving and Ejecting Tape" << drive << ":" << m_currentTapePath[drive];
        coleco_save_tape(drive, cOldPath.constData());
        coleco_eject_tape(drive);
        m_currentTapePath[drive].clear();
    }
    emit tapeStatusChanged(drive, ""); // Stuur lege string
}

