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
#include "coleco.h"
#include "joypadwindow.h"
#include "kbwidget.h"
#include "printwindow.h"

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
    m_kbWidget(nullptr),
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
    m_actAdamKeyboard(nullptr),
    m_actAdamJoystick(nullptr),
    m_adamInputModeJoystick(false),
    m_debugWin(nullptr),
    m_cartInfoDialog(nullptr)
{
    QCoreApplication::setOrganizationName("DVdHSoft");
    QCoreApplication::setApplicationName("ADAMP_EMU");

    // Hier kun je de versie eenvoudig wijzigen
    appVersion = "0.2.1125";

    setWindowTitle(QString("ADAM+ Emulator - v%1").arg(appVersion));

    // --- VOEG DE WALLPAPER-LABEL TOE ---
    m_wallpaperLabel = new QLabel(this);
    // BELANGRIJK: U moet zelf een .png of .jpg toevoegen aan uw resources (qrc)
    // en hier het pad ernaartoe opgeven.
    QPixmap wallpaper(":/images/images/wallpaper_coleco.png");
    m_wallpaperLabel->setPixmap(wallpaper);
    m_wallpaperLabel->setScaledContents(true); // Rek de afbeelding uit
    m_wallpaperLabel->hide(); // Verberg standaard
    // --- EINDE TOEVOEGING ---

    m_screenWidget = new ScreenWidget(this);
    //m_screenWidget->setScale(2.9);

    m_logoLabel = new QLabel(this);
    QPixmap logoPixmap(":/images/images/adamp_logo.png");
    m_logoLabel->setPixmap(logoPixmap);
    m_logoLabel->installEventFilter(this);
    m_logoLabel->setCursor(Qt::PointingHandCursor);

    m_logoLabel->setAttribute(Qt::WA_TranslucentBackground);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0); // Geen witruimte rondom

    // 1. Voeg het spelscherm toe met een stretch factor van 1
    //    Dit betekent dat het alle beschikbare verticale ruimte zal innemen.
    mainLayout->addWidget(m_screenWidget, 1);

    // 2. Voeg het logo toe met een stretch factor van 0
    //    en lijn het onderaan en in het midden uit.
    mainLayout->addWidget(m_logoLabel, 0, Qt::AlignHCenter | Qt::AlignBottom);


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

    // Maak de *hele* centrale container transparant
    centralContainer->setAttribute(Qt::WA_TranslucentBackground); // <-- VOEG DIT TOE

    // 5. Stel de container in als de central widget
    setCentralWidget(centralContainer);

    m_wallpaperLabel->lower();

    m_inputWidget = new InputWidget(this);
    m_inputWidget->attachTo(m_screenWidget);
    m_inputWidget->setFocusPolicy(Qt::NoFocus);
    m_inputWidget->setOverlayVisible(false); // Verberg de overlay...
    m_inputWidget->show();                   // ...maar toon de (transparante) widget
    m_inputWidget->raise();

    setUpLogWindow();

    this->setFixedWidth(770);
    this->setFixedHeight(700);

    configurePlatformSettings();

    setStatusBar();

    loadSettings();
    // Menu's/acties
    setupUI();

    m_screenWidget->setScalingMode(static_cast<ScalingMode>(m_scalingMode));

    if (m_sysLabel) {
        m_sysLabel->setText(m_machineType ? "ADAM" : "COLECO");
    }

    // Pas de geladen instellingen toe op de UI bij het opstarten ---
    HardwareConfig initialConfig;
    initialConfig.machine = (m_machineType ? MACHINE_ADAM : MACHINE_COLECO);
    initialConfig.palette = m_paletteIndex;
    initialConfig.sgmEnabled = m_sgmEnabled;
    initialConfig.f18aEnabled = m_f18aEnabled;
    initialConfig.steeringWheel = m_ctrlSteering;
    initialConfig.rollerCtrl = m_ctrlRoller;
    initialConfig.superAction = m_ctrlSuperAction;

    // Deze functie zal nu de menu-items correct instellen (enabled/disabled)
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

    // 1. controller + thread klaarzetten
    setupEmulatorThread();

    // (Moet NA setupEmulatorThread() komen)
    m_kbWidget = new KbWidget(this);
    m_kbWidget->setController(m_colecoController); // m_colecoController is nu geldig

    // --- Connecteer de nieuwe media-signalen ---
    connect(m_colecoController, &ColecoController::tapeStatusChanged,
            this, &MainWindow::onTapeStatusChanged,
            Qt::QueuedConnection);
    connect(m_colecoController, &ColecoController::diskStatusChanged,
            this, &MainWindow::onDiskStatusChanged,
            Qt::QueuedConnection);

    // 2. debugger
    // --- Koppel de controller aan de debugger ---
    m_debugWin = new DebuggerWindow(this);
    m_debugWin->setController(m_colecoController);

    // Zorg ervoor dat de debugger het pad kent *direct na het aanmaken*.
    m_debugWin->setBreakpointPath(m_breakpointPath);

    connect(m_debugWin, &DebuggerWindow::requestStepCPU,
            this,       &MainWindow::onDebuggerStepCPU);
    connect(m_debugWin, &DebuggerWindow::requestRunCPU,
            this,       &MainWindow::onDebuggerRunCPU);
    connect(m_debugWin, &DebuggerWindow::requestBreakCPU,
            this,       &MainWindow::onDebuggerBreakCPU);
    connect(m_debuggerAction, &QAction::triggered,
            this, &MainWindow::onOpenDebugger);

    // --- VOEG DIT BLOK TOE ---
    // Pas de geladen full-screen instelling toe bij het opstarten
    if (m_startFullScreen) {
        // Gebruik een timer om dit te doen nadat het venster
        // volledig is getoond en de event loop draait.
        QTimer::singleShot(0, this, [this]() {
            // We hoeven de actie niet te 'checken', we roepen de functie
            // gewoon direct aan die de full-screen logica uitvoert.
            onToggleFullScreen(true);
            // Zorg dat de actie-knop zelf ook 'checked' is
            if(m_actFullScreen) m_actFullScreen->setChecked(true);
        });
    }

    QTimer::singleShot(0, this, [this]() {
        if (m_screenWidget) {
            m_screenWidget->setFocus(Qt::OtherFocusReason);
        }
    });
}

MainWindow::~MainWindow()
{
    if (m_emulatorThread) {
        m_emulatorThread->quit();
        m_emulatorThread->wait(1000);
    }
}

// (We gebruiken deze nu niet meer direct, maar het is goed om hem te hebben)
void MainWindow::setDebugger(DebuggerWindow *debugger)
{
    m_debugWin        = debugger;
}

void MainWindow::onCycleScalingMode()
{
    // 0=Sharp, 1=Smooth, 2=EPX
    // Fiets naar de volgende modus: 0 -> 1 -> 2 -> 0
    m_scalingMode = (m_scalingMode + 1) % 3;

    QString scaleText;
    if (m_scalingMode == 0) { // ModeSharp
        scaleText = "Scaling: Sharp";
    } else if (m_scalingMode == 1) { // ModeSmooth
        scaleText = "Scaling: Smooth";
    } else { // ModeEPX
        scaleText = "Scaling: EPX";
    }

    // 1. Update de menu tekst
    m_actToggleSmoothing->setText(scaleText);

    // 2. Stuur de wijziging door naar de widget
    if (m_screenWidget) {
        m_screenWidget->setScalingMode(static_cast<ScalingMode>(m_scalingMode));
    }

    // 3. Sla de nieuwe instelling op
    saveSettings();
}

// In mainwindow.cpp

void MainWindow::onOpenSettings()
{
    // 1. Laad de huidige instellingen in het dialoogvenster
    //    We geven nog steeds de (mogelijk relatieve) paden mee
    m_settingsWindow->setRomPath(m_romPath);
    m_settingsWindow->setDiskPath(m_diskPath);
    m_settingsWindow->setTapePath(m_tapePath);
    m_settingsWindow->setStatePath(m_statePath);
    m_settingsWindow->setBreakpointPath(m_breakpointPath);

    // 2. Toon het dialoogvenster modaal
    if (m_settingsWindow->exec() == QDialog::Accepted) {

        // 3. Haal de waarden op (dit zijn waarschijnlijk absolute paden
        //    als de gebruiker een "Browse" knop heeft gebruikt)
        QString newRomPath = m_settingsWindow->romPath();
        QString newDiskPath = m_settingsWindow->diskPath();
        QString newTapePath = m_settingsWindow->tapePath();
        QString newStatePath = m_settingsWindow->statePath();
        QString newBreakpointPath = m_settingsWindow->breakpointPath();

        // --- HIER IS DE FIX ---
        // Converteer deze paden terug naar relatieve paden
        // (relatief aan de .exe-map) voordat we ze opslaan.

        QDir appDir(QCoreApplication::applicationDirPath());

        // Zorg ervoor dat paden 'schoon' zijn (geen /../ of C://)
        // en controleer of ze bestaan voordat we ze relativeren.
        QFileInfo romInfo(newRomPath);
        if (romInfo.exists() && romInfo.isDir()) {
            m_romPath = appDir.relativeFilePath(newRomPath);
        } else {
            m_romPath = newRomPath; // Behoud de getypte waarde (bv. ".")
        }

        QFileInfo diskInfo(newDiskPath);
        if (diskInfo.exists() && diskInfo.isDir()) {
            m_diskPath = appDir.relativeFilePath(newDiskPath);
        } else {
            m_diskPath = newDiskPath;
        }

        QFileInfo tapeInfo(newTapePath);
        if (tapeInfo.exists() && tapeInfo.isDir()) {
            m_tapePath = appDir.relativeFilePath(newTapePath);
        } else {
            m_tapePath = newTapePath;
        }

        QFileInfo stateInfo(newStatePath);
        if (stateInfo.exists() && stateInfo.isDir()) {
            m_statePath = appDir.relativeFilePath(newStatePath);
        } else {
            m_statePath = newStatePath;
        }

        QFileInfo bpInfo(newBreakpointPath);
        if (bpInfo.exists() && bpInfo.isDir()) {
            m_breakpointPath = appDir.relativeFilePath(newBreakpointPath);
        } else {
            m_breakpointPath = newBreakpointPath;
        }

        // Update de debugger *live* met het nieuwe pad
        if (m_debugWin) {
            m_debugWin->setBreakpointPath(m_breakpointPath);
        }

        // 4. Sla de (nu relatieve) paden op.
        saveSettings();
    }
}

