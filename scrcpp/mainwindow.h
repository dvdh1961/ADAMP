#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QThread>
#include <QMoveEvent>
#include <QSettings>
#include <QMap>
#include <QProgressDialog>
#include <QSoundEffect>

#include "simplejoystick.h"
#include "hardwarewindow.h"

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
class QActionGroup;
class QMenu;

struct HardwareConfig;

enum ScanlinesMode {
    ScanlinesOff = 0,
    ScanlinesTV,
    ScanlinesLCD,
    ScanlinesRaster  };

enum ColorFilterMode {
    ColorFilterOff = 0,
    ColorFilterMonochrome,
    ColorFilterSepia,
    ColorFilterGreenCRT,
    ColorFilterAmberCRT,
    ColorFilterCMY,
    ColorFilterRGB
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
    void onOpenSettings();

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void appendLog(const QString& line);
    void setDebugger(DebuggerWindow *debugger);
    bool emutype = 0;
    bool m_cpm_status = 0;

public slots:
    void powerOff();
    void onRunStop();
    void onToggleSGM(bool checked);
    void onToggleKeyboard(bool on);
    void onToggleVideoStandard();
    void onShowNameTable();
    void onShowPatternTable();
    void onShowSpriteTable();
    void onShowPrinterWindow();
    void onToggleSnap(bool checked);
    void onMachineTypeChanged(int newType);
    void onToggleResetAdamBlink();
    void onToggleResetCartBlink();

    // callbacks van emulator / thread
    void onThreadFinished();
    void onFramePresented();
    void onFpsUpdated(int fps);
    void onSgmStatusChanged(bool enabled);
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
    void onSaveBreakpoint();
    void onLoadBreakpoint();
    void onResetWindowSize();
    void onOpenJoypadMapper();
    void onReleaseAll();
    void forceStatusBarMediaFlag(QLabel* label);
    void forceStatusBarMediaFlags();

private slots:
    void onLoadDisk(int drive);
    void onLoadTape(int drive);
    void onEjectDisk(int drive);
    void onEjectTape(int drive);
    void onOpenAdamRom();
    void onEjectAdamRom();
    void onOpenColecoRom();
    void onEjectColecoRom();
    // --- MEDIA STATUS UPDATE SLOTS ---
    void onDiskStatusChanged(int drive, const QString& fileName);
    void onTapeStatusChanged(int drive, const QString& fileName);
    void onAdamGameMode();
    void onToggleFullScreen(bool checked);
    void onFrameReceived(const QImage &frame);
    void onCycleScalingMode();
    void onToggleBezels(bool checked);
    void onScalingModeChanged(QAction* action);
    void onJoystickTypeChanged(QAction* action);
    void onResetAdamBtnClicked();
    void onResetCartBtnClicked();
    void onCartridgeStatusChanged(const QString& colecoName, const QString& adamName);
    void onPowerBtnClicked();
    void onTogglePaddleMode(bool checked);
    void onSaveSymbolDefinitions();
    void onLoadSymbolDefinitions();
    void onColorFilterModeChanged(QAction* action);
    void onBiosStatusUpdated(int colecoExt, int eosExt, int writerExt);
    void onBiosCFramesDone();
    void onBiosAFramesDone();
    void onToggleDTsound(bool checked);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void configurePlatformSettings();
    void onScanlinesModeChanged(QAction* action);

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
    bool m_isShuttingDown = false;
    QProgressDialog *m_shutdownDialog = nullptr;
    void switchToAdamMode();
    void switchToColecoMode();
    void updateHardwareWindowMediaDisplay();
    void handleFatalBiosError(const QString& errorMessage);
    void loadExternalBiosRoms();

private:
    // emulator thread en controller
    QThread          *m_emulatorThread = nullptr;
    ColecoController *m_colecoController = nullptr;
    NTableWindow     *m_ntableWindow = nullptr;
    PatternWindow    *m_patternWindow = nullptr;
    SpriteWindow     *m_spriteWindow = nullptr;
    SettingsWindow   *m_settingsWindow = nullptr;
    SimpleJoystick   *m_joystick = nullptr;

    // hoofd UI elementen
    ScreenWidget *m_screenWidget = nullptr;
    InputWidget  *m_inputWidget  = nullptr;
    LogWidget    *m_logView      = nullptr;

    QWidget      *m_logoContainer = nullptr;  // Het transparante panel dat m_logoLabel vervangt
    QLabel       *m_logoLabel0    = nullptr;  // adamp_logo0.png
    QPushButton  *m_powerBtn      = nullptr;  // Power Reset knop
    QLabel       *m_logoLabel1    = nullptr;  // adamp_logo1.png
    QPushButton  *m_resetAdamBtn  = nullptr;  // Adam Reset knop
    QPushButton  *m_resetCartBtn  = nullptr;  // Cartridge Reset knop
    QLabel       *m_logoLabel2    = nullptr;  // adam_logo2.png

