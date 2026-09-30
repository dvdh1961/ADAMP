#include "mcu2_gateway.h"

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <atomic>
#include <functional>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <setupapi.h>
#  include <hidsdi.h>
#  include <hidpi.h>
#elif defined(Q_OS_LINUX)
#  include <cerrno>
#  include <cstring>
#  include <fcntl.h>
#  include <poll.h>
#  include <unistd.h>
#endif

namespace {

#if defined(Q_OS_WIN)

/*
 * Qt's MinGW packages do not always ship a compatible HID import library.
 * Resolve the standard Windows HID functions from hid.dll instead, avoiding
 * link-time dependencies while keeping the same native Windows API.
 */
struct HidApi
{
    using GetHidGuidFn = void (WINAPI *)(LPGUID);
    using GetAttributesFn = BOOLEAN (WINAPI *)(HANDLE, PHIDD_ATTRIBUTES);
    using GetPreparsedDataFn = BOOLEAN (WINAPI *)(HANDLE, PHIDP_PREPARSED_DATA *);
    using FreePreparsedDataFn = BOOLEAN (WINAPI *)(PHIDP_PREPARSED_DATA);
    using GetCapsFn = LONG (WINAPI *)(PHIDP_PREPARSED_DATA, PHIDP_CAPS);

    QLibrary library{QStringLiteral("hid")};
    GetHidGuidFn getHidGuid = nullptr;
    GetAttributesFn getAttributes = nullptr;
    GetPreparsedDataFn getPreparsedData = nullptr;
    FreePreparsedDataFn freePreparsedData = nullptr;
    GetCapsFn getCaps = nullptr;

