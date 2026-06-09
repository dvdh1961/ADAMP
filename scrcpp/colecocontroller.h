#ifndef COLECOCONTROLLER_H
#define COLECOCONTROLLER_H

#include <QObject>
#include <QImage>
#include <QAudioSink>
#include <QIODevice>
#include <QElapsedTimer>
#include "CORE/cv.h"
#include <QSoundEffect>

#define KB_F1 0x54
#define KB_F2 0x55
#define KB_F3 0x56
#define KB_F4 0x57
#define KB_F5 0x58
#define KB_F6 0x59

#define KB_F7 0x5A
#define KB_F8 0x5B

// ADAM KEYBOARD (ADAMNET)
#define F1 0x81 // I
#define F2 0x82 // II
#define F3 0x83 // III
#define F4 0x84 // IV
#define F5 0x85 // V
#define F6 0x86 // VI

#define STORE 0x93 // Store
#define PRINT 0X95 // Print

class ColecoController : public QObject
{
    Q_OBJECT

public:
    explicit ColecoController(QObject *parent = nullptr);
    ~ColecoController();
    enum MachineType {
        Machine_Coleco = 0,
        Machine_Adam   = 1
    };
    Q_ENUM(MachineType)
    bool AlreadyReset = false;
    bool m_sgm_enabled = false;
    bool m_switchingMachineType = false;

private:
    std::atomic_bool m_running {false};
    int      m_realFrames = 0;
    QImage m_frameBuffer;
    //bool m_paused = false;
    std::atomic_bool m_pauseOnBios{false};
    std::atomic_bool m_paused{false};

    QAudioSink* m_audioSink = nullptr;
    QIODevice* m_audioDevice = nullptr;

    int16_t* m_monoBuf = nullptr;
    int16_t* m_stereoBuf = nullptr;

public slots:
    void startEmulation();
    void stopEmulation();
    void loadRom(const QString &romPath);
    void AdamCartridge(const QString &romPath);
    void ejectAdamCartridge();
    void ColecoCartridge(const QString &romPath);
    void resetDkaLoadedCartridge();
    void ejectColecoCartridge();
    void resetMachine();
    void resethMachine();
    void powerOffMachine();
    void setSGMEnabled(bool enabled);
    void setVideoStandard(bool isNTSC);
    void onAdamKeyEvent(int adamKeyCode);
    void pauseEmulation();
    void resumeEmulation();
    void stepOnce();
    void stepOver(uint16_t returnAddress);
    void sstepOnce();
    void gotoAddr(uint16_t newPC);
    bool isPaused() const { return m_paused; }
    void loadDisk(int drive, const QString& path);
    void loadTape(int drive, const QString& path);
    void ejectDisk(int drive);
    void ejectTape(int drive);
    void saveState(const QString& filePath);
    void loadState(const QString& filePath);
    void setMachineType(MachineType machineType);
    void resetAdam();
    void resetColeco();
    void coldStartAdam();
    void loadBiosRoms(const QString& colecoPath, const QString& eosPath, const QString& writerPath);
    void updateBiosStatus();
    void prepareForNewCRomAndPauseOnBios(bool doReset = true);
    void prepareForNewARomAndPauseOnBios(bool doReset = true);
    void startWithBios(const QString& colecoPath,
                       const QString& eosPath,
                       const QString& writerPath);
    void bootCpmDisk();

public slots:
    void setDTsoundEnabled(bool enabled);
    void bootPreparedColecoCartridge(const QString &romPath);

signals:
    void frameReady(const QImage &frame);
    void emulationStopped();
    void emuPausedChanged(bool paused);
    void videoStandardChanged(const QString &standard);
    void fpsUpdated(int fps);
    void sgmStatusChanged(bool enabled);
    void col80StateChanged(bool enabled);
    void diskStatusChanged(int drive, const QString& fileName);
    void tapeStatusChanged(int drive, const QString& fileName);
    void machineTypeChanged(MachineType newType);
    void cartridgeStatusChanged(const QString& colecoName, const QString& adamName);
    void onBiosStatusUpdated(int colecoExt, int eosExt, int writerExt);
    void fatalBiosError(const QString& errorMessage);
    void biosCFramesDone();
    void biosAFramesDone();
    void requestPlayDiskSound();
    void requestPlayTapeSound();

private:
    bool   m_isNTSC;
    int    m_Clock;
    int    m_SampleRate;
    int    m_AudioChunkFrames;
    int    m_AudioChunkBytes;
    double m_tstates_per_sample;
    int    m_BytesPerSampleStereo;
    QString m_currentDiskPath[MAX_DISKS];
    QString m_currentTapePath[MAX_TAPES];
    QString m_currentColecoCartPath;
    QString m_currentAdamCartPath;
    QByteArray m_colecoPathBytes;
    QByteArray m_eosPathBytes;
    QByteArray m_writerPathBytes;

    QElapsedTimer m_fpsCalcTimer;
    int m_fpsFrameCount;

    QImage frameFromBridge();
    void doHardReset();
    int  m_frameCCounter = 0;
    bool m_waitForCBiosFrames = false;
    int  m_frameACounter = 0;
    bool m_waitForABiosFrames = false;
    bool m_coreInitialized = false;
    bool m_preserveMediaOnStop = false;

    bool m_pendingCpmBootstrap = false;
    bool m_cartridgeBootInProgress = false;

    void onResetPressed();
    enum class BootPlan {
        None,
        NormalDiskBoot,
        CpmTapeThenDiskA
    };

    BootPlan m_bootPlan = BootPlan::None;
    QString  m_bootDisk0Path;   // wat er in slot 0 zit (pad)

    // Deferred mount (CP/M tape-boot): hide disk during ROM boot, mount a bit later
    QString m_deferredMountDisk0Path;
    int m_deferredMountFramesRemaining = 0;

    std::atomic<bool> m_dtSoundEnabled{true};
};

#endif