// Laadt de instellingen
// ---------------------------------------------------------
// Vervang de bestaande loadSettings() door deze versie:
// ---------------------------------------------------------
void MainWindow::loadSettings()
{
    // Bepaal het pad naar settings.ini naast de executable
    QString iniPath = QCoreApplication::applicationDirPath() + "/settings.ini";

    // Gebruik IniFormat en geef het specifieke pad mee
    QSettings settings(iniPath, QSettings::IniFormat);

    m_romPath      = settings.value("romPath", ".").toString();
    m_diskPath     = settings.value("diskPath", ".").toString();
    m_tapePath     = settings.value("tapePath", ".").toString();
    m_statePath    = settings.value("statePath", ".").toString();
    m_breakpointPath = settings.value("breakpointPath", "media/breakpoints").toString();

    m_paletteIndex = settings.value("video/palette", 0).toInt();
    m_machineType  = settings.value("machine/type", 0).toInt();

    // Nieuw: Additional Hardware
    m_sgmEnabled  = settings.value("hardware/sgm",  false).toBool();
    m_f18aEnabled = settings.value("hardware/f18a", false).toBool();

    // Nieuw: Additional Controllers
    m_ctrlSteering    = settings.value("controller/steering",    false).toBool();
    m_ctrlRoller      = settings.value("controller/roller",      false).toBool();
    m_ctrlSuperAction = settings.value("controller/superaction", false).toBool();

    m_scalingMode = settings.value("video/scalingMode", 1).toInt();
    m_startFullScreen = settings.value("video/fullscreen", false).toBool();

    qDebug() << "Loading settings from:" << iniPath;
    qDebug() << "Loaded settings:"
             << "machine="  << m_machineType
             << "palette="  << m_paletteIndex
             << "sgm="      << m_sgmEnabled
             << "f18a="     << m_f18aEnabled
             << "steer="    << m_ctrlSteering
             << "roller="   << m_ctrlRoller
             << "saction="  << m_ctrlSuperAction
             << "bppath="   << m_breakpointPath;
}

