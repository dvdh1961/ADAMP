#include "6801/adnet_mcu2.h"

#include "CORE/cv.h"
#include "mcu2_gateway.h"

#include <QByteArray>
#include <QDebug>
#include <QFuture>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QString>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

namespace {

std::atomic_bool s_requestedEnabled{false};
std::atomic_bool s_enabled{false};
/* One bit per logical ADAM block drive: D5..D8, then D1..D4.  Disk D5 stays
 * the compatibility default; additional physical drives are armed only by
 * the hardware media preparation flow. */
std::atomic_uint s_physicalBlockMask{0x01u};
quint64 s_generation = 0;
bool s_statusCacheValid[16] = {};
byte s_statusCache[16][4] = {};
bool s_expectFujiDirectoryRecord = false;
QString s_fujiMountedPath;
bool s_fujiMountedPathFresh = false;
bool s_fujiMountPendingBoot = false;
std::atomic_bool s_fujiResetProbeArmed{false};
QByteArray s_fujiLastMountCommand;
Mcu2FujiBootInterceptor s_fujiBootInterceptor;
Mcu2FujiColecoRomReady s_fujiColecoRomReadyHandler;
Mcu2FujiColecoRomProgress s_fujiColecoRomProgressHandler;
Mcu2FujiDirectRomFetch s_fujiDirectRomFetchHandler;
std::atomic_bool s_fujiDirectRomEnabled{false};
byte s_fujiExpectedResponseCommand = 0;
QByteArray s_fujiHostSlots;
QByteArray s_fujiDeviceSlots;
int s_fujiSelectedDeviceSlot = -1;
std::atomic<bool> s_fujiTapeD1ViaD5{false};
std::atomic<bool> s_fujiColecoLoaderActive{false};
std::atomic<bool> s_fujiNativeColecoRequested{false};
QByteArray s_fujiNativeFirstRomBlock;
QByteArray s_fujiNativeRomCapture;
qint64 s_fujiNativeExpectedBytes = 0;

QString mcu2SettingsPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("settings.ini"));
}

void rememberFujiMountCommand(const QByteArray &command)
{
    if (command.size() < 3 || quint8(command[0]) != 0xF8)
        return;
    s_fujiLastMountCommand = command.left(3);
    QSettings settings(mcu2SettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("fujinet/last_mount_command"),
                      QString::fromLatin1(s_fujiLastMountCommand.toHex()));
    settings.sync();
    qDebug() << "[MCU2][BOOT] remembered exact FujiNet F8 command:"
             << s_fujiLastMountCommand.toHex(' ');
}

QByteArray rememberedFujiMountCommand()
{
    if (s_fujiLastMountCommand.size() == 3
        && quint8(s_fujiLastMountCommand[0]) == 0xF8)
        return s_fujiLastMountCommand;
    QSettings settings(mcu2SettingsPath(), QSettings::IniFormat);
    const QByteArray restored = QByteArray::fromHex(settings.value(
        QStringLiteral("fujinet/last_mount_command")).toByteArray());
    if (restored.size() == 3 && quint8(restored[0]) == 0xF8) {
        s_fujiLastMountCommand = restored;
        qDebug() << "[MCU2][BOOT] restored exact FujiNet F8 command:"
                 << restored.toHex(' ');
    }
    return s_fujiLastMountCommand;
}

bool queryFujiDirectSource(QString *host, QString *path)
{
    const QByteArray mount = rememberedFujiMountCommand();
    if (mount.size() < 2 || !host || !path)
        return false;
    const int deviceSlot = quint8(mount[1]);
    auto request = [](const QByteArray &command, int attempts,
                      QByteArray *reply) -> bool {
        const Mcu2Gateway::TransferResult sent =
            Mcu2Gateway::sendBlock(15, command);
        if (!sent.transportOk || sent.masterResult != 0)
            return false;
        const Mcu2Gateway::TransferResult received =
            Mcu2Gateway::receiveBlock(15, attempts);
        if (!received.transportOk || received.masterResult != 0)
            return false;
        *reply = received.data;
        return true;
    };

    QByteArray hosts;
    QByteArray devices;
    QByteArray fullPath;
    if (!request(QByteArray(1, char(0xF4)), 40, &hosts)
        || !request(QByteArray(1, char(0xF2)), 40, &devices)) {
        qWarning() << "[MCU2][DIRECT] unable to query FujiNet F4/F2 metadata";
        return false;
    }
    QByteArray getPath;
    getPath.append(char(0xDA));
    getPath.append(char(deviceSlot));
    if (!request(getPath, 40, &fullPath)) {
        qWarning() << "[MCU2][DIRECT] unable to query FujiNet DA full path";
        return false;
    }
    const int record = deviceSlot * 38;
    if (record >= devices.size())
        return false;
    const int hostSlot = quint8(devices[record]);
    if (hostSlot < 0 || hostSlot >= 8 || hostSlot * 32 >= hosts.size())
        return false;
    QByteArray rawHost = hosts.mid(hostSlot * 32, 32);
    QByteArray rawPath = fullPath.left(256);
    const int hostNul = rawHost.indexOf('\0');
    const int pathNul = rawPath.indexOf('\0');
    if (hostNul >= 0) rawHost.truncate(hostNul);
    if (pathNul >= 0) rawPath.truncate(pathNul);
    *host = QString::fromUtf8(rawHost).trimmed();
    *path = QString::fromUtf8(rawPath).trimmed();
    qDebug() << "[MCU2][DIRECT] live FujiNet source queried; device-slot="
             << deviceSlot << "host-slot=" << hostSlot << "host=" << *host
             << "path=" << *path;
    return !host->isEmpty() && !path->isEmpty();
}

QByteArray inspectFujiD5(QString *error)
{
    QByteArray blocks;
    for (quint32 block = 0; block < 3; ++block) {
        const Mcu2Gateway::DcbResult inspection =
            Mcu2Gateway::inspectBlock(4, 0, block);
        if (!inspection.transportOk || inspection.masterResult != 0
            || inspection.data.size() != 1024) {
            if (error)
                *error = inspection.error;
            break;
        }
        blocks.append(inspection.data);
    }
    return blocks;
}

