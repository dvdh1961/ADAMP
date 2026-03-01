#include "mainwindow.h"
#include "customfiledialog.h"
#include "colecocontroller.h"
#include "screenwidget.h"
#include "inputwidget.h"
#include "logwindow.h"
#include "debuggerwindow.h"
#include "disasm_bridge.h"
#include "cartridgeinfowindow.h"
#include "ntablewindow.h"
#include "patternwindow.h"
#include "spritewindow.h"
#include "settingswindow.h"
#include "hardwarewindow.h"
#include "joypadwindow.h"
#include "printwindow.h"
#include "simplejoystick.h"

// Qt includes
#include <QMenuBar>
#include <QSplitter>
#include <QTextEdit>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QDebug>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QSettings>
#include <QStyle>
#include <QLayout>
#include <QStatusBar>
#include <QLabel>
#include <QTimer>
#include <QSizePolicy>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QFont>
#include <QMap>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QProgressDialog>


// MainWindow core (constructor/destructor)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    m_emulatorThread(nullptr),
    m_colecoController(nullptr),
    m_ntableWindow(nullptr),
    m_patternWindow(nullptr),
    m_spriteWindow(nullptr),
    m_settingsWindow(nullptr),
    m_screenWidget(nullptr),
    m_inputWidget(nullptr),
    m_logView(nullptr),
    m_actFullScreen(nullptr),
    m_actToggleSmoothing(nullptr),
    m_diskMenuA(nullptr),
    m_diskMenuB(nullptr),
    m_diskMenuC(nullptr),
    m_diskMenuD(nullptr),
    m_tapeMenuA(nullptr),
    m_tapeMenuB(nullptr),
    m_tapeMenuC(nullptr),
    m_tapeMenuD(nullptr),
    m_isDiskLoadedA(false),
    m_isDiskLoadedB(false),
    m_isDiskLoadedC(false),
    m_isDiskLoadedD(false),
    m_isTapeLoadedA(false),
    m_isTapeLoadedB(false),
    m_isTapeLoadedC(false),
    m_isTapeLoadedD(false),
    m_scalingMode(1),
    m_startFullScreen(false),
    m_adamInputGroup(nullptr),
    m_adamInputMenu(nullptr),
    m_actAdamGameOn(nullptr),
    m_actAdamGameOff(nullptr),
    m_adamGameMode(false),
    m_debugWin(nullptr),
    m_cartInfoDialog(nullptr),
    m_hardwareWindow(nullptr),
    m_openAdamRomAction(nullptr),
    m_openColecoRomAction(nullptr)