// Slaat de instellingen op
void MainWindow::saveSettings()
{
    // Bepaal het pad naar settings.ini naast de executable
    QString iniPath = QCoreApplication::applicationDirPath() + "/settings.ini";

    // Gebruik IniFormat en geef het specifieke pad mee
    QSettings settings(iniPath, QSettings::IniFormat);

    settings.setValue("romPath",        m_romPath);
    settings.setValue("diskPath",       m_diskPath);
    settings.setValue("tapePath",       m_tapePath);
    settings.setValue("statePath",      m_statePath);
    settings.setValue("breakpointPath", m_breakpointPath);
    settings.setValue("video/palette",  m_paletteIndex);
    settings.setValue("machine/type",   m_machineType);

    // Nieuw: Additional Hardware
    settings.setValue("hardware/sgm",   m_sgmEnabled);
    settings.setValue("hardware/f18a",  m_f18aEnabled);

    // Nieuw: Additional Controllers
    settings.setValue("controller/steering",    m_ctrlSteering);
    settings.setValue("controller/roller",      m_ctrlRoller);
    settings.setValue("controller/superaction", m_ctrlSuperAction);

    settings.setValue("video/scalingMode", m_scalingMode);
    settings.setValue("video/fullscreen", m_startFullScreen);

    // Optioneel: Forceer schrijven naar schijf (gebeurt normaal ook bij destructor)
    settings.sync();

    qDebug() << "Settings saved to:" << iniPath
             << "palette=" << m_paletteIndex
             << "machine=" << m_machineType
             << "sgm="     << m_sgmEnabled
             << "f18a="    << m_f18aEnabled
             << "steer="   << m_ctrlSteering
             << "roller="  << m_ctrlRoller
             << "saction=" << m_ctrlSuperAction
             << "bppath="  << m_breakpointPath;
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
    Qt::WindowFlags flags = windowFlags();

    // 1. Verwijder de knoppen (Maximize, Minimize, Close)
    flags &= ~Qt::WindowMaximizeButtonHint;
    flags &= ~Qt::WindowMinimizeButtonHint;
    flags &= ~Qt::WindowCloseButtonHint; // <-- Verberg het sluitkruisje

    // 2. Voeg de vlag toe die Qt toelaat de knoppen te customizen/verbergen
    flags |= Qt::CustomizeWindowHint;

    // 3. Wijs de nieuwe, correct getypete vlaggen toe
    setWindowFlags(flags);

    this->setFixedWidth(770);

    // --- NIEUWE LABEL: systeemnaam ---
    m_sysLabel = new QLabel("COLECO", this);
    m_sysLabel->setObjectName("sysLabel");
    m_sysLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_sysLabel->setMinimumWidth(80);
    m_sysLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    // --- NIEUWE LABELS: SGM ---
    m_sepLabelSGM = new QLabel("|", this);
    m_sgmLabel = new QLabel("SGM", this);
    m_sgmLabel->setObjectName("sgmLabel");
    m_sgmLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_sgmLabel->setMinimumWidth(40);
    m_sgmLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_sgmLabel->hide(); // Standaard verborgen
    m_sepLabelSGM->hide(); // Standaard verborgen
    // --- EINDE NIEUW ---

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

    m_sepLabelMedia2a = new QLabel("|", this);
    m_tapeLabelA = new QLabel("D1: -", this);
    m_tapeLabelA->setObjectName("tapeLabel");
    m_tapeLabelA->setMinimumWidth(50);

    m_sepLabelMedia2b = new QLabel("|", this);
    m_tapeLabelB = new QLabel("D2: -", this);
    m_tapeLabelB->setObjectName("tapeLabel");
    m_tapeLabelB->setMinimumWidth(50);

    m_sepLabelMedia2c = new QLabel("|", this);
    m_tapeLabelC = new QLabel("D3: -", this);
    m_tapeLabelC->setObjectName("tapeLabel");
    m_tapeLabelC->setMinimumWidth(50);

    m_sepLabelMedia2d = new QLabel("|", this);
    m_tapeLabelD = new QLabel("D4: -", this);
    m_tapeLabelD->setObjectName("tapeLabel");
    m_tapeLabelD->setMinimumWidth(50);

    m_sepLabelMedia1a = new QLabel("|", this);
    m_diskLabelA = new QLabel("D5: -", this);
    m_diskLabelA->setObjectName("diskLabel");
    m_diskLabelA->setMinimumWidth(50); // Geef het wat ruimte

    m_sepLabelMedia1b = new QLabel("|", this);
    m_diskLabelB = new QLabel("D6: -", this);
    m_diskLabelB->setObjectName("diskLabel");
    m_diskLabelB->setMinimumWidth(50); // Geef het wat ruimte

    m_sepLabelMedia1c = new QLabel("|", this);
    m_diskLabelC = new QLabel("D7: -", this);
    m_diskLabelC->setObjectName("diskLabel");
    m_diskLabelC->setMinimumWidth(50); // Geef het wat ruimte

    m_sepLabelMedia1d = new QLabel("|", this);
    m_diskLabelD = new QLabel("D8: -", this);
    m_diskLabelD->setObjectName("diskLabel");
    m_diskLabelD->setMinimumWidth(50); // Geef het wat ruimte

    // Verberg ze standaard (worden getoond in ADAM-modus)
    m_sepLabelMedia2a->hide();
    m_tapeLabelA->hide();

    m_sepLabelMedia2b->hide();
    m_tapeLabelB->hide();

    m_sepLabelMedia2c->hide();
    m_tapeLabelC->hide();

    m_sepLabelMedia2d->hide();
    m_tapeLabelD->hide();

    m_sepLabelMedia1a->hide();
    m_diskLabelA->hide();

    m_sepLabelMedia1b->hide();
    m_diskLabelB->hide();

    m_sepLabelMedia1c->hide();
    m_diskLabelC->hide();

    m_sepLabelMedia1d->hide();
    m_diskLabelD->hide();

    m_stdLabel = new QLabel(this);

    m_romLabel = new QLabel("No cart", this);
    m_romLabel->setObjectName("romLabel");
    m_romLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_romLabel->setFixedWidth(480);

    m_sepLabel1 = new QLabel("|", this);
    m_sepLabel2 = new QLabel("|", this);
    m_sepLabel3 = new QLabel("|", this);
    m_sepLabel4 = new QLabel("|", this);

    // Volgorde in statusbar: systeem | std | fps | run | rom
    statusBar()->addWidget(m_sysLabel);
    statusBar()->addWidget(m_sepLabelSGM);
    statusBar()->addWidget(m_sgmLabel);

    statusBar()->addWidget(m_sepLabelMedia2a);
    statusBar()->addWidget(m_tapeLabelA);
    statusBar()->addWidget(m_sepLabelMedia2b);
    statusBar()->addWidget(m_tapeLabelB);
    statusBar()->addWidget(m_sepLabelMedia2c);
    statusBar()->addWidget(m_tapeLabelC);
    statusBar()->addWidget(m_sepLabelMedia2d);
    statusBar()->addWidget(m_tapeLabelD);

    statusBar()->addWidget(m_sepLabelMedia1a);
    statusBar()->addWidget(m_diskLabelA);
    statusBar()->addWidget(m_sepLabelMedia1b);
    statusBar()->addWidget(m_diskLabelB);
    statusBar()->addWidget(m_sepLabelMedia1c);
    statusBar()->addWidget(m_diskLabelC);
    statusBar()->addWidget(m_sepLabelMedia1d);
    statusBar()->addWidget(m_diskLabelD);

    statusBar()->addWidget(m_sepLabel1);
    statusBar()->addWidget(m_stdLabel);
    statusBar()->addWidget(m_sepLabel1);
    statusBar()->addWidget(m_stdLabel);
    statusBar()->addWidget(m_sepLabel2);
    statusBar()->addWidget(m_fpsLabel);
    statusBar()->addWidget(m_sepLabel3);
    statusBar()->addWidget(m_runLabel);
    statusBar()->addWidget(m_sepLabel4);
    statusBar()->addWidget(m_romLabel);

    QFont statusFont("Roboto", 9);
    statusFont.setBold(false);

    m_sysLabel->setFont(statusFont);
    m_sgmLabel->setFont(statusFont);
    m_sepLabelSGM->setFont(statusFont);
    m_tapeLabelA->setFont(statusFont);
    m_tapeLabelB->setFont(statusFont);
    m_tapeLabelC->setFont(statusFont);
    m_tapeLabelD->setFont(statusFont);
    m_diskLabelA->setFont(statusFont);
    m_diskLabelB->setFont(statusFont);
    m_diskLabelC->setFont(statusFont);
    m_diskLabelD->setFont(statusFont);
    m_sepLabelMedia2a->setFont(statusFont);
    m_sepLabelMedia2b->setFont(statusFont);
    m_sepLabelMedia2c->setFont(statusFont);
    m_sepLabelMedia2d->setFont(statusFont);
    m_sepLabelMedia1a->setFont(statusFont);
    m_sepLabelMedia1b->setFont(statusFont);
    m_sepLabelMedia1c->setFont(statusFont);
    m_sepLabelMedia1d->setFont(statusFont);
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
    QMenu* fileMenu = menuBar()->addMenu(tr("File"));
    m_openRomAction = new QAction(tr("Cartridge"), this);
    m_openRomAction->setShortcut(QKeySequence::Open);
    connect(m_openRomAction, &QAction::triggered, this, &MainWindow::onOpenRom);
    fileMenu->addAction(m_openRomAction);

    // --- ADAM MEDIA SECTIE TOEVOEGEN ---
    fileMenu->addSeparator();

    // drive 1. Maak Tape Submenu
    m_tapeMenuA = new QMenu(tr("Tape D1"), this);
    m_loadTapeActionA = new QAction(tr("Load"), this);
    connect(m_loadTapeActionA, &QAction::triggered, this, [this]() { onLoadTape(0); });
    m_tapeMenuA->addAction(m_loadTapeActionA);

    m_ejectTapeActionA = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionA, &QAction::triggered, this, [this]() { onEjectTape(0); });
    m_tapeMenuA->addAction(m_ejectTapeActionA);
    fileMenu->addMenu(m_tapeMenuA); // Voeg het submenu toe aan File

    // drive 2. Maak Tape Submenu
    m_tapeMenuB = new QMenu(tr("Tape D2"), this);
    m_loadTapeActionB = new QAction(tr("Load"), this);
    connect(m_loadTapeActionB, &QAction::triggered, this, [this]() { onLoadTape(1); });
    m_tapeMenuB->addAction(m_loadTapeActionB);

    m_ejectTapeActionB = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionB, &QAction::triggered, this, [this]() { onEjectTape(1); });
    m_tapeMenuB->addAction(m_ejectTapeActionB);
    fileMenu->addMenu(m_tapeMenuB); // Voeg het submenu toe aan File

    // drive 3. Maak Tape Submenu
    m_tapeMenuC = new QMenu(tr("Tape D3"), this);
    m_loadTapeActionC = new QAction(tr("Load"), this);
    connect(m_loadTapeActionC, &QAction::triggered, this, [this]() { onLoadTape(2); });
    m_tapeMenuC->addAction(m_loadTapeActionC);

    m_ejectTapeActionC = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionC, &QAction::triggered, this, [this]() { onEjectTape(2); });
    m_tapeMenuC->addAction(m_ejectTapeActionC);
    fileMenu->addMenu(m_tapeMenuC); // Voeg het submenu toe aan File

    // drive 4. Maak Tape Submenu
    m_tapeMenuD = new QMenu(tr("Tape D4"), this);
    m_loadTapeActionD = new QAction(tr("Load"), this);
    connect(m_loadTapeActionD, &QAction::triggered, this, [this]() { onLoadTape(3); });
    m_tapeMenuD->addAction(m_loadTapeActionD);

    m_ejectTapeActionD = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionD, &QAction::triggered, this, [this]() { onEjectTape(3); });
    m_tapeMenuD->addAction(m_ejectTapeActionD);
    fileMenu->addMenu(m_tapeMenuD); // Voeg het submenu toe aan File

    // drive 5. Maak Disk 5 Submenu
    m_diskMenuA = new QMenu(tr("Disk  D5"), this);
    m_loadDiskActionA = new QAction(tr("Load"), this);
    connect(m_loadDiskActionA, &QAction::triggered, this, [this]() { onLoadDisk(0); });
    m_diskMenuA->addAction(m_loadDiskActionA);

    m_ejectDiskActionA = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionA, &QAction::triggered, this, [this]() { onEjectDisk(0); });
    m_diskMenuA->addAction(m_ejectDiskActionA);
    fileMenu->addMenu(m_diskMenuA); // Voeg het submenu toe aan File

    // drive 6. Maak Disk 6 Submenu
    m_diskMenuB = new QMenu(tr("Disk  D6"), this);
    m_loadDiskActionB = new QAction(tr("Load"), this);
    connect(m_loadDiskActionB, &QAction::triggered, this, [this]() { onLoadDisk(1); });
    m_diskMenuB->addAction(m_loadDiskActionB);

    m_ejectDiskActionB = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionB, &QAction::triggered, this, [this]() { onEjectDisk(1); });
    m_diskMenuB->addAction(m_ejectDiskActionB);
    fileMenu->addMenu(m_diskMenuB); // Voeg het submenu toe aan File

    // drive 7. Maak Disk 7 Submenu
    m_diskMenuC = new QMenu(tr("Disk  D7"), this);
    m_loadDiskActionC = new QAction(tr("Load"), this);
    connect(m_loadDiskActionC, &QAction::triggered, this,  [this]() { onLoadDisk(2); });
    m_diskMenuC->addAction(m_loadDiskActionC);

    m_ejectDiskActionC = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionC, &QAction::triggered, this, [this]() { onEjectDisk(2); });
    m_diskMenuC->addAction(m_ejectDiskActionC);
    fileMenu->addMenu(m_diskMenuC); // Voeg het submenu toe aan File

    // drive 8. Maak Disk 8 Submenu
    m_diskMenuD = new QMenu(tr("Disk  D8"), this);
    m_loadDiskActionD = new QAction(tr("Load"), this);
    connect(m_loadDiskActionD, &QAction::triggered, this,  [this]() { onLoadDisk(3); });
    m_diskMenuD->addAction(m_loadDiskActionD);

    m_ejectDiskActionD = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionD, &QAction::triggered, this, [this]() { onEjectDisk(3); });
    m_diskMenuD->addAction(m_ejectDiskActionD);
    fileMenu->addMenu(m_diskMenuD); // Voeg het submenu toe aan File

    // Standaard uitgeschakeld
    m_diskMenuA->setEnabled(false);
    m_diskMenuB->setEnabled(false);
    m_diskMenuC->setEnabled(false);
    m_diskMenuD->setEnabled(false);
    m_tapeMenuA->setEnabled(false);
    m_tapeMenuB->setEnabled(false);
    m_tapeMenuC->setEnabled(false);
    m_tapeMenuD->setEnabled(false);

    fileMenu->addSeparator();

    // --- SAVE/LOAD STATE ---
    m_actSaveState = new QAction(tr("Save State..."), this);
    connect(m_actSaveState, &QAction::triggered, this, &MainWindow::onSaveState);
    fileMenu->addAction(m_actSaveState);

    m_actLoadState = new QAction(tr("Load State..."), this);
    connect(m_actLoadState, &QAction::triggered, this, &MainWindow::onLoadState);
    fileMenu->addAction(m_actLoadState);

    m_actSaveState->setEnabled(false);
    m_actLoadState->setEnabled(false);

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
    QMenu* debugMenu = menuBar()->addMenu(tr("Debug"));
    m_startAction = new QAction(tr("Run/Stop"), this);
    m_startAction->setShortcut(Qt::Key_F11);
    connect(m_startAction, &QAction::triggered, this, &MainWindow::onRunStop);
    debugMenu->addAction(m_startAction);
    debugMenu->addSeparator();
    m_resetAction = new QAction(tr("&Soft Reset"), this);
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

    QAction* actClearLog = new QAction(tr("Clear Logger"), this);

    connect(actClearLog, &QAction::triggered, this, [this]() {
        if (m_logView) {
            m_logView->clear(); // <-- Roep de nieuwe slot aan op m_logView
        }
    });
    debugMenu->addAction(actClearLog);

    debugMenu->addSeparator();
    m_debuggerAction = new QAction(tr("Debugger"), this);
    //m_debuggerAction->setShortcut(Qt::Key_F10);
    debugMenu->addAction(m_debuggerAction);


    // Tools
    QMenu* toolsMenu = menuBar()->addMenu(tr("Tools"));
    m_actToggleKeyboard = new QAction(tr("Keypad"), this);
    m_actToggleKeyboard->setCheckable(true);
    m_actToggleKeyboard->setChecked(false);
    //m_actToggleKeyboard->setShortcut(Qt::Key_F10);
    toolsMenu->addAction(m_actToggleKeyboard);
    m_actJoypadMapper = new QAction(tr("Keypad mapper"), this);
    toolsMenu->addAction(m_actJoypadMapper);
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
    // Probeer bestaande "Tools" te vinden
    for (auto* m : menuBar()->findChildren<QMenu*>()) {
        if (m->title().contains("Tools", Qt::CaseInsensitive)) { toolsMenu = m; break; }
    }
    // Of maak er één aan
    if (!toolsMenu) toolsMenu = menuBar()->addMenu("&Tools");

    // Actie toevoegen
    m_actPrinterOutput = toolsMenu->addAction("Printer Output...", this, &MainWindow::onShowPrinterWindow);
    m_actPrinterOutput->setShortcut(QKeySequence("Ctrl+Shift+P"));

    // Options
    QMenu* optionsMenu = menuBar()->addMenu(tr("Options"));
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

    m_actToggleSmoothing = new QAction(this);
    m_actToggleSmoothing->setCheckable(false);

    // Stel de initiële tekst in op basis van de geladen modus
    if (m_scalingMode == 0) { // ModeSharp
        m_actToggleSmoothing->setText("Scaling: Sharp");
    } else if (m_scalingMode == 2) { // ModeEPX
        m_actToggleSmoothing->setText("Scaling: EPX");
    } else { // ModeSmooth (default)
        m_actToggleSmoothing->setText("Scaling: Smooth");
    }
    optionsMenu->addAction(m_actToggleSmoothing);
    optionsMenu->addSeparator();
    m_actFullScreen = new QAction(tr("Full Screen"), this);
    m_actFullScreen->setCheckable(true);
    m_actFullScreen->setChecked(m_startFullScreen);
    // Use Alt+Enter as a common shortcut
    m_actFullScreen->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    optionsMenu->addAction(m_actFullScreen);

    optionsMenu->addSeparator();
    m_actSaveScreenshot = new QAction(tr("Save Screenshot..."), this);
    optionsMenu->addAction(m_actSaveScreenshot);
    m_actSaveScreenshot->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));

    optionsMenu->addSeparator();
    m_actHardware = new QAction(tr("Hardware..."), this);
    optionsMenu->addAction(m_actHardware);
    connect(m_actHardware, &QAction::triggered, this, &MainWindow::onOpenHardware);

    optionsMenu->addSeparator();

    m_adamInputMenu = optionsMenu->addMenu(tr("ADAM Input Mode"));
    m_adamInputGroup = new QActionGroup(this);
    m_adamInputGroup->setExclusive(true);

    m_actAdamKeyboard = new QAction(tr("ADAM Keyboard (Typing)"), this);
    m_actAdamKeyboard->setCheckable(true);
    m_actAdamKeyboard->setChecked(true); // Standaard
    m_adamInputGroup->addAction(m_actAdamKeyboard);
    m_adamInputMenu->addAction(m_actAdamKeyboard);

    m_actAdamJoystick = new QAction(tr("Coleco Joystick (Games)"), this);
    m_actAdamJoystick->setCheckable(true);
    m_actAdamJoystick->setChecked(false);
    m_adamInputGroup->addAction(m_actAdamJoystick);
    m_adamInputMenu->addAction(m_actAdamJoystick);

    // Schakel standaard uit (wordt ingeschakeld in ADAM-modus)
    m_adamInputMenu->setEnabled(false);

    connect(m_actAdamKeyboard, &QAction::triggered, this, &MainWindow::onAdamInputModeChanged);
    connect(m_actAdamJoystick, &QAction::triggered, this, &MainWindow::onAdamInputModeChanged);

    // Help
    QMenu* helpMenu = menuBar()->addMenu(tr("Help"));
    m_actWiki = new QAction(tr("Github page"), this);
    helpMenu->addAction(m_actWiki);
    m_actReport = new QAction(tr("Report a bug"), this);
    helpMenu->addAction(m_actReport);
    //m_actChat = new QAction(tr("Chat with community"), this);
    //helpMenu->addAction(m_actChat);
    helpMenu->addSeparator();
    m_actDonate = new QAction(tr("Donate"), this);
    helpMenu->addAction(m_actDonate);
    helpMenu->addSeparator();
    m_actAbout = new QAction(tr("About"), this);
    helpMenu->addAction(m_actAbout);


    // Connects
    connect(m_actJoypadMapper, &QAction::triggered, this, &MainWindow::onOpenJoypadMapper);

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

    connect(m_actFullScreen, &QAction::toggled, this, &MainWindow::onToggleFullScreen);
    connect(m_actToggleSmoothing, &QAction::triggered, this, &MainWindow::onCycleScalingMode);

    connect(m_actSaveScreenshot, &QAction::triggered,this, &MainWindow::onSaveScreenshot);

    connect(m_actWiki, &QAction::triggered, this, [](){
        QString link = "https://github.com/dvdh1961/ADAMP";
        QDesktopServices::openUrl(QUrl(link));
    });

    connect(m_actReport, &QAction::triggered, this, [](){
        QString link = "https://github.com/dvdh1961/ADAMP/issues";
        QDesktopServices::openUrl(QUrl(link));
    });

    connect(m_actDonate, &QAction::triggered, this, [](){
        QString link = "https://www.paypal.com/donate?business=dannyvdh@pandora.be";
        QDesktopServices::openUrl(QUrl(link));
    });

    onEmuPausedChanged(false); // of true, afhankelijk van je default
}

