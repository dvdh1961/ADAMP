#include "mainwindow.h"
#include "colecocontroller.h"
#include "screenwidget.h"
#include "inputwidget.h"
#include "logwindow.h"
#include "debuggerwindow.h"
#include "cartridgeinfowindow.h"
#include "ntablewindow.h"
#include "patternwindow.h"
#include "spritewindow.h"
#include "settingswindow.h"
#include "hardwarewindow.h"
#include "coleco.h"

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
    m_debugWin(nullptr),
    m_cartInfoDialog(nullptr)
{
    QCoreApplication::setOrganizationName("DVdHSoft");
    QCoreApplication::setApplicationName("ADAMP_EMU");

    setWindowTitle("ADAM+ ColecoVision Emulator");
    m_screenWidget = new ScreenWidget(this);
    m_screenWidget->setScale(2.9);

    m_logoLabel = new QLabel(this);
    QPixmap logoPixmap(":/images/images/adamp_logo.png");
    m_logoLabel->setPixmap(logoPixmap);
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0); // Geen witruimte rondom
    mainLayout->addWidget(m_screenWidget, 0, Qt::AlignHCenter);
    mainLayout->addStretch(1);
    mainLayout->addWidget(m_logoLabel, 0, Qt::AlignHCenter);
    mainLayout->addStretch(1);

    m_ntableWindow = new NTableWindow(this);
    m_ntableWindow->hide(); // Zorg dat het verborgen start

    m_patternWindow = new PatternWindow(this);
    m_patternWindow->hide();

    m_spriteWindow = new SpriteWindow(this);
    m_spriteWindow->hide();

    m_settingsWindow = new SettingsWindow(this);

    // 4. Maak een container-widget
    QWidget *centralContainer = new QWidget(this);
    centralContainer->setLayout(mainLayout);

    // 5. Stel de container in als de central widget
    setCentralWidget(centralContainer);
    m_inputWidget = new InputWidget(this);
    m_inputWidget->attachTo(m_screenWidget);

    //setCentralWidget(m_screenWidget);

    m_inputWidget = new InputWidget(this);
    m_inputWidget->attachTo(m_screenWidget);
    m_inputWidget->hide();

    setUpLogWindow();

    this->setFixedWidth(770);
    this->setFixedHeight(700);

    setStatusBar();

    // Menu's/acties
    setupUI();
    loadSettings();

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

    // 1. controller + thread klaarzetten
    setupEmulatorThread();

    // 2. debugger
    m_debugWin = new DebuggerWindow(this);
    connect(m_debugWin, &DebuggerWindow::requestStepCPU,
            this,       &MainWindow::onDebuggerStepCPU);
    connect(m_debugWin, &DebuggerWindow::requestRunCPU,
            this,       &MainWindow::onDebuggerRunCPU);
    connect(m_debugWin, &DebuggerWindow::requestBreakCPU,
            this,       &MainWindow::onDebuggerBreakCPU);
    connect(m_debuggerAction, &QAction::triggered,
            this, &MainWindow::onOpenDebugger);

    // 3. Cart Info Dialoog (maken we hier al aan)
    m_cartInfoDialog = new CartridgeInfoDialog(this);
    m_cartInfoDialog->hide(); // Standaard verborgen
}

MainWindow::~MainWindow()
{
    if (m_emulatorThread) {
        m_emulatorThread->quit();
        m_emulatorThread->wait(1000);
    }
}

void MainWindow::onOpenSettings()
{
    // 1. Laad de huidige instellingen in het dialoogvenster
    m_settingsWindow->setRomPath(m_romPath); // <--- AANGEPAST

    // 2. Toon het dialoogvenster modaal
    if (m_settingsWindow->exec() == QDialog::Accepted) { // <--- AANGEPAST
        // 3. Als de gebruiker op OK klikt, haal de waarden op...
        m_romPath = m_settingsWindow->romPath(); // <--- AANGEPAST
        // 4. ...en sla ze direct op.
        saveSettings();
    }
}