    bool load(QString &error)
    {
        if (!library.load()) {
            error = QStringLiteral("Unable to load Windows hid.dll: %1")
                        .arg(library.errorString());
            return false;
        }

        getHidGuid = reinterpret_cast<GetHidGuidFn>(library.resolve("HidD_GetHidGuid"));
        getAttributes = reinterpret_cast<GetAttributesFn>(library.resolve("HidD_GetAttributes"));
        getPreparsedData = reinterpret_cast<GetPreparsedDataFn>(library.resolve("HidD_GetPreparsedData"));
        freePreparsedData = reinterpret_cast<FreePreparsedDataFn>(library.resolve("HidD_FreePreparsedData"));
        getCaps = reinterpret_cast<GetCapsFn>(library.resolve("HidP_GetCaps"));

        if (!getHidGuid || !getAttributes || !getPreparsedData
            || !freePreparsedData || !getCaps) {
            error = QStringLiteral("Windows hid.dll is missing a required HID function");
            return false;
        }
        return true;
    }
};

#endif // Q_OS_WIN

constexpr quint16 kVendorId = 0x258A;
constexpr quint16 kProductId = 0x0036;
constexpr quint16 kUsagePage = 0xFF00;
constexpr int kPacketSize = 64;
constexpr int kReportSize = 65; // Report ID byte followed by the 64-byte packet.
constexpr quint8 kProtocol = 1;
constexpr quint8 kCommandPing = 0x01;
constexpr quint8 kCommandVersion = 0x02;
constexpr quint8 kCommandMasterReceive = 0x17;
constexpr quint8 kCommandMasterSend = 0x18;
constexpr quint8 kCommandDiskChunk = 0x1D;
constexpr quint8 kCommandDcbExec = 0x1E;
constexpr quint8 kCommandReceiveChunk = 0x1F;
constexpr quint8 kCommandSendBegin = 0x20;
constexpr quint8 kCommandSendChunk = 0x21;
constexpr quint8 kCommandSendCommit = 0x22;
constexpr quint8 kCommandStreamBegin = 0x23;
constexpr quint8 kCommandStreamData = 0x24;
std::atomic_uint s_mcu2Sequence{0x40};

quint8 nextMcu2Sequence()
{
    return quint8(s_mcu2Sequence.fetch_add(1, std::memory_order_relaxed));
}

QByteArray makeReport(quint8 command, quint8 sequence, const QByteArray &payload,
                      quint8 address = 0)
{
    QByteArray report(kReportSize, char(0));
    char *packet = report.data() + 1;
    packet[0] = 'A';
    packet[1] = 'L';
    packet[2] = char(kProtocol);
    packet[3] = char(command);
    packet[4] = char(sequence);
    packet[5] = char(address);
    packet[6] = char(payload.size());
    for (int index = 0; index < payload.size(); ++index)
        packet[8 + index] = payload.at(index);
    return report;
}

bool decodeReply(const QByteArray &input, quint8 command, quint8 sequence,
                 QByteArray &reply, QString &error)
{
    if (input.size() != kPacketSize && input.size() != kReportSize) {
        error = QStringLiteral("MCU2 returned an invalid HID report length");
        return false;
    }

    const int offset = (input.size() == kReportSize) ? 1 : 0;
    const uchar *answer = reinterpret_cast<const uchar *>(input.constData() + offset);
    if (answer[0] != 'A' || answer[1] != 'L' || answer[2] != kProtocol) {
        error = QStringLiteral("MCU2 returned an invalid protocol header");
        return false;
    }
    if (answer[3] != quint8(command | 0x80) || answer[4] != sequence) {
        error = QStringLiteral("MCU2 returned an unexpected command or sequence");
        return false;
    }
    if (answer[7] != 0) {
        error = QStringLiteral("MCU2 gateway error %1").arg(answer[7]);
        return false;
    }
    if (answer[6] > 56) {
        error = QStringLiteral("MCU2 returned an invalid payload length");
        return false;
    }

    reply = QByteArray(reinterpret_cast<const char *>(answer + 8), answer[6]);
    return true;
}

using ExchangeFunction = std::function<bool(
    quint8, quint8, const QByteArray &, QByteArray &, QString &)>;
using AddressedExchangeFunction = std::function<bool(
    quint8, quint8, const QByteArray &, quint8, QByteArray &, QString &)>;
enum class StreamOutcome { Success, Unsupported, Failed };
using CachedDownloadFunction = std::function<StreamOutcome(
    quint8, QByteArray &, QString &)>;
using RawWriteFunction = std::function<bool(const QByteArray &, QString &)>;
using RawReadFunction = std::function<bool(QByteArray &, QString &)>;

Mcu2Gateway::TransferResult transferProtocol(
    bool send, quint8 address, const QByteArray &data,
    const AddressedExchangeFunction &exchangeFunction)
{
    Mcu2Gateway::TransferResult result;
    if (address < 1 || address > 15) {
        result.error = QStringLiteral("MCU2 ADAMnet address must be 1 through 15");
        return result;
    }
    if (send && data.size() > 1024) {
        result.error = QStringLiteral("MCU2 ADAMnet block exceeds 1024 bytes");
        return result;
    }

    QByteArray reply;
    if (send && data.size() > 48) {
        QByteArray begin(2, char(0));
        begin[0] = char(data.size() & 0xFF);
        begin[1] = char((data.size() >> 8) & 0xFF);
        if (!exchangeFunction(kCommandSendBegin, nextMcu2Sequence(), begin,
                              address, reply, result.error))
            return result;
        if (reply.size() != 1 || quint8(reply[0]) != 0) {
            result.error = QStringLiteral("MCU2 rejected the cached SEND length");
            return result;
        }

        int offset = 0;
        while (offset < data.size()) {
            const int count = qMin(54, data.size() - offset);
            QByteArray chunk(2, char(0));
            chunk[0] = char(offset & 0xFF);
            chunk[1] = char((offset >> 8) & 0xFF);
            chunk.append(data.constData() + offset, count);
            reply.clear();
            if (!exchangeFunction(kCommandSendChunk, nextMcu2Sequence(), chunk,
                                  address, reply, result.error))
                return result;
            if (reply.size() != 1 || quint8(reply[0]) != 0) {
                result.error = QStringLiteral("MCU2 rejected a cached SEND chunk");
                return result;
            }
            offset += count;
        }

        reply.clear();
        if (!exchangeFunction(kCommandSendCommit, nextMcu2Sequence(), QByteArray(),
                              address, reply, result.error))
            return result;
    } else {
        const quint8 command = send ? kCommandMasterSend : kCommandMasterReceive;
        if (!exchangeFunction(command, nextMcu2Sequence(),
                              send ? data : QByteArray(), address, reply,
                              result.error))
            return result;
    }

    if (send) {
        if (reply.size() < 3 || reply.size() != 3 + quint8(reply[2])) {
            result.error = QStringLiteral("MCU2 returned an invalid MASTER_SEND envelope");
            return result;
        }
        result.masterResult = quint8(reply[0]);
        result.stage = quint8(reply[1]);
        result.data = reply.mid(3, quint8(reply[2]));
    } else {
        if (reply.size() < 2) {
            result.error = QStringLiteral("MCU2 returned an invalid MASTER_RECEIVE envelope");
            return result;
        }
        result.masterResult = quint8(reply[0]);
        const quint8 lengthMarker = quint8(reply[1]);
        if (lengthMarker != 0xFF) {
            if (reply.size() != 2 + lengthMarker) {
                result.error = QStringLiteral("MCU2 returned an invalid inline MASTER_RECEIVE block");
                return result;
            }
            result.data = reply.mid(2, lengthMarker);
        } else {
            if (result.masterResult != 0 || reply.size() != 5) {
                result.error = QStringLiteral("MCU2 returned invalid cached receive metadata");
                return result;
            }

            const int totalLength = quint8(reply[2]) | (quint8(reply[3]) << 8);
            const quint8 expectedChecksum = quint8(reply[4]);
            if (totalLength <= 48 || totalLength > 1024) {
                result.error = QStringLiteral("MCU2 returned an invalid cached receive length");
                return result;
            }

            result.data.reserve(totalLength);
            while (result.data.size() < totalLength) {
                const int requested = qMin(52, totalLength - result.data.size());
                QByteArray request(3, char(0));
                request[0] = char(result.data.size() & 0xFF);
                request[1] = char((result.data.size() >> 8) & 0xFF);
                request[2] = char(requested);

                QByteArray chunk;
                if (!exchangeFunction(kCommandReceiveChunk, nextMcu2Sequence(),
                                      request, address, chunk, result.error)) {
                    result.data.clear();
                    return result;
                }
                if (chunk.size() != requested + 2 || quint8(chunk[0]) != 0
                    || quint8(chunk[1]) != requested) {
                    result.error = QStringLiteral("MCU2 returned an invalid receive chunk");
                    result.data.clear();
                    return result;
                }
                result.data.append(chunk.constData() + 2, requested);
            }

            quint8 checksum = 0;
            for (char value : result.data)
                checksum ^= quint8(value);
            if (checksum != expectedChecksum) {
                result.error = QStringLiteral("MCU2 cached receive checksum mismatch");
                result.data.clear();
                return result;
            }
        }
    }
    result.transportOk = true;
    result.attempts = 1;
    return result;
}

Mcu2Gateway::TransferResult receiveProtocolWithRetry(
    quint8 address, int maximumAttempts,
    const AddressedExchangeFunction &exchangeFunction)
{
    constexpr unsigned long kRetryDelayMs = 100;
    Mcu2Gateway::TransferResult result;

    for (int attempt = 1; attempt <= maximumAttempts; ++attempt) {
        result = transferProtocol(false, address, QByteArray(), exchangeFunction);
        result.attempts = attempt;

        /*
         * FujiNet performs Wi-Fi scanning asynchronously. While that work is
         * in progress MN_RECEIVE can time out or return NACK. Keep polling the
         * pending response without retransmitting the device command. BUS_BUSY
         * is also transient because MCU2 serializes every ADAMnet transaction.
         */
        const bool transientResult = result.transportOk
            && (result.masterResult == 1 || result.masterResult == 7
                || result.masterResult == 9);
        const bool responsePending = transientResult;
        if (!responsePending)
            return result;
        if (attempt == maximumAttempts) {
            return result;
        }
        QThread::msleep(kRetryDelayMs);
    }
    return result;
}

Mcu2Gateway::TransferResult sendProtocolWithReadyRetry(
    quint8 address, const QByteArray &data,
    const AddressedExchangeFunction &exchangeFunction)
{
    constexpr int kNormalMaximumAttempts = 40;
    constexpr int kNetworkOpenMaximumAttempts = 180;
    constexpr unsigned long kRetryDelayMs = 50;
    const bool longFujiNetNetworkOpen = address == 15 && data.size() == 258
        && quint8(data[0]) == 0xF7;
    const int maximumAttempts = longFujiNetNetworkOpen
        ? kNetworkOpenMaximumAttempts : kNormalMaximumAttempts;
    Mcu2Gateway::TransferResult result;

    for (int attempt = 1; attempt <= maximumAttempts; ++attempt) {
        result = transferProtocol(true, address, data, exchangeFunction);
        result.attempts = attempt;

        /*
         * Retry only while MN_READY (stage 4) proves that the data packet was
         * not sent. Retrying stage 5 could duplicate a non-idempotent command,
         * for example READ_DIR_ENTRY, and would silently skip an entry.
         */
        const bool nodeNotReady = result.transportOk && result.stage == 4
            && (result.masterResult == 1 || result.masterResult == 7
                || result.masterResult == 9);
        if (!nodeNotReady || attempt == maximumAttempts)
            return result;

        if (longFujiNetNetworkOpen && attempt == kNormalMaximumAttempts) {
            qDebug() << "[MCU2] FujiNet F7 is still waiting for MN_READY; extending the safe phase-4 wait";
        }
        QThread::msleep(kRetryDelayMs);
    }
    return result;
}

void addCredentialReadbackDiagnostic(
    Mcu2Gateway::TransferResult &setResult, quint8 address,
    const QByteArray &setPacket,
    const AddressedExchangeFunction &exchangeFunction)
{
    if (!setResult.transportOk || setResult.masterResult != 0
        || address != 15 || setPacket.size() != 98
        || quint8(setPacket[0]) != 0xFB)
        return;

    /*
     * FujiNet SET_SSID contains command FB followed by a 97-byte NetConfig.
     * GET_SSID (FE) is read-only and lets this diagnostic prove that every
     * byte stored by FujiNet matches the block supplied by EOS. Password bytes
     * are never copied into the log.
     */
    const Mcu2Gateway::TransferResult getCommand = sendProtocolWithReadyRetry(
        address, QByteArray(1, char(0xFE)), exchangeFunction);
    if (!getCommand.transportOk || getCommand.masterResult != 0) {
        setResult.diagnostic = QStringLiteral(
            "credential readback command failed; result=%1")
            .arg(getCommand.masterResult);
        return;
    }

    const Mcu2Gateway::TransferResult readback = receiveProtocolWithRetry(
        address, 40, exchangeFunction);
    if (!readback.transportOk || readback.masterResult != 0) {
        setResult.diagnostic = QStringLiteral(
            "credential readback receive failed; result=%1")
            .arg(readback.masterResult);
        return;
    }

    const QByteArray expected = setPacket.mid(1);
    if (readback.data.size() != expected.size()) {
        setResult.diagnostic = QStringLiteral(
            "credential readback length mismatch; sent=%1 stored=%2")
            .arg(expected.size()).arg(readback.data.size());
        return;
    }
    for (int index = 0; index < expected.size(); ++index) {
        if (expected[index] != readback.data[index]) {
            setResult.diagnostic = QStringLiteral(
                "credential readback mismatch at NetConfig byte %1")
                .arg(index);
            return;
        }
    }
    setResult.diagnostic = QStringLiteral(
        "credential readback MATCH; all 97 NetConfig bytes stored correctly");
}

Mcu2Gateway::DcbResult executeDcbProtocol(
    const QByteArray &dcb, const ExchangeFunction &exchangeFunction,
    const CachedDownloadFunction &streamFunction)
{
    Mcu2Gateway::DcbResult result;
    if (dcb.size() != 21) {
        result.error = QStringLiteral("MCU2 DCB must contain exactly 21 bytes");
        return result;
    }

    QByteArray envelope;
    if (!exchangeFunction(kCommandDcbExec, nextMcu2Sequence(), dcb, envelope, result.error))
        return result;
    if (envelope.size() != 25) {
        result.error = QStringLiteral("MCU2 returned an invalid DCB envelope");
        return result;
    }

    result.masterResult = quint8(envelope[0]);
    result.stage = quint8(envelope[1]);
    result.attempts = quint8(envelope[2]);
    result.checksum = quint8(envelope[3]);
    result.returnedDcb = envelope.mid(4, 21);
    result.transportOk = true;

    const bool successfulRead = quint8(dcb[0]) == 4
                             && result.masterResult == 0
                             && quint8(result.returnedDcb[0]) == 0x80;
    if (!successfulRead)
        return result;

    if (streamFunction) {
        QByteArray streamedBlock;
        const StreamOutcome streamResult=
            streamFunction(result.checksum,streamedBlock,result.error);
        if (streamResult==StreamOutcome::Success) {
            result.data=streamedBlock;
            result.streamed=true;
            return result;
        }
        if (streamResult==StreamOutcome::Failed) {
            result.transportOk=false;
            result.data.clear();
            return result;
        }
        /* Firmware before 2.13 does not know 0x23; use proven chunk reads. */
        result.error.clear();
    }

    QByteArray block;
    block.reserve(1024);
    while (block.size() < 1024) {
        const int requested = qMin(52, 1024 - block.size());
        QByteArray request(3, char(0));
        request[0] = char(block.size() & 0xFF);
        request[1] = char((block.size() >> 8) & 0xFF);
        request[2] = char(requested);

        QByteArray chunk;
        if (!exchangeFunction(kCommandDiskChunk, nextMcu2Sequence(), request, chunk, result.error)) {
            result.transportOk = false;
            result.data.clear();
            return result;
        }
        if (chunk.size() != requested + 2 || quint8(chunk[0]) != 0
            || quint8(chunk[1]) != requested) {
            result.transportOk = false;
            result.error = QStringLiteral("MCU2 returned an invalid disk chunk");
            result.data.clear();
            return result;
        }
        block.append(chunk.constData() + 2, requested);
    }

    quint8 checksum = 0;
    for (char value : block)
        checksum ^= quint8(value);
    if (checksum != result.checksum) {
        result.transportOk = false;
        result.error = QStringLiteral("MCU2 disk checksum mismatch");
        return result;
    }

    result.data = block;
    return result;
}

Mcu2Gateway::DcbResult executeDcbWriteProtocol(
    const QByteArray &dcb, const QByteArray &block,
    const ExchangeFunction &exchangeFunction)
{
    Mcu2Gateway::DcbResult result;
    if (dcb.size() != 21 || quint8(dcb[0]) != 3) {
        result.error = QStringLiteral("MCU2 disk WRITE requires a 21-byte WRITE DCB");
        return result;
    }
    if (block.size() != 1024) {
        result.error = QStringLiteral("MCU2 disk WRITE requires exactly 1024 bytes");
        return result;
    }

    QByteArray reply;
    QByteArray begin(2, char(0));
    begin[0] = char(block.size() & 0xFF);
    begin[1] = char((block.size() >> 8) & 0xFF);
    if (!exchangeFunction(kCommandSendBegin, nextMcu2Sequence(), begin,
                          reply, result.error))
        return result;
    if (reply.size() != 1 || quint8(reply[0]) != 0) {
        result.error = QStringLiteral("MCU2 rejected the disk WRITE cache length");
        return result;
    }

    for (int offset = 0; offset < block.size(); ) {
        const int count = qMin(54, block.size() - offset);
        QByteArray chunk(2, char(0));
        chunk[0] = char(offset & 0xFF);
        chunk[1] = char((offset >> 8) & 0xFF);
        chunk.append(block.constData() + offset, count);
        reply.clear();
        if (!exchangeFunction(kCommandSendChunk, nextMcu2Sequence(), chunk,
                              reply, result.error))
            return result;
        if (reply.size() != 1 || quint8(reply[0]) != 0) {
            result.error = QStringLiteral("MCU2 rejected a disk WRITE cache chunk");
            return result;
        }
        offset += count;
    }

    /* Do not issue SEND_COMMIT: DCB_EXEC WRITE consumes this 1 KiB cache.
     * The write is deliberately one-shot; an ambiguous failure must never
     * cause the emulator to write the same sector a second time.
     */
    result = executeDcbProtocol(dcb, exchangeFunction, CachedDownloadFunction());
    result.gatewayAttempts = 1;
    return result;
}

Mcu2Gateway::DcbResult executeBlockDcbProtocol(
    const QByteArray &dcb, const QByteArray &writeBlock,
    const AddressedExchangeFunction &exchangeFunction)
{
    Mcu2Gateway::DcbResult result;
    if (dcb.size() != 21) {
        result.error = QStringLiteral("MCU2 block DCB must contain exactly 21 bytes");
        return result;
    }

    const quint8 command = quint8(dcb[0]);
    const quint8 address = quint8(dcb[16]) & 0x0F;
    const int bufferLength = quint8(dcb[3]) | (quint8(dcb[4]) << 8);
    const int maximumLength = quint8(dcb[17]) | (quint8(dcb[18]) << 8);
    if ((command != 3 && command != 4) || address < 1 || address > 15) {
        result.error = QStringLiteral("MCU2 block DCB requires READ or WRITE and a valid address");
        return result;
    }
    if (bufferLength != 1024 || maximumLength != 1024
        || (command == 3 && writeBlock.size() != 1024)) {
        result.error = QStringLiteral("MCU2 block DCB requires exactly 1024 bytes");
        return result;
    }

    /* Block devices receive the four-byte block number followed by the DCB
     * device number. The latter selects D1/D2 on address 8 and D3/D4 on 9.
     */
    QByteArray selector = dcb.mid(5, 4);
    selector.append(dcb[9]);
    Mcu2Gateway::TransferResult transfer = sendProtocolWithReadyRetry(
        address, selector, exchangeFunction);
    if (transfer.transportOk && transfer.masterResult == 0) {
        transfer = (command == 4)
            ? receiveProtocolWithRetry(address, 40, exchangeFunction)
            : sendProtocolWithReadyRetry(address, writeBlock, exchangeFunction);
    }

    result.transportOk = transfer.transportOk;
    result.masterResult = transfer.masterResult;
    result.stage = transfer.stage;
    result.attempts = transfer.attempts;
    result.gatewayAttempts = 1;
    if (!transfer.transportOk) {
        result.error = transfer.error;
        return result;
    }

    result.returnedDcb = dcb;
    const bool success = transfer.masterResult == 0
        && (command == 3 || transfer.data.size() == 1024);
    result.returnedDcb[0] = char(success ? 0x80 : 0x03);
    if (!success) {
        result.error = (command == 4 && transfer.masterResult == 0)
            ? QStringLiteral("MCU2 block device returned an invalid block length")
            : QStringLiteral("MCU2 block-device transaction failed");
        return result;
    }

    if (command == 4)
        result.data = transfer.data;
    quint8 checksum = 0;
    const QByteArray &checkData = command == 4 ? result.data : writeBlock;
    for (char value : checkData)
        checksum ^= quint8(value);
    result.checksum = checksum;
    return result;
}

Mcu2Gateway::DcbResult executeDcbWithReadRetry(
    const QByteArray &dcb, const ExchangeFunction &exchangeFunction,
    const CachedDownloadFunction &streamFunction)
{
    constexpr int kMaximumAttempts = 12;
    constexpr unsigned long kRetryDelayMs = 100;
    Mcu2Gateway::DcbResult result;

    for (int attempt = 1; attempt <= kMaximumAttempts; ++attempt) {
        result = executeDcbProtocol(dcb, exchangeFunction, streamFunction);
        result.gatewayAttempts = attempt;

        /*
         * A block READ is idempotent. FujiNet may acknowledge mounting before
         * its virtual disk is ready to supply the first block, so retry only
         * READ command 4 after transient timeout, BUS_BUSY, or NACK. WRITE,
         * RESET, STATUS, and transport/protocol failures remain one-shot.
         */
        const bool readPending = dcb.size() == 21 && quint8(dcb[0]) == 4
            && result.transportOk && result.data.isEmpty()
            && (result.masterResult == 1 || result.masterResult == 7
                || result.masterResult == 9);
        if (!readPending || attempt == kMaximumAttempts)
            return result;
        QThread::msleep(kRetryDelayMs);
    }
    return result;
}

#if defined(Q_OS_WIN)

QString windowsError(const QString &operation)
{
    return QStringLiteral("%1 (Windows error %2)")
        .arg(operation)
        .arg(static_cast<qulonglong>(GetLastError()));
}

bool waitForIo(HANDLE device, OVERLAPPED &overlapped, DWORD &transferred,
               DWORD timeoutMs, QString &error)
{
    const DWORD waitResult = WaitForSingleObject(overlapped.hEvent, timeoutMs);
    if (waitResult != WAIT_OBJECT_0) {
        CancelIoEx(device, &overlapped);
        WaitForSingleObject(overlapped.hEvent, 250);
        error = (waitResult == WAIT_TIMEOUT)
            ? QStringLiteral("MCU2 response timed out")
            : windowsError(QStringLiteral("Waiting for MCU2 failed"));
        return false;
    }

    if (!GetOverlappedResult(device, &overlapped, &transferred, FALSE)) {
        error = windowsError(QStringLiteral("Completing MCU2 transfer failed"));
        return false;
    }
    return true;
}

bool writeReport(HANDLE device, const QByteArray &report, QString &error)
{
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        error = windowsError(QStringLiteral("Creating MCU2 write event failed"));
        return false;
    }

