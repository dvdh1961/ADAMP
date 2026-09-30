#ifndef MCU2_GATEWAY_H
#define MCU2_GATEWAY_H

#include <QString>
#include <QByteArray>

/*
 * Small, self-contained connection probe for the CH559 ADAMnet master.
 *
 * The emulator does not depend on an external HID library.  On Windows the
 * implementation uses the standard HID/SetupAPI interfaces; other platforms
 * return a clear "not supported" result and continue in software mode.
 * Runtime DCB routing is deliberately kept out of this class until the
 * connection-only integration has been validated on the real hardware.
 */
class Mcu2Gateway
{
public:
    struct ProbeResult {
        bool connected = false;
        QString message;
        int firmwareMajor = 0;
        int firmwareMinor = 0;
        int firmwarePatch = 0;
        int protocolVersion = 0;
        int targetCode = 0;
    };

    struct DcbResult {
        bool transportOk = false;
        QString error;
        QByteArray returnedDcb;
        QByteArray data;
        int masterResult = -1;
        int stage = 0;
        int attempts = 0;
        int gatewayAttempts = 1;
        int gatewayElapsedMs = 0;
        bool streamed = false;
        int checksum = 0;
    };

    struct TransferResult {
        bool transportOk = false;
        QString error;
        QString diagnostic;
        QByteArray data;
        int masterResult = -1;
        int stage = 0;
        int attempts = 0;
    };

    static ProbeResult probe();
    static DcbResult executeDcb(const QByteArray &dcb);
    static DcbResult executeDcbWrite(const QByteArray &dcb,
                                     const QByteArray &block);
    static DcbResult executeBlockDcb(const QByteArray &dcb,
                                     const QByteArray &writeBlock = QByteArray());
    /* Read-only preflight used by the hardware media loader.  It inspects a
     * mounted physical medium before the emulator chooses EOS/CP/M/T-DOS. */
    static DcbResult inspectBlock(int address, int deviceNumber,
                                  quint32 blockNumber);
    static TransferResult sendBlock(int address, const QByteArray &data);
    static TransferResult receiveBlock(int address, int maximumAttempts = 40);
};

#endif // MCU2_GATEWAY_H