qint64 detectNativeFujiRomBytes()
{
    /* ROM media consists of D5 loader blocks 0/1 and raw ROM blocks from 2.
     * Find the last readable raw block with exponential then binary probing.
     * This costs O(log n) ADAMNet reads instead of scanning the ROM twice. */
    constexpr quint32 MaxRawBlocks = (16u * 1024u * 1024u) / 1024u;
    auto readable = [](quint32 rawIndex) {
        const Mcu2Gateway::DcbResult probe =
            Mcu2Gateway::inspectBlock(4, 0, 2u + rawIndex);
        return probe.transportOk && probe.masterResult == 0
            && probe.data.size() == 1024;
    };

    quint32 lastGood = 0;
    quint32 firstBad = MaxRawBlocks;
    quint32 candidate = 7; // standard minimum cartridge size: 8 KiB
    while (candidate < MaxRawBlocks) {
        if (!readable(candidate)) {
            firstBad = candidate;
            break;
        }
        lastGood = candidate;
        if (candidate >= (MaxRawBlocks / 2u) - 1u)
            break;
        candidate = (candidate + 1u) * 2u - 1u;
    }

    if (firstBad == MaxRawBlocks) {
        if (lastGood + 1u >= MaxRawBlocks || readable(MaxRawBlocks - 1u))
            return qint64(MaxRawBlocks) * 1024;
        firstBad = MaxRawBlocks - 1u;
    }

    quint32 lo = lastGood + 1u;
    quint32 hi = firstBad;
    while (lo < hi) {
        const quint32 mid = lo + (hi - lo) / 2u;
        if (readable(mid)) {
            lastGood = mid;
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    const qint64 bytes = qint64(lastGood + 1u) * 1024;
    qDebug() << "[MCU2][BOOT] native ROM size detected before D9; bytes=" << bytes;
    return bytes;
}

void reportNativeFujiProgress(bool finished = false)
{
    if (s_fujiColecoRomProgressHandler) {
        s_fujiColecoRomProgressHandler(
            s_fujiNativeRomCapture.size(), s_fujiNativeExpectedBytes, finished);
    }
}

void rememberNativeFujiFirstRomBlock(const QByteArray &headerBlocks,
                                     int decision)
{
    s_fujiNativeFirstRomBlock.clear();
    s_fujiNativeExpectedBytes = 0;
    if (decision != 8 || headerBlocks.size() < 3 * 1024)
        return;

    /* Before D9, FujiNet exposes its two ADAM loader blocks followed by
     * exact ROM offset zero in D5 block 2. Do not search for 55-AA here:
    * a banked MegaCart can keep that signature in a later 16 KiB bank. */
    s_fujiNativeFirstRomBlock = headerBlocks.mid(2 * 1024, 1024);
    reportNativeFujiProgress(false); // show indeterminate bar during size probes
    s_fujiNativeExpectedBytes = detectNativeFujiRomBytes();
    qDebug() << "[MCU2][BOOT] cached exact native ROM block zero from pre-D9 D5 block 2;"
             << "first8=" << s_fujiNativeFirstRomBlock.left(8).toHex(' ');
    reportNativeFujiProgress(false); // switch to a determinate percentage
}

QString saveCapturedFujiColecoRom()
{
    if (s_fujiNativeRomCapture.size() < 8 * 1024)
        return QString();

    /* A conventional Coleco ROM starts with 55 AA (or AA 55), but banked
     * MegaCart images can keep that header at the start of a later 16 KiB
     * bank. D5 block 2 is raw file offset zero, so preserve the stream
     * exactly and only use bank-boundary headers as a sanity check. */
    bool hasColecoHeader = false;
    for (int offset = 0; offset + 1 < s_fujiNativeRomCapture.size();
         offset += 16 * 1024) {
        const quint8 a = quint8(s_fujiNativeRomCapture[offset]);
        const quint8 b = quint8(s_fujiNativeRomCapture[offset + 1]);
        if ((a == 0x55 && b == 0xAA) || (a == 0xAA && b == 0x55)) {
            hasColecoHeader = true;
            break;
        }
    }
    if (!hasColecoHeader) {
        qWarning() << "[MCU2][BOOT] captured D5 stream has no Coleco header on a 16 KiB bank boundary";
        return QString();
    }

    QDir dir(QCoreApplication::applicationDirPath());
    dir.mkpath(QStringLiteral("media/roms/FujiNet"));
    const QString path = dir.filePath(
        QStringLiteral("media/roms/FujiNet/FujiNet.rom"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(s_fujiNativeRomCapture) != s_fujiNativeRomCapture.size()
        || !file.commit()) {
        qWarning() << "[MCU2][BOOT] failed to save D5-loaded FujiNet ROM:"
                   << path << file.errorString();
        return QString();
    }
    qDebug() << "[MCU2][BOOT] complete D5-loaded ROM saved:"
             << path << "bytes=" << s_fujiNativeRomCapture.size()
             << "header=" << s_fujiNativeRomCapture.left(8).toHex(' ');
    return path;
}

bool completeNativeFujiColecoCapture(const char *reason)
{
    const QString savedPath = saveCapturedFujiColecoRom();
    reportNativeFujiProgress(true);
    s_fujiColecoLoaderActive.store(false, std::memory_order_release);
    s_fujiNativeColecoRequested.store(false, std::memory_order_release);
    s_fujiNativeFirstRomBlock.clear();
    s_fujiNativeRomCapture.clear();
    s_fujiNativeExpectedBytes = 0;
    if (savedPath.isEmpty() || !s_fujiColecoRomReadyHandler)
        return false;
    qDebug() << "[MCU2][BOOT] D5 ROM transfer complete; reason=" << reason
             << "switching through the normal software Coleco cartridge route";
    s_fujiColecoRomReadyHandler(savedPath);
    return true;
}

bool extendNativeFujiColecoCapturePast32K()
{
    /* FujiNet's ADAM loader naturally stops after its 32 KiB RAM window, but
     * native Coleco MegaCart/SGM images may be much larger. Continue reading
     * the same physical D5 block stream ourselves until FujiNet reports the
     * real end of media. The upper bound is only a corruption guard; normal
     * Coleco images end far below it. */
    constexpr int MaxNativeRomBytes = 16 * 1024 * 1024;
    unsigned int sector = 34;
    while (s_fujiNativeRomCapture.size() < MaxNativeRomBytes) {
        const Mcu2Gateway::DcbResult part =
            Mcu2Gateway::inspectBlock(4, 0, sector);
        if (!part.transportOk || part.masterResult != 0
            || part.data.size() != 1024) {
            qDebug() << "[MCU2][BOOT] native ROM extension reached real D5 end-of-media;"
                     << "next-sector=" << sector
                     << "bytes=" << s_fujiNativeRomCapture.size()
                     << "result=" << part.masterResult;
            return true;
        }
        s_fujiNativeRomCapture.append(part.data);
        reportNativeFujiProgress(false);
        qDebug() << "[MCU2][BOOT] native ROM extension block captured;"
                 << "sector=" << sector
                 << "total-bytes=" << s_fujiNativeRomCapture.size();
        ++sector;
    }
    qWarning() << "[MCU2][BOOT] native ROM extension hit 16 MiB safety limit;"
               << "refusing to silently truncate the cartridge";
    return false;
}

void applyFujiBootDecision(int decision)
{
    s_fujiTapeD1ViaD5.store(decision >= 5 && decision <= 7,
                            std::memory_order_release);
    s_fujiColecoLoaderActive.store(decision == 3 || decision == 8,
                                   std::memory_order_release);
    s_fujiNativeColecoRequested.store(decision == 8,
                                      std::memory_order_release);
    if (decision != 8) {
        s_fujiNativeRomCapture.clear();
    }
    const int profile = decision >= 5 && decision <= 7
        ? decision - 5 : (decision == 8 ? 3 : decision);
    if (profile < 0 || profile > 3)
        return;
    m_cpm_enabled = profile == 1 || profile == 2;
    m_tdos_enabled = profile == 2;
    m_cpm_selected = false;
}

struct PendingDcb
{
    enum Kind { Dcb, CharacterSend, CharacterReceive } kind = Dcb;
    bool active = false;
    byte device = 0;
    word bufferAddress = 0;
    word bufferLength = 0;
    unsigned int sector = 0;
    quint64 generation = 0;
    const char *deviceName = "ADAMnet device";
    bool cacheStatus = false;
    byte address = 0;
    byte commandByte = 0;
    bool autoCpmBootRetry = false;
    bool cpmBootTrace = false;
    bool fujiTapeBridge = false;
    QByteArray requestDcb;
    QFuture<Mcu2Gateway::DcbResult> future;
    QFuture<Mcu2Gateway::TransferResult> transferFuture;
};

PendingDcb s_pending;
std::atomic<int> s_completedFujiBootProfile{-1};
int s_currentFujiD9Decision = -1;
unsigned int s_cpmPhysicalLastBlock = ~0u;

QByteArray copyDcb(byte device)
{
    QByteArray dcb(DCB_SIZE, char(0));
    for (int offset = 0; offset < DCB_SIZE; ++offset)
        dcb[offset] = char(GetDCB(device, byte(offset)));
    return dcb;
}

const char *physicalBlockDeviceName(byte address, byte deviceNumber)
{
    if (address >= 4 && address <= 7) {
        static const char *const disks[] = { "Disk D5", "Disk D6", "Disk D7", "Disk D8" };
        return disks[address - 4];
    }
    if (address == 8)
        return deviceNumber == 0 ? "Tape D1" : "Tape D2";
    if (address == 9)
        return deviceNumber == 0 ? "Tape D3" : "Tape D4";
    return "ADAMnet block device";
}

void finishPendingTransfer()
{
    const byte device = s_pending.device;
    const word bufferAddress = s_pending.bufferAddress;
    const quint64 generation = s_pending.generation;
    const char *deviceName = s_pending.deviceName;
    const bool cacheStatus = s_pending.cacheStatus;
    const byte address = s_pending.address;
    const PendingDcb::Kind kind = s_pending.kind;
    const word bufferLength = s_pending.bufferLength;
    const unsigned int sector = s_pending.sector;
    const byte commandByte = s_pending.commandByte;
    const bool autoCpmBootRetry = s_pending.autoCpmBootRetry;
    const bool cpmBootTrace = s_pending.cpmBootTrace;
    const bool fujiTapeBridge = s_pending.fujiTapeBridge;
    const QByteArray requestDcb = s_pending.requestDcb;
    s_pending.active = false;
    /* A PCB reset invalidates the captured DCB and buffer address. */
    if (generation != s_generation)
        return;

    if (kind != PendingDcb::Dcb) {
        const Mcu2Gateway::TransferResult result = s_pending.transferFuture.result();
        if (!result.transportOk || result.masterResult != 0) {
            SetDCB(device, DCB_CMD_STAT, 0x03);
            qWarning() << "[MCU2]" << deviceName
                       << (kind == PendingDcb::CharacterSend ? "SEND" : "RECEIVE")
                       << "failed; result=" << result.masterResult
                       << "stage=" << result.stage
                       << "attempts=" << result.attempts
                       << "buffer=" << QStringLiteral("%1")
                              .arg(bufferAddress, 4, 16, QLatin1Char('0'))
                       << "requested=" << bufferLength << result.error;
            return;
        }

        if (kind == PendingDcb::CharacterReceive) {
            if (result.data.size() > 1024
                || (bufferLength != 0 && result.data.size() > bufferLength)) {
                SetDCB(device, DCB_CMD_STAT, 0xE0);
                qWarning() << "[MCU2]" << deviceName
                           << "RECEIVE exceeded the EOS buffer";
                return;
            }
            if (address == 15 && result.data.isEmpty()) {
                /* Preserve CONFIG's cleared response array at end-of-directory. */
                for (word index = 0; index < bufferLength; ++index)
                    coleco_writebyte((bufferAddress + index) & 0xFFFF, 0);
            } else {
                for (int index = 0; index < result.data.size(); ++index)
                    coleco_writebyte((bufferAddress + index) & 0xFFFF,
                                     quint8(result.data[index]));
            }
            if (address == 15 && s_fujiExpectedResponseCommand == 0xF4
                && result.data.size() >= 256) {
                s_fujiHostSlots = result.data.left(256);
                qDebug() << "[MCU2][DIRECT] cached FujiNet host slots";
            } else if (address == 15 && s_fujiExpectedResponseCommand == 0xF2
                       && result.data.size() >= 38) {
                s_fujiDeviceSlots = result.data;
                qDebug() << "[MCU2][DIRECT] cached FujiNet device slots; bytes="
                         << result.data.size();
            }
            if (address == 15)
                s_fujiExpectedResponseCommand = 0;
        }
        if (kind == PendingDcb::CharacterSend && address == 15) {
            if (commandByte == 0xF2 || commandByte == 0xF4)
                s_fujiExpectedResponseCommand = commandByte;
            s_expectFujiDirectoryRecord = (commandByte == 0xF6);
            if (commandByte == 0xD9 && s_currentFujiD9Decision >= 0) {
                /* D9 itself belongs to FujiNet's EOS launcher DCB. Keep the
                 * EOS PCB router active until that DCB has completed; changing
                 * to CP/M earlier prevents all later completion polls. */
                const int completedDecision = s_currentFujiD9Decision;
                applyFujiBootDecision(completedDecision);
                const int completedProfile = completedDecision >= 5
                    && completedDecision <= 7
                    ? completedDecision - 5 : completedDecision;
                if (completedProfile == 1 || completedProfile == 2) {
                    s_completedFujiBootProfile.store(
                        completedProfile, std::memory_order_release);
                    qDebug() << "[MCU2][BOOT] D9 completed under EOS; post-D9 core boot armed for"
                             << (completedProfile == 1 ? "CP/M" : "T-DOS");
                }
                if (completedDecision == 8) {
                    s_fujiNativeRomCapture.clear();
                    qDebug() << "[MCU2][BOOT] D9 completed; native route will first collect"
                             << "the complete ROM through the D5 loader";
                }
                s_currentFujiD9Decision = -1;
            }
        }
        SetDCB(device, DCB_CMD_STAT, RSP_STATUS);
        qDebug() << "[MCU2]" << deviceName
                 << (kind == PendingDcb::CharacterSend ? "SEND" : "RECEIVE")
                 << "completed; bytes=" << result.data.size()
                 << "first=" << (result.data.isEmpty()
                     ? QStringLiteral("--")
                     : QStringLiteral("%1").arg(quint8(result.data[0]), 2, 16,
                                                  QLatin1Char('0')))
                 << "stage=" << result.stage
                 << "attempts=" << result.attempts;
        if (!result.diagnostic.isEmpty())
            qDebug() << "[MCU2]" << deviceName << result.diagnostic;
        return;
    }

    Mcu2Gateway::DcbResult result = s_pending.future.result();

    if (s_fujiNativeColecoRequested.load(std::memory_order_acquire)
        && address == 4 && commandByte == CMD_READ && sector == 2
        && result.masterResult == 0 && result.data.size() == 1024
        && s_fujiNativeFirstRomBlock.size() == 1024) {
        qDebug() << "[MCU2][BOOT] restoring exact pre-D9 ROM block zero over post-D9 loader block;"
                 << "loader=" << result.data.left(8).toHex(' ')
                 << "rom=" << s_fujiNativeFirstRomBlock.left(8).toHex(' ');
        result.data = s_fujiNativeFirstRomBlock;
    }

    if (s_fujiNativeColecoRequested.load(std::memory_order_acquire)
        && address == 4 && commandByte == CMD_READ && sector >= 2
        && result.masterResult == 0 && result.data.size() == 1024) {
        const int offset = int(sector - 2) * 1024;
        if (offset < 32 * 1024) {
            if (s_fujiNativeRomCapture.size() < offset + 1024)
                s_fujiNativeRomCapture.resize(offset + 1024);
            std::copy(result.data.cbegin(), result.data.cend(),
                      s_fujiNativeRomCapture.begin() + offset);
            reportNativeFujiProgress(false);
            qDebug() << "[MCU2][BOOT] native ROM block captured from D5;"
                     << "sector=" << sector << "rom-offset=" << offset;
            /* Sector 33 ends only the ADAM loader's 32 KiB window, not
             * necessarily the cartridge. Probe D5 beyond that window until
             * its real end so MegaCart and large SGM images remain complete. */
            if (sector == 33 && extendNativeFujiColecoCapturePast32K())
                completeNativeFujiColecoCapture(
                    "ADAM 32 KiB window complete; physical D5 fully extended");
        }
    }

    if (s_fujiNativeColecoRequested.load(std::memory_order_acquire)
        && address == 4 && commandByte == CMD_READ && sector > 2
        && result.masterResult != 0 && !s_fujiNativeRomCapture.isEmpty()) {
        completeNativeFujiColecoCapture("D5 end-of-media");
    }

    if (!result.transportOk || result.returnedDcb.size() != DCB_SIZE) {
        SetDCB(device, DCB_CMD_STAT, 0x03);
        SetDCB(device, DCB_NODE_TYPE,
               byte(GetDCB(device, DCB_NODE_TYPE) | 0x03));
        qWarning() << "[MCU2]" << deviceName << "transaction failed:" << result.error;
        return;
    }

    for (int offset = 0; offset < DCB_SIZE; ++offset)
        SetDCB(device, byte(offset), quint8(result.returnedDcb[offset]));

    if (fujiTapeBridge) {
        /* The backend DCB names physical D5.  Never leak that rewritten
         * identity into the guest's logical Tape D1 descriptor. */
        for (int offset = DCB_BA_LO; offset < DCB_SIZE; ++offset)
            SetDCB(device, byte(offset), quint8(requestDcb[offset]));
        SetDCB(device, DCB_CMD_STAT,
               quint8(result.returnedDcb[DCB_CMD_STAT]));
    }

    if (autoCpmBootRetry && result.masterResult == 0
        && quint8(result.returnedDcb[DCB_CMD_STAT]) == RSP_STATUS) {
        /* This is CP/M's second-phase temporary logical DCB.  The software
         * drive copies the cached block and changes only completion status;
         * retain the guest descriptor while doing the same physical copy. */
        for (int offset = DCB_BA_LO; offset < DCB_SIZE; ++offset)
            SetDCB(device, byte(offset), quint8(requestDcb[offset]));
        SetDCB(device, DCB_CMD_STAT, RSP_STATUS);
        qDebug() << "[MCU2][CPM] temporary DCB completed with software-cache semantics; final="
                 << copyDcb(device).toHex(' ');
    }

    if (cpmBootTrace) {
        qDebug() << "[MCU2][CPM][TRACE] DCB transfer"
                 << "device=" << int(device)
                 << "sector=" << sector
                 << "buffer=" << QStringLiteral("%1")
                        .arg(bufferAddress, 4, 16, QLatin1Char('0'))
                 << "length=" << bufferLength
                 << "PCB=" << QStringLiteral("%1")
                        .arg(PCBAddr, 4, 16, QLatin1Char('0'))
                 << "request=" << requestDcb.toHex(' ')
                 << "response=" << result.returnedDcb.toHex(' ');
    }

    if (cacheStatus && result.masterResult == 0
        && quint8(result.returnedDcb[DCB_CMD_STAT]) == RSP_STATUS) {
        for (int index = 0; index < 4; ++index)
            s_statusCache[address][index] =
                quint8(result.returnedDcb[DCB_MAXL_LO + index]);
        s_statusCacheValid[address] = true;
        qDebug() << "[MCU2]" << deviceName
                 << "STATUS cached for ADAMnet address" << Qt::hex << int(address);
    }

    if (!result.data.isEmpty()) {
        if (result.data.size() != 1024) {
            SetDCB(device, DCB_CMD_STAT, 0x03);
            qWarning() << "[MCU2]" << deviceName << "returned an invalid block size";
            return;
        }
        /* Match the software disk backend: an EOS transfer buffer may overlap
         * the live PCB/DCB table.  Writing disk data over that table corrupts
         * the command which is still being completed and eventually leaves
         * the Z80 at a blank screen.  The real Master 6801 owns this area, so
         * it must not be overwritten by the emulated DMA copy either.
         */
        const word pcbLo = PCBAddr;
        const word pcbHi = word(PCBAddr + PCB_SIZE
                                + (GetMaxDCB() + 1) * DCB_SIZE);
        int protectedBytes = 0;
        quint32 hash = 2166136261u; // FNV-1a, diagnostic only.
        for (int offset = 0; offset < result.data.size(); ++offset) {
            const byte value = quint8(result.data[offset]);
            hash = (hash ^ value) * 16777619u;
            const word destination = word(bufferAddress + offset);
            if (destination >= pcbLo && destination < pcbHi) {
                ++protectedBytes;
                continue;
            }
            RAM_Memory[destination] = value;
        }

        const QByteArray first = result.data.left(8).toHex(' ');
        const QByteArray last = result.data.right(8).toHex(' ');
        qDebug() << "[MCU2]" << deviceName << "READ data; sector=" << sector
                 << "buffer=" << QStringLiteral("%1")
                        .arg(bufferAddress, 4, 16, QLatin1Char('0'))
                 << "requested=" << bufferLength
                 << "received=" << result.data.size()
                 << "protected=" << protectedBytes
                 << "fnv1a=" << QStringLiteral("%1")
                        .arg(hash, 8, 16, QLatin1Char('0'))
                 << "first8=" << first << "last8=" << last;
    }

    qDebug() << "[MCU2]" << deviceName << "DCB completed; result=" << result.masterResult
             << "command=" << QStringLiteral("%1").arg(commandByte, 2, 16, QLatin1Char('0'))
             << "dcb-status=" << QStringLiteral("%1").arg(
                    quint8(result.returnedDcb[DCB_CMD_STAT]), 2, 16, QLatin1Char('0'))
             << "node-type=" << QStringLiteral("%1").arg(
                    GetDCB(device, DCB_NODE_TYPE), 2, 16, QLatin1Char('0'))
             << "stage=" << result.stage << "attempts=" << result.attempts
             << "gateway-attempts=" << result.gatewayAttempts
             << "gateway-ms=" << result.gatewayElapsedMs
             << "stream=" << (result.streamed ? "YES" : "NO")
             << "bytes=" << result.data.size();
}

} // namespace

void mcu2_disk_set_enabled(bool enabled)
{
    /* The selection is latched by mcu2_disk_reset() on the emulator thread.
     * This prevents a GUI change from switching backends during a live DCB.
     */
    s_requestedEnabled.store(enabled, std::memory_order_release);
}

bool mcu2_disk_is_enabled(void)
{
    return s_enabled.load(std::memory_order_acquire);
}

int mcu2_take_completed_fuji_boot_profile(void)
{
    return s_completedFujiBootProfile.exchange(-1, std::memory_order_acq_rel);
}

unsigned int physicalBlockBit(byte deviceId)
{
    if (deviceId >= 0x04 && deviceId <= 0x07)
        return 1u << (deviceId - 0x04);
    if (deviceId == 0x08) return 1u << 4; // D1
    if (deviceId == 0x18) return 1u << 5; // D2
    if (deviceId == 0x09) return 1u << 6; // D3
    if (deviceId == 0x19) return 1u << 7; // D4
    return 0u;
}

void mcu2_block_set_device_enabled(byte deviceId, bool enabled)
{
    const unsigned int bit = physicalBlockBit(deviceId);
    if (!bit)
        return;
    if (enabled)
        s_physicalBlockMask.fetch_or(bit, std::memory_order_acq_rel);
    else
        s_physicalBlockMask.fetch_and(~bit, std::memory_order_acq_rel);
}

bool mcu2_block_device_is_enabled(byte deviceId)
{
    const unsigned int bit = physicalBlockBit(deviceId);
    return mcu2_disk_is_enabled() && bit
        && (s_physicalBlockMask.load(std::memory_order_acquire) & bit) != 0;
}

void mcu2_set_fuji_boot_interceptor(Mcu2FujiBootInterceptor interceptor)
{
    s_fujiBootInterceptor = std::move(interceptor);
    /* Also cover the first hardware boot after application startup. */
    s_fujiResetProbeArmed = bool(s_fujiBootInterceptor);
}

void mcu2_set_fuji_coleco_rom_ready_handler(Mcu2FujiColecoRomReady handler)
{
    s_fujiColecoRomReadyHandler = std::move(handler);
}

void mcu2_set_fuji_coleco_rom_progress_handler(
    Mcu2FujiColecoRomProgress handler)
{
    s_fujiColecoRomProgressHandler = std::move(handler);
}

void mcu2_set_fuji_direct_rom_enabled(bool enabled)
{
    s_fujiDirectRomEnabled.store(enabled, std::memory_order_release);
    qDebug() << "[MCU2][DIRECT] fast native ROM route="
             << (enabled ? "ENABLED" : "disabled");
}

void mcu2_set_fuji_direct_rom_fetch_handler(Mcu2FujiDirectRomFetch handler)
{
    s_fujiDirectRomFetchHandler = std::move(handler);
}

void mcu2_arm_fuji_reset_boot_probe(void)
{
    /* Reset ADAM after a native Coleco game starts a genuinely new FujiNet
     * session.  The Coleco switch can occur just after D5 end-of-media, while
     * the old guest DCB/session flags still describe that completed ROM
     * transfer.  Merely making D5 visible again lets the preflight dialog
     * work, but the following BIOS read then inherits stale state and stalls.
     * Invalidate all transient work before arming the new preflight. */
    if (s_fujiNativeExpectedBytes > 0 || !s_fujiNativeRomCapture.isEmpty())
        reportNativeFujiProgress(true);
    ++s_generation;
    s_pending.active = false;
    s_fujiMountPendingBoot = false;
    s_fujiMountedPathFresh = false;
    s_fujiMountedPath.clear();
    s_currentFujiD9Decision = -1;
    s_fujiColecoLoaderActive.store(false, std::memory_order_release);
    s_fujiNativeColecoRequested.store(false, std::memory_order_release);
    s_fujiNativeFirstRomBlock.clear();
    s_fujiNativeRomCapture.clear();
    s_fujiNativeExpectedBytes = 0;
    s_cpmPhysicalLastBlock = ~0u;
    std::memset(s_statusCacheValid, 0, sizeof(s_statusCacheValid));
    s_requestedEnabled.store(true, std::memory_order_release);

    /* A previous reset dialog may have cancelled D5 for that boot only. */
    mcu2_block_set_device_enabled(0x04, true);
    s_fujiResetProbeArmed = true;
    qDebug() << "[MCU2][BOOT] fresh FujiNet session prepared;"
             << "next reset D5 preflight armed";
}

bool mcu2_fuji_reset_boot_probe_is_armed(void)
{
    return s_fujiResetProbeArmed.load(std::memory_order_acquire);
}

bool mcu2_prepare_fuji_reset_boot(void)
{
    if (!s_fujiResetProbeArmed.load(std::memory_order_acquire))
        return true;
    if (!(s_enabled.load(std::memory_order_acquire)
          || s_requestedEnabled.load(std::memory_order_acquire))
        || !(s_physicalBlockMask.load(std::memory_order_acquire) & 0x01u)
        || !s_fujiBootInterceptor)
        return true;

    if (!s_fujiResetProbeArmed.exchange(false, std::memory_order_acq_rel))
        return true;

    QString inspectError;
    const QByteArray headerBlocks = inspectFujiD5(&inspectError);
    if (headerBlocks.size() != 3 * 1024) {
        qWarning() << "[MCU2][BOOT] pre-reset D5 preflight unavailable:"
                   << inspectError;
        return true;
    }

    qDebug() << "[MCU2][BOOT] pre-reset D5 preflight; header-bytes="
             << headerBlocks.size();
    const int decision = s_fujiBootInterceptor(headerBlocks, QString());
    if (decision < 0) {
        mcu2_block_set_device_enabled(0x04, false);
        qWarning() << "[MCU2][BOOT] pre-reset D5 boot cancelled; D5 hidden until next reset";
        return false;
    }

    if (decision == 8
        && s_fujiDirectRomEnabled.load(std::memory_order_acquire)
        && s_fujiDirectRomFetchHandler) {
        QString host;
        QString path;
        if (queryFujiDirectSource(&host, &path)) {
            const QString directRom = s_fujiDirectRomFetchHandler(host, path);
            if (directRom == QStringLiteral("::ADAMP_DIRECT_CANCELLED::")) {
                mcu2_block_set_device_enabled(0x04, false);
                qDebug() << "[MCU2][DIRECT] pre-reset download cancelled; D5 fallback suppressed";
                return false;
            }
            if (!directRom.isEmpty()) {
                applyFujiBootDecision(8);
                qDebug() << "[MCU2][DIRECT] pre-reset direct ROM complete;"
                         << "physical D5 loader skipped:" << directRom;
                if (s_fujiColecoRomReadyHandler)
                    s_fujiColecoRomReadyHandler(directRom);
                /* The retained-media boot has been replaced completely by
                 * the direct cartridge route. Tell the controller not to
                 * continue with its normal ADAM BIOS/D5 reset afterwards. */
                return false;
            }
        }
        qWarning() << "[MCU2][DIRECT] pre-reset direct route unavailable;"
                   << "falling back to physical D5";
    }

    rememberNativeFujiFirstRomBlock(headerBlocks, decision);

    if ((decision >= 0 && decision <= 3)
        || (decision >= 5 && decision <= 7)) {
        /* Replay the exact F8 bytes captured from FujiNet. The third byte is
         * mount state/mode and cannot be inferred from EOS versus ROM: the
         * working SmartBASIC trace itself uses F8 00 01. */
        QByteArray mountCommand = rememberedFujiMountCommand();
        if (mountCommand.size() != 3) {
            mcu2_block_set_device_enabled(0x04, false);
            qWarning() << "[MCU2][BOOT] no remembered F8 command; select the image once in FujiNet";
            return false;
        }
        const Mcu2Gateway::TransferResult mount =
            Mcu2Gateway::sendBlock(15, mountCommand);
        const bool mounted = mount.transportOk && mount.masterResult == 0;
        qDebug() << "[MCU2][BOOT] pre-reset retained media F8 completed; result="
                 << mount.masterResult << "stage=" << mount.stage
                 << "attempts=" << mount.attempts << "mounted=" << mounted;
        if (!mounted) {
            mcu2_block_set_device_enabled(0x04, false);
            qWarning() << "[MCU2][BOOT] pre-reset F8 failed; D5 hidden:"
                       << mount.error;
            return false;
        }

        const Mcu2Gateway::TransferResult boot =
            Mcu2Gateway::sendBlock(15, QByteArray::fromHex("d900"));
        const bool ready = boot.transportOk && boot.masterResult == 0;
        qDebug() << "[MCU2][BOOT] pre-reset retained media D9 completed; result="
                 << boot.masterResult << "stage=" << boot.stage
                 << "attempts=" << boot.attempts << "ready=" << ready;
        if (!ready) {
            mcu2_block_set_device_enabled(0x04, false);
            qWarning() << "[MCU2][BOOT] pre-reset D9 failed; D5 hidden:"
                       << boot.error;
            return false;
        }
    }

    applyFujiBootDecision(decision);
    qDebug() << "[MCU2][BOOT] pre-reset route=" << decision
             << "target=" << (decision >= 5 && decision <= 7
                                  ? "Tape D1" : "Disk D5");
    return true;
}

void mcu2_disk_reset(void)
{
    ++s_generation;
    s_pending.active = false;
    s_cpmPhysicalLastBlock = ~0u;
    std::memset(s_statusCacheValid, 0, sizeof(s_statusCacheValid));
    /* A normal Reset ADAM must boot the configured FujiNet start medium.
     * It must never inherit a stale mount->boot interception arm. */
    s_fujiMountPendingBoot = false;
    s_fujiMountedPathFresh = false;
    s_fujiMountedPath.clear();
    s_enabled.store(
        s_requestedEnabled.load(std::memory_order_acquire),
        std::memory_order_release);

    /* Retained-media preparation is completed by the controller before it
     * enters the BIOS reset. A PCB reset is too late for FujiNet D9. */
}

void UpdateDSK_MCU2_EOS(byte Dev, int command)
{
    if (!mcu2_disk_is_enabled())
        return;

    if (command < 0) {
        if (!s_pending.active || s_pending.device != Dev)
            return;
        if (!s_pending.future.isFinished()) {
            SetDCB(Dev, DCB_CMD_STAT, 0x00);
            return;
        }
        finishPendingTransfer();
        return;
    }

    /* Never queue two physical transactions at the same time. */
    if (s_pending.active) {
        SetDCB(Dev, DCB_CMD_STAT, 0x03);
        return;
    }

    if (command == CMD_FORMAT) {
        SetDCB(Dev, DCB_CMD_STAT, 0xE2);
        return;
    }
    if (command != CMD_STATUS && command != CMD_SOFT_RESET
        && command != CMD_READ && command != CMD_WRITE) {
        SetDCB(Dev, DCB_CMD_STAT, 0xE2);
        return;
    }

    const QByteArray dcb = copyDcb(Dev);
    const byte address = byte(GetDCB(Dev, DCB_ADD_CODE) & 0x0F);
    const byte deviceNumber = GetDCB(Dev, DCB_DEV_NUM);
    const bool tapeDevice = address == 8 || address == 9;
    const bool fujiTapeBridge = address == 8 && deviceNumber == 0
        && s_fujiTapeD1ViaD5.load(std::memory_order_acquire);
    const word bufferAddress = GetDCBBase(Dev);
    const word bufferLength = GetDCBLen(Dev);
    QByteArray backendDcb = dcb;
    if (fujiTapeBridge) {
        /* FujiNet mounts a selected DDP in physical slot-zero D5.  Preserve
         * the guest-facing Tape D1 identity while using that real backing
         * node for its block transfer. */
        backendDcb[DCB_ADD_CODE] = char(0x04);
        backendDcb[DCB_DEV_NUM] = char(0x00);
    }

    /* MCU2's generic DCB adapter is the disk-node path (addresses 4..7).
     * Tape nodes 8/9 use the addressed block protocol for their actual data,
     * which deliberately only implements READ and WRITE.  Present an enabled
     * physical tape to the ADAM BIOS with the same local STATUS/SOFT RESET
     * response as the software DDP backend; subsequent block data still goes
     * through MCU2 and FujiNet. */
    if (tapeDevice && command == CMD_STATUS) {
        SetDCB(Dev, DCB_NODE_TYPE, 0x00);
        ReportDevice(Dev, 0x0400, 1);
        qDebug() << "[MCU2]" << physicalBlockDeviceName(address, deviceNumber)
                 << "STATUS completed locally; addressed block device ready";
        return;
    }
    if (tapeDevice && command == CMD_SOFT_RESET) {
        SetDCB(Dev, DCB_NODE_TYPE, 0x00);
        SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        qDebug() << "[MCU2]" << physicalBlockDeviceName(address, deviceNumber)
                 << "SOFT RESET completed locally";
        return;
    }

    QByteArray writeBlock;
    if (command == CMD_WRITE) {
        if (bufferLength != 1024) {
            SetDCB(Dev, DCB_CMD_STAT, 0xE0);
            qWarning() << "[MCU2] block WRITE requires 1024 bytes; requested="
                       << bufferLength;
            return;
        }
        writeBlock.reserve(1024);
        for (int offset = 0; offset < 1024; ++offset)
            writeBlock.append(char(coleco_readbyte(
                (bufferAddress + offset) & 0xFFFF)));
    }
    s_pending.active = true;
    s_pending.device = Dev;
    s_pending.bufferAddress = bufferAddress;
    s_pending.bufferLength = bufferLength;
    s_pending.sector = GetDCBSector(Dev);
    s_pending.generation = s_generation;
    s_pending.deviceName = physicalBlockDeviceName(address, deviceNumber);
    s_pending.kind = PendingDcb::Dcb;
    s_pending.commandByte = byte(command);
    s_pending.cacheStatus = false;
    s_pending.address = address;
    s_pending.autoCpmBootRetry = false;
    s_pending.cpmBootTrace = m_cpm_enabled && address == 4
        && command == CMD_READ && s_pending.sector >= 7 && s_pending.sector <= 8;
    s_pending.fujiTapeBridge = fujiTapeBridge;
    s_pending.requestDcb = dcb;
    SetDCB(Dev, DCB_CMD_STAT, 0x00);
    if (command == CMD_READ || command == CMD_WRITE) {
        if (address >= 4 && address <= 7)
            g_diskSoundActive.store(true, std::memory_order_relaxed);
        else if (tapeDevice)
            g_tapeSoundActive.store(true, std::memory_order_relaxed);
    }
    if (command == CMD_WRITE) {
        quint32 writeHash = 2166136261u;
        for (char value : writeBlock)
            writeHash = (writeHash ^ quint8(value)) * 16777619u;
        qDebug() << "[MCU2]" << s_pending.deviceName
                 << "WRITE queued; sector=" << s_pending.sector
                 << "buffer=" << QStringLiteral("%1")
                        .arg(bufferAddress, 4, 16, QLatin1Char('0'))
                 << "bytes=" << writeBlock.size()
                 << "fnv1a=" << QStringLiteral("%1")
                        .arg(writeHash, 8, 16, QLatin1Char('0'))
                 << "first8=" << writeBlock.left(8).toHex(' ')
                 << "last8=" << writeBlock.right(8).toHex(' ');
        s_pending.future = QtConcurrent::run(
            [backendDcb, writeBlock, tapeDevice, fujiTapeBridge]() {
            return tapeDevice && !fujiTapeBridge
                ? Mcu2Gateway::executeBlockDcb(backendDcb, writeBlock)
                : Mcu2Gateway::executeDcbWrite(backendDcb, writeBlock);
        });
    } else {
        if (fujiTapeBridge && command == CMD_READ)
            qDebug() << "[MCU2][BOOT] Tape D1 READ bridged to FujiNet D5; sector="
                     << s_pending.sector;
        s_pending.future = QtConcurrent::run(
            [backendDcb, tapeDevice, command, fujiTapeBridge]() {
            return tapeDevice && command == CMD_READ && !fujiTapeBridge
                ? Mcu2Gateway::executeBlockDcb(backendDcb)
                : Mcu2Gateway::executeDcb(backendDcb);
        });
    }
}

void UpdateDSK_MCU2_CPM_D5(byte Dev, int command)
{
    if (!mcu2_disk_is_enabled())
        return;

    if (command < 0) {
        /* CP/M may issue this temporary read immediately after starting a
         * STATUS on another DCB, then stop polling that older DCB.  Drain the
         * older serialized request from this read's polling loop and queue
         * the requested block as soon as the bus becomes free. */
        if (s_pending.active && s_pending.device != Dev) {
            if (!s_pending.future.isFinished()) {
                SetDCB(Dev, DCB_CMD_STAT, 0x00);
                return;
            }
            const bool sameBlock = s_pending.sector == GetDCBSector(Dev)
                && s_pending.bufferAddress == GetDCBBase(Dev)
                && s_pending.bufferLength == GetDCBLen(Dev);
            qDebug() << "[MCU2][CPM] completing older DCB"
                     << int(s_pending.device) << "before temporary DCB"
                     << int(Dev);
            finishPendingTransfer();
            if (sameBlock) {
                /* The second phase already delivered this exact block to this
                 * exact buffer.  Identity 52 is the software cache-completion
                 * alias: acknowledge it without a third physical transfer and
                 * without rewriting any of its logical descriptor fields. */
                SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
                qDebug() << "[MCU2][CPM] temporary DCB satisfied by second-phase D5 cache; sector="
                         << GetDCBSector(Dev);
                return;
            }
            UpdateDSK_MCU2_CPM_D5(Dev, CMD_READ);
            return;
        }
        UpdateDSK_MCU2_EOS(Dev, command);
        return;
    }
    if (command != CMD_READ) {
        SetDCB(Dev, DCB_CMD_STAT, 0xE2);
        qWarning() << "[MCU2][CPM] direct D5 rejected unexpected command="
                   << Qt::hex << command;
        return;
    }

    const unsigned int requestedSector = GetDCBSector(Dev);
    if (requestedSector != s_cpmPhysicalLastBlock) {
        /* Match UpdateDSK_CPM(): the first request for a new block reports
         * 9B and only arms the block.  CP/M repeats that same READ, at which
         * point the software backend copies the cached data and returns 80. */
        s_cpmPhysicalLastBlock = requestedSector;
        SetDCB(Dev, DCB_CMD_STAT, RSP_TIMEOUT);
        qDebug() << "[MCU2][CPM] physical D5 first phase; sector="
                 << requestedSector << "status=9b";
        return;
    }
    if (s_pending.active) {
        SetDCB(Dev, DCB_CMD_STAT, 0x00);
        qDebug() << "[MCU2][CPM] direct D5 waiting behind pending DCB="
                   << int(s_pending.device) << "requested DCB=" << int(Dev);
        return;
    }

    const word bufferAddress = GetDCBBase(Dev);
    const word bufferLength = GetDCBLen(Dev);
    const unsigned int sector = requestedSector;
    const QByteArray requestDcb = copyDcb(Dev);
    if (bufferLength != 1024) {
        SetDCB(Dev, DCB_CMD_STAT, 0xE0);
        qWarning() << "[MCU2][CPM] direct D5 rejected buffer length="
                   << bufferLength;
        return;
    }

    s_pending.active = true;
    s_pending.device = Dev;
    s_pending.bufferAddress = bufferAddress;
    s_pending.bufferLength = bufferLength;
    s_pending.sector = sector;
    s_pending.generation = s_generation;
    s_pending.deviceName = "Disk D5";
    s_pending.kind = PendingDcb::Dcb;
    s_pending.commandByte = CMD_READ;
    s_pending.cacheStatus = false;
    s_pending.address = 4;
    s_pending.autoCpmBootRetry = (sector == 8);
    s_pending.cpmBootTrace = (sector >= 7 && sector <= 8);
    s_pending.fujiTapeBridge = false;
    s_pending.requestDcb = requestDcb;
    SetDCB(Dev, DCB_CMD_STAT, 0x00);
    g_diskSoundActive.store(true, std::memory_order_relaxed);

    qDebug() << "[MCU2][CPM] direct physical D5 READ queued; DCB=" << int(Dev)
             << "sector=" << sector
             << "buffer=" << QStringLiteral("%1")
                    .arg(bufferAddress, 4, 16, QLatin1Char('0'));
    s_pending.future = QtConcurrent::run([sector]() {
        return Mcu2Gateway::inspectBlock(4, 0, sector);
    });
}

void UpdateMCU2CharacterEOS(byte Dev, int command, const char *deviceName)
{
    if (!mcu2_disk_is_enabled())
        return;

    if (command < 0) {
        if (!s_pending.active || s_pending.device != Dev)
            return;
        const bool finished = (s_pending.kind == PendingDcb::Dcb)
            ? s_pending.future.isFinished() : s_pending.transferFuture.isFinished();
        if (!finished) {
            SetDCB(Dev, DCB_CMD_STAT, 0x00);
            return;
        }
        finishPendingTransfer();
        return;
    }

    if (s_pending.active) {
        SetDCB(Dev, DCB_CMD_STAT, 0x03);
        qWarning() << "[MCU2]" << (deviceName ? deviceName : "ADAMnet device")
                   << "STATUS deferred because the physical bus is busy";
        return;
    }
    if (command != CMD_STATUS && command != CMD_WRITE && command != CMD_READ) {
        SetDCB(Dev, DCB_CMD_STAT, 0xE2);
        qWarning() << "[MCU2]" << (deviceName ? deviceName : "ADAMnet device")
                   << "command" << Qt::hex << command
                   << "is not supported by the character-device bridge";
        return;
    }

    const byte address = byte(GetDCB(Dev, DCB_ADD_CODE) & 0x0F);
    const word bufferAddress = GetDCBBase(Dev);
    const word bufferLength = GetDCBLen(Dev);

    if (command == CMD_WRITE && bufferLength > 1024) {
        SetDCB(Dev, DCB_CMD_STAT, 0xE0);
        qWarning() << "[MCU2]" << (deviceName ? deviceName : "ADAMnet device")
                   << "SEND length" << bufferLength << "exceeds 1024 bytes";
        return;
    }

    if (command == CMD_STATUS && s_statusCacheValid[address]) {
        for (int index = 0; index < 4; ++index)
            SetDCB(Dev, byte(DCB_MAXL_LO + index), s_statusCache[address][index]);
        SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        return;
    }

    s_pending.active = true;
    s_pending.device = Dev;
    s_pending.bufferAddress = bufferAddress;
    s_pending.bufferLength = bufferLength;
    s_pending.generation = s_generation;
    s_pending.deviceName = deviceName ? deviceName : "ADAMnet device";
    s_pending.cacheStatus = (command == CMD_STATUS);
    s_pending.address = address;
    s_pending.autoCpmBootRetry = false;
    s_pending.cpmBootTrace = false;
    s_pending.fujiTapeBridge = false;
    s_pending.requestDcb.clear();
    SetDCB(Dev, DCB_CMD_STAT, 0x00);
    if (command == CMD_STATUS) {
        const QByteArray dcb = copyDcb(Dev);
        /* copyDcb() must preserve the original STATUS command, not BUSY. */
        QByteArray request = dcb;
        request[DCB_CMD_STAT] = char(CMD_STATUS);
        s_pending.kind = PendingDcb::Dcb;
        s_pending.future = QtConcurrent::run([request]() {
            return Mcu2Gateway::executeDcb(request);
        });
    } else if (command == CMD_WRITE) {
        QByteArray data;
        data.reserve(bufferLength);
        for (word index = 0; index < bufferLength; ++index)
            data.append(char(coleco_readbyte(
                (bufferAddress + index) & 0xFFFF)));

        if (address == 15 && !data.isEmpty() && quint8(data[0]) == 0xE2
            && data.size() > 2) {
            s_fujiSelectedDeviceSlot = quint8(data[1]);
            const QByteArray pathBytes = data.mid(2);
            const int terminator = pathBytes.indexOf('\0');
            s_fujiMountedPath = QString::fromUtf8(
                terminator >= 0 ? pathBytes.left(terminator) : pathBytes);
            s_fujiMountedPathFresh = true;
            qDebug() << "[MCU2][BOOT] FujiNet mounted path captured:"
                     << s_fujiMountedPath;
        }

        if (address == 15 && data.size() >= 3 && quint8(data[0]) == 0xF8) {
            rememberFujiMountCommand(data);
            s_fujiMountPendingBoot = true;
            qDebug() << "[MCU2][BOOT] FujiNet F8 mount observed; next D9 armed";
        }

        /* D9 is FujiNet's reboot/boot command.  F8 has already mounted the
         * selected image, so this is the last safe point to inspect D5 and
         * choose the emulator OS/input route before any boot sector runs. */
        if (address == 15 && data.size() >= 2 && quint8(data[0]) == 0xD9
            && s_fujiMountPendingBoot
            && s_fujiBootInterceptor) {
            s_fujiMountPendingBoot = false;
            QString inspectError;
            const QByteArray headerBlocks = inspectFujiD5(&inspectError);
            qDebug() << "[MCU2][BOOT] D9 intercepted; header-bytes="
                     << headerBlocks.size() << "path=" << s_fujiMountedPath
                     << "inspection-error=" << inspectError;

            const QString interceptedPath = s_fujiMountedPathFresh
                ? s_fujiMountedPath : QString();
            const int decision = s_fujiBootInterceptor(
                headerBlocks, interceptedPath);
            rememberNativeFujiFirstRomBlock(headerBlocks, decision);
            if (decision == 8)
                s_fujiNativeRomCapture.clear();
            s_fujiMountedPathFresh = false;
            s_fujiMountedPath.clear();
            if (decision < 0) {
                s_pending.active = false;
                SetDCB(Dev, DCB_CMD_STAT, 0x03);
                qWarning() << "[MCU2][BOOT] FujiNet D9 cancelled by user";
                return;
            }
            if (decision == 8
                && s_fujiDirectRomEnabled.load(std::memory_order_acquire)
                && s_fujiDirectRomFetchHandler) {
                int slot = s_fujiSelectedDeviceSlot;
                if (slot < 0 && s_fujiLastMountCommand.size() >= 2)
                    slot = quint8(s_fujiLastMountCommand[1]);
                int hostSlot = -1;
                if (slot >= 0 && (slot * 38 + 1) < s_fujiDeviceSlots.size())
                    hostSlot = quint8(s_fujiDeviceSlots[slot * 38]);
                QString host;
                QString directPath = interceptedPath;
                if (hostSlot >= 0 && hostSlot < 8
                    && hostSlot * 32 < s_fujiHostSlots.size()) {
                    QByteArray raw = s_fujiHostSlots.mid(hostSlot * 32, 32);
                    const int nul = raw.indexOf('\0');
                    if (nul >= 0) raw.truncate(nul);
                    host = QString::fromUtf8(raw).trimmed();
                }
                if (host.isEmpty() || directPath.isEmpty())
                    queryFujiDirectSource(&host, &directPath);
                qDebug() << "[MCU2][DIRECT] attempting direct native ROM; device-slot="
                         << slot << "host-slot=" << hostSlot << "host=" << host
                         << "path=" << directPath;
                const QString directRom = s_fujiDirectRomFetchHandler(
                    host, directPath);
                if (directRom == QStringLiteral("::ADAMP_DIRECT_CANCELLED::")) {
                    s_pending.active = false;
                    SetDCB(Dev, DCB_CMD_STAT, 0x03);
                    qDebug() << "[MCU2][DIRECT] live download cancelled; D9/D5 fallback suppressed";
                    return;
                }
                if (!directRom.isEmpty()) {
                    s_pending.active = false;
                    s_currentFujiD9Decision = -1;
                    applyFujiBootDecision(8);
                    SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
                    s_fujiMountedPathFresh = false;
                    s_fujiMountedPath.clear();
                    qDebug() << "[MCU2][DIRECT] direct ROM complete; FujiNet D9 skipped:"
                             << directRom;
                    if (s_fujiColecoRomReadyHandler)
                        s_fujiColecoRomReadyHandler(directRom);
                    return;
                }
                qWarning() << "[MCU2][DIRECT] direct route unavailable; falling back to physical D5";
            }
            /* Do not switch the PCB router here. D9 is still an outstanding
             * EOS character-device command and must first reach RSP_STATUS. */
            s_currentFujiD9Decision = decision;
            const int selectedProfile = decision >= 5 && decision <= 7
                ? decision - 5 : (decision == 8 ? 3 : decision);
            qDebug() << "[MCU2][BOOT] route selected; deferred until D9 completion:"
                     << (decision == 8 ? "native Coleco cartridge"
                         : selectedProfile == 3 ? "Coleco game via ADAM loader"
                         : selectedProfile == 2 ? "T-DOS"
                         : selectedProfile == 1 ? "CP/M" : "EOS")
                     << (decision >= 5 ? "Tape D1" : "Disk D5");
        }
        qDebug() << "[MCU2]" << s_pending.deviceName
                 << "SEND queued; bytes=" << data.size()
                 << "buffer=" << QStringLiteral("%1")
                        .arg(bufferAddress, 4, 16, QLatin1Char('0'))
                 << "payload=" << QString::fromLatin1(
                        data.left(data.size() == 258 ? 64 : 16).toHex(' '))
                 << "first=" << (data.isEmpty()
                     ? QStringLiteral("--")
                     : QStringLiteral("%1").arg(quint8(data[0]), 2, 16,
                                                  QLatin1Char('0')));
        s_pending.kind = PendingDcb::CharacterSend;
        s_pending.commandByte = data.isEmpty() ? 0 : quint8(data[0]);
        s_pending.transferFuture = QtConcurrent::run([address, data]() {
            return Mcu2Gateway::sendBlock(address, data);
        });
    } else {
        const bool waitForNetworkDirectory = address == 15
            && s_expectFujiDirectoryRecord;
        s_expectFujiDirectoryRecord = false;
        qDebug() << "[MCU2]" << s_pending.deviceName
                 << "RECEIVE queued; buffer=" << QStringLiteral("%1")
                        .arg(bufferAddress, 4, 16, QLatin1Char('0'))
                 << "requested=" << bufferLength;
        s_pending.kind = PendingDcb::CharacterReceive;
        s_pending.transferFuture = QtConcurrent::run(
            [address, waitForNetworkDirectory]() {
            return Mcu2Gateway::receiveBlock(
                address, waitForNetworkDirectory ? 180 : 40);
        });
    }
}