    DWORD transferred = 0;
    const BOOL started = WriteFile(device, report.constData(), DWORD(report.size()),
                                   &transferred, &overlapped);
    bool ok = true;
    if (!started && GetLastError() == ERROR_IO_PENDING)
        ok = waitForIo(device, overlapped, transferred, 2000, error);
    else if (!started) {
        error = windowsError(QStringLiteral("Writing MCU2 HID report failed"));
        ok = false;
    }

    CloseHandle(overlapped.hEvent);
    if (ok && transferred != DWORD(report.size())) {
        error = QStringLiteral("MCU2 accepted an incomplete HID report");
        ok = false;
    }
    return ok;
}

bool readReport(HANDLE device, QByteArray &report, QString &error)
{
    report.fill(char(0), kReportSize);
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        error = windowsError(QStringLiteral("Creating MCU2 read event failed"));
        return false;
    }

    DWORD transferred = 0;
    const BOOL started = ReadFile(device, report.data(), DWORD(report.size()),
                                  &transferred, &overlapped);
    bool ok = true;
    if (!started && GetLastError() == ERROR_IO_PENDING)
        ok = waitForIo(device, overlapped, transferred, 2000, error);
    else if (!started) {
        error = windowsError(QStringLiteral("Reading MCU2 HID report failed"));
        ok = false;
    }

    CloseHandle(overlapped.hEvent);
    if (!ok)
        return false;

    report.resize(int(transferred));
    if (report.size() != kPacketSize && report.size() != kReportSize) {
        error = QStringLiteral("MCU2 returned %1 instead of 64 data bytes")
                    .arg(report.size() > 0 ? report.size() - 1 : 0);
        return false;
    }
    return true;
}