// Laadt de instellingen
void MainWindow::loadSettings()
{
    QSettings settings;
    m_romPath      = settings.value("romPath", ".").toString();
    m_paletteIndex = settings.value("video/palette", 0).toInt();
    m_machineType  = settings.value("machine/type", 0).toInt();

    // Nieuw: Additional Hardware
    m_sgmEnabled  = settings.value("hardware/sgm",  false).toBool();
    m_f18aEnabled = settings.value("hardware/f18a", false).toBool();

    // Nieuw: Additional Controllers
    m_ctrlSteering    = settings.value("controller/steering",    false).toBool();
    m_ctrlRoller      = settings.value("controller/roller",      false).toBool();
    m_ctrlSuperAction = settings.value("controller/superaction", false).toBool();

    qDebug() << "Loaded settings:"
             << "machine="  << m_machineType
             << "palette="  << m_paletteIndex
             << "sgm="      << m_sgmEnabled
             << "f18a="     << m_f18aEnabled
             << "steer="    << m_ctrlSteering
             << "roller="   << m_ctrlRoller
             << "saction="  << m_ctrlSuperAction;
}

// Slaat de instellingen op
void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("romPath",        m_romPath);
    settings.setValue("video/palette",  m_paletteIndex);
    settings.setValue("machine/type",   m_machineType);

    // Nieuw: Additional Hardware
    settings.setValue("hardware/sgm",   m_sgmEnabled);
    settings.setValue("hardware/f18a",  m_f18aEnabled);

    // Nieuw: Additional Controllers
    settings.setValue("controller/steering",    m_ctrlSteering);
    settings.setValue("controller/roller",      m_ctrlRoller);
    settings.setValue("controller/superaction", m_ctrlSuperAction);

    qDebug() << "Settings saved."
             << "palette=" << m_paletteIndex
             << "machine=" << m_machineType
             << "sgm="     << m_sgmEnabled
             << "f18a="    << m_f18aEnabled
             << "steer="   << m_ctrlSteering
             << "roller="  << m_ctrlRoller
             << "saction=" << m_ctrlSuperAction;
}

void MainWindow::setUpLogWindow()
{
    m_logView = new LogWidget(nullptr);
    LogWidget::bindTo(m_logView);
    m_logView->setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    m_logView->setWindowTitle("ADAM+ Debug logger");
    m_logView->resize(770, 200);
    m_logView->setAttribute(Qt::WA_DeleteOnClose, false);
    m_logView->hide();
    m_logView->installEventFilter(this);
}

void MainWindow::setStatusBar()
{
    statusBar()->setSizeGripEnabled(true);
    setWindowFlags(windowFlags()
                   & ~Qt::WindowMaximizeButtonHint
                   & ~Qt::WindowMinimizeButtonHint);

    this->setFixedWidth(770);

    // --- NIEUWE LABEL: systeemnaam ---
    m_sysLabel = new QLabel("COLECO", this);
    m_sysLabel->setObjectName("sysLabel");
    m_sysLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_sysLabel->setMinimumWidth(80);
    m_sysLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    m_stdLabel = new QLabel(this);
    m_stdLabel->setObjectName("stdLabel");
    m_stdLabel->setText(QString("%1").arg(m_currentStd));
    m_stdLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_stdLabel->setMinimumWidth(50);
    m_stdLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    m_fpsLabel = new QLabel("0fps", this);
    m_fpsLabel->setObjectName("fpsLabel");
    m_fpsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_fpsLabel->setMinimumWidth(50);
    m_fpsLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    m_runLabel = new QLabel("RUN", this);
    m_runLabel->setObjectName("runLabel");
    m_runLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_runLabel->setMinimumWidth(50);
    m_runLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    m_romLabel = new QLabel("No cart", this);
    m_romLabel->setObjectName("romLabel");
    m_romLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_romLabel->setMinimumWidth(260);
    m_romLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_sepLabel1 = new QLabel("|", this);
    m_sepLabel2 = new QLabel("|", this);
    m_sepLabel3 = new QLabel("|", this);
    m_sepLabel4 = new QLabel("|", this);

    // Volgorde in statusbar: systeem | std | fps | run | rom
    statusBar()->addWidget(m_sysLabel);
    statusBar()->addWidget(m_sepLabel1);
    statusBar()->addWidget(m_stdLabel);
    statusBar()->addWidget(m_sepLabel2);
    statusBar()->addWidget(m_fpsLabel);
    statusBar()->addWidget(m_sepLabel3);
    statusBar()->addWidget(m_runLabel);
    statusBar()->addWidget(m_sepLabel4);
    statusBar()->addWidget(m_romLabel);

    QFont statusFont("Roboto", 12);
    statusFont.setBold(false);

    m_sysLabel->setFont(statusFont);
    m_stdLabel->setFont(statusFont);
    m_fpsLabel->setFont(statusFont);
    m_romLabel->setFont(statusFont);
    m_runLabel->setFont(statusFont);
    m_sepLabel1->setFont(statusFont);
    m_sepLabel2->setFont(statusFont);
    m_sepLabel3->setFont(statusFont);
    m_sepLabel4->setFont(statusFont);
}


