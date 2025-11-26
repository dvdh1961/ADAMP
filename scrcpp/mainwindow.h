#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QThread>
#include <QMoveEvent>
#include <QSettings>
#include <QMap>
#include "simplejoystick.h"

class ColecoController;
class ScreenWidget;
class InputWidget;
class LogWidget;
class DebuggerWindow;
class QAction;
class QLabel;
class CartridgeInfoDialog;
class NTableWindow;
class PatternWindow;
class SpriteWindow;
class SettingsWindow;
class HardwareWindow;
class JoypadWindow;
class KbWidget;

struct HardwareConfig;

class MainWindow : public QMainWindow
{
    Q_OBJECT
    void onOpenSettings();

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void appendLog(const QString& line);
    void setDebugger(DebuggerWindow *debugger);

public slots:
    // menu / UI acties
    void onOpenRom();
    void onReset();
    void onhReset();
    void onRunStop();
    void onToggleSGM(bool checked);
    void onToggleKeyboard(bool on);
    void onToggleVideoStandard();
    void onShowNameTable(bool checked);
    void onShowPatternTable(bool checked);
    void onShowSpriteTable(bool checked);
    void onShowPrinterWindow();
    void onToggleSnap(bool checked);

    // callbacks van emulator / thread
    void onThreadFinished();
    void onFramePresented();
    // VERWIJDERD: void onFpsTick();
    void onFpsUpdated(int fps); // <-- NIEUWE SLOT
    void onSgmStatusChanged(bool enabled); // <-- NIEUWE SLOT
    void setVideoStandard(const QString& standard);

    // debugger-acties
    void onOpenDebugger();
    void onDebuggerStepCPU();
    void onDebuggerRunCPU();
    void onDebuggerBreakCPU();
    void onOpenCartInfo();

    // run/stop integratie
    void onEmuPausedChanged(bool paused);
    void onStartActionTriggered();
    void onOpenHardware();
    void onSaveState();
    void onLoadState();
    void onSaveScreenshot();
    void onResetWindowSize();
    void onOpenJoypadMapper();

private slots:
    void onLoadDisk(int drive);
    void onLoadTape(int drive);
    void onEjectDisk(int drive);
    void onEjectTape(int drive);
    // --- MEDIA STATUS UPDATE SLOTS ---
    void onDiskStatusChanged(int drive, const QString& fileName);
    void onTapeStatusChanged(int drive, const QString& fileName);
    void onAdamInputModeChanged();
    void onToggleFullScreen(bool checked);
    void onFrameReceived(const QImage &frame);
    void onCycleScalingMode();
    void onToggleBezels(bool checked);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void configurePlatformSettings();

private:
    // helpers
    void setupUI();
    void setupEmulatorThread();
    void setStatusBar();
    void setUpLogWindow();
    void positionDebugger();
    void positionPrinter();
    void saveSettings();
    void loadSettings();
    void showAboutDialog();
    void applyHardwareConfig(const HardwareConfig& cfg);
    void updateFullScreenWallpaper();

private:
    // emulator thread en controller
    QThread          *m_emulatorThread = nullptr;
    ColecoController *m_colecoController = nullptr;
    NTableWindow     *m_ntableWindow = nullptr;
    PatternWindow    *m_patternWindow = nullptr;
    SpriteWindow     *m_spriteWindow = nullptr;
    SettingsWindow   *m_settingsWindow = nullptr;
    SimpleJoystick *m_joystick = nullptr;

    // hoofd UI elementen
    ScreenWidget *m_screenWidget = nullptr;
    InputWidget  *m_inputWidget  = nullptr;
    LogWidget    *m_logView      = nullptr;
    QLabel       *m_logoLabel    = nullptr;
    KbWidget     *m_kbWidget     = nullptr;

    // statusbar dingen
    QLabel *m_stdLabel  = nullptr;
    QLabel *m_fpsLabel  = nullptr;
    QLabel *m_runLabel  = nullptr;
    QLabel *m_romLabel  = nullptr;
    QLabel *m_sepLabel1 = nullptr;
    QLabel *m_sepLabel2 = nullptr;
    QLabel *m_sepLabel3 = nullptr;
    QLabel *m_sepLabel4 = nullptr;
    QLabel *m_sysLabel  = nullptr;   // COLECO / ADAM
    QLabel *m_sepLabelSGM = nullptr;
    QLabel *m_sgmLabel    = nullptr;
    QString m_currentStd;
    QString m_currentRomName;
    QString appVersion;

    // acties / menu
    QAction *m_openRomAction      = nullptr;
    QAction *m_quitAction         = nullptr;
    QAction *m_startAction        = nullptr; // Run/Stop (F9)
    QAction *m_resetAction        = nullptr; // Reset (F11)
    QAction *m_hresetAction       = nullptr; // Hard Reset (F12)
    QAction *m_actShowLog         = nullptr;
    QAction *m_actToggleKeyboard  = nullptr;
    QAction *m_debuggerAction     = nullptr; // Debugger (F8)
    QAction *m_actToggleSGM       = nullptr;
    QAction *m_actToggleNTSC      = nullptr;
    QAction *m_actTogglePAL       = nullptr;
    QAction *m_cartInfoAction     = nullptr;
    QAction *m_actShowNameTable   = nullptr;
    QAction *m_actShowPatternTable = nullptr;
    QAction *m_actShowSpriteTable  = nullptr;
    QAction *m_actWiki            = nullptr;
    QAction *m_actReport          = nullptr;
    QAction *m_actChat            = nullptr;
    QAction *m_actDonate          = nullptr;
    QAction *m_actAbout           = nullptr;
    QAction *m_settingsAction     = nullptr;
    QAction *m_actHardware        = nullptr;
    QAction *m_actJoypadMapper    = nullptr;
    QAction *m_actSaveScreenshot  = nullptr;