bool exchange(HANDLE device, quint8 command, quint8 sequence,
              const QByteArray &payload, QByteArray &reply, QString &error,
              quint8 address = 0)
{
    if (payload.size() > 56) {
        error = QStringLiteral("MCU2 payload is too large");
        return false;
    }

    const QByteArray report = makeReport(command, sequence, payload, address);

    if (!writeReport(device, report, error))
        return false;

    QByteArray input;
    if (!readReport(device, input, error))
        return false;

    return decodeReply(input, command, sequence, reply, error);
}

HANDLE openMcu2(HidApi &hid, QString &error)
{
    GUID hidGuid;
    hid.getHidGuid(&hidGuid);
    HDEVINFO deviceInfo = SetupDiGetClassDevsW(
        &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        error = windowsError(QStringLiteral("Enumerating HID devices failed"));
        return INVALID_HANDLE_VALUE;
    }

    HANDLE found = INVALID_HANDLE_VALUE;
    for (DWORD index = 0; ; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData = {};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &hidGuid,
                                         index, &interfaceData)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                break;
            continue;
        }

        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, nullptr,
                                         0, &required, nullptr);
        QByteArray detailStorage(int(required), char(0));
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(detailStorage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData,
                                              detail, required, nullptr, nullptr))
            continue;

        HANDLE candidate = CreateFileW(
            detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, nullptr);
        if (candidate == INVALID_HANDLE_VALUE)
            continue;

        HIDD_ATTRIBUTES attributes = {};
        attributes.Size = sizeof(attributes);
        bool matches = hid.getAttributes(candidate, &attributes)
                    && attributes.VendorID == kVendorId
                    && attributes.ProductID == kProductId;

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps = {};
        if (matches) {
            matches = hid.getPreparsedData(candidate, &preparsed)
                   && hid.getCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS
                   && caps.UsagePage == kUsagePage;
        }
        if (preparsed)
            hid.freePreparsedData(preparsed);

        if (matches) {
            found = candidate;
            break;
        }
        CloseHandle(candidate);
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    if (found == INVALID_HANDLE_VALUE)
        error = QStringLiteral("CH559 MCU2 HID interface (258A:0036, FF00) not found");
    return found;
}