void MainWindow::onSaveScreenshot()
{
    if (!m_screenWidget)
        return;

    // 1. Neem screenshot van alleen het spel-scherm
    QPixmap pix = m_screenWidget->grab();
    if (pix.isNull()) {
        qWarning() << "Screenshot: nothing to grab from m_screenWidget";
        return;
    }

    // 2. Standaard map: <appdir>/screenshots
    QString absoluteShotPath =
        QDir::cleanPath(QCoreApplication::applicationDirPath() + "/screenshots");

    QDir shotDir(absoluteShotPath);
    if (!shotDir.exists())
        shotDir.mkpath(".");

    // 3. Basisnaam: huidige ROM of "screen"
    QString baseName = m_currentRomName;
    if (baseName.isEmpty())
        baseName = "screen";

    QFileInfo fi(baseName);
    baseName = fi.completeBaseName();

    // 4. Timestamp toevoegen
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

    // 5. Volledige default bestandsnaam
    QString defaultFile = shotDir.filePath(baseName + "_" + timestamp + ".png");

    // 6. Dialoog tonen (zelfde CustomFileDialog als bij Save State)
    const QString filePath = CustomFileDialog::getSaveFileName(
        this,
        tr("Save Screenshot"),
        defaultFile,
        tr("PNG Image (*.png);;All Files (*.*)")
        );

    if (filePath.isEmpty())
        return;

    QString finalPath = filePath;
    if (!finalPath.endsWith(".png", Qt::CaseInsensitive))
        finalPath += ".png";

    // 7. Bewaren
    if (!pix.save(finalPath, "PNG")) {
        qWarning() << "Failed to save screenshot to" << finalPath;
    } else {
        qDebug() << "Screenshot saved to" << finalPath;
    }
}

void MainWindow::onToggleFullScreen(bool checked)
{
    m_startFullScreen = checked;

    if (checked) {
        // --- GA NAAR GEMAXIMALISEERD VENSTER ---

        // Verwijder de vaste grootte zodat maximaliseren werkt
        this->setMinimumSize(QSize(0, 0));
        this->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));

        // Maximaliseer het venster
        showMaximized();
        updateFullScreenWallpaper();
        m_wallpaperLabel->show(); // <-- TOON DE WALLPAPER
        m_screenWidget->setFullScreenMode(true); // <-- ZET SCREENWIDGET TRANSPARANT
    } else {
        // --- TERUG NAAR NORMAAL VENSTER ---

        // Herstel normale venstermodus
        showNormal();

        m_wallpaperLabel->hide(); // <-- VERBERG DE WALLPAPER
        m_screenWidget->setFullScreenMode(false); // <-- ZET SCREENWIDGET OPAAK
        // Pas de vaste grootte opnieuw toe
        this->setFixedWidth(770);
        this->setFixedHeight(700);
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    // Zorg dat de wallpaper-label altijd de grootte van het hoofdvenster heeft
    if (m_wallpaperLabel) {
        m_wallpaperLabel->setGeometry(this->rect());
    }
    QMainWindow::resizeEvent(event);
}