    // --- MEDIA ACTIES ---
    QAction *m_loadDiskActionA    = nullptr;
    QAction *m_loadDiskActionB    = nullptr;
    QAction *m_loadDiskActionC    = nullptr;
    QAction *m_loadDiskActionD    = nullptr;
    QAction *m_loadTapeActionA    = nullptr;
    QAction *m_loadTapeActionB    = nullptr;
    QAction *m_loadTapeActionC    = nullptr;
    QAction *m_loadTapeActionD    = nullptr;
    QAction *m_ejectDiskActionA   = nullptr;
    QAction *m_ejectDiskActionB   = nullptr;
    QAction *m_ejectDiskActionC   = nullptr;
    QAction *m_ejectDiskActionD   = nullptr;
    QAction *m_ejectTapeActionA   = nullptr;
    QAction *m_ejectTapeActionB   = nullptr;
    QAction *m_ejectTapeActionC   = nullptr;
    QAction *m_ejectTapeActionD   = nullptr;

    QAction* m_actPrinterOutput   = nullptr;
    QAction* m_actFullScreen      = nullptr;
    QAction* m_actToggleSmoothing = nullptr;

    QAction* m_actSaveState       = nullptr;
    QAction* m_actLoadState       = nullptr;
    QAction* m_actResetSize       = nullptr;
    QAction* m_actToggleBezels    = nullptr;
    QAction* m_actToggleSnap = nullptr;

    // --- MEDIA STATUSBALK LABELS ---
    QLabel *m_diskLabelA          = nullptr;
    QLabel *m_diskLabelB          = nullptr;
    QLabel *m_diskLabelC          = nullptr;
    QLabel *m_diskLabelD          = nullptr;
    QLabel *m_tapeLabelA          = nullptr;
    QLabel *m_tapeLabelB          = nullptr;
    QLabel *m_tapeLabelC          = nullptr;
    QLabel *m_tapeLabelD          = nullptr;
    QLabel *m_sepLabelMedia1a     = nullptr;
    QLabel *m_sepLabelMedia1b     = nullptr;
    QLabel *m_sepLabelMedia1c     = nullptr;
    QLabel *m_sepLabelMedia1d     = nullptr;
    QLabel *m_sepLabelMedia2a     = nullptr;
    QLabel *m_sepLabelMedia2b     = nullptr;
    QLabel *m_sepLabelMedia2c     = nullptr;
    QLabel *m_sepLabelMedia2d     = nullptr;
    QLabel* m_wallpaperLabel      = nullptr;
    QMenu *m_diskMenuA            = nullptr;
    QMenu *m_diskMenuB            = nullptr;
    QMenu *m_diskMenuC            = nullptr;
    QMenu *m_diskMenuD            = nullptr;
    QMenu *m_tapeMenuA            = nullptr;
    QMenu *m_tapeMenuB            = nullptr;
    QMenu *m_tapeMenuC            = nullptr;
    QMenu *m_tapeMenuD            = nullptr;

    bool m_isDiskLoadedA;
    bool m_isDiskLoadedB;
    bool m_isDiskLoadedC;
    bool m_isDiskLoadedD;
    bool m_isTapeLoadedA;
    bool m_isTapeLoadedB;
    bool m_isTapeLoadedC;
    bool m_isTapeLoadedD;
    int  m_scalingMode; // 0=Sharp, 1=Smooth, 2=EPX
    bool m_startFullScreen;
    bool m_useBezels;
    bool m_snapWindows;

    QActionGroup* m_adamInputGroup;
    QMenu* m_adamInputMenu;
    QAction* m_actAdamKeyboard;
    QAction* m_actAdamJoystick;
    bool     m_adamInputModeJoystick; // false = KB, true = Joystick

    void updateMediaMenuState();
    void updateMediaStatusLabels();

    QMap<int, uint8_t> m_pressedKeyMap;

    // debugger venster
    DebuggerWindow *m_debugWin = nullptr;
    CartridgeInfoDialog *m_cartInfoDialog = nullptr;

    bool m_isPaused = false;
    QString m_romPath;
    QString m_diskPath;
    QString m_tapePath;
    QString m_statePath;
    QString m_breakpointPath;

    int m_paletteIndex = 0;
    int m_machineType = 0; // 0=Coleco/Phoenix, 1=ADAM
    bool m_sgmEnabled       = false;
    bool m_f18aEnabled      = false;
    bool m_ctrlSteering     = false;
    bool m_ctrlRoller       = false;
    bool m_ctrlSuperAction  = false;
};

#endif // MAINWINDOW_H