#endif // Q_OS_WIN

#if defined(Q_OS_LINUX)

QString linuxError(const QString &operation)
{
    return QStringLiteral("%1: %2").arg(operation, QString::fromLocal8Bit(std::strerror(errno)));
}

int openLinuxMcu2(QString &error)
{
    const QDir hidrawClass(QStringLiteral("/sys/class/hidraw"));
    const QStringList devices = hidrawClass.entryList(
        QStringList() << QStringLiteral("hidraw*"), QDir::Dirs | QDir::NoDotAndDotDot);

    bool matchingDeviceFound = false;
    for (const QString &name : devices) {
        QFile uevent(hidrawClass.filePath(name + QStringLiteral("/device/uevent")));
        if (!uevent.open(QIODevice::ReadOnly))
            continue;
        const QByteArray identity = uevent.readAll().toUpper();
        if (!identity.contains("HID_ID=0003:0000258A:00000036"))
            continue;

        /* Confirm the vendor-defined FF00 usage page in the HID descriptor.
         * The descriptor encodes a 16-bit Usage Page item as 06 00 FF.
         */
        QFile descriptor(hidrawClass.filePath(name + QStringLiteral("/device/report_descriptor")));
        if (!descriptor.open(QIODevice::ReadOnly))
            continue;
        if (!descriptor.readAll().contains(QByteArray::fromHex("0600ff")))
            continue;

        matchingDeviceFound = true;
        const QByteArray devicePath = QStringLiteral("/dev/%1").arg(name).toLocal8Bit();
        const int descriptorFd = ::open(devicePath.constData(), O_RDWR | O_NONBLOCK);
        if (descriptorFd >= 0)
            return descriptorFd;

        if (errno == EACCES) {
            error = QStringLiteral("MCU2 found at %1 but access was denied; install the supplied udev rule")
                        .arg(QString::fromLocal8Bit(devicePath));
            return -1;
        }
        error = linuxError(QStringLiteral("Opening %1 failed").arg(QString::fromLocal8Bit(devicePath)));
        return -1;
    }

    error = matchingDeviceFound
        ? QStringLiteral("CH559 MCU2 HID interface could not be opened")
        : QStringLiteral("CH559 MCU2 HID interface (258A:0036, FF00) not found");
    return -1;
}

bool writeReportLinux(int device, const QByteArray &report, QString &error)
{
    const ssize_t written=::write(device,report.constData(),size_t(report.size()));
    if (written!=report.size()) {
        error=linuxError(QStringLiteral("Writing MCU2 hidraw report failed"));
        return false;
    }
    return true;
}