void MainWindow::onOpenJoypadMapper()
{
    JoypadWindow dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        if (m_inputWidget) m_inputWidget->reloadMappings(); // ⟵ hier updaten
    }
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
    // 1) Machine type (0=Coleco, 1=ADAM)
    const int newMachineType = (cfg.machine == MACHINE_ADAM ? 1 : 0);
    if (m_machineType != newMachineType) {
        m_machineType = newMachineType;

        // Alleen via de controller; die roept coleco_initialise() (zie patch 1)
        QMetaObject::invokeMethod(
            m_colecoController, "setMachineType",
            Qt::QueuedConnection, Q_ARG(int, m_machineType)
            );
    }

    // 2) Palette doorzetten
    if (m_paletteIndex != cfg.palette) {
        m_paletteIndex = cfg.palette;
        if (m_colecoController) {
            QMetaObject::invokeMethod(
                m_colecoController,
                [this]() { coleco_setpalette(m_paletteIndex); },
                Qt::QueuedConnection
                );
        }
    }

    // 3) Additional Hardware
    //    SGM bestaat niet op ADAM → altijd uit & NIET togglen naar core
    const bool desiredSgm = (m_machineType == 0) ? cfg.sgmEnabled : false;
    m_f18aEnabled = cfg.f18aEnabled;

    if (m_machineType == 1) { // ADAM
        m_sgmEnabled = false;
        if (m_actToggleSGM) m_actToggleSGM->setChecked(false);

        // BUGFIX: We MOETEN de core vertellen SGM uit te zetten,
        // zodat de poorten correct gereset worden.
        QMetaObject::invokeMethod(
            m_colecoController, "setSGMEnabled",
            Qt::QueuedConnection, Q_ARG(bool, false) // Altijd 'false' voor ADAM
            );
    } else { // Coleco
        if (desiredSgm != m_sgmEnabled) {
            m_sgmEnabled = desiredSgm;
            if (m_actToggleSGM) m_actToggleSGM->setChecked(m_sgmEnabled);
            QMetaObject::invokeMethod(
                m_colecoController, "setSGMEnabled",
                Qt::QueuedConnection, Q_ARG(bool, m_sgmEnabled)
                );
        }
    }

    // 4) Controllers (UI state; core-calls optioneel)
    m_ctrlSteering    = cfg.steeringWheel;
    m_ctrlRoller      = cfg.rollerCtrl;
    m_ctrlSuperAction = cfg.superAction;

    // 5) Status + bewaren
    if (m_sysLabel) m_sysLabel->setText(m_machineType ? "ADAM" : "COLECO");
    saveSettings();

    updateFullScreenWallpaper();

    // --- UI UPDATE VOOR MEDIA ---
    bool isAdam = (m_machineType == 1);

    if (m_adamInputMenu) m_adamInputMenu->setEnabled(isAdam);

    // Schakel "Open ROM..." uit in ADAM-modus
    if (m_openRomAction) m_openRomAction->setEnabled(!isAdam);

    // Hide ROM label and its separator in ADAM mode
    if (m_romLabel) m_romLabel->setVisible(!isAdam);
    if (m_sepLabel4) m_sepLabel4->setVisible(!isAdam);

    // Update de media labels (regelt ADAM-modus EN media lock)
    updateMediaStatusLabels();

    // Als we naar Coleco wisselen, eject alle media
    if (!isAdam) {
        onEjectDisk(0);
        onEjectDisk(1);
        onEjectDisk(2);
        onEjectDisk(3);
        onEjectTape(0);
        onEjectTape(1);
        onEjectTape(2);
        onEjectTape(3);

        m_adamInputModeJoystick = false;
        if (m_actAdamKeyboard) m_actAdamKeyboard->setChecked(true);
    }

    updateMediaMenuState();

    // Hide ROM label and its separator in ADAM mode
    if (m_romLabel) m_romLabel->setVisible(!isAdam);
    if (m_sepLabel4) m_sepLabel4->setVisible(!isAdam);

}

void MainWindow::showAboutDialog()
{
    QDialog aboutDialog(this);
    aboutDialog.setWindowTitle("About ADAM+");
    aboutDialog.setFixedSize(420, 560);

    QVBoxLayout *layout = new QVBoxLayout(&aboutDialog);
    layout->setAlignment(Qt::AlignCenter);

    // Logo
    QLabel *logoLabel = new QLabel(&aboutDialog);
    QPixmap logo(":/images/images/ADAMP.png");
    logoLabel->setPixmap(logo.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(logoLabel);

    QLabel *textLabel = new QLabel(&aboutDialog);

    // Tip: Zorg dat de HTML link ook echt klikbaar is in de browser
    textLabel->setOpenExternalLinks(true);

    textLabel->setText(QString(
                           "Version %1<br>"  // %1 wordt vervangen door appVersion
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
                           ).arg(appVersion));

    textLabel->setOpenExternalLinks(true);
    textLabel->setWordWrap(true);
    textLabel->setAlignment(Qt::AlignCenter);
    QFont font("Roboto", 10);
    textLabel->setFont(font);
    layout->addWidget(textLabel);
    layout->addStretch(1);

    // // OK-knop
    // QPushButton *okButton = new QPushButton("OK", &aboutDialog);
    // okButton->setFixedWidth(80);
    // connect(okButton, &QPushButton::clicked, &aboutDialog, &QDialog::accept);
    // layout->addWidget(okButton, 0, Qt::AlignCenter);

    // 1. Laad icoon en pixmap
    QIcon okIcon(":/images/images/OK.png");
    QPixmap okPixmap(":/images/images/OK.png");
    if (okIcon.isNull()) {
        qWarning() << "AboutDialog: Kon OK.png niet laden.";
    }

    // 2. Maak de knop aan
    QPushButton *okButton = new QPushButton(&aboutDialog);

    // 3. Pas de stijl toe
    okButton->setIcon(okIcon);
    okButton->setIconSize(okPixmap.size());
    okButton->setFixedSize(okPixmap.size()); // Grootte van de PNG
    okButton->setText("");
    okButton->setFlat(true);
    okButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );
    // ==================================================
    // --- EINDE AANPASSING ---
    // ==================================================

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
    // Vang een klik op het logo af om het hardware-venster te openen
    if (obj == m_logoLabel && event->type() == QEvent::MouseButtonPress) {
        onOpenHardware(); // Roep de bestaande slot aan
        return true;      // Event afgehandeld, niet doorsturen
    }

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
   // m_logView->editor()->appendPlainText(line);
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
        // De widget bestaat al, toon alleen de overlay
        m_inputWidget->setOverlayVisible(true);
        m_inputWidget->raise();
        //m_inputWidget->setFocus(Qt::OtherFocusReason); // Geef focus aan de overlay
    } else {
        // De widget blijft actief, verberg alleen de overlay
        m_inputWidget->setOverlayVisible(false);
        if (m_screenWidget) m_screenWidget->setFocus(Qt::OtherFocusReason); // Geef focus terug
    }
}

void MainWindow::onFrameReceived(const QImage &frame)
{
    if (!m_screenWidget || frame.isNull()) return;

    // Stuur de ORIGINELE, KLEINE (256x192) frame
    // direct door naar de widget.
    // Alle schaal-logica, canvas, en painter zijn hier weg.
    m_screenWidget->setFrame(frame);
}

