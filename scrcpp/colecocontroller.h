#ifndef COLECOCONTROLLER_H
#define COLECOCONTROLLER_H

#include <QObject>
#include <QImage>
#include <QAudioSink>
#include <QIODevice>
#include <QElapsedTimer>
#include <atomic>
#include "coleco.h"

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

private:
    std::atomic_bool m_running {false};
    int      m_realFrames = 0;
    QImage m_frameBuffer;
    bool m_paused = false;

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
    void ejectColecoCartridge();
    void resetMachine();
    void resethMachine();
    void setSGMEnabled(bool enabled);
    void setVideoStandard(bool isNTSC);
    void onAdamKeyEvent(int adamKeyCode);
    void pauseEmulation();
    void resumeEmulation();
    void stepOnce();
    void sstepOnce();
    void gotoAddr(uint16_t newPC);
    bool isPaused() const { return m_paused; }
    void loadDisk(int drive, const QString& path);
    void loadTape(int drive, const QString& path);
    void ejectDisk(int drive);
    void ejectTape(int drive);
    void saveState(const QString& filePath);
    void loadState(const QString& filePath);
    Q_INVOKABLE void setMachineType(int machineType); // 0=Coleco/Phoenix, 1=ADAM
    Q_INVOKABLE void setMachineType(MachineType machineType);
    Q_INVOKABLE void resetAdam();
    Q_INVOKABLE void resetColeco();

signals:
    void frameReady(const QImage &frame);
    void emulationStopped();
    void emuPausedChanged(bool paused);
    void videoStandardChanged(const QString &standard);
    void fpsUpdated(int fps);
    void sgmStatusChanged(bool enabled);
    void diskStatusChanged(int drive, const QString& fileName);
    void tapeStatusChanged(int drive, const QString& fileName);
    void machineTypeChanged(MachineType newType);
    void cartridgeStatusChanged(const QString& colecoName, const QString& adamName);

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

    QElapsedTimer m_fpsCalcTimer;
    int m_fpsFrameCount;

    QImage frameFromBridge();
    void doHardReset();
};

#endif