{
    setUpLogWindow();
    configurePlatformSettings();

    QCoreApplication::setOrganizationName("DVdHSoft");
    QCoreApplication::setApplicationName("ADAMP_EMU");

    // Version
    appVersion = "0.8.02.26";

    setWindowTitle(QString("ADAM+ Emulator - v%1").arg(appVersion));

    m_wallpaperLabel = new QLabel(this);
    QPixmap wallpaper(":/images/images/wallpaper_coleco.png");
    m_wallpaperLabel->setPixmap(wallpaper);
    m_wallpaperLabel->setScaledContents(true);
    m_wallpaperLabel->hide();

    m_screenWidget = new ScreenWidget(this);

    m_logoContainer = new QWidget(this);
    m_logoContainer->setAttribute(Qt::WA_TranslucentBackground);

    QHBoxLayout *hLayout = new QHBoxLayout(m_logoContainer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(0);

    m_logoLabel0 = new QLabel(m_logoContainer);
    QPixmap logo0Pixmap(":/images/images/adamp_logo0.png");
    m_logoLabel0->setPixmap(logo0Pixmap);
    m_logoLabel0->setScaledContents(false);
    hLayout->addWidget(m_logoLabel0);

    m_powerBtn = new QPushButton(m_logoContainer);
    m_powerBtn->setCheckable(true);
    m_powerBtn->setFlat(true);
    m_powerBtn->setStyleSheet("border: none;");
    m_powerBtn->setIcon(QIcon(":/images/images/adamp_logo_power_adam_off.png"));
    m_powerBtn->setIconSize(QPixmap(":/images/images/adamp_logo_power_adam_off.png").size());
    hLayout->addWidget(m_powerBtn);
    m_powerBtn->setCursor(Qt::PointingHandCursor);
    m_powerBtn->setFocusPolicy(Qt::NoFocus);

    m_logoLabel1 = new QLabel(m_logoContainer);
    QPixmap logo1Pixmap(":/images/images/adamp_logo1.png");
    m_logoLabel1->setPixmap(logo1Pixmap);
    m_logoLabel1->setScaledContents(false);
    hLayout->addWidget(m_logoLabel1);

    m_logoLabel1->installEventFilter(this);
    m_logoLabel1->setCursor(Qt::PointingHandCursor);

    m_resetAdamBtn = new QPushButton(m_logoContainer);
    m_resetAdamBtn->setCheckable(true);
    m_resetAdamBtn->setFlat(true);
    m_resetAdamBtn->setStyleSheet("border: none;");
    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
    m_resetAdamBtn->setIconSize(QPixmap(":/images/images/adamp_logo_reset_adam_off.png").size());
    hLayout->addWidget(m_resetAdamBtn);
    m_resetAdamBtn->setCursor(Qt::PointingHandCursor);
    m_resetAdamBtn->setFocusPolicy(Qt::NoFocus);

    m_resetCartBtn = new QPushButton(m_logoContainer);
    m_resetCartBtn->setCheckable(true);
    m_resetCartBtn->setFlat(true);
    m_resetCartBtn->setStyleSheet("border: none;");
    m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png"));
    m_resetCartBtn->setIconSize(QPixmap(":/images/images/adamp_logo_reset_cartridge_off.png").size());
    hLayout->addWidget(m_resetCartBtn);
    m_resetCartBtn->setCursor(Qt::PointingHandCursor);
    m_resetCartBtn->setFocusPolicy(Qt::NoFocus);

    m_logoLabel2 = new QLabel(m_logoContainer);
    QPixmap logo2Pixmap(":/images/images/adamp_logo2.png");
    m_logoLabel2->setPixmap(logo2Pixmap);
    m_logoLabel2->setScaledContents(false);

    hLayout->addWidget(m_logoLabel2);

    m_logoContainer->setLayout(hLayout);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);

    mainLayout->addWidget(m_screenWidget, 1);

    mainLayout->addWidget(m_logoContainer, 0, Qt::AlignHCenter | Qt::AlignBottom);

    m_ntableWindow = new NTableWindow(this);
    m_ntableWindow->hide();

    m_patternWindow = new PatternWindow(this);
    m_patternWindow->hide();

    m_spriteWindow = new SpriteWindow(this);
    m_spriteWindow->hide();

    m_settingsWindow = new SettingsWindow(this);

    QWidget *centralContainer = new QWidget(this);
    centralContainer->setLayout(mainLayout);

    centralContainer->setAttribute(Qt::WA_TranslucentBackground);

    setCentralWidget(centralContainer);

    m_wallpaperLabel->lower();

    m_inputWidget = new InputWidget(this);
    m_inputWidget->attachTo(m_screenWidget);
    m_inputWidget->setFocusPolicy(Qt::NoFocus);
    m_inputWidget->setOverlayVisible(false);
    m_inputWidget->show();
    m_inputWidget->raise();

    this->setMinimumSize(770, 700);

    setStatusBar();

    loadSettings();

    setupUI();

    m_joystick = new SimpleJoystick(this);

    if (m_joystick) {
        m_joystick->setJoystickType(m_joystickType);
    }

    connect(m_joystick, &SimpleJoystick::directionChanged,
            m_inputWidget, &InputWidget::setJoystickDirection);

    connect(m_joystick, &SimpleJoystick::fireLeftChanged,
            m_inputWidget, &InputWidget::setJoystickFireL);

    connect(m_joystick, &SimpleJoystick::fireRightChanged,
            m_inputWidget, &InputWidget::setJoystickFireR);

    connect(m_joystick, &SimpleJoystick::startPressed,
            m_inputWidget, &InputWidget::setJoystickStart);

    connect(m_joystick, &SimpleJoystick::selectPressed,
            m_inputWidget, &InputWidget::setJoystickSelect);

    connect(m_joystick, &SimpleJoystick::analogXChanged,
            m_inputWidget, &InputWidget::setJoystickAnalogX);

    m_joystick->stopPolling();
    m_joystick->startPolling(0);

    if (m_actTogglePaddleMode) {
        onTogglePaddleMode(m_usePaddleMode);
    }

    m_screenWidget->setScalingMode(static_cast<ScreenWidget::ScalingMode>(m_scalingMode));

    if (m_screenWidget) {
        m_screenWidget->setScalingMode(static_cast<ScreenWidget::ScalingMode>(m_scalingMode));
        m_screenWidget->setScanlinesMode(m_scanlinesMode);
        m_screenWidget->setColorFilterMode(m_colorFilterMode);   // NIEUW
    }

    if (m_sysLabel) {
        m_sysLabel->setText(m_machineType ? "ADAM" : "COLECO");
    }

    HardwareConfig initialConfig;
    initialConfig.machine = (m_machineType ? MACHINE_ADAM : MACHINE_COLECO);
    initialConfig.realhardware = m_realhardware;
    initialConfig.palette = m_paletteIndex;
    initialConfig.sgmEnabled = m_sgmEnabled;
    initialConfig.c80Enabled = m_c80Enabled;
    initialConfig.Joys = m_ctrlJoys;
    initialConfig.AdamNet = m_ctrlAdamNet;
    initialConfig.Cartridge = m_ctrlCartridge;

    m_hardwareWindow = new HardwareWindow(initialConfig, this);

    applyHardwareConfig(initialConfig);

    connect(m_actShowLog, &QAction::toggled, this, [this](bool on){
        if (!m_logView) return;

        if (on) {
            m_logView->show();
            m_logView->raise();
            m_logView->activateWindow();
        } else {
            m_logView->hide();
        }
    });

    setupEmulatorThread();

    if (m_inputWidget) {
        m_inputWidget->setController(m_colecoController);
        qDebug() << "[MAINWINDOW] Controller connected to InputWidget";
    }

    connect(m_colecoController, &ColecoController::tapeStatusChanged,
            this, &MainWindow::onTapeStatusChanged,
            Qt::QueuedConnection);
    connect(m_colecoController, &ColecoController::diskStatusChanged,
            this, &MainWindow::onDiskStatusChanged,
            Qt::QueuedConnection);

    m_debugWin = new DebuggerWindow(this);
    m_debugWin->setController(m_colecoController);

    connect(m_debugWin, &DebuggerWindow::requestStepCPU,
            this,       &MainWindow::onDebuggerStepCPU);
    connect(m_debugWin, &DebuggerWindow::requestRunCPU,
            this,       &MainWindow::onDebuggerRunCPU);
    connect(m_debugWin, &DebuggerWindow::requestBreakCPU,
            this,       &MainWindow::onDebuggerBreakCPU);
    connect(m_debuggerAction, &QAction::triggered,
            this, &MainWindow::onOpenDebugger);
    connect(m_debugWin, &DebuggerWindow::requestBpLoad,
            this, &MainWindow::onLoadBreakpoint);
    connect(m_debugWin, &DebuggerWindow::requestBpSave,
            this, &MainWindow::onSaveBreakpoint);
    connect(m_debugWin, &DebuggerWindow::requestSymLoad,
            this, &MainWindow::onLoadSymbolDefinitions);
    connect(m_debugWin, &DebuggerWindow::requestSymSave,
            this, &MainWindow::onSaveSymbolDefinitions);

    connect(m_debugWin, &DebuggerWindow::requestStepOver,
            m_colecoController, &ColecoController::stepOver);

    // Timer initialisatie
    m_resetAdamBlinkTimer = new QTimer(this);
    m_resetCartBlinkTimer = new QTimer(this);

    // Verbind de timers met de nieuwe slots
    connect(m_resetAdamBlinkTimer, &QTimer::timeout, this, &MainWindow::onToggleResetAdamBlink);
    connect(m_resetCartBlinkTimer, &QTimer::timeout, this, &MainWindow::onToggleResetCartBlink);

    if (m_startFullScreen) {
        QTimer::singleShot(0, this, [this]() {
            onToggleFullScreen(true);
            if(m_actFullScreen) m_actFullScreen->setChecked(true);
        });
    }

    QTimer::singleShot(0, this, [this]() {
        if (m_screenWidget) {
            m_screenWidget->setFocus(Qt::OtherFocusReason);
        }

        QResizeEvent re(size(), size());
        resizeEvent(&re);
    });

    if (emulator->currentMachineType == 0) // COLECO
        emutype = false;
    else
        emutype = true;

    m_diskSound = new QSoundEffect(this);
    m_diskSound->setSource(QUrl("qrc:/sounds/sounds/adam_disk1.wav"));
    m_diskSound->setVolume(1.0f);

    m_tapeSound = new QSoundEffect(this);
    m_tapeSound->setSource(QUrl("qrc:/sounds/sounds/adam_tape.wav"));
    m_tapeSound->setVolume(0.6f);

    connect(m_colecoController, &ColecoController::requestPlayDiskSound,
            this, [this]() {
                if (!m_diskSound) return;
                m_diskSound->stop();
                m_diskSound->play();
            },
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::requestPlayTapeSound,
            this, [this]() {
                if (!m_tapeSound) return;
                m_tapeSound->stop();
                m_tapeSound->play();
            },
            Qt::QueuedConnection);

}

MainWindow::~MainWindow()
{
    if (m_emulatorThread) {
        m_emulatorThread->quit();
        m_emulatorThread->wait(1000);
    }
}