void MainWindow::setupEmulatorThread()
{
    qDebug() << "Thread setup: Aanmaken thread en controller...";

    m_emulatorThread = new QThread(this);
    m_colecoController = new ColecoController();
    m_colecoController->moveToThread(m_emulatorThread);

    connect(m_colecoController, &ColecoController::frameReady,
            this,               &MainWindow::onFrameReceived, // <-- NIEUW
            Qt::QueuedConnection);

    // connect(m_colecoController, &ColecoController::frameReady,
    //         m_screenWidget,     &ScreenWidget::setFrame,
    //         Qt::QueuedConnection);

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

    // --- NIEUWE CONNECTIE VOOR SGM STATUS ---
    connect(m_colecoController, &ColecoController::sgmStatusChanged,
            this, &MainWindow::onSgmStatusChanged,
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

    // --- NIEUW: Stuur initiele hardware settings ---
    // Stuur de SGM-status (geladen uit settings) door naar de core
    QMetaObject::invokeMethod(m_colecoController, "setSGMEnabled",
                              Qt::QueuedConnection,
                              Q_ARG(bool, m_sgmEnabled));

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

    const QString filePath = CustomFileDialog::getOpenFileName(
    //const QString filePath = QFileDialog::getOpenFileName(
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

    // --- NIEUW: Update en elide de ROM naam ---
    QFontMetrics metrics(m_romLabel->font());
    QString elidedText = metrics.elidedText(m_currentRomName, Qt::ElideRight, m_romLabel->width()); // width() is nu de 450px
    m_romLabel->setText(elidedText);
    // --- EINDE NIEUW ---

    if (m_currentRomName.contains("ddp", Qt::CaseInsensitive) ||
        m_currentRomName.contains("dsk", Qt::CaseInsensitive))
        m_sysLabel->setText("ADAM");
    else
        m_sysLabel->setText("COLECO");

    QMetaObject::invokeMethod(m_colecoController, "resethMachine",
                              Qt::QueuedConnection);

    QMetaObject::invokeMethod(m_colecoController, "loadRom",
                              Qt::QueuedConnection,
                              Q_ARG(QString, filePath));
}

void MainWindow::onSgmStatusChanged(bool enabled)
{
    if (!m_sgmLabel || !m_sepLabelSGM) return;

    if (enabled) {
        m_sgmLabel->setText("SGM");
        m_sgmLabel->show();
        m_sepLabelSGM->show();
    } else {
        m_sgmLabel->hide();
        m_sepLabelSGM->hide();
    }
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

    // Alleen positioneren als we NIET gemaximaliseerd zijn
    if (isMaximized()) {
        return;
    }

    // Roep onze helper aan om de debugger mee te schuiven
    positionDebugger();
    positionPrinter();

}

void MainWindow::onDebuggerStepCPU()
{
    m_isPaused = true;
    QMetaObject::invokeMethod(m_colecoController, "pauseEmulation",
                              Qt::QueuedConnection);

    coleco_clear_debug_flags();

    QMetaObject::invokeMethod(m_colecoController, "stepOnce",
                              Qt::QueuedConnection);
    if (m_debugWin && m_debugWin->isVisible()) {
        m_debugWin->updateAllViews();
    }
}

void MainWindow::onDebuggerRunCPU()
{
    m_isPaused = false;
    coleco_clear_debug_flags();
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
    qDebug() << "Close Event: Applicatie afsluiten.";

    saveSettings();

    if (m_emulatorThread && m_emulatorThread->isRunning()) {
        qDebug() << "Thread netjes stoppen.";

        // Emulatie in de worker stoppen
        QMetaObject::invokeMethod(
            m_colecoController,
            "stopEmulation",
            Qt::QueuedConnection
            );

        m_emulatorThread->quit();

        if (!m_emulatorThread->wait(2000)) {
            qWarning() << "Thread wilde niet stoppen, wordt geforceerd.";
            m_emulatorThread->terminate();
            m_emulatorThread->wait();
        }
    }

    // Gewoon sluiten, geen qApp->quit() meer nodig
    event->accept();
    QMainWindow::closeEvent(event);
}

void MainWindow::onEmuPausedChanged(bool paused)
{
    m_isPaused = paused;
    if (m_startAction) {
        if (paused) {
            m_startAction->setText(tr("Run emulation"));
            m_runLabel->setText("STOP");
        } else {
            m_runLabel->setText("RUN");
            m_startAction->setText(tr("Stop emulation"));
        }
    }

    // Save/Load State alleen als de emu 'STOP' is
    const bool allowState = paused;
    if (m_actSaveState) m_actSaveState->setEnabled(allowState);
    if (m_actLoadState) m_actLoadState->setEnabled(true);
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

// mainwindow.cpp
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();

    // --- F1..F6: naar AdamNet als "function group" (MAKE) ---
    if (key >= Qt::Key_F1 && key <= Qt::Key_F10) {
        if (!event->isAutoRepeat()) {
            static const uint8_t fg[10] = { 0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C,0x5D };
            const int idx = key - Qt::Key_F1;
            const int marked = 0x100 | fg[idx]; // MAKE
            QMetaObject::invokeMethod(
                m_colecoController, "onAdamKeyEvent",
                Qt::QueuedConnection, Q_ARG(int, marked)
                );
        }
        event->accept();
        return; // Nooit via ASCII sturen
    }

    // --- Coleco of joystick-modus: ALLES via mapper ---
    if ((m_machineType != 1) || m_adamInputModeJoystick) {
        bool handled = (m_inputWidget && m_inputWidget->handleKey(event, true));
        if (handled) event->accept(); else event->ignore();
        return;
    }

    // ===== ADAM keyboard mode =====

    // --- KEYpad overlay actief? Eerst overlay proberen; zo niet: val door naar alfabet ---
    const bool keypadOn = (m_actToggleKeyboard && m_actToggleKeyboard->isChecked());
    if (keypadOn) {
        bool handled = (m_inputWidget && m_inputWidget->handleKey(event, true));
        if (handled) { event->accept(); return; }
        // niet herkend door overlay → verder met normale typ-route
    }

    if (event->isAutoRepeat()) { event->ignore(); return; }

    // Cijfers 0..9 → ASCII
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        const uint8_t code = uint8_t('0' + (key - Qt::Key_0));
        QMetaObject::invokeMethod(
            m_colecoController, "onAdamKeyEvent",
            Qt::QueuedConnection, Q_ARG(int, int(code))
            );
        m_pressedKeyMap.insert(key, code);
        event->accept();
        return;
    }

    // Tab → AdamNet (geen ASCII)
    if (key == Qt::Key_Tab) {
        const int marked = 0x100 | 0x09;
        QMetaObject::invokeMethod(
            m_colecoController, "onAdamKeyEvent",
            Qt::QueuedConnection, Q_ARG(int, marked)
            );
        event->accept();
        return;
    }

    // ASCII-branch, F1..F6 uitgesloten
    const QString text = event->text();
    if (!text.isEmpty() &&
        key != Qt::Key_Return && key != Qt::Key_Enter &&
        key != Qt::Key_Backspace && key != Qt::Key_Space &&
        key != Qt::Key_Tab &&
        !(key >= Qt::Key_F1 && key <= Qt::Key_F10))
    {
        const uint8_t code = uint8_t(text.at(0).toLatin1());
        if (code > 0 && code < 0x80) {
            QMetaObject::invokeMethod(
                m_colecoController, "onAdamKeyEvent",
                Qt::QueuedConnection, Q_ARG(int, int(code))
                );
            m_pressedKeyMap.insert(key, code);
        }
        event->accept();
        return;
    }

    // Overige specials → widget
    if (m_kbWidget) m_kbWidget->handleKey(event, true);
    event->accept();
}



void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    const int key = event->key();

    if (key >= Qt::Key_F11 && key <= Qt::Key_F12) {
        QMainWindow::keyReleaseEvent(event);
        return;
    }

    if ((m_machineType != 1) || m_adamInputModeJoystick) {
        bool handled = false;
        if (m_inputWidget) handled = m_inputWidget->handleKey(event, false);
        if (handled) event->accept(); else event->ignore();
        return;
    }

    // ===== ADAM keyboard mode =====

    // F1..F6 → AdamNet (break) + F7..F10
    if (key >= Qt::Key_F1 && key <= Qt::Key_F10) {
        if (!event->isAutoRepeat()) {
            static const uint8_t fg[10] = { 0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0X5B,0x5C,0x5D };
            const int idx = key - Qt::Key_F1;
            const int marked = 0x100 | (uint8_t)(fg[idx] | 0x80);
            QMetaObject::invokeMethod(
                m_colecoController,"onAdamKeyEvent",
                Qt::QueuedConnection, Q_ARG(int, marked)
                );
        }
        event->accept();
        return;
    }

    if (event->isAutoRepeat()) { event->ignore(); return; }

    // Tab break → AdamNet
    if (key == Qt::Key_Tab) {
        const int marked = 0x100 | (0x09 | 0x80);
        QMetaObject::invokeMethod(
            m_colecoController,"onAdamKeyEvent",
            Qt::QueuedConnection, Q_ARG(int, marked)
            );
        event->accept();
        return;
    }

    // ASCII release
    auto it = m_pressedKeyMap.find(key);
    if (it != m_pressedKeyMap.end()) {
        const uint8_t brk = uint8_t(it.value() | 0x80);
        QMetaObject::invokeMethod(
            m_colecoController,"onAdamKeyEvent",
            Qt::QueuedConnection, Q_ARG(int, int(brk))
            );
        m_pressedKeyMap.erase(it);
        event->accept();
        return;
    }

    if (m_kbWidget) m_kbWidget->handleKey(event, false);
    event->accept();
}

// --- NIEUWE SLOTS VOOR MEDIA ---

void MainWindow::onLoadDisk(int drive)
{
    if (m_machineType != 1) return;

    QString absoluteDiskPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + m_diskPath);

    const QString filePath = CustomFileDialog::getOpenFileName(
    //const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open ADAM Disk Image"),
        absoluteDiskPath, // Start in de Disk-map
        tr("ADAM Disk (*.dsk *.img);;All Files (*.*)")
        );

    if (filePath.isEmpty()) return;

    // Update m_diskPath naar de map van het zojuist geopende bestand
    QFileInfo fileInfo(filePath);
    QDir appDir(QCoreApplication::applicationDirPath());
    m_diskPath = appDir.relativeFilePath(fileInfo.absolutePath());

    // Stuur naar controller-thread
    QMetaObject::invokeMethod(m_colecoController, "loadDisk",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive), // Drive 0
                              Q_ARG(QString, filePath));
}

void MainWindow::onLoadTape(int drive)
{
    if (m_machineType != 1) return;

    QString absoluteTapePath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + m_tapePath);

    const QString filePath = CustomFileDialog::getOpenFileName(
    //const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open ADAM Tape Image"),
        absoluteTapePath, // Start in de Tape-map
        tr("ADAM Tape (*.ddp);;All Files (*.*)")
        );

    if (filePath.isEmpty()) return;

    // Update m_tapePath naar de map van het zojuist geopende bestand
    QFileInfo fileInfo(filePath);
    QDir appDir(QCoreApplication::applicationDirPath());
    m_tapePath = appDir.relativeFilePath(fileInfo.absolutePath());

    QMetaObject::invokeMethod(m_colecoController, "loadTape",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive), // Drive 0
                              Q_ARG(QString, filePath));
}

void MainWindow::onEjectDisk(int drive)
{
    // Ejecten mag altijd (ook als de knop verborgen is)
    // om de save-logica te triggeren
    qDebug() << "UI: Eject/Save Disk " << drive + 5;
    QMetaObject::invokeMethod(m_colecoController, "ejectDisk",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive)); // Drive 0
}

void MainWindow::onEjectTape(int drive)
{
    qDebug() << "UI: Eject/Save Tape " << drive;
    QMetaObject::invokeMethod(m_colecoController, "ejectTape",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive)); // Drive 0
}