bool readReportLinux(int device, QByteArray &input, QString &error)
{
    pollfd pollDescriptor={};
    pollDescriptor.fd=device;
    pollDescriptor.events=POLLIN;
    const int pollResult=::poll(&pollDescriptor,1,2000);
    if (pollResult==0) {
        error=QStringLiteral("MCU2 response timed out");
        return false;
    }
    if (pollResult<0) {
        error=linuxError(QStringLiteral("Waiting for MCU2 failed"));
        return false;
    }

    input.fill(char(0),kReportSize);
    const ssize_t received=::read(device,input.data(),size_t(input.size()));
    if (received<0) {
        error=linuxError(QStringLiteral("Reading MCU2 hidraw report failed"));
        return false;
    }
    input.resize(int(received));
    if (input.size()!=kPacketSize && input.size()!=kReportSize) {
        error=QStringLiteral("MCU2 returned an invalid HID report length");
        return false;
    }
    return true;
}

bool exchangeLinux(int device, quint8 command, quint8 sequence,
                   const QByteArray &payload, QByteArray &reply, QString &error,
                   quint8 address = 0)
{
    if (payload.size() > 56) {
        error = QStringLiteral("MCU2 payload is too large");
        return false;
    }

    const QByteArray report = makeReport(command, sequence, payload, address);
    if (!writeReportLinux(device,report,error))
        return false;

    QByteArray input;
    if (!readReportLinux(device,input,error))
        return false;
    return decodeReply(input, command, sequence, reply, error);
}

#endif // Q_OS_LINUX

StreamOutcome downloadCachedStream(
    const RawWriteFunction &writeFunction,const RawReadFunction &readFunction,
    quint8 expectedChecksum,QByteArray &block,QString &error)
{
    const quint8 sequence=nextMcu2Sequence();
    if (!writeFunction(makeReport(kCommandStreamBegin,sequence,QByteArray()),error))
        return StreamOutcome::Failed;

    QByteArray input;
    if (!readFunction(input,error))
        return StreamOutcome::Failed;
    const int inputOffset=(input.size()==kReportSize) ? 1 : 0;
    const uchar *answer=reinterpret_cast<const uchar *>(input.constData()+inputOffset);

    /* Firmware 2.12 returns the normal BAD_COMMAND envelope for command 23. */
    if (answer[0]=='A' && answer[1]=='L' && answer[2]==kProtocol
        && answer[3]==quint8(0x7F|0x80) && answer[7]==5)
        return StreamOutcome::Unsupported;

    QByteArray acknowledgement;
    if (!decodeReply(input,kCommandStreamBegin,sequence,acknowledgement,error))
        return StreamOutcome::Failed;
    if (acknowledgement.size()!=3 || quint8(acknowledgement[0])!=0
        || quint8(acknowledgement[1])!=0 || quint8(acknowledgement[2])!=4) {
        error=QStringLiteral("MCU2 returned invalid stream metadata");
        return StreamOutcome::Failed;
    }

    block.clear();
    block.reserve(1024);
    while (block.size()<1024) {
        if (!readFunction(input,error)) {
            block.clear();
            return StreamOutcome::Failed;
        }
        const int offset=(input.size()==kReportSize) ? 1 : 0;
        const uchar *packet=
            reinterpret_cast<const uchar *>(input.constData()+offset);
        if (packet[0]!='A' || packet[1]!='L' || packet[2]!=kProtocol
            || packet[3]!=quint8(kCommandStreamData|0x80)
            || packet[4]!=sequence || packet[7]!=0
            || packet[6]<3 || packet[6]>56) {
            error=QStringLiteral("MCU2 returned an invalid stream packet");
            block.clear();
            return StreamOutcome::Failed;
        }
        const int packetOffset=int(packet[8])|(int(packet[9])<<8);
        const int chunkLength=int(packet[6])-2;
        if (packetOffset!=block.size() || block.size()+chunkLength>1024) {
            error=QStringLiteral("MCU2 stream packet offset is out of sequence");
            block.clear();
            return StreamOutcome::Failed;
        }
        block.append(reinterpret_cast<const char *>(packet+10),chunkLength);
    }

    quint8 checksum=0;
    for (char value:block) checksum^=quint8(value);
    if (checksum!=expectedChecksum) {
        error=QStringLiteral("MCU2 streamed disk checksum mismatch");
        block.clear();
        return StreamOutcome::Failed;
    }
    return StreamOutcome::Success;
}

/*
 * Keep one connection open for the emulator session. Re-enumerating the
 * CH559 for every disk block adds considerable host-side latency. The mutex
 * prevents concurrent DCB jobs from interleaving protocol packets.
 *
 * After a transport failure the cached handle is discarded. The next request
 * reconnects normally, without automatically repeating a destructive command.
 */
QMutex gPersistentDeviceMutex;

#if defined(Q_OS_WIN)
HidApi gPersistentHidApi;
HANDLE gPersistentDevice = INVALID_HANDLE_VALUE;

HANDLE persistentDevice(QString &error)
{
    if (gPersistentDevice != INVALID_HANDLE_VALUE)
        return gPersistentDevice;
    if (!gPersistentHidApi.load(error))
        return INVALID_HANDLE_VALUE;
    gPersistentDevice = openMcu2(gPersistentHidApi, error);
    return gPersistentDevice;
}

void invalidatePersistentDevice()
{
    if (gPersistentDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(gPersistentDevice);
        gPersistentDevice = INVALID_HANDLE_VALUE;
    }
}
#elif defined(Q_OS_LINUX)
int gPersistentDevice = -1;

int persistentDevice(QString &error)
{
    if (gPersistentDevice < 0)
        gPersistentDevice = openLinuxMcu2(error);
    return gPersistentDevice;
}

void invalidatePersistentDevice()
{
    if (gPersistentDevice >= 0) {
        ::close(gPersistentDevice);
        gPersistentDevice = -1;
    }
}
#endif

} // namespace