void MainWindow::setupUI()
{
    // File
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    m_openRomAction = new QAction(tr("&Open ROM..."), this);
    m_openRomAction->setShortcut(QKeySequence::Open);
    connect(m_openRomAction, &QAction::triggered, this, &MainWindow::onOpenRom);
    fileMenu->addAction(m_openRomAction);
    fileMenu->addSeparator();
    m_settingsAction = new QAction(tr("Settings..."), this);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);
    fileMenu->addAction(m_settingsAction);
    fileMenu->addSeparator();
    m_quitAction = new QAction(tr("&Exit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(m_quitAction);

    // Debug
    QMenu* debugMenu = menuBar()->addMenu(tr("&Debug"));
    m_startAction = new QAction(tr("Run/Stop"), this);
    m_startAction->setShortcut(Qt::Key_F9);
    connect(m_startAction, &QAction::triggered, this, &MainWindow::onRunStop);
    debugMenu->addAction(m_startAction);
    debugMenu->addSeparator();
    m_resetAction = new QAction(tr("&Soft Reset"), this);
    m_resetAction->setShortcut(Qt::Key_F11);
    connect(m_resetAction, &QAction::triggered, this, &MainWindow::onReset);
    debugMenu->addAction(m_resetAction);
    m_hresetAction = new QAction(tr("&Hard Reset"), this);
    m_hresetAction->setShortcut(Qt::Key_F12);
    connect(m_hresetAction, &QAction::triggered, this, &MainWindow::onhReset);
    debugMenu->addAction(m_hresetAction);
    debugMenu->addSeparator();
    m_actShowLog = new QAction(tr("Logger"), this);
    m_actShowLog->setCheckable(true);
    m_actShowLog->setChecked(false);
    debugMenu->addAction(m_actShowLog);
    debugMenu->addSeparator();
    m_debuggerAction = new QAction(tr("&Debugger"), this);
    m_debuggerAction->setShortcut(Qt::Key_F8);
    debugMenu->addAction(m_debuggerAction);


    // Tools
    QMenu* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    m_actToggleKeyboard = new QAction(tr("Keypad"), this);
    m_actToggleKeyboard->setCheckable(true);
    m_actToggleKeyboard->setChecked(false);
    m_actToggleKeyboard->setShortcut(Qt::Key_F10);
    toolsMenu->addAction(m_actToggleKeyboard);
    toolsMenu->addSeparator();
    m_actShowNameTable = new QAction(tr("Name Table Viewer"), this);
    m_actShowNameTable->setCheckable(true);
    connect(m_actShowNameTable, &QAction::toggled, this, &MainWindow::onShowNameTable);
    toolsMenu->addAction(m_actShowNameTable); // Of een ander menu
    m_actShowPatternTable = new QAction(tr("Pattern Table Viewer"), this);
    m_actShowPatternTable->setCheckable(true);
    connect(m_actShowPatternTable, &QAction::toggled, this, &MainWindow::onShowPatternTable);
    toolsMenu->addAction(m_actShowPatternTable);
    m_actShowSpriteTable = new QAction(tr("Sprite Table Viewer"), this);
    m_actShowSpriteTable->setCheckable(true);
    connect(m_actShowSpriteTable, &QAction::toggled, this, &MainWindow::onShowSpriteTable);
    toolsMenu->addAction(m_actShowSpriteTable);
    toolsMenu->addSeparator();
    m_cartInfoAction = new QAction(tr("Cart profile"), this);
    toolsMenu->addAction(m_cartInfoAction);


    // Options
    QMenu* optionsMenu = menuBar()->addMenu(tr("&Options"));
    QActionGroup* videoGroup = new QActionGroup(this);
    m_actToggleNTSC = new QAction(tr("NTSC (60Hz)"), this);
    m_actToggleNTSC->setCheckable(true);
    m_actToggleNTSC->setChecked(true); // Default op NTSC
    videoGroup->addAction(m_actToggleNTSC);
    optionsMenu->addAction(m_actToggleNTSC);
    m_actTogglePAL = new QAction(tr("PAL (50Hz)"), this);
    m_actTogglePAL->setCheckable(true);
    videoGroup->addAction(m_actTogglePAL);
    optionsMenu->addAction(m_actTogglePAL);
    optionsMenu->addSeparator();
    m_actHardware = new QAction(tr("Hardware..."), this);
    optionsMenu->addAction(m_actHardware);
    connect(m_actHardware, &QAction::triggered, this, &MainWindow::onOpenHardware);

    // Help
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    m_actWiki = new QAction(tr("Online Wiki"), this);
    helpMenu->addAction(m_actWiki);
    m_actReport = new QAction(tr("Report a bug"), this);
    helpMenu->addAction(m_actReport);
    m_actChat = new QAction(tr("Chat with community"), this);
    helpMenu->addAction(m_actChat);
    helpMenu->addSeparator();
    m_actDonate = new QAction(tr("Donate"), this);
    helpMenu->addAction(m_actDonate);
    helpMenu->addSeparator();
    m_actAbout = new QAction(tr("About"), this);
    helpMenu->addAction(m_actAbout);


    // Connects
    connect(m_actToggleSGM, &QAction::toggled, this, &MainWindow::onToggleSGM);
    connect(m_actToggleKeyboard, &QAction::toggled, this, &MainWindow::onToggleKeyboard);

    // Connect de nieuwe acties
    connect(m_actToggleNTSC, &QAction::triggered, this, &MainWindow::onToggleVideoStandard);
    connect(m_actTogglePAL, &QAction::triggered, this, &MainWindow::onToggleVideoStandard);

    connect(m_cartInfoAction, &QAction::triggered, this, &MainWindow::onOpenCartInfo);


    // Zorg ervoor dat de actie wordt uitgevinkt als het venster wordt gesloten
    connect(m_ntableWindow, &NTableWindow::windowClosed, this, [this]() {
        m_actShowNameTable->setChecked(false);
    });
    connect(m_patternWindow, &PatternWindow::windowClosed, this, [this]() {
        m_actShowPatternTable->setChecked(false);
    });
    connect(m_spriteWindow, &SpriteWindow::windowClosed, this, [this]() {
        m_actShowSpriteTable->setChecked(false);
    });

    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::onOpenHardware()
{
    HardwareConfig cur;

    // Machine + Video
    cur.machine = (m_machineType ? MACHINE_ADAM : MACHINE_COLECO);
    cur.palette = m_paletteIndex;

    // Additional Hardware (uit settings/members)
    cur.sgmEnabled  = m_sgmEnabled;
    cur.f18aEnabled = m_f18aEnabled;

    // Additional Controllers (uit settings/members)
    cur.steeringWheel = m_ctrlSteering;
    cur.rollerCtrl    = m_ctrlRoller;
    cur.superAction   = m_ctrlSuperAction;

    const int prevPalette = m_paletteIndex;

    HardwareWindow dlg(cur, this);
    if (dlg.exec() == QDialog::Accepted) {
        HardwareConfig chosen = dlg.config();

        // --- Palette direct toepassen
        m_paletteIndex = chosen.palette;
        coleco_setpalette(m_paletteIndex);

        // --- Alles doorgeven aan central apply (machine/hw/controllers)
        applyHardwareConfig(chosen);

        // --- Bewaren
        saveSettings();
    } else {
        // Cancel → palette preview ongedaan
        m_paletteIndex = prevPalette;
        coleco_setpalette(m_paletteIndex);
    }
}

void MainWindow::applyHardwareConfig(const HardwareConfig& cfg)
{
    int newMachineType = (cfg.machine == MACHINE_ADAM ? 1 : 0);
    if (m_machineType != newMachineType) {
        m_machineType = newMachineType;
        QMetaObject::invokeMethod(m_colecoController, [this](){
            coleco_set_machine_type(m_machineType);
            coleco_hardreset();
            coleco_reset_and_restart_bios();
        }, Qt::QueuedConnection);
    }

    if (m_machineType == 1) { // ADAM → SGM uitschakelen (zoals je had)
        m_sgmEnabled = false; // sync intern
        if (m_actToggleSGM) m_actToggleSGM->setChecked(false);
        onToggleSGM(false);
    }

    // --- Palette (reeds aanwezig) ---
    if (m_paletteIndex != cfg.palette) {
        m_paletteIndex = cfg.palette;
        if (m_colecoController) {
            QMetaObject::invokeMethod(
                m_colecoController,
                [this]() { coleco_setpalette(m_paletteIndex); },
                Qt::QueuedConnection);
        }
    }

    // --- Additional Hardware ---
    m_sgmEnabled  = (m_machineType == 0) ? cfg.sgmEnabled : false; // SGM uit bij ADAM
    m_f18aEnabled = cfg.f18aEnabled;

    // (optioneel: core-calls als je slots hebt:)
    // QMetaObject::invokeMethod(m_colecoController, "setSGMEnabled",
    //                           Qt::QueuedConnection, Q_ARG(bool, m_sgmEnabled));
    // QMetaObject::invokeMethod(m_colecoController, "setF18AEnabled",
    //                           Qt::QueuedConnection, Q_ARG(bool, m_f18aEnabled));

    // --- Additional Controllers ---
    m_ctrlSteering     = cfg.steeringWheel;
    m_ctrlRoller       = cfg.rollerCtrl;
    m_ctrlSuperAction  = cfg.superAction;

    // (optioneel: core-calls als je slots hebt:)
    // QMetaObject::invokeMethod(m_colecoController, "setSteeringWheelEnabled",
    //                           Qt::QueuedConnection, Q_ARG(bool, m_ctrlSteering));
    // QMetaObject::invokeMethod(m_colecoController, "setRollerControllerEnabled",
    //                           Qt::QueuedConnection, Q_ARG(bool, m_ctrlRoller));
    // QMetaObject::invokeMethod(m_colecoController, "setSuperActionEnabled",
    //                           Qt::QueuedConnection, Q_ARG(bool, m_ctrlSuperAction));

    // --- Statuslabel bijwerken ---
    if (m_sysLabel) m_sysLabel->setText(m_machineType ? "ADAM" : "COLECO");

    // --- Bewaar direct ---
    saveSettings();
}

void MainWindow::showAboutDialog()
{
    QDialog aboutDialog(this);
    aboutDialog.setWindowTitle("About ADAM+");
    aboutDialog.setFixedSize(420, 360);

    QVBoxLayout *layout = new QVBoxLayout(&aboutDialog);
    layout->setAlignment(Qt::AlignCenter);

    // Logo
    QLabel *logoLabel = new QLabel(&aboutDialog);
    QPixmap logo(":/images/images/ADAMP.png");
    logoLabel->setPixmap(logo.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(logoLabel);

    // Tekst
    QLabel *textLabel = new QLabel(&aboutDialog);
    textLabel->setText(
        "Version 0.1.1025<br>"
        "©2025 DannyVdH<br>"
        "<a href='https://github.com/dvdh1961/ADAMP'>VDH Productions</a><br><br>"
        "This program is released under the GNU GPL-3.0 license.<br>"
        "Please see the attached <i>readme.md</i> and <i>license</i> file that should be included with this distribution.<br><br>"
        "The goal is to go even deeper into my ADAM+ hardware project<br>"
        " — this emulator — <br>"
        "will go much further in integrating specific hardware components.<br><br>"
        "Credits goes to.<br>"
        "A lot of interfacing and parts of code based on the EmulTwo project.<br>"
        "Parts of ADAM emulation code from Marat Fayzullin’s ColEm project.<br>"
        "Wavemotion-dave, for improving compatibility issues.<br>"
        "Parts of EightyOne created by Michael D Wynne.<br>"
        "Z80 core taken from FUSE, the free UNIX emulator.<br>"
        "AY8910 code from Z81 ©1995–2001 Russell Marks.<br>"
        "And all the ones that were involved and that I forgot to mention.<br><br>"
        );
    textLabel->setOpenExternalLinks(true);
    textLabel->setWordWrap(true);
    textLabel->setAlignment(Qt::AlignCenter);
    QFont font("Roboto", 10);
    textLabel->setFont(font);
    layout->addWidget(textLabel);

    // OK-knop
    QPushButton *okButton = new QPushButton("OK", &aboutDialog);
    okButton->setFixedWidth(80);
    connect(okButton, &QPushButton::clicked, &aboutDialog, &QDialog::accept);
    layout->addWidget(okButton, 0, Qt::AlignCenter);

    aboutDialog.exec();
}

// Maak deze slot aan in MainWindow
void MainWindow::onShowNameTable(bool checked)
{
    if (checked) {
        m_ntableWindow->show();
    } else {
        m_ntableWindow->hide();
    }
}

void MainWindow::onShowPatternTable(bool checked)
{
    if (checked) {
        m_patternWindow->show();
    } else {
        m_patternWindow->hide();
    }
}

void MainWindow::onShowSpriteTable(bool checked)
{
    if (checked) {
        m_spriteWindow->show();
    } else {
        m_spriteWindow->hide();
    }
}

void MainWindow::onOpenCartInfo()
{
    if (!m_cartInfoDialog) {
        m_cartInfoDialog = new CartridgeInfoDialog(this);
    }

    // Zorg dat de data vers is *voordat* we het tonen
    m_cartInfoDialog->refreshData();

    m_cartInfoDialog->show();
    m_cartInfoDialog->raise();
    m_cartInfoDialog->activateWindow();
}

void MainWindow::onToggleVideoStandard()
{
    bool isNTSC = m_actToggleNTSC->isChecked();
    qDebug() << "UI: Video standard set to" << (isNTSC ? "NTSC" : "PAL");

    // Stuur commando naar de controller-thread
    QMetaObject::invokeMethod(m_colecoController, "setVideoStandard",
                              Qt::QueuedConnection,
                              Q_ARG(bool, isNTSC));
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_logView && event->type() == QEvent::Close) {
        if (m_actShowLog) {
            m_actShowLog->setChecked(false);
        }
        m_logView->hide();
        event->ignore();
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::appendLog(const QString& line)
{
    m_logView->editor()->appendPlainText(line);
}

void MainWindow::onToggleSGM(bool checked)
{
    qDebug() << "UI: Toggle SGM =" << checked;
    QMetaObject::invokeMethod(m_colecoController, "setSGMEnabled",
                              Qt::QueuedConnection,
                              Q_ARG(bool, checked));
    QMetaObject::invokeMethod(m_colecoController, "resetMachine",
                              Qt::QueuedConnection);
}

void MainWindow::onToggleKeyboard(bool on)
{
    if (on) {
        if (!m_inputWidget) {
            m_inputWidget = new InputWidget(this);
            m_inputWidget->attachTo(m_screenWidget);
        }
        m_inputWidget->setOverlayVisible(true);
        m_inputWidget->show();
        m_inputWidget->raise();
        m_inputWidget->setFocus(Qt::OtherFocusReason);
    } else {
        if (m_inputWidget) {
            m_inputWidget->setOverlayVisible(false);
            m_inputWidget->hide();
        }
        if (m_screenWidget) m_screenWidget->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::setupEmulatorThread()
{
    qDebug() << "Thread setup: Aanmaken thread en controller...";

    m_emulatorThread = new QThread(this);
    m_colecoController = new ColecoController();
    m_colecoController->moveToThread(m_emulatorThread);

    connect(m_colecoController, &ColecoController::frameReady,
            m_screenWidget,     &ScreenWidget::setFrame,
            Qt::QueuedConnection);
    connect(m_colecoController, &ColecoController::frameReady,
            this, &MainWindow::onFramePresented,
            Qt::QueuedConnection);
    connect(m_colecoController, SIGNAL(videoStandardChanged(QString)),
            this, SLOT(setVideoStandard(QString)),
            Qt::QueuedConnection);

    // === NIEUWE FPS CONNECTIE ===
    connect(m_colecoController, &ColecoController::fpsUpdated,
            this, &MainWindow::onFpsUpdated,
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::emuPausedChanged,
            this, &MainWindow::onEmuPausedChanged,
            Qt::QueuedConnection);

    // start emulatie als thread start
    connect(m_emulatorThread, &QThread::started,
            m_colecoController, &ColecoController::startEmulation);

    connect(m_colecoController, &ColecoController::frameReady,
            m_screenWidget, &ScreenWidget::updateFrame,
            Qt::QueuedConnection);
    connect(m_colecoController, &ColecoController::emulationStopped,
            this, &MainWindow::onThreadFinished,
            Qt::QueuedConnection);
    connect(m_emulatorThread, &QThread::finished,
            m_colecoController, &QObject::deleteLater);


    coleco_set_machine_type(m_machineType);

    m_emulatorThread->start();

    // Palette éénmalig toepassen zodra het 1e frame binnen is (VDP is init)
    connect(
        m_colecoController, &ColecoController::frameReady,
        this,
        [this](const QImage &) {
            //qDebug() << "frameReady → applying saved palette:" << m_paletteIndex;
            // Zet de call in de emulatorthread (voorkomt cross-thread issues)
            QMetaObject::invokeMethod(
                m_colecoController,
                [this]() { coleco_setpalette(m_paletteIndex); },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection
        );

    qDebug() << "Thread setup: Thread is gestart.";

}

// --- Slots ---

void MainWindow::onOpenRom()
{
    // Gebruik het opgeslagen (relatieve) pad om een absoluut pad te maken
    QString absoluteRomPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + m_romPath);

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open ColecoVision ROM"),
        absoluteRomPath, // Start in de laatst gebruikte map
        tr("ROM Bestanden (*.rom *.col *.bin);;Alle bestanden (*.*)")
        );

    if (filePath.isEmpty()) return;

    // VVVV --- BELANGRIJKE TOEVOEGING --- VVVV
    // Update m_romPath naar de map van het zojuist geopende bestand
    QFileInfo fileInfo(filePath);
    QDir appDir(QCoreApplication::applicationDirPath());
    m_romPath = appDir.relativeFilePath(fileInfo.absolutePath());
    // ^^^^ --- EINDE TOEVOEGING --- ^^^^

    qDebug() << "UI: Gevraagd om ROM te laden:" << filePath;
    qDebug() << "Nieuw relatief ROM pad opgeslagen:" << m_romPath;

    m_currentRomName = fileInfo.fileName();

    if (m_currentRomName.contains("ddp", Qt::CaseInsensitive) ||
        m_currentRomName.contains("dsk", Qt::CaseInsensitive))
        m_sysLabel->setText("ADAM");
    else
        m_sysLabel->setText("COLECO");

    QMetaObject::invokeMethod(m_colecoController, "loadRom",
                              Qt::QueuedConnection,
                              Q_ARG(QString, filePath));
    QMetaObject::invokeMethod(m_colecoController, "resetMachine",
                              Qt::QueuedConnection);
}

void MainWindow::onReset()
{
    qDebug() << "UI: Gevraagd om machine te resetten.";
    QMetaObject::invokeMethod(m_colecoController, "resetMachine",
                              Qt::QueuedConnection);
}

void MainWindow::onhReset()
{
    qDebug() << "UI: Gevraagd om machine te resetten (hard).";
    m_romLabel->setText(QString("No cart"));
    QMetaObject::invokeMethod(m_colecoController, "resethMachine",
                              Qt::QueuedConnection);
}

void MainWindow::onThreadFinished()
{
    qDebug() << "MainWindow: 'emulationStopped' ontvangen.";
    m_emulatorThread->quit();
}

// Elke frame van de controller
void MainWindow::onFramePresented()
{
    // VERWIJDERD: ++m_frameCounter;

    // [DEBUGGER] live refresh als venster open is
    if (m_debugWin && m_debugWin->isVisible()) {
        m_debugWin->updateAllViews();
    }

    if (m_ntableWindow && m_ntableWindow->isVisible()) {
        m_ntableWindow->doRefresh();
    }
}

// VERWIJDERD: onFpsTick()

// === NIEUWE SLOT ===
// Wordt 1x per seconde aangeroepen door de emulator-thread
void MainWindow::onFpsUpdated(int fps)
{
    m_fpsLabel->setText(QString("%1fps").arg(fps));

}


void MainWindow::setVideoStandard(const QString& standard)
{
    const QString upper = standard.trimmed().toUpper();
    if (upper == "NTSC" || upper == "PAL") {
        m_currentStd = upper;
        m_stdLabel->setText(QString("%1").arg(m_currentStd));
    } else {
        m_currentStd = "NTSC";
        m_stdLabel->setText("NTSC");
    }
}

void MainWindow::onOpenDebugger()
{
    if (!m_debugWin) return;
    m_isPaused = true;
    QMetaObject::invokeMethod(m_colecoController, "resumeEmulation",
                              Qt::QueuedConnection);
    m_debugWin->show();

    // Gebruik de nieuwe helperfunctie om de positie te bepalen
    positionDebugger();

    m_debugWin->raise();
    m_debugWin->activateWindow();
    m_debugWin->updateAllViews();
}

void MainWindow::positionDebugger()
{
    // Doe alleen iets als het debug-venster bestaat én zichtbaar is
    if (m_debugWin && m_debugWin->isVisible()) {
        const int gap = 10; // De gevraagde gleuf van 10 pixels
        // Bereken de new X-positie:
        // (X van mainwindow) + (breedte van mainwindow) + (gleuf)
        int newX = this->x() + this->width() + gap;
        // Gebruik de Y-positie van het mainwindow om ze bovenaan uit te lijnen
        int newY = this->y();
        m_debugWin->move(newX, newY);
    }
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    // Roep altijd eerst de basis-implementatie aan
    QMainWindow::moveEvent(event);

    // Roep onze helper aan om de debugger mee te schuiven
    positionDebugger();
}

void MainWindow::onDebuggerStepCPU()
{
    m_isPaused = true;
    QMetaObject::invokeMethod(m_colecoController, "pauseEmulation",
                              Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_colecoController, "stepOnce",
                              Qt::QueuedConnection);
    if (m_debugWin && m_debugWin->isVisible()) {
        m_debugWin->updateAllViews();
    }
}

void MainWindow::onDebuggerRunCPU()
{
    m_isPaused = false;
    QMetaObject::invokeMethod(m_colecoController, "resumeEmulation",
                              Qt::QueuedConnection);
}

void MainWindow::onDebuggerBreakCPU()
{
    QMetaObject::invokeMethod(m_colecoController, "pauseEmulation",
                              Qt::QueuedConnection);
    m_isPaused = true;
    if (m_debugWin && m_debugWin->isVisible()) {
        m_debugWin->updateAllViews();
    }
}

void MainWindow::onRunStop()
{
    m_isPaused = !m_isPaused;
    if (m_isPaused) {
        QMetaObject::invokeMethod(m_colecoController, "pauseEmulation",
                                  Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(m_colecoController, "resumeEmulation",
                                  Qt::QueuedConnection);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "Close Event: Applicatie afsluiten...";

    // Sla de instellingen altijd op bij het afsluiten
    saveSettings();

    if (m_emulatorThread && m_emulatorThread->isRunning())
    {
        qDebug() << "Thread netjes stoppen...";
        connect(m_emulatorThread, &QThread::finished, qApp, &QCoreApplication::quit);
        QMetaObject::invokeMethod(m_colecoController, "stopEmulation", Qt::QueuedConnection);
        m_emulatorThread->quit();
        if (!m_emulatorThread->wait(2000)) {
            qDebug() << "Waarschuwing: Thread wilde niet stoppen, wordt geforceerd.";
            m_emulatorThread->terminate();
            m_emulatorThread->wait();
            qApp->quit();
        } else {
            qDebug() << "Thread netjes gestopt.";
        }
        event->ignore();
    }
    else
    {
        qDebug() << "Geen thread actief, direct sluiten.";
        event->accept();
    }
}

void MainWindow::onEmuPausedChanged(bool paused)
{
    m_isPaused = paused;
    if (m_startAction) {
        if (paused) {
            m_startAction->setText(tr("Run (F9)"));
            m_runLabel->setText("STOP");
        } else {
            m_runLabel->setText("RUN");
            m_startAction->setText(tr("Stop (F9)"));
        }
    }
}

void MainWindow::onStartActionTriggered()
{
    if (!m_colecoController)
        return;

    if (m_isPaused) {
        m_runLabel->setText("STOP");
        QMetaObject::invokeMethod(
            m_colecoController,
            "resumeEmulation",
            Qt::QueuedConnection
            );
    } else {
        m_runLabel->setText("RUN");
        QMetaObject::invokeMethod(
            m_colecoController,
            "pauseEmulation",
            Qt::QueuedConnection
            );
    }
}