void MainWindow::onDiskStatusChanged(int drive, const QString& fileName)
{
    // Haal een standaard vinkje-icoon op uit de huidige stijl
    QIcon checkIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);

    if (drive == 0) {
        m_isDiskLoadedA = !fileName.isEmpty();
        // Zet icoon als geladen, anders leeg icoon
        if (m_diskMenuA) m_diskMenuA->setIcon(m_isDiskLoadedA ? checkIcon : QIcon());

        if (m_diskLabelA) {
            const QString label = "D5: " + (fileName.isEmpty() ? "-" : fileName);
            QFontMetrics fm(m_diskLabelA->font());
            m_diskLabelA->setText(fm.elidedText(label, Qt::ElideRight, m_diskLabelA->width()));
        }
    }
    if (drive == 1) {
        m_isDiskLoadedB = !fileName.isEmpty();
        if (m_diskMenuB) m_diskMenuB->setIcon(m_isDiskLoadedB ? checkIcon : QIcon());

        if (m_diskLabelB) {
            const QString label = "D6: " + (fileName.isEmpty() ? "-" : fileName);
            QFontMetrics fm(m_diskLabelB->font());
            m_diskLabelB->setText(fm.elidedText(label, Qt::ElideRight, m_diskLabelB->width()));
        }
    }
    if (drive == 2) {
        m_isDiskLoadedC = !fileName.isEmpty();
        if (m_diskMenuC) m_diskMenuC->setIcon(m_isDiskLoadedC ? checkIcon : QIcon());

        if (m_diskLabelC) {
            const QString label = "D7: " + (fileName.isEmpty() ? "-" : fileName);
            QFontMetrics fm(m_diskLabelC->font());
            m_diskLabelC->setText(fm.elidedText(label, Qt::ElideRight, m_diskLabelC->width()));
        }
    }
    if (drive == 3) {
        m_isDiskLoadedD = !fileName.isEmpty();
        if (m_diskMenuD) m_diskMenuD->setIcon(m_isDiskLoadedD ? checkIcon : QIcon());

        if (m_diskLabelD) {
            const QString label = "D8: " + (fileName.isEmpty() ? "-" : fileName);
            QFontMetrics fm(m_diskLabelD->font());
            m_diskLabelD->setText(fm.elidedText(label, Qt::ElideRight, m_diskLabelD->width()));
        }
    }

    updateMediaStatusLabels();
    updateMediaMenuState();
}

void MainWindow::onTapeStatusChanged(int drive, const QString& fileName)
{
    // Haal een standaard vinkje-icoon op uit de huidige stijl
    QIcon checkIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);

    if (drive == 0) {
        m_isTapeLoadedA = !fileName.isEmpty();
        if (m_tapeMenuA) m_tapeMenuA->setIcon(m_isTapeLoadedA ? checkIcon : QIcon());

        updateMediaMenuState();
        if (m_tapeLabelA) {
            QString label = "D1: " + (fileName.isEmpty() ? "-" : fileName);
            QFontMetrics metrics(m_tapeLabelA->font());
            QString elidedText = metrics.elidedText(label, Qt::ElideRight, m_tapeLabelA->width());
            m_tapeLabelA->setText(elidedText);
            updateMediaStatusLabels();
        }
    }
    if (drive == 1) {
        m_isTapeLoadedB = !fileName.isEmpty();
        if (m_tapeMenuB) m_tapeMenuB->setIcon(m_isTapeLoadedB ? checkIcon : QIcon());

        updateMediaMenuState();
        if (m_tapeLabelB) {
            QString label = "D2: " + (fileName.isEmpty() ? "-" : fileName);
            QFontMetrics metrics(m_tapeLabelB->font());
            QString elidedText = metrics.elidedText(label, Qt::ElideRight, m_tapeLabelB->width());
            m_tapeLabelB->setText(elidedText);
            updateMediaStatusLabels();
        }
    }
    if (drive == 2) {
        m_isTapeLoadedC = !fileName.isEmpty();
        if (m_tapeMenuC) m_tapeMenuC->setIcon(m_isTapeLoadedC ? checkIcon : QIcon());

        updateMediaMenuState();
        if (m_tapeLabelC) {
            QString label = "D3: " + (fileName.isEmpty() ? "-" : fileName);
            QFontMetrics metrics(m_tapeLabelC->font());
            QString elidedText = metrics.elidedText(label, Qt::ElideRight, m_tapeLabelC->width());
            m_tapeLabelC->setText(elidedText);
            updateMediaStatusLabels();
        }
    }
    if (drive == 3) {
        m_isTapeLoadedD = !fileName.isEmpty();
        if (m_tapeMenuD) m_tapeMenuD->setIcon(m_isTapeLoadedD ? checkIcon : QIcon());

        updateMediaMenuState();
        if (m_tapeLabelD) {
            QString label = "D4: " + (fileName.isEmpty() ? "-" : fileName);
            QFontMetrics metrics(m_tapeLabelD->font());
            QString elidedText = metrics.elidedText(label, Qt::ElideRight, m_tapeLabelD->width());
            m_tapeLabelD->setText(elidedText);
            updateMediaStatusLabels();
        }
    }
}

void MainWindow::updateMediaStatusLabels()
{
    const bool isAdam = (m_machineType == 1);

    bool showDisk = false;
    bool showTape = false;

    if (isAdam) {
         showDisk = true;
         showTape = true;
    }

    if (m_tapeLabelA)       m_tapeLabelA->setVisible(showTape);
    if (m_sepLabelMedia2a)  m_sepLabelMedia2a->setVisible(showTape);
    if (m_tapeLabelB)       m_tapeLabelB->setVisible(showTape);
    if (m_sepLabelMedia2b)  m_sepLabelMedia2b->setVisible(showTape);
    if (m_tapeLabelC)       m_tapeLabelC->setVisible(showTape);
    if (m_sepLabelMedia2c)  m_sepLabelMedia2c->setVisible(showTape);
    if (m_tapeLabelD)       m_tapeLabelD->setVisible(showTape);
    if (m_sepLabelMedia2d)  m_sepLabelMedia2d->setVisible(showTape);

    if (m_diskLabelA)      m_diskLabelA->setVisible(showDisk);
    if (m_sepLabelMedia1a) m_sepLabelMedia1a->setVisible(showDisk);
    if (m_diskLabelB)      m_diskLabelB->setVisible(showDisk);
    if (m_sepLabelMedia1b) m_sepLabelMedia1b->setVisible(showDisk);
    if (m_diskLabelC)      m_diskLabelC->setVisible(showDisk);
    if (m_sepLabelMedia1c) m_sepLabelMedia1c->setVisible(showDisk);
    if (m_diskLabelD)      m_diskLabelD->setVisible(showDisk);
    if (m_sepLabelMedia1d) m_sepLabelMedia1d->setVisible(showDisk);
}