Mcu2Gateway::ProbeResult Mcu2Gateway::probe()
{
    ProbeResult result;
    QMutexLocker deviceLock(&gPersistentDeviceMutex);

#if defined(Q_OS_WIN)
    QString error;
    HANDLE device = persistentDevice(error);
    if (device == INVALID_HANDLE_VALUE) {
        result.message = error;
        return result;
    }

    QByteArray ping(56, char(0));
    for (int index = 0; index < ping.size(); ++index)
        ping[index] = char((index ^ 0xA5) & 0xFF);

    QByteArray reply;
    if (!exchange(device, kCommandPing, 1, ping, reply, error) || reply != ping) {
        invalidatePersistentDevice();
        result.message = error.isEmpty()
            ? QStringLiteral("MCU2 PING data did not match") : error;
        return result;
    }

    if (!exchange(device, kCommandVersion, 2, QByteArray(), reply, error)) {
        invalidatePersistentDevice();
        result.message = error;
        return result;
    }
#elif defined(Q_OS_LINUX)
    QString error;
    const int device = persistentDevice(error);
    if (device < 0) {
        result.message = error;
        return result;
    }

    QByteArray ping(56, char(0));
    for (int index = 0; index < ping.size(); ++index)
        ping[index] = char((index ^ 0xA5) & 0xFF);

    QByteArray reply;
    if (!exchangeLinux(device, kCommandPing, 1, ping, reply, error) || reply != ping) {
        invalidatePersistentDevice();
        result.message = error.isEmpty()
            ? QStringLiteral("MCU2 PING data did not match") : error;
        return result;
    }

    if (!exchangeLinux(device, kCommandVersion, 2, QByteArray(), reply, error)) {
        invalidatePersistentDevice();
        result.message = error;
        return result;
    }
#else
    result.message = QStringLiteral("MCU2 probing is not supported on this platform");
    return result;
#endif

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    if (reply.size() != 5) {
        result.message = QStringLiteral("MCU2 returned an invalid VERSION response");
        return result;
    }

    result.firmwareMajor = quint8(reply[0]);
    result.firmwareMinor = quint8(reply[1]);
    result.firmwarePatch = quint8(reply[2]);
    result.protocolVersion = quint8(reply[3]);
    result.targetCode = quint8(reply[4]);
    if (result.protocolVersion != kProtocol || result.targetCode != 0x59) {
        result.message = QStringLiteral("Unsupported MCU2 protocol or target");
        return result;
    }

    result.connected = true;
    result.message = QStringLiteral("Connected - firmware %1.%2.%3, protocol %4")
        .arg(result.firmwareMajor).arg(result.firmwareMinor)
        .arg(result.firmwarePatch).arg(result.protocolVersion);
#endif

    return result;
}

Mcu2Gateway::DcbResult Mcu2Gateway::executeDcb(const QByteArray &dcb)
{
    QElapsedTimer gatewayTimer;
    gatewayTimer.start();
    QMutexLocker deviceLock(&gPersistentDeviceMutex);
#if defined(Q_OS_WIN)
    DcbResult failed;
    QString error;
    HANDLE device = persistentDevice(error);
    if (device == INVALID_HANDLE_VALUE) {
        failed.error = error;
        return failed;
    }
    const ExchangeFunction exchangeFunction = [device](
        quint8 command, quint8 sequence, const QByteArray &payload,
        QByteArray &reply, QString &exchangeError) {
        return exchange(device, command, sequence, payload, reply, exchangeError);
    };
    const CachedDownloadFunction streamFunction = [device](
        quint8 checksum,QByteArray &block,QString &streamError) {
        const RawWriteFunction writer=[device](const QByteArray &report,QString &error) {
            return writeReport(device,report,error);
        };
        const RawReadFunction reader=[device](QByteArray &report,QString &error) {
            return readReport(device,report,error);
        };
        return downloadCachedStream(writer,reader,checksum,block,streamError);
    };
    DcbResult result = executeDcbWithReadRetry(dcb, exchangeFunction, streamFunction);
    result.gatewayElapsedMs = int(gatewayTimer.elapsed());
    if (!result.transportOk)
        invalidatePersistentDevice();
    return result;
#elif defined(Q_OS_LINUX)
    DcbResult failed;
    QString error;
    const int device = persistentDevice(error);
    if (device < 0) {
        failed.error = error;
        return failed;
    }
    const ExchangeFunction exchangeFunction = [device](
        quint8 command, quint8 sequence, const QByteArray &payload,
        QByteArray &reply, QString &exchangeError) {
        return exchangeLinux(device, command, sequence, payload, reply, exchangeError);
    };
    const CachedDownloadFunction streamFunction = [device](
        quint8 checksum,QByteArray &block,QString &streamError) {
        const RawWriteFunction writer=[device](const QByteArray &report,QString &error) {
            return writeReportLinux(device,report,error);
        };
        const RawReadFunction reader=[device](QByteArray &report,QString &error) {
            return readReportLinux(device,report,error);
        };
        return downloadCachedStream(writer,reader,checksum,block,streamError);
    };
    DcbResult result = executeDcbWithReadRetry(dcb, exchangeFunction, streamFunction);
    result.gatewayElapsedMs = int(gatewayTimer.elapsed());
    if (!result.transportOk)
        invalidatePersistentDevice();
    return result;
#else
    DcbResult failed;
    failed.error = QStringLiteral("MCU2 DCB transport is not supported on this platform");
    return failed;
#endif
}

Mcu2Gateway::DcbResult Mcu2Gateway::executeDcbWrite(
    const QByteArray &dcb, const QByteArray &block)
{
    QElapsedTimer gatewayTimer;
    gatewayTimer.start();
    QMutexLocker deviceLock(&gPersistentDeviceMutex);
#if defined(Q_OS_WIN)
    DcbResult failed;
    QString error;
    HANDLE device = persistentDevice(error);
    if (device == INVALID_HANDLE_VALUE) {
        failed.error = error;
        return failed;
    }
    const ExchangeFunction exchangeFunction = [device](
        quint8 command, quint8 sequence, const QByteArray &payload,
        QByteArray &reply, QString &exchangeError) {
        return exchange(device, command, sequence, payload, reply, exchangeError);
    };
    DcbResult result = executeDcbWriteProtocol(dcb, block, exchangeFunction);
    result.gatewayElapsedMs = int(gatewayTimer.elapsed());
    if (!result.transportOk)
        invalidatePersistentDevice();
    return result;
#elif defined(Q_OS_LINUX)
    DcbResult failed;
    QString error;
    const int device = persistentDevice(error);
    if (device < 0) {
        failed.error = error;
        return failed;
    }
    const ExchangeFunction exchangeFunction = [device](
        quint8 command, quint8 sequence, const QByteArray &payload,
        QByteArray &reply, QString &exchangeError) {
        return exchangeLinux(device, command, sequence, payload, reply, exchangeError);
    };
    DcbResult result = executeDcbWriteProtocol(dcb, block, exchangeFunction);
    result.gatewayElapsedMs = int(gatewayTimer.elapsed());
    if (!result.transportOk)
        invalidatePersistentDevice();
    return result;
#else
    Q_UNUSED(dcb)
    Q_UNUSED(block)
    DcbResult failed;
    failed.error = QStringLiteral("MCU2 DCB transport is not supported on this platform");
    return failed;
#endif
}

