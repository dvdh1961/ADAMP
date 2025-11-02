#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer> // QTimer wordt niet meer gebruikt, maar mag blijven staan
#include <QThread>
#include <QMoveEvent>
#include <QSettings>

// Geen SoundManager meer
// #include "soundmanager.h"

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

struct HardwareConfig;

class MainWindow : public QMainWindow
{
    Q_OBJECT
    void onOpenSettings();

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void appendLog(const QString& line);

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

    // callbacks van emulator / thread
    void onThreadFinished();
    void onFramePresented();
    // VERWIJDERD: void onFpsTick();
    void onFpsUpdated(int fps); // <-- NIEUWE SLOT
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

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    // helpers
    void setupUI();
    void setupEmulatorThread();
    void setStatusBar();
    void setUpLogWindow();
    void positionDebugger();
    void saveSettings();
    void loadSettings();
    void showAboutDialog();
    void applyHardwareConfig(const HardwareConfig& cfg);

private:
    // emulator thread en controller
    QThread          *m_emulatorThread = nullptr;
    ColecoController *m_colecoController = nullptr;
    NTableWindow     *m_ntableWindow = nullptr;
    PatternWindow    *m_patternWindow = nullptr;
    SpriteWindow     *m_spriteWindow = nullptr;
    SettingsWindow   *m_settingsWindow = nullptr;

    // hoofd UI elementen
    ScreenWidget *m_screenWidget = nullptr;
    InputWidget  *m_inputWidget  = nullptr;
    LogWidget    *m_logView      = nullptr;
    QLabel       *m_logoLabel    = nullptr;

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

    QString m_currentStd;
    QString m_currentRomName;

    // fps tracking
    // VERWIJDERD: QTimer m_fpsTimer;
    // VERWIJDERD: int    m_frameCounter = 0;

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

    // debugger venster
    DebuggerWindow *m_debugWin = nullptr;
    CartridgeInfoDialog *m_cartInfoDialog = nullptr;

    // huidige pauze-state van de emulator
    bool m_isPaused = false;
    QString m_romPath;
    int m_paletteIndex = 0;
    int m_machineType = 0; // 0=Coleco/Phoenix, 1=ADAM
    bool m_sgmEnabled       = false;
    bool m_f18aEnabled      = false;
    bool m_ctrlSteering     = false;
    bool m_ctrlRoller       = false;
    bool m_ctrlSuperAction  = false;
};

#endif // MAINWINDOW_H
