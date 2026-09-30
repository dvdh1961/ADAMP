#include "mainwindow.h"
#include "customfiledialog.h"
#include "colecocontroller.h"
#include "screenwidget.h"
#include "inputwidget.h"
#include "logwindow.h"
#include "debuggerwindow.h"
#include "ataridebuggerwindow.h"
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
#include "GRAPH/f18a_term80_tdos.h"

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
#include <QSaveFile>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHostInfo>
#include <QUdpSocket>
#include <QElapsedTimer>
#include <QApplication>
#include <QStyleFactory>
#include "6801/adnet_mcu2.h"
#include "vdp_bridge.h"


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
    const QByteArray picoConfigPath =
        QDir(QCoreApplication::applicationDirPath()).filePath("pico9918.cfg").toLocal8Bit();
    vdp_bridge_set_config_path(picoConfigPath.constData());

    setUpLogWindow();
    configurePlatformSettings();

    QCoreApplication::setOrganizationName("DVdHSoft");
    QCoreApplication::setApplicationName("ADAMP_EMU");
    setObjectName(QStringLiteral("adampMainWindow"));

    // Restore the visual mode before the UI is assembled.  Night mode remains
    // the default for existing installations.
    QSettings themeSettings;
    m_darkTheme = themeSettings.value("appearance/darkTheme", true).toBool();
    applyDayNightTheme(m_darkTheme);

    // Version
    // Keep the Atari core revision visible so a stale shadow-build cannot be
    // mistaken for the newly compiled PAL renderer.
    appVersion = QStringLiteral("2.0.09.26");

    setWindowTitle(QString("ADAM+ Emulator - v%1").arg(appVersion));

    m_wallpaperLabel = new QLabel(this);
    QPixmap wallpaper(":/images/images/wallpaper_coleco.png");
    m_wallpaperLabel->setPixmap(wallpaper);
    m_wallpaperLabel->setScaledContents(true);
    m_wallpaperLabel->hide();

    // Zwarte balk onderaan op de background
    m_bottomBlackBar = new QFrame(this);
    m_bottomBlackBar->setStyleSheet("background-color: black;");
    m_bottomBlackBar->setFixedHeight(40);   // vaste hoogte
    //m_bottomBlackBar->hide();

    m_splashLabel = new QLabel(this);
    QPixmap splash(":/images/images/ADAMP_SPLASH_LOGO.png");

    if (!splash.isNull())
    {
        QPixmap halfSplash = splash.scaled(
            splash.width() / 1.7,
            splash.height() / 1.7,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
            );

        m_splashLabel->setPixmap(halfSplash);
    }

    m_splashLabel->setScaledContents(false);
    m_splashLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_splashLabel->setStyleSheet("background: transparent;");
    m_splashLabel->adjustSize();
    //m_splashLabel->hide();

    m_imageManagerDialog = new AimDialog (this);
    m_imageManagerDialog->hide();

    m_screenWidget = new ScreenWidget(this);
    showSplash();

    m_logoContainer = new QWidget(this);
    m_logoContainer->setAttribute(Qt::WA_TranslucentBackground);

    QHBoxLayout *hLayout = new QHBoxLayout(m_logoContainer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    //hLayout->setSpacing(0);

    //m_logoLabel0 = new QLabel(m_logoContainer);
    //QPixmap logo0Pixmap(":/images/images/adamp_logo0.png");
    //m_logoLabel0->setPixmap(logo0Pixmap);
    //m_logoLabel0->setScaledContents(false);
    //hLayout->addWidget(m_logoLabel0);

    hLayout->addSpacing(10);

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

    //m_logoLabel2 = new QLabel(m_logoContainer);
    //QPixmap logo2Pixmap(":/images/images/adamp_logo2.png");
    //m_logoLabel2->setPixmap(logo2Pixmap);
    //m_logoLabel2->setScaledContents(false);

    //hLayout->addWidget(m_logoLabel2);

    hLayout->addSpacing(10);

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

    // Dedicated overlay for the frameless application's outer edge.  A QSS
    // border on QMainWindow is not reliably painted above its dock areas and
    // central widget on Windows.
    m_windowBorderOverlay = new QFrame(this);
    m_windowBorderOverlay->setObjectName(QStringLiteral("windowBorderOverlay"));
    m_windowBorderOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_windowBorderOverlay->setFrameShape(QFrame::NoFrame);
    m_windowBorderOverlay->setGeometry(rect());
    m_windowBorderOverlay->raise();

    loadSettings();

    setupUI();
    setupThemeButton();

    connect(m_inputWidget, &InputWidget::atariKeypadShortcut,
            this, [this](int key) {
        if (m_machineType != MACHINE_ATARI2600)
            return;

        if (key == 1) {
            if (m_atariColorAction)
                m_atariColorAction->trigger();
            return;
        }

        if (key == 2) {
            m_atariLeftDifficultyA = !m_atariLeftDifficultyA;
            if (m_atariLeftDifficultyAAction) m_atariLeftDifficultyAAction->setChecked(m_atariLeftDifficultyA);
            if (m_atariLeftDifficultyBAction) m_atariLeftDifficultyBAction->setChecked(!m_atariLeftDifficultyA);
            m_inputWidget->setAtari2600LeftDifficultyA(m_atariLeftDifficultyA);
            saveSettings();
            updateAtariStatusBar();
            return;
        }

        if (key == 3) {
            m_atariRightDifficultyA = !m_atariRightDifficultyA;
            if (m_atariRightDifficultyAAction) m_atariRightDifficultyAAction->setChecked(m_atariRightDifficultyA);
            if (m_atariRightDifficultyBAction) m_atariRightDifficultyBAction->setChecked(!m_atariRightDifficultyA);
            m_inputWidget->setAtari2600RightDifficultyA(m_atariRightDifficultyA);
            saveSettings();
            updateAtariStatusBar();
            return;
        }

        if (key == 4 && m_atariGameResetAction)
            m_atariGameResetAction->trigger();
    });

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
    if (m_actToggleDrivingControllerMode) {
        onToggleDrivingControllerMode(m_useDrivingControllerMode);
    }

    m_screenWidget->setScalingMode(static_cast<ScreenWidget::ScalingMode>(m_scalingMode));

    if (m_screenWidget) {
        m_screenWidget->setScalingMode(static_cast<ScreenWidget::ScalingMode>(m_scalingMode));
        m_screenWidget->setScanlinesMode(m_scanlinesMode);
        m_screenWidget->setColorFilterMode(m_colorFilterMode);   // NIEUW
    }

    if (m_sysLabel) {
        if (m_machineType == MACHINE_ATARI2600)
            m_sysLabel->setText("ATARI 2600");
        else
            m_sysLabel->setText(m_machineType == MACHINE_ADAM ? "ADAM" : "COLECO");
    }

    m_c80Enabled = false;
    coleco_80col_enabled = 0;

    if (m_screenWidget) {
        m_screenWidget->set80ColumnMode(false);
    }

   // cpm80_disable();
    cpm80_reset();

    HardwareConfig initialConfig;
    initialConfig.machine = static_cast<MachineType>(m_machineType);
    initialConfig.realhardware = m_realhardware;
    initialConfig.palette = m_paletteIndex;
    initialConfig.vdpType = m_vdpType;
    initialConfig.sgmEnabled = m_sgmEnabled;
    initialConfig.c80Enabled = m_c80Enabled;
    initialConfig.Joys = m_ctrlJoys;
    initialConfig.AdamNet = m_ctrlAdamNet;
    initialConfig.fujiNetDirectRom = m_fujiNetDirectRom;
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

    mcu2_set_fuji_boot_interceptor(
        [this](const QByteArray &headerBlocks, const QString &mountedPath) {
            int decision = -1;
            if (QThread::currentThread() == thread())
                return handleFujiNetBootIntercept(headerBlocks, mountedPath);
            QMetaObject::invokeMethod(
                this,
                [this, &decision, headerBlocks, mountedPath]() {
                    decision = handleFujiNetBootIntercept(headerBlocks, mountedPath);
                },
                Qt::BlockingQueuedConnection);
            return decision;
        });

    mcu2_set_fuji_coleco_rom_ready_handler(
        [this](const QString &romPath) {
            QMetaObject::invokeMethod(this, [this, romPath]() {
                /* The D5 loader reached the first block after the ROM. Let
                 * that final failed/end-of-media DCB retire before switching
                 * the running core from ADAM to native Coleco. */
                qDebug() << "[UI][BOOT] FujiNet ROM saved; software cartridge boot deferred:"
                         << romPath;
                QTimer::singleShot(1000, this, [this, romPath]() {
                    qDebug() << "[UI][BOOT] FujiNet transfer settled -> normal software cartridge route:"
                             << romPath;
                    m_adamGameMode = false;
                    adamnet_set_game_mode(false);
                    loadColecoRomFromPath(romPath, true);
                });
            }, Qt::QueuedConnection);
        });

    mcu2_set_fuji_coleco_rom_progress_handler(
        [this](qint64 loadedBytes, qint64 totalBytes, bool finished) {
            QMetaObject::invokeMethod(this, [this, loadedBytes, totalBytes, finished]() {
                if (finished) {
                    if (m_fujiRomProgressDialog) {
                        m_fujiRomProgressDialog->close();
                        m_fujiRomProgressDialog->deleteLater();
                        m_fujiRomProgressDialog = nullptr;
                    }
                    return;
                }

                if (!m_fujiRomProgressDialog) {
                    m_fujiRomProgressDialog = new QProgressDialog(this);
                    m_fujiRomProgressDialog->setWindowTitle(
                        tr("Loading FujiNet cartridge"));
                    m_fujiRomProgressDialog->setCancelButton(nullptr);
                    m_fujiRomProgressDialog->setAutoClose(false);
                    m_fujiRomProgressDialog->setAutoReset(false);
                    m_fujiRomProgressDialog->setMinimumDuration(0);
                    m_fujiRomProgressDialog->setWindowModality(Qt::WindowModal);
                    m_fujiRomProgressDialog->show();
                }

                const qint64 loadedKiB = loadedBytes / 1024;
                if (totalBytes > 0) {
                    const int totalKiB = int(totalBytes / 1024);
                    const int valueKiB = qBound(0, int(loadedKiB), totalKiB);
                    const int percent = totalKiB > 0
                        ? (valueKiB * 100) / totalKiB : 0;
                    m_fujiRomProgressDialog->setRange(0, totalKiB);
                    m_fujiRomProgressDialog->setValue(valueKiB);
                    m_fujiRomProgressDialog->setLabelText(
                        tr("Loading native Coleco ROM from FujiNet\n"
                           "%1 KiB / %2 KiB — %3%")
                            .arg(valueKiB).arg(totalKiB).arg(percent));
                } else {
                    m_fujiRomProgressDialog->setRange(0, 0);
                    m_fujiRomProgressDialog->setLabelText(
                        loadedBytes == 0
                            ? tr("Determining FujiNet ROM size…")
                            : tr("Loading native Coleco ROM from FujiNet\n"
                                 "%1 KiB received").arg(loadedKiB));
                }
            }, Qt::QueuedConnection);
        });

    mcu2_set_fuji_direct_rom_fetch_handler(
        [this](const QString &host, const QString &path) {
            if (QThread::currentThread() == thread())
                return fetchFujiNetRomDirect(host, path);
            QString result;
            QMetaObject::invokeMethod(this, [this, &result, host, path]() {
                result = fetchFujiNetRomDirect(host, path);
            }, Qt::BlockingQueuedConnection);
            return result;
        });

    /* The initial hardware configuration can queue an ADAM reset before the
     * interceptor exists. Queue one correctly ordered hardware power boot:
     * retained-media choice/D9 first, BIOS reset second. */
    if (m_machineType == MACHINE_ADAM && m_ctrlAdamNet) {
        QMetaObject::invokeMethod(
            m_colecoController,
            [ctrl = m_colecoController]() {
                /* If the already queued initial reset consumed the probe, do
                 * not interrupt its boot with a second power cycle. */
                if (mcu2_fuji_reset_boot_probe_is_armed())
                    ctrl->powerOffMachine(true);
            },
            Qt::QueuedConnection);
    }

    QMetaObject::invokeMethod(m_colecoController,"setAtari2600PhosphorEffect",
                              Qt::QueuedConnection,Q_ARG(bool,m_atariPhosphorEffect));

    if (m_inputWidget) {
        m_inputWidget->setController(m_colecoController);
        m_inputWidget->setAtari2600Color(m_atariColor);
        m_inputWidget->setAtari2600LeftDifficultyA(m_atariLeftDifficultyA);
        m_inputWidget->setAtari2600RightDifficultyA(m_atariRightDifficultyA);
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
    m_atariDebugWin = new AtariDebuggerWindow(this);
    m_atariDebugWin->setController(m_colecoController);

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

    m_allowSaveSettings = true;

    qDebug() << "[SETTINGS] saving enabled after startup";

}

QString MainWindow::fetchFujiNetRomDirect(const QString &hostValue,
                                          const QString &pathValue)
{
    const QString cancelledResult = QStringLiteral("::ADAMP_DIRECT_CANCELLED::");
    const QString host = hostValue.trimmed();
    QString path = pathValue.trimmed();
    if (host.isEmpty() || path.isEmpty()) {
        qWarning() << "[UI][DIRECT] missing FujiNet host/path metadata";
        return QString();
    }
    if (!path.startsWith('/'))
        path.prepend('/');

    if (!m_fujiRomProgressDialog) {
        m_fujiRomProgressDialog = new QProgressDialog(this);
        m_fujiRomProgressDialog->setWindowTitle(tr("Loading FujiNet cartridge"));
        m_fujiRomProgressDialog->setCancelButtonText(tr("Cancel"));
        m_fujiRomProgressDialog->setAutoClose(false);
        m_fujiRomProgressDialog->setAutoReset(false);
        m_fujiRomProgressDialog->setMinimumDuration(0);
        m_fujiRomProgressDialog->setWindowModality(Qt::WindowModal);
    }
    /* A live D9 route may already have created the indeterminate physical
     * progress dialog while probing the header. Convert that same dialog to
     * a cancellable direct-download dialog as well. */
    m_fujiRomProgressDialog->setCancelButtonText(tr("Cancel"));
    m_fujiRomProgressDialog->setRange(0, 0);
    m_fujiRomProgressDialog->setLabelText(
        tr("Connecting directly to %1…").arg(host));
    m_fujiRomProgressDialog->show();

    const QString outputDir = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("media/roms/FujiNet"));
    if (!QDir().mkpath(outputDir)) {
        m_fujiRomProgressDialog->close();
        m_fujiRomProgressDialog->deleteLater();
        m_fujiRomProgressDialog = nullptr;
        return QString();
    }
    const QString outputPath = QDir(outputDir).filePath(QStringLiteral("FujiNet.rom"));
    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) {
        m_fujiRomProgressDialog->close();
        m_fujiRomProgressDialog->deleteLater();
        m_fujiRomProgressDialog = nullptr;
        return QString();
    }

    qint64 loaded = 0;
    qint64 total = -1;
    QByteArray validationBytes;
    bool transferOk = false;
    bool cancelled = false;
    QString failure;
    const QMetaObject::Connection cancelConnection = connect(
        m_fujiRomProgressDialog, &QProgressDialog::canceled,
        this, [&cancelled]() { cancelled = true; });
    auto updateProgress = [&]() {
        if (total > 0) {
            m_fujiRomProgressDialog->setRange(0, 1000);
            m_fujiRomProgressDialog->setValue(int(qMin<qint64>(1000,
                loaded * 1000 / total)));
            m_fujiRomProgressDialog->setLabelText(
                tr("Loading FujiNet cartridge directly: %1 / %2 KiB (%3%)")
                    .arg(loaded / 1024).arg(total / 1024)
                    .arg(loaded * 100 / total));
        } else {
            m_fujiRomProgressDialog->setRange(0, 0);
            m_fujiRomProgressDialog->setLabelText(
                tr("Loading FujiNet cartridge directly: %1 KiB").arg(loaded / 1024));
        }
        QApplication::processEvents();
    };
    auto store = [&](const QByteArray &chunk) -> bool {
        if (chunk.isEmpty()) return true;
        if (loaded + chunk.size() > 16 * 1024 * 1024) {
            failure = tr("ROM exceeds the 16 MiB safety limit");
            return false;
        }
        validationBytes.append(chunk);
        if (output.write(chunk) != chunk.size()) {
            failure = output.errorString();
            return false;
        }
        loaded += chunk.size();
        updateProgress();
        return true;
    };

    const bool webSource = host.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || host.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
        || host.compare(QStringLiteral("SD"), Qt::CaseInsensitive) == 0;
    if (webSource) {
        QUrl url(host.compare(QStringLiteral("SD"), Qt::CaseInsensitive) == 0
                     ? QStringLiteral("http://fujinet.local/dav") : host);
        QString urlPath = url.path();
        if (!urlPath.endsWith('/')) urlPath += '/';
        urlPath += path.mid(1);
        url.setPath(urlPath);
        QNetworkAccessManager manager;
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = manager.get(request);
        connect(m_fujiRomProgressDialog, &QProgressDialog::canceled,
                reply, &QNetworkReply::abort);
        QEventLoop loop;
        connect(reply, &QNetworkReply::downloadProgress, this,
                [&](qint64 received, qint64 announced) {
            Q_UNUSED(received)
            if (announced > 0) total = announced;
            updateProgress();
        });
        connect(reply, &QIODevice::readyRead, this, [&]() {
            if (!failure.isEmpty() || cancelled) return;
            store(reply->readAll());
        });
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (failure.isEmpty()) store(reply->readAll());
        transferOk = !cancelled && failure.isEmpty()
            && reply->error() == QNetworkReply::NoError;
        if (!transferOk && failure.isEmpty()) failure = reply->errorString();
        reply->deleteLater();
    } else {
        const QHostInfo info = QHostInfo::fromName(host);
        QHostAddress address;
        for (const QHostAddress &candidate : info.addresses()) {
            if (candidate.protocol() == QAbstractSocket::IPv4Protocol) {
                address = candidate;
                break;
            }
        }
        if (address.isNull()) {
            failure = tr("TNFS host could not be resolved");
        } else {
            QUdpSocket socket;
            socket.connectToHost(address, 16384);
            quint16 session = 0;
            quint8 sequence = 0;
            auto transaction = [&](quint8 command, const QByteArray &payload,
                                   QByteArray *response) -> bool {
                for (int attempt = 0; attempt < 4; ++attempt) {
                    if (cancelled) return false;
                    QByteArray packet;
                    packet.append(char(session & 0xff));
                    packet.append(char((session >> 8) & 0xff));
                    packet.append(char(sequence));
                    packet.append(char(command));
                    packet.append(payload);
                    socket.write(packet);
                    socket.flush();
                    QElapsedTimer timer;
                    timer.start();
                    while (timer.elapsed() < 1600) {
                        if (cancelled) return false;
                        if (!socket.waitForReadyRead(100)) {
                            QApplication::processEvents();
                            continue;
                        }
                        while (socket.hasPendingDatagrams()) {
                            QByteArray reply;
                            reply.resize(int(socket.pendingDatagramSize()));
                            socket.readDatagram(reply.data(), reply.size());
                            if (reply.size() >= 5 && quint8(reply[2]) == sequence
                                && quint8(reply[3]) == command) {
                                ++sequence;
                                *response = reply;
                                return true;
                            }
                        }
                    }
                }
                ++sequence;
                return false;
            };
            QByteArray response;
            QByteArray mount;
            mount.append(char(0)); mount.append(char(1));
            mount.append('/'); mount.append(char(0));
            mount.append(char(0)); mount.append(char(0));
            if (!transaction(0x00, mount, &response) || quint8(response[4]) != 0) {
                failure = tr("TNFS mount failed");
            } else {
                session = quint8(response[0]) | (quint16(quint8(response[1])) << 8);
                QByteArray pathUtf8 = path.toUtf8();
                pathUtf8.append(char(0));
                if (!transaction(0x24, pathUtf8, &response)
                    || quint8(response[4]) != 0 || response.size() < 15) {
                    failure = tr("TNFS file information failed");
                } else {
                    total = quint8(response[11])
                        | (qint64(quint8(response[12])) << 8)
                        | (qint64(quint8(response[13])) << 16)
                        | (qint64(quint8(response[14])) << 24);
                    QByteArray open;
                    open.append(char(1)); open.append(char(0));
                    open.append(char(0)); open.append(char(0));
                    open.append(pathUtf8);
                    if (!transaction(0x29, open, &response)
                        || quint8(response[4]) != 0 || response.size() < 6) {
                        failure = tr("TNFS open failed");
                    } else {
                        const quint8 handle = quint8(response[5]);
                        transferOk = true;
                        while (loaded < total) {
                            if (cancelled) {
                                transferOk = false;
                                break;
                            }
                            const quint16 wanted = quint16(qMin<qint64>(525, total - loaded));
                            QByteArray read;
                            read.append(char(handle));
                            read.append(char(wanted & 0xff));
                            read.append(char((wanted >> 8) & 0xff));
                            if (!transaction(0x21, read, &response)
                                || response.size() < 7 || quint8(response[4]) != 0) {
                                transferOk = false;
                                failure = tr("TNFS read failed at %1 KiB").arg(loaded / 1024);
                                break;
                            }
                            const int count = quint8(response[5])
                                | (int(quint8(response[6])) << 8);
                            if (count <= 0 || response.size() < 7 + count
                                || !store(response.mid(7, count))) {
                                transferOk = false;
                                if (failure.isEmpty()) failure = tr("Invalid TNFS read response");
                                break;
                            }
                        }
                        QByteArray closePayload(1, char(handle));
                        transaction(0x23, closePayload, &response);
                    }
                }
                QByteArray none;
                transaction(0x01, none, &response);
            }
        }
    }

    bool signatureOk = false;
    auto hasHeaderAt = [&](qint64 offset) {
        if (offset < 0 || offset + 1 >= validationBytes.size()) return false;
        const quint8 a = quint8(validationBytes[int(offset)]);
        const quint8 b = quint8(validationBytes[int(offset + 1)]);
        return (a == 0xAA && b == 0x55) || (a == 0x55 && b == 0xAA);
    };
    for (qint64 offset = 0; offset + 1 < validationBytes.size();
         offset += 16 * 1024) {
        if (hasHeaderAt(offset)) {
            signatureOk = true;
            break;
        }
    }
    /* Match coleco_loadcart(): some banked images place the boot header at
     * the beginning of their final 16 KiB bank rather than at an absolute
     * 16 KiB boundary. Small legacy carts and known 128 KiB headerless carts
     * are accepted by that same established software loader. */
    if (!signatureOk && validationBytes.size() >= 16 * 1024)
        signatureOk = hasHeaderAt(validationBytes.size() - 16 * 1024);
    if (!signatureOk && (validationBytes.size() <= 32 * 1024
                         || validationBytes.size() == 128 * 1024))
        signatureOk = true;
    const bool sizeOk = loaded >= 8 * 1024 && loaded <= 16 * 1024 * 1024
        && (total <= 0 || loaded == total);
    disconnect(cancelConnection);
    if (cancelled) {
        output.cancelWriting();
        qDebug() << "[UI][DIRECT] download cancelled by user; physical fallback suppressed";
        m_fujiRomProgressDialog->close();
        m_fujiRomProgressDialog->deleteLater();
        m_fujiRomProgressDialog = nullptr;
        return cancelledResult;
    }
    if (!transferOk || !signatureOk || !sizeOk) {
        output.cancelWriting();
        qWarning() << "[UI][DIRECT] direct ROM rejected; host=" << host
                   << "path=" << path << "loaded=" << loaded << "total=" << total
                   << "bank-header=" << signatureOk << "error=" << failure;
        m_fujiRomProgressDialog->close();
        m_fujiRomProgressDialog->deleteLater();
        m_fujiRomProgressDialog = nullptr;
        return QString();
    }
    if (!output.commit()) {
        qWarning() << "[UI][DIRECT] unable to commit ROM:" << output.errorString();
        m_fujiRomProgressDialog->close();
        m_fujiRomProgressDialog->deleteLater();
        m_fujiRomProgressDialog = nullptr;
        return QString();
    }
    m_fujiRomProgressDialog->setValue(1000);
    m_fujiRomProgressDialog->close();
    m_fujiRomProgressDialog->deleteLater();
    m_fujiRomProgressDialog = nullptr;
    qDebug() << "[UI][DIRECT] complete; protocol=" << (webSource ? "HTTP/WebDAV" : "TNFS")
             << "bytes=" << loaded << "file=" << outputPath;
    return outputPath;
}

MainWindow::~MainWindow()
{
    mcu2_set_fuji_direct_rom_fetch_handler(Mcu2FujiDirectRomFetch());
    mcu2_set_fuji_coleco_rom_ready_handler(Mcu2FujiColecoRomReady());
    mcu2_set_fuji_coleco_rom_progress_handler(Mcu2FujiColecoRomProgress());
    mcu2_set_fuji_boot_interceptor(Mcu2FujiBootInterceptor());
    if (m_emulatorThread) {
        m_emulatorThread->quit();
        m_emulatorThread->wait(1000);
    }
}

void MainWindow::showSplash()
{
    // Startup: eerst gamescreen 2 seconden verbergen
    m_screenWidget->hide();

    QTimer::singleShot(3000, this, [this]() {
        if (m_screenWidget) {
            m_screenWidget->show();
            m_screenWidget->raise();
            m_screenWidget->setFocus(Qt::OtherFocusReason);
        }
    });
}

void MainWindow::centerSplash()
{
    if (!m_splashLabel)
        return;

    m_splashLabel->adjustSize();

    const int x = (width()  - m_splashLabel->width())  / 2;
    const int y = (height() - m_splashLabel->height()) / 2;

    m_splashLabel->move(x, y);
}