Mcu2Gateway::DcbResult Mcu2Gateway::executeBlockDcb(
    const QByteArray &dcb, const QByteArray &writeBlock)
{
    QElapsedTimer gatewayTimer;
    gatewayTimer.start();
    QMutexLocker deviceLock(&gPersistentDeviceMutex);
#if defined(Q_OS_WIN)
    DcbResult failed;
    QString error;
    HANDLE device = persistentDevice(error);
    if (device == INVALID_HANDLE_VALUE) {
        failed.error = error;
        return failed;
    }
    const AddressedExchangeFunction fn = [device](
        quint8 command, quint8 sequence, const QByteArray &payload, quint8 node,
        QByteArray &reply, QString &exchangeError) {
        return exchange(device, command, sequence, payload, reply, exchangeError, node);
    };
    DcbResult result = executeBlockDcbProtocol(dcb, writeBlock, fn);
    result.gatewayElapsedMs = int(gatewayTimer.elapsed());
    if (!result.transportOk)
        invalidatePersistentDevice();
    return result;
#elif defined(Q_OS_LINUX)
    DcbResult failed;
    QString error;
    const int device = persistentDevice(error);
    if (device < 0) {
        failed.error = error;
        return failed;
    }
    const AddressedExchangeFunction fn = [device](
        quint8 command, quint8 sequence, const QByteArray &payload, quint8 node,
        QByteArray &reply, QString &exchangeError) {
        return exchangeLinux(device, command, sequence, payload, reply, exchangeError, node);
    };
    DcbResult result = executeBlockDcbProtocol(dcb, writeBlock, fn);
    result.gatewayElapsedMs = int(gatewayTimer.elapsed());
    if (!result.transportOk)
        invalidatePersistentDevice();
    return result;
#else
    Q_UNUSED(dcb)
    Q_UNUSED(writeBlock)
    DcbResult failed;
    failed.error = QStringLiteral("MCU2 block DCB transport is not supported on this platform");
    return failed;
#endif
}

Mcu2Gateway::DcbResult Mcu2Gateway::inspectBlock(
    int address, int deviceNumber, quint32 blockNumber)
{
    DcbResult failed;
    if (address < 4 || address > 9 || deviceNumber < 0 || deviceNumber > 1) {
        failed.error = QStringLiteral("Invalid physical ADAM block drive");
        return failed;
    }

    QByteArray dcb(21, char(0));
    dcb[0] = char(4); // CMD_READ
    dcb[3] = char(0x00);
    dcb[4] = char(0x04); // 1024 bytes
    dcb[5] = char(blockNumber & 0xFF);
    dcb[6] = char((blockNumber >> 8) & 0xFF);
    dcb[7] = char((blockNumber >> 16) & 0xFF);
    dcb[8] = char((blockNumber >> 24) & 0xFF);
    dcb[9] = char(deviceNumber);
    dcb[16] = char(address);
    dcb[17] = char(0x00);
    dcb[18] = char(0x04); // maximum length 1024

    /* Disk nodes 4..7 use the firmware DCB adapter.  DDP nodes 8/9 use the
     * addressed block exchange introduced for the all-block-drive work. */
    return address <= 7 ? executeDcb(dcb) : executeBlockDcb(dcb);
}

Mcu2Gateway::TransferResult Mcu2Gateway::sendBlock(
    int address, const QByteArray &data)
{
    QMutexLocker deviceLock(&gPersistentDeviceMutex);
#if defined(Q_OS_WIN)
    TransferResult failed;
    QString error;
    HANDLE device = persistentDevice(error);
    if (device == INVALID_HANDLE_VALUE) { failed.error = error; return failed; }
    const AddressedExchangeFunction fn = [device](
        quint8 command, quint8 sequence, const QByteArray &payload, quint8 node,
        QByteArray &reply, QString &exchangeError) {
        return exchange(device, command, sequence, payload, reply, exchangeError, node);
    };
    TransferResult result = sendProtocolWithReadyRetry(
        quint8(address), data, fn);
    addCredentialReadbackDiagnostic(result, quint8(address), data, fn);
    if (!result.transportOk)
        invalidatePersistentDevice();
    return result;
#elif defined(Q_OS_LINUX)
    TransferResult failed;
    QString error;
    const int device = persistentDevice(error);
    if (device < 0) { failed.error = error; return failed; }
    const AddressedExchangeFunction fn = [device](
        quint8 command, quint8 sequence, const QByteArray &payload, quint8 node,
        QByteArray &reply, QString &exchangeError) {
        return exchangeLinux(device, command, sequence, payload, reply, exchangeError, node);
    };
    TransferResult result = sendProtocolWithReadyRetry(
        quint8(address), data, fn);
    addCredentialReadbackDiagnostic(result, quint8(address), data, fn);
    if (!result.transportOk)
        invalidatePersistentDevice();
    return result;
#else
    TransferResult failed;
    failed.error = QStringLiteral("MCU2 block transport is not supported on this platform");
    return failed;
#endif
}

Mcu2Gateway::TransferResult Mcu2Gateway::receiveBlock(
    int address, int maximumAttempts)
{
    QMutexLocker deviceLock(&gPersistentDeviceMutex);
#if defined(Q_OS_WIN)
    TransferResult failed;
    QString error;
    HANDLE device = persistentDevice(error);
    if (device == INVALID_HANDLE_VALUE) { failed.error = error; return failed; }
    const AddressedExchangeFunction fn = [device](
        quint8 command, quint8 sequence, const QByteArray &payload, quint8 node,
        QByteArray &reply, QString &exchangeError) {
        return exchange(device, command, sequence, payload, reply, exchangeError, node);
    };
    TransferResult result = receiveProtocolWithRetry(
        quint8(address), maximumAttempts, fn);
    if (!result.transportOk)
        invalidatePersistentDevice();
    return result;
#elif defined(Q_OS_LINUX)
    TransferResult failed;
    QString error;
    const int device = persistentDevice(error);
    if (device < 0) { failed.error = error; return failed; }
    const AddressedExchangeFunction fn = [device](
        quint8 command, quint8 sequence, const QByteArray &payload, quint8 node,
        QByteArray &reply, QString &exchangeError) {
        return exchangeLinux(device, command, sequence, payload, reply, exchangeError, node);
    };
    TransferResult result = receiveProtocolWithRetry(
        quint8(address), maximumAttempts, fn);
    if (!result.transportOk)
        invalidatePersistentDevice();
    return result;
#else
    TransferResult failed;
    failed.error = QStringLiteral("MCU2 block transport is not supported on this platform");
    return failed;
#endif
}