    QTimer *m_resetAdamBlinkTimer = nullptr;
    QTimer *m_resetCartBlinkTimer = nullptr;

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
    QAction *m_actDTsound         = nullptr;
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
    QAction* m_actSaveSymbols     = nullptr;
    QAction* m_actLoadSymbols     = nullptr;
    QAction* m_actResetSize       = nullptr;
    QAction* m_actToggleBezels    = nullptr;
    QAction* m_actToggleSnap      = nullptr;
    QAction *m_actStatusColecoBios = nullptr;
    QAction *m_actStatusEosBios    = nullptr;
    QAction *m_actStatusWriterBios = nullptr;

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

    QActionGroup *m_joystickGroup = nullptr;
    QAction *m_actJoystickGeneral = nullptr;
    QAction *m_actJoystickPS      = nullptr;
    QAction *m_actJoystickXbox    = nullptr;
    int m_joystickType = 0; // 0=General, 1=PS, 2=Xbox (NIEUWE MEMBER)
    QAction *m_actTogglePaddleMode = nullptr;
    bool m_usePaddleMode = false;
    bool m_useDTsound = false;

    bool m_isDiskLoadedA;
    bool m_isDiskLoadedB;
    bool m_isDiskLoadedC;
    bool m_isDiskLoadedD;
    bool m_isTapeLoadedA;
    bool m_isTapeLoadedB;
    bool m_isTapeLoadedC;
    bool m_isTapeLoadedD;
    ScanlinesMode m_scanlinesMode = ScanlinesOff;
    QMenu *m_scanlinesMenu = nullptr;
    QActionGroup *m_scanlinesGroup = nullptr;
    QAction *m_actScanlinesTV = nullptr;
    QAction *m_actScanlinesLCD = nullptr;
    QAction *m_actScanlinesRaster = nullptr;
    QMenu *m_scalingMenu = nullptr;
    QActionGroup *m_scalingGroup = nullptr;
    QAction *m_actScalingSmooth = nullptr;
    QAction *m_actScalingSharp = nullptr;
    QAction *m_actScalingEPX = nullptr;
    ColorFilterMode m_colorFilterMode = ColorFilterOff;
    QMenu *m_colorFilterMenu = nullptr;
    QActionGroup *m_colorFilterGroup = nullptr;

    QString m_loadedTapeNames[4]; // D1 t/m D4
    QString m_loadedDiskNames[4]; // D5 t/m D8

    int  m_scalingMode;
    bool m_startFullScreen;
    bool m_useBezels;
    bool m_snapWindows;
    bool m_useScanlines = false;

    QActionGroup* m_adamInputGroup = nullptr;
    QMenu* m_adamInputMenu = nullptr;
    QAction* m_actAdamGameOn;
    QAction* m_actAdamGameOff;
    bool     m_adamGameMode; // false = GameOff, true = GameOn

    void updateMediaMenuState();
    void updateMediaStatusLabels();

    // debugger venster
    DebuggerWindow *m_debugWin = nullptr;
    CartridgeInfoDialog *m_cartInfoDialog = nullptr;
    HardwareWindow   *m_hardwareWindow = nullptr;

    bool m_isPaused = false;
    QString m_romPath;
    QString m_diskPath;
    QString m_tapePath;
    QString m_statePath;
    QString m_breakpointPath;
    QString m_screenshotsPath;
    QString m_symbolsPath;
    QString m_adamBezelPath;
    QString m_cvBezelPath;
    QString m_colecoBiosPath;
    QString m_eosBiosPath;
    QString m_writerBiosPath;
    QAction *m_openAdamRomAction = nullptr;
    QAction *m_openColecoRomAction = nullptr;
    bool m_isAdamRomLoaded = false;
    bool m_isColecoRomLoaded = false;
    QString m_currentARomName;
    QString m_currentRomName;
    QAction *m_actReleaseAll = nullptr;

    // BIOS Status Menu
    QMenu   *m_biosSourceMenu = nullptr;
    QAction *m_actColecoBiosSource = nullptr;
    QAction *m_actEosBiosSource = nullptr;
    QAction *m_actWriterBiosSource = nullptr;

    int m_paletteIndex = 0;
    int m_machineType = 0; // 0=Coleco, 1=ADAM
    bool m_realhardware = false;
    bool m_sgmEnabled       = false;
    bool m_c80Enabled      = false;
    bool m_ctrlJoys     = false;
    bool m_ctrlAdamNet       = false;
    bool m_ctrlCartridge  = false;
    bool m_resetAdamLocked  = false;
    bool m_AdamTMedia_insert = false;
    bool m_AdamDMedia_insert = false;
    bool m_resetColecoLocked = false;
    bool m_ColecoMedia_insert = false;

    QSoundEffect *m_diskSound = nullptr;
    QSoundEffect *m_tapeSound = nullptr;

};

#endif // MAINWINDOW_H