void MainWindow::updateMediaMenuState()
{
    const bool isAdam = (m_machineType == 1);

    if (!isAdam) {
        if (m_tapeMenuA) m_tapeMenuA->setEnabled(false);
        if (m_tapeMenuB) m_tapeMenuB->setEnabled(false);
        if (m_tapeMenuC) m_tapeMenuC->setEnabled(false);
        if (m_tapeMenuD) m_tapeMenuD->setEnabled(false);
        if (m_diskMenuA) m_diskMenuA->setEnabled(false);
        if (m_diskMenuB) m_diskMenuB->setEnabled(false);
        if (m_diskMenuC) m_diskMenuC->setEnabled(false);
        if (m_diskMenuD) m_diskMenuD->setEnabled(false);

        if (m_loadTapeActionA)  m_loadTapeActionA->setEnabled(false);
        if (m_ejectTapeActionA) m_ejectTapeActionA->setEnabled(false);
        if (m_loadTapeActionB)  m_loadTapeActionB->setEnabled(false);
        if (m_ejectTapeActionB) m_ejectTapeActionB->setEnabled(false);
        if (m_loadTapeActionC)  m_loadTapeActionC->setEnabled(false);
        if (m_ejectTapeActionC) m_ejectTapeActionC->setEnabled(false);
        if (m_loadTapeActionD)  m_loadTapeActionD->setEnabled(false);
        if (m_ejectTapeActionD) m_ejectTapeActionD->setEnabled(false);
        if (m_loadDiskActionA)  m_loadDiskActionA->setEnabled(false);
        if (m_ejectDiskActionA) m_ejectDiskActionA->setEnabled(false);
        if (m_loadDiskActionB)  m_loadDiskActionB->setEnabled(false);
        if (m_ejectDiskActionB) m_ejectDiskActionB->setEnabled(false);
        if (m_loadDiskActionC)  m_loadDiskActionC->setEnabled(false);
        if (m_ejectDiskActionC) m_ejectDiskActionC->setEnabled(false);
        if (m_loadDiskActionD)  m_loadDiskActionD->setEnabled(false);
        if (m_ejectDiskActionD) m_ejectDiskActionD->setEnabled(false);
        return;
    }

    // Tape ↔ Disk exclusie
    const bool canUseTape = true;//!m_isDiskLoadedA && !m_isDiskLoadedB && !m_isDiskLoadedC && !m_isDiskLoadedD;
    const bool canUseDisk = true;//!m_isTapeLoadedA && !m_isTapeLoadedB && !m_isTapeLoadedC && !m_isTapeLoadedD;

    // Tape
    if (m_tapeMenuA) m_tapeMenuA->setEnabled(canUseTape);
    if (m_tapeMenuB) m_tapeMenuB->setEnabled(canUseTape);
    if (m_tapeMenuC) m_tapeMenuC->setEnabled(canUseTape);
    if (m_tapeMenuD) m_tapeMenuD->setEnabled(canUseTape);
    if (canUseTape) {
        if (m_loadTapeActionA)  m_loadTapeActionA->setEnabled(!m_isTapeLoadedA);
        if (m_ejectTapeActionA) m_ejectTapeActionA->setEnabled(m_isTapeLoadedA);
        if (m_loadTapeActionB)  m_loadTapeActionB->setEnabled(!m_isTapeLoadedB);
        if (m_ejectTapeActionB) m_ejectTapeActionB->setEnabled(m_isTapeLoadedB);
        if (m_loadTapeActionC)  m_loadTapeActionC->setEnabled(!m_isTapeLoadedC);
        if (m_ejectTapeActionC) m_ejectTapeActionC->setEnabled(m_isTapeLoadedC);
        if (m_loadTapeActionD)  m_loadTapeActionD->setEnabled(!m_isTapeLoadedD);
        if (m_ejectTapeActionD) m_ejectTapeActionD->setEnabled(m_isTapeLoadedD);
    } else {
        if (m_loadTapeActionA)  m_loadTapeActionA->setEnabled(false);
        if (m_ejectTapeActionA) m_ejectTapeActionA->setEnabled(false);
        if (m_loadTapeActionB)  m_loadTapeActionB->setEnabled(false);
        if (m_ejectTapeActionB) m_ejectTapeActionB->setEnabled(false);
        if (m_loadTapeActionC)  m_loadTapeActionC->setEnabled(false);
        if (m_ejectTapeActionC) m_ejectTapeActionC->setEnabled(false);
        if (m_loadTapeActionD)  m_loadTapeActionD->setEnabled(false);
        if (m_ejectTapeActionD) m_ejectTapeActionD->setEnabled(false);
    }

    // Disks
    if (m_diskMenuA) m_diskMenuA->setEnabled(canUseDisk);
    if (m_diskMenuB) m_diskMenuB->setEnabled(canUseDisk);
    if (m_diskMenuC) m_diskMenuC->setEnabled(canUseDisk);
    if (m_diskMenuD) m_diskMenuD->setEnabled(canUseDisk);

    if (canUseDisk) {
        if (m_loadDiskActionA)  m_loadDiskActionA->setEnabled(!m_isDiskLoadedA);
        if (m_ejectDiskActionA) m_ejectDiskActionA->setEnabled(m_isDiskLoadedA);

        if (m_loadDiskActionB)  m_loadDiskActionB->setEnabled(!m_isDiskLoadedB);
        if (m_ejectDiskActionB) m_ejectDiskActionB->setEnabled(m_isDiskLoadedB);

        if (m_loadDiskActionC)  m_loadDiskActionC->setEnabled(!m_isDiskLoadedC);
        if (m_ejectDiskActionC) m_ejectDiskActionC->setEnabled(m_isDiskLoadedC);

        if (m_loadDiskActionD)  m_loadDiskActionD->setEnabled(!m_isDiskLoadedD);
        if (m_ejectDiskActionD) m_ejectDiskActionD->setEnabled(m_isDiskLoadedD);
    } else {
        if (m_loadDiskActionA)  m_loadDiskActionA->setEnabled(false);
        if (m_ejectDiskActionA) m_ejectDiskActionA->setEnabled(false);
        if (m_loadDiskActionB)  m_loadDiskActionB->setEnabled(false);
        if (m_ejectDiskActionB) m_ejectDiskActionB->setEnabled(false);
        if (m_loadDiskActionC)  m_loadDiskActionC->setEnabled(false);
        if (m_ejectDiskActionC) m_ejectDiskActionC->setEnabled(false);
        if (m_loadDiskActionD)  m_loadDiskActionD->setEnabled(false);
        if (m_ejectDiskActionD) m_ejectDiskActionD->setEnabled(false);
    }

}

void MainWindow::onAdamInputModeChanged()
{
    m_adamInputModeJoystick = m_actAdamJoystick->isChecked();
    qDebug() << "ADAM Input Mode set to:" << (m_adamInputModeJoystick ? "Joystick" : "Keyboard");

    // Geef focus terug aan het scherm
    if (m_screenWidget) {
        m_screenWidget->setFocus(Qt::OtherFocusReason);
    }
}


// menu/actie “Open Printer Window”
void MainWindow::onShowPrinterWindow()
{
    PrintWindow* w = PrintWindow::instance();

    // 1) Toon & focus
    if (!w->isVisible()) {
        w->show();
    }
    w->raise();
    w->activateWindow();

    // 2) Positioneer rechts naast het hoofdvenster (zoals debugger)
    //    Houd 10px marge aan en voorkom dat we buiten het scherm vallen.
    const QRect mainGeom = this->frameGeometry();           // inclusief vensterrand
    const QRect avail    = screen()->availableGeometry();   // werkbare schermruimte

    QPoint pos(mainGeom.right() + 10, mainGeom.top());
    QSize  sz  = w->size();

    // Als we buiten het scherm dreigen te gaan, schuif naar links of onder
    if (pos.x() + sz.width() > avail.right())
        pos.setX(qMax(avail.left(), mainGeom.left() - 10 - sz.width()));
    if (pos.y() + sz.height() > avail.bottom())
        pos.setY(qMax(avail.top(), avail.bottom() - sz.height()));

    w->move(pos);

    // 3) Abonneer (idempotent) op AdamNet-printer
}

void MainWindow::positionPrinter()
{
    PrintWindow* w = PrintWindow::instance();
    if (!w->isVisible()) return;

    const QRect mainGeom = this->frameGeometry();
    QPoint pos(mainGeom.right() + 10, mainGeom.top());
    // Eenvoudig: alleen X bijwerken (rechts blijven “plakken”)
    w->move(pos);
}

void MainWindow::updateFullScreenWallpaper()
{
    if (!m_wallpaperLabel) return;

    QString wallpaperPath;

    // m_machineType == 1 is ADAM
    if (m_machineType == 1) {
        wallpaperPath = ":/images/images/wallpaper_adamp.png";
    }
    // m_machineType == 0 is Coleco
    else {
        wallpaperPath = ":/images/images/wallpaper_coleco.png";
    }

    QPixmap newWallpaper(wallpaperPath);

    // Fallback voor als de afbeelding niet geladen kan worden
    if (newWallpaper.isNull()) {
        qWarning() << "Kan wallpaper niet laden:" << wallpaperPath;
        m_wallpaperLabel->clear();
        m_wallpaperLabel->setStyleSheet("background-color: black;");
    } else {
        m_wallpaperLabel->setStyleSheet(""); // Verwijder eerdere stijl
        m_wallpaperLabel->setPixmap(newWallpaper);
    }
}

// In mainwindow.cpp

void MainWindow::onSaveState()
{
    // 1. Gebruik HETZELFDE pad-logica als in onLoadState
    // m_statePath is relatief (bv "."), dus maak het absoluut
    QString absoluteStatePath =
        QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + m_statePath);

    // 2. Gebruik dit absolute pad voor QDir
    QDir statesDir(absoluteStatePath);
    if (!statesDir.exists())
        statesDir.mkpath("."); // Maak de map aan (op het absolute pad)

    // Bestandsnaam gebaseerd op huidige ROM
    QString baseName = m_currentRomName;
    if (baseName.isEmpty())
        baseName = "state";

    // Haal alleen de naam zonder extensie eruit
    QFileInfo fi(baseName);
    baseName = fi.completeBaseName();

    // 3. Maak het standaard *volledige bestandspad*
    // Dit is nu een absoluut pad, bv: "/app/pad/states/mijnrom.sta"
    QString defaultFile = statesDir.filePath(baseName + ".sta");

    // 4. Geef dit volledige bestandspad door.
    // CustomFileDialog::setInitialDirectory is ontworpen om dit
    // te splitsen in een map (path) en een bestandsnaam (fileName).
    const QString filePath = CustomFileDialog::getSaveFileName(
        this,
        tr("Save State"),
        defaultFile, // <-- Dit is nu een correct, absoluut pad
        tr("State files (*.sta);;All Files (*.*)")
        );

    if (filePath.isEmpty())
        return;

    QString finalPath = filePath;
    if (!finalPath.endsWith(".sta", Qt::CaseInsensitive))
        finalPath += ".sta";

    // 5) Update m_statePath naar de map van het gekozen bestand (zoals bij ROM/Disk/Tape)
    QFileInfo outInfo(finalPath);
    QDir appDir(QCoreApplication::applicationDirPath());
    m_statePath = appDir.relativeFilePath(outInfo.absolutePath());
    saveSettings();   // nu wordt je nieuwe state map ook echt bewaard

    // 6) Via de emu-thread laten uitvoeren
    QMetaObject::invokeMethod(
        m_colecoController,
        "saveState",
        Qt::QueuedConnection,
        Q_ARG(QString, finalPath)
        );
}


void MainWindow::onLoadState()
{
    // Maak van de (relatieve) statePath een absoluut pad
    QString absoluteStatePath =
        QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + m_statePath);

    QDir statesDir(absoluteStatePath);
    if (!statesDir.exists())
        statesDir.mkpath(".");

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Load State"),
        statesDir.absolutePath(),                 // start in statesDir
        tr("State files (*.sta);;All Files (*.*)")
        );

    if (filePath.isEmpty())
        return;

    // Update m_statePath naar de gekozen map
    QFileInfo inInfo(filePath);
    QDir appDir(QCoreApplication::applicationDirPath());
    m_statePath = appDir.relativeFilePath(inInfo.absolutePath());
    saveSettings(); // optioneel

    QMetaObject::invokeMethod(
        m_colecoController,
        "loadState",
        Qt::QueuedConnection,
        Q_ARG(QString, filePath)
        );
}

void MainWindow::configurePlatformSettings()
{
#if defined(Q_OS_WIN)
    qDebug() << "Running on Windows.";
#elif defined(Q_OS_LINUX)
    qDebug() << "Running on Linux.";
#else
    qDebug() << "Running on an unknown platform.";
#endif
}
