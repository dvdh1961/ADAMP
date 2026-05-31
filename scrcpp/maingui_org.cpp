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
#include "cv.h"
#include "f18a.h"
#include "adnet.h"
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
#include "adnet.h"

//---------------------------------------------------------------------------------------------
// GUI implementations (menus, widgets, media, settings, input, etc.)
//---------------------------------------------------------------------------------------------
// Declaratie bovenaan je bestand
extern "C" void adamnet_set_game_mode(bool enabled);


// Verkort statusbar tekst met drie puntjes afhankelijk van de beschikbare breedte.
static QString statusBarElideText3Dots(const QString& text, const QFont& font, int maxWidth)
{
    const QString dots = "...";
    if (text.isEmpty()) return text;

    QFontMetrics fm(font);
    if (fm.horizontalAdvance(text) <= maxWidth) return text;
    if (maxWidth <= fm.horizontalAdvance(dots)) return dots;

    int low = 0;
    int high = text.length();
    int best = 0;

    while (low <= high) {
        const int mid = (low + high) / 2;
        const QString candidate = text.left(mid) + dots;
        if (fm.horizontalAdvance(candidate) <= maxWidth) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    return text.left(best) + dots;
}

// Bereken de vrije statusbar-ruimte na m_sepLabel4 en pas m_romLabel daarop aan.
static void updateRomLabelForStatusBar(QStatusBar* bar, QLabel* sepLabel4, QLabel* romLabel, const QString& fullText)
{
    if (!romLabel) return;

    QString display = fullText.trimmed();
    if (display.isEmpty()) display = "No cart";

    int maxWidth = 480;

    if (bar && sepLabel4) {
        const int rightMargin = 24; // ruimte voor statusbar-rand / sizegrip
        const QPoint sepPos = sepLabel4->mapTo(bar, QPoint(0, 0));
        const int startX = sepPos.x() + sepLabel4->width() + 4;
        maxWidth = bar->width() - startX - rightMargin;
    }

    if (maxWidth < 60) maxWidth = 60;
    if (maxWidth > 480) maxWidth = 480;

    romLabel->setFixedWidth(maxWidth);
    romLabel->setText(statusBarElideText3Dots(display, romLabel->font(), maxWidth - 4));
    romLabel->setToolTip(display);
}


//---------------------------------------------------------------------------------------------
// STATUSBAR
//---------------------------------------------------------------------------------------------
void MainWindow::forceStatusBarMediaFlag(QLabel* label)
{
    if (!label) return;

    const QString text = label->text().trimmed();

    // Verwacht formaat "D1: ..." / "D5: ..."
    const int idx = text.indexOf(':');
    if (idx < 0) return;

    const QString prefix = text.left(idx + 1); // "D1:"

    // Alles behalve "-" telt als "geladen"
    const bool loaded = !text.endsWith('-');

    label->setText(QString("%1 %2").arg(prefix, loaded ? "X" : "-"));

    // Kleur (status-LED stijl)
    label->setStyleSheet(loaded
                             ? "color: #00C853;"   // groen = geladen
                             : "color: #9E9E9E;"   // grijs = leeg
                         );
}

void MainWindow::forceStatusBarMediaFlags()
{
    auto setMediaLabel = [](QLabel* label, const QString& prefix, bool loaded) {
        if (!label) return;
        label->setText(QString("%1 %2").arg(prefix, loaded ? "X" : "-"));
        label->setStyleSheet(loaded ? "color: #00C853;" : "color: #9E9E9E;");
    };

    setMediaLabel(m_tapeLabelA, "D1:", !m_loadedTapeNames[0].trimmed().isEmpty());
    setMediaLabel(m_tapeLabelB, "D2:", !m_loadedTapeNames[1].trimmed().isEmpty());
    setMediaLabel(m_tapeLabelC, "D3:", !m_loadedTapeNames[2].trimmed().isEmpty());
    setMediaLabel(m_tapeLabelD, "D4:", !m_loadedTapeNames[3].trimmed().isEmpty());

    setMediaLabel(m_diskLabelA, "D5:", !m_loadedDiskNames[0].trimmed().isEmpty());
    setMediaLabel(m_diskLabelB, "D6:", !m_loadedDiskNames[1].trimmed().isEmpty());
    setMediaLabel(m_diskLabelC, "D7:", !m_loadedDiskNames[2].trimmed().isEmpty());
    setMediaLabel(m_diskLabelD, "D8:", !m_loadedDiskNames[3].trimmed().isEmpty());
}

void MainWindow::setStatusBar()
{
    statusBar()->setSizeGripEnabled(true);

    // Window flags
    Qt::WindowFlags flags = windowFlags();
    flags &= ~Qt::WindowMaximizeButtonHint;
    flags &= ~Qt::WindowMinimizeButtonHint;
    flags |= Qt::CustomizeWindowHint;
    setWindowFlags(flags);

    auto mkLabel = [&](const QString& text,
                       const char* objName,
                       int minWidth,
                       Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter,
                       QSizePolicy::Policy hPol = QSizePolicy::Fixed) -> QLabel*
    {
        QLabel* l = new QLabel(text, this);
        if (objName && *objName) l->setObjectName(objName);
        l->setAlignment(align);
        if (minWidth > 0) l->setMinimumWidth(minWidth);
        l->setSizePolicy(hPol, QSizePolicy::Preferred);
        return l;
    };

    auto mkSep = [&]() -> QLabel* {
        return mkLabel("|", "", 0);
    };

    // ---- Core labels ----
    m_sysLabel = mkLabel("COLECO", "sysLabel", 50);

    m_sepLabelSGM = mkSep();
    m_sgmLabel = mkLabel("SGM", "sgmLabel", 40);
    m_sgmLabel->hide();
    m_sepLabelSGM->hide();

    m_stdLabel = mkLabel(QString("%1").arg(m_currentStd), "stdLabel", 30);
    m_fpsLabel = mkLabel("0fps", "fpsLabel", 40);
    m_runLabel = mkLabel("RUN", "runLabel", 30);

    // ---- Media labels (geen filenames meer: enkel X of -) ----
    auto mkMedia = [&](const char* obj, const char* prefix, QLabel*& sep, QLabel*& lab) {
        sep = mkSep();
        lab = mkLabel(QString("%1 -").arg(prefix), obj, 35); // start = "-"
    };

    mkMedia("tapeLabel", "D1:", m_sepLabelMedia2a, m_tapeLabelA);
    mkMedia("tapeLabel", "D2:", m_sepLabelMedia2b, m_tapeLabelB);
    mkMedia("tapeLabel", "D3:", m_sepLabelMedia2c, m_tapeLabelC);
    mkMedia("tapeLabel", "D4:", m_sepLabelMedia2d, m_tapeLabelD);

    mkMedia("diskLabel", "D5:", m_sepLabelMedia1a, m_diskLabelA);
    mkMedia("diskLabel", "D6:", m_sepLabelMedia1b, m_diskLabelB);
    mkMedia("diskLabel", "D7:", m_sepLabelMedia1c, m_diskLabelC);
    mkMedia("diskLabel", "D8:", m_sepLabelMedia1d, m_diskLabelD);

    // Start verborgen (zoals je had)
    QLabel* hideAtStart[] = {
        m_sepLabelMedia2a, m_tapeLabelA,
        m_sepLabelMedia2b, m_tapeLabelB,
        m_sepLabelMedia2c, m_tapeLabelC,
        m_sepLabelMedia2d, m_tapeLabelD,
        m_sepLabelMedia1a, m_diskLabelA,
        m_sepLabelMedia1b, m_diskLabelB,
        m_sepLabelMedia1c, m_diskLabelC,
        m_sepLabelMedia1d, m_diskLabelD
    };
    for (QLabel* w : hideAtStart) if (w) w->hide();

    // ---- ROM + separators ----
    m_romLabel = mkLabel("No cart", "romLabel", 0);
    m_romLabel->setFixedWidth(480);

    m_sepLabel1 = mkSep();
    m_sepLabel2 = mkSep();
    m_sepLabel3 = mkSep();
    m_sepLabel4 = mkSep();

    // ---- Add to statusbar ----
    QWidget* widgets[] = {
        m_sysLabel,
        m_sepLabelSGM, m_sgmLabel,

        m_sepLabelMedia2a, m_tapeLabelA,
        m_sepLabelMedia2b, m_tapeLabelB,
        m_sepLabelMedia2c, m_tapeLabelC,
        m_sepLabelMedia2d, m_tapeLabelD,

        m_sepLabelMedia1a, m_diskLabelA,
        m_sepLabelMedia1b, m_diskLabelB,
        m_sepLabelMedia1c, m_diskLabelC,
        m_sepLabelMedia1d, m_diskLabelD,

        m_sepLabel1, m_stdLabel,
        m_sepLabel2, m_fpsLabel,
        m_sepLabel3, m_runLabel,
        m_sepLabel4, m_romLabel
    };
    for (QWidget* w : widgets) if (w) statusBar()->addWidget(w);

    // ---- set Font ----
    QFont statusFont("Roboto", 9);
    statusFont.setBold(false);

    QWidget* fontWidgets[] = {
        m_sysLabel, m_sepLabelSGM, m_sgmLabel,
        m_sepLabelMedia2a, m_tapeLabelA,
        m_sepLabelMedia2b, m_tapeLabelB,
        m_sepLabelMedia2c, m_tapeLabelC,
        m_sepLabelMedia2d, m_tapeLabelD,
        m_sepLabelMedia1a, m_diskLabelA,
        m_sepLabelMedia1b, m_diskLabelB,
        m_sepLabelMedia1c, m_diskLabelC,
        m_sepLabelMedia1d, m_diskLabelD,
        m_sepLabel1, m_stdLabel,
        m_sepLabel2, m_fpsLabel,
        m_sepLabel3, m_runLabel,
        m_sepLabel4, m_romLabel
    };
    for (QWidget* w : fontWidgets) if (w) w->setFont(statusFont);

    updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel, "No cart");
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

void MainWindow::onFpsUpdated(int fps)
{
    m_fpsLabel->setText(QString("%1fps").arg(fps));

}

//---------------------------------------------------------------------------------------------
// GUI INTERFACE
//---------------------------------------------------------------------------------------------

void MainWindow::setupUI()
{
    // 1. Laad het lettertype uit je resources of lokale map
    // Pas het pad aan naar waar jouw .ttf staat (bijv. ":/fonts/mijnfont.ttf")
    int fontId = QFontDatabase::addApplicationFont(":/fonts/fonts/BeautifulPoliceOfficer-rvv8x.ttf");

    QString family;
    if (fontId != -1) {
        family = QFontDatabase::applicationFontFamilies(fontId).at(0);
        //qDebug() << "[UI] Custom font geladen:" << family;
    } else {
        qDebug() << "[UI] Kon custom font niet laden, fallback naar Roboto";
        family = "Roboto";
    }

    QFont menuFont(family, 16);
    menuFont.setBold(false);

    // Pas het toe op de menubalk zelf
    menuBar()->setFont(menuFont);

    // De variabele 'family' bevat de naam van je geladen font
    QString style = QString(
                        "QMenuBar { "
                        "  font-family: '%1';"
                        "  font-size: 16pt;"
                        "  background-color: #000000;" // Optioneel: geef de menu background een kleur
                        "}"
                        "QMenu { "
                        "  font-family: '%1';"
                        "  font-size: 16pt;"
                        "  background-color: #2e2e2e;" // Optioneel: geef de dropdown een kleur
                        "  color: white;"
                        "  border: 1px solid black;"
                        "}"
                        "QMenu::item:selected { " // Kleur wanneer je met de muis over een optie gaat
                        "  background-color: #4a90e2;"
                        "}"
                        ).arg(family);

    this->setStyleSheet(style);

    // Optioneel: Pas het toe op alle menu's die worden toegevoegd
    // (In Qt erven acties en submenu's vaak het font van de parent)

    QMenu* fileMenu = menuBar()->addMenu(tr("File"));

    fileMenu->setFont(menuFont); // Specifiek voor het dropdown gedeelte

    // Coleco cartridge
    m_openColecoRomAction = new QAction(tr("Coleco Cartridge"), this);
    connect(m_openColecoRomAction, &QAction::triggered, this, &MainWindow::onOpenColecoRom);
    fileMenu->addAction(m_openColecoRomAction);

    fileMenu->addSeparator();

    // Adam cartridge
    m_openAdamRomAction = new QAction(tr("Adam Cartridge"), this);
    connect(m_openAdamRomAction, &QAction::triggered, this, &MainWindow::onOpenAdamRom);
    fileMenu->addAction(m_openAdamRomAction);

    // Tape Menu's
    m_tapeMenuA = new QMenu(tr("Tape D1"), this);
    m_loadTapeActionA = new QAction(tr("Load"), this);
    connect(m_loadTapeActionA, &QAction::triggered, this, [this]() { onLoadTape(0); });
    m_tapeMenuA->addAction(m_loadTapeActionA);
    m_ejectTapeActionA = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionA, &QAction::triggered, this, [this]() { onEjectTape(0); });
    m_tapeMenuA->addAction(m_ejectTapeActionA);
    fileMenu->addMenu(m_tapeMenuA);
    m_tapeMenuB = new QMenu(tr("Tape D2"), this);
    m_loadTapeActionB = new QAction(tr("Load"), this);
    connect(m_loadTapeActionB, &QAction::triggered, this, [this]() { onLoadTape(1); });
    m_tapeMenuB->addAction(m_loadTapeActionB);
    m_ejectTapeActionB = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionB, &QAction::triggered, this, [this]() { onEjectTape(1); });
    m_tapeMenuB->addAction(m_ejectTapeActionB);
    fileMenu->addMenu(m_tapeMenuB);
    m_tapeMenuC = new QMenu(tr("Tape D3"), this);
    m_loadTapeActionC = new QAction(tr("Load"), this);
    connect(m_loadTapeActionC, &QAction::triggered, this, [this]() { onLoadTape(2); });
    m_tapeMenuC->addAction(m_loadTapeActionC);
    m_ejectTapeActionC = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionC, &QAction::triggered, this, [this]() { onEjectTape(2); });
    m_tapeMenuC->addAction(m_ejectTapeActionC);
    fileMenu->addMenu(m_tapeMenuC);
    m_tapeMenuD = new QMenu(tr("Tape D4"), this);
    m_loadTapeActionD = new QAction(tr("Load"), this);
    connect(m_loadTapeActionD, &QAction::triggered, this, [this]() { onLoadTape(3); });
    m_tapeMenuD->addAction(m_loadTapeActionD);
    m_ejectTapeActionD = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionD, &QAction::triggered, this, [this]() { onEjectTape(3); });
    m_tapeMenuD->addAction(m_ejectTapeActionD);
    fileMenu->addMenu(m_tapeMenuD);

    // Disk Menu's
    m_diskMenuA = new QMenu(tr("Disk  D5"), this);
    m_loadDiskActionA = new QAction(tr("Load"), this);
    connect(m_loadDiskActionA, &QAction::triggered, this, [this]() { onLoadDisk(0); });
    m_diskMenuA->addAction(m_loadDiskActionA);
    m_ejectDiskActionA = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionA, &QAction::triggered, this, [this]() { onEjectDisk(0); });
    m_diskMenuA->addAction(m_ejectDiskActionA);
    fileMenu->addMenu(m_diskMenuA);
    m_diskMenuB = new QMenu(tr("Disk  D6"), this);
    m_loadDiskActionB = new QAction(tr("Load"), this);
    connect(m_loadDiskActionB, &QAction::triggered, this, [this]() { onLoadDisk(1); });
    m_diskMenuB->addAction(m_loadDiskActionB);
    m_ejectDiskActionB = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionB, &QAction::triggered, this, [this]() { onEjectDisk(1); });
    m_diskMenuB->addAction(m_ejectDiskActionB);
    fileMenu->addMenu(m_diskMenuB);
    m_diskMenuC = new QMenu(tr("Disk  D7"), this);
    m_loadDiskActionC = new QAction(tr("Load"), this);
    connect(m_loadDiskActionC, &QAction::triggered, this,  [this]() { onLoadDisk(2); });
    m_diskMenuC->addAction(m_loadDiskActionC);
    m_ejectDiskActionC = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionC, &QAction::triggered, this, [this]() { onEjectDisk(2); });
    m_diskMenuC->addAction(m_ejectDiskActionC);
    fileMenu->addMenu(m_diskMenuC);
    m_diskMenuD = new QMenu(tr("Disk  D8"), this);
    m_loadDiskActionD = new QAction(tr("Load"), this);
    connect(m_loadDiskActionD, &QAction::triggered, this,  [this]() { onLoadDisk(3); });
    m_diskMenuD->addAction(m_loadDiskActionD);
    m_ejectDiskActionD = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionD, &QAction::triggered, this, [this]() { onEjectDisk(3); });
    m_diskMenuD->addAction(m_ejectDiskActionD);
    fileMenu->addMenu(m_diskMenuD);

    // Initial state disabled
    m_diskMenuA->setEnabled(false);
    m_diskMenuB->setEnabled(false);
    m_diskMenuC->setEnabled(false);
    m_diskMenuD->setEnabled(false);
    m_tapeMenuA->setEnabled(false);
    m_tapeMenuB->setEnabled(false);
    m_tapeMenuC->setEnabled(false);
    m_tapeMenuD->setEnabled(false);

    fileMenu->addSeparator();

    m_actReleaseAll = new QAction(tr("Release all media"), this);
    connect(m_actReleaseAll, &QAction::triggered, this, &MainWindow::onReleaseAll);
    fileMenu->addAction(m_actReleaseAll);

    fileMenu->addSeparator();
    m_actSaveState = new QAction(tr("Save State..."), this);
    connect(m_actSaveState, &QAction::triggered, this, &MainWindow::onSaveState);
    fileMenu->addAction(m_actSaveState);
    m_actLoadState = new QAction(tr("Load State..."), this);
    connect(m_actLoadState, &QAction::triggered, this, &MainWindow::onLoadState);
    fileMenu->addAction(m_actLoadState);
    m_actSaveState->setEnabled(false);
    m_actLoadState->setEnabled(false);
    fileMenu->addSeparator();
    m_settingsAction = new QAction(tr("Settings"), this);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);
    fileMenu->addAction(m_settingsAction);
    fileMenu->addSeparator();
    m_quitAction = new QAction(tr("&Exit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(m_quitAction);

    // --- DEBUG MENU ---
    QMenu* debugMenu = menuBar()->addMenu(tr("Debug"));
    m_startAction = new QAction(tr("Run/Stop"), this);
    m_startAction->setShortcut(Qt::Key_F11);
    connect(m_startAction, &QAction::triggered, this, &MainWindow::onRunStop);
    debugMenu->addAction(m_startAction);
    debugMenu->addSeparator();
    m_actShowLog = new QAction(tr("Logger"), this);
    m_actShowLog->setCheckable(true);
    m_actShowLog->setChecked(false);
    debugMenu->addAction(m_actShowLog);

    QAction* actClearLog = new QAction(tr("Clear Logger"), this);
    connect(actClearLog, &QAction::triggered, this, [this]() {
        if (m_logView) {
            m_logView->clear();
        }
    });
    debugMenu->addAction(actClearLog);
    debugMenu->addSeparator();
    m_debuggerAction = new QAction(tr("Debugger"), this);
    debugMenu->addAction(m_debuggerAction);

    // --- TOOLS MENU ---
    QMenu* toolsMenu = menuBar()->addMenu(tr("Tools"));
    m_actShowNameTable = new QAction(tr("Name Table Viewer"), this);
    connect(m_actShowNameTable, &QAction::triggered, this, &MainWindow::onShowNameTable);
    toolsMenu->addAction(m_actShowNameTable);
    m_actShowPatternTable = new QAction(tr("Pattern Table Viewer"), this);
    connect(m_actShowPatternTable, &QAction::triggered, this, &MainWindow::onShowPatternTable);
    toolsMenu->addAction(m_actShowPatternTable);
    m_actShowSpriteTable = new QAction(tr("Sprite Table Viewer"), this);
    connect(m_actShowSpriteTable, &QAction::triggered, this, &MainWindow::onShowSpriteTable);
    toolsMenu->addAction(m_actShowSpriteTable);
    toolsMenu->addSeparator();
    m_cartInfoAction = new QAction(tr("Cart profile"), this);
    toolsMenu->addAction(m_cartInfoAction);

     m_actImageManager = new QAction(tr("EOS Media Manager"), this);
     connect(m_actImageManager, &QAction::triggered, this, [this]() {
        if (!m_imageManagerDialog)
            return;

        m_imageManagerDialog->setDiskRootPath(m_diskPath);
        m_imageManagerDialog->setTapeRootPath(m_tapePath);

        m_imageManagerDialog->show();
        m_imageManagerDialog->raise();
        m_imageManagerDialog->activateWindow();
    });
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_actImageManager);



    // --- INPUT MENU ---
    QMenu* inputMenu = menuBar()->addMenu(tr("Input"));

    // Keypad (was in Tools)
    m_actToggleKeyboard = new QAction(tr("Keypad"), this);
    m_actToggleKeyboard->setCheckable(true);
    m_actToggleKeyboard->setChecked(false);
    inputMenu->addAction(m_actToggleKeyboard);

    // Keypad mapper (was in Tools)
    m_actJoypadMapper = new QAction(tr("Keypad mapper"), this);
    inputMenu->addAction(m_actJoypadMapper);

    inputMenu->addSeparator();

    // Joystick Controller Menu (was in Tools)
    QMenu* joystickControllerMenu = inputMenu->addMenu(tr("Joystick Controller")); // Hoofdmenu

    // Actiegroep om ervoor te zorgen dat slechts één type is geselecteerd
    m_joystickGroup = new QActionGroup(this);
    m_joystickGroup->setExclusive(true);

    // 1. Algemene Controller
    m_actJoystickGeneral = new QAction(tr("General"), this);
    m_actJoystickGeneral->setCheckable(true);
    m_actJoystickGeneral->setData(0); // 0 = Generic/Default type
    m_joystickGroup->addAction(m_actJoystickGeneral);
    joystickControllerMenu->addAction(m_actJoystickGeneral);

    // 2. PlayStation Controller
    m_actJoystickPS = new QAction(tr("PlayStation"), this);
    m_actJoystickPS->setCheckable(true);
    m_actJoystickPS->setData(1); // 1 = PlayStation type (voor POV mapping)
    m_joystickGroup->addAction(m_actJoystickPS);
    joystickControllerMenu->addAction(m_actJoystickPS);

    // 3. Xbox Controller
    m_actJoystickXbox = new QAction(tr("Xbox"), this);
    m_actJoystickXbox->setCheckable(true);
    m_actJoystickXbox->setData(2); // 2 = Xbox type (optionele toekomstige mapping)
    m_joystickGroup->addAction(m_actJoystickXbox);
    joystickControllerMenu->addAction(m_actJoystickXbox);

    // Verbinden met de afhandelingsslot
    connect(m_joystickGroup, &QActionGroup::triggered, this, &MainWindow::onJoystickTypeChanged);

    // Synchronisatie van de geladen waarde
    if (m_joystickGroup) {
        const QList<QAction*> actions = m_joystickGroup->actions();
        for (int i = 0; i < actions.size(); ++i) {
            QAction* action = actions.at(i);
            if (action && action->data().toInt() == m_joystickType) {
                action->setChecked(true);
                break;
            }
        }
    }

    inputMenu->addSeparator();
    m_actTogglePaddleMode = new QAction(tr("Paddle Mode"), this);
    m_actTogglePaddleMode->setCheckable(true);
    m_actTogglePaddleMode->setChecked(m_usePaddleMode); // Synchroniseer met instelling
    inputMenu->addAction(m_actTogglePaddleMode);

    // De connectie
    connect(m_actTogglePaddleMode, &QAction::toggled, this, &MainWindow::onTogglePaddleMode);

    // --- VIDEO MENU ---
    QMenu* videoMenu = menuBar()->addMenu(tr("Video"));
    QActionGroup* videoGroup = new QActionGroup(this);
    // NTSC & PAL
    m_actToggleNTSC = new QAction(tr("NTSC"), this);
    m_actToggleNTSC->setCheckable(true);
    m_actToggleNTSC->setChecked(true);
    videoGroup->addAction(m_actToggleNTSC);
    videoMenu->addAction(m_actToggleNTSC);
    m_actTogglePAL  = new QAction(tr("PAL "), this);
    m_actTogglePAL->setCheckable(true);
    videoGroup->addAction(m_actTogglePAL);
    videoMenu->addAction(m_actTogglePAL);
    videoMenu->addSeparator();
    m_scanlinesMenu = videoMenu->addMenu(tr("Simulate Scanlines"));
    m_scanlinesGroup = new QActionGroup(this);
    m_scanlinesGroup->setExclusive(true);
    // Optie 1: Scanlines UIT
    QAction* actScanlinesOff = new QAction(tr("Off"), this);
    actScanlinesOff->setCheckable(true);
    actScanlinesOff->setChecked(m_scanlinesMode == ScanlinesOff);
    actScanlinesOff->setData(ScanlinesOff);
    m_scanlinesGroup->addAction(actScanlinesOff);
    m_scanlinesMenu->addAction(actScanlinesOff);
    m_scanlinesMenu->addSeparator();
    // Optie 2: TV Scanlines (TelevizeImage)
    m_actScanlinesTV = new QAction(tr("Horizontal"), this);
    m_actScanlinesTV->setCheckable(true);
    m_actScanlinesTV->setChecked(m_scanlinesMode == ScanlinesTV);
    m_actScanlinesTV->setData(ScanlinesTV);
    m_scanlinesGroup->addAction(m_actScanlinesTV);
    m_scanlinesMenu->addAction(m_actScanlinesTV);
    // Optie 3: LCD Scanlines (LcdizeImage)
    m_actScanlinesLCD = new QAction(tr("Vertical"), this);
    m_actScanlinesLCD->setCheckable(true);
    m_actScanlinesLCD->setChecked(m_scanlinesMode == ScanlinesLCD);
    m_actScanlinesLCD->setData(ScanlinesLCD);
    m_scanlinesGroup->addAction(m_actScanlinesLCD);
    m_scanlinesMenu->addAction(m_actScanlinesLCD);
    // Optie 4: LCD Raster (RasterizeImage)
    m_actScanlinesRaster = new QAction(tr("Raster"), this);
    m_actScanlinesRaster->setCheckable(true);
    m_actScanlinesRaster->setChecked(m_scanlinesMode == ScanlinesRaster);
    m_actScanlinesRaster->setData(ScanlinesRaster);
    m_scanlinesGroup->addAction(m_actScanlinesRaster);
    m_scanlinesMenu->addAction(m_actScanlinesRaster);

    videoMenu->addSeparator();
    m_colorFilterMenu = videoMenu->addMenu(tr("Color Filter"));
    m_colorFilterGroup = new QActionGroup(this);
    m_colorFilterGroup->setExclusive(true);

    // Off
    QAction *actFilterOff = new QAction(tr("Off"), this);
    actFilterOff->setCheckable(true);
    actFilterOff->setData(ColorFilterOff);
    actFilterOff->setChecked(m_colorFilterMode == ColorFilterOff);
    m_colorFilterGroup->addAction(actFilterOff);
    m_colorFilterMenu->addAction(actFilterOff);
    m_colorFilterMenu->addSeparator();

    // Monochrome
    QAction *actFilterMono = new QAction(tr("Monochrome"), this);
    actFilterMono->setCheckable(true);
    actFilterMono->setData(ColorFilterMonochrome);
    actFilterMono->setChecked(m_colorFilterMode == ColorFilterMonochrome);
    m_colorFilterGroup->addAction(actFilterMono);
    m_colorFilterMenu->addAction(actFilterMono);

    // Sepia Tones
    QAction *actFilterSepia = new QAction(tr("Sepia Tones"), this);
    actFilterSepia->setCheckable(true);
    actFilterSepia->setData(ColorFilterSepia);
    actFilterSepia->setChecked(m_colorFilterMode == ColorFilterSepia);
    m_colorFilterGroup->addAction(actFilterSepia);
    m_colorFilterMenu->addAction(actFilterSepia);

    // Green CRT
    QAction *actFilterGreen = new QAction(tr("Green CRT"), this);
    actFilterGreen->setCheckable(true);
    actFilterGreen->setData(ColorFilterGreenCRT);
    actFilterGreen->setChecked(m_colorFilterMode == ColorFilterGreenCRT);
    m_colorFilterGroup->addAction(actFilterGreen);
    m_colorFilterMenu->addAction(actFilterGreen);

    // Amber CRT
    QAction *actFilterAmber = new QAction(tr("Amber CRT"), this);
    actFilterAmber->setCheckable(true);
    actFilterAmber->setData(ColorFilterAmberCRT);
    actFilterAmber->setChecked(m_colorFilterMode == ColorFilterAmberCRT);
    m_colorFilterGroup->addAction(actFilterAmber);
    m_colorFilterMenu->addAction(actFilterAmber);

    // CMY Raster
    QAction *actFilterCMY = new QAction(tr("CMY Raster"), this);
    actFilterCMY->setCheckable(true);
    actFilterCMY->setData(ColorFilterCMY);
    actFilterCMY->setChecked(m_colorFilterMode == ColorFilterCMY);
    m_colorFilterGroup->addAction(actFilterCMY);
    m_colorFilterMenu->addAction(actFilterCMY);

    // RGB Raster
    QAction *actFilterRGB = new QAction(tr("RGB Raster"), this);
    actFilterRGB->setCheckable(true);
    actFilterRGB->setData(ColorFilterRGB);
    actFilterRGB->setChecked(m_colorFilterMode == ColorFilterRGB);
    m_colorFilterGroup->addAction(actFilterRGB);
    m_colorFilterMenu->addAction(actFilterRGB);

    // React op wijziging
    connect(m_colorFilterGroup, &QActionGroup::triggered,
            this, &MainWindow::onColorFilterModeChanged);



    videoMenu->addSeparator();
    m_scalingMenu = videoMenu->addMenu(tr("Video Mode"));
    m_scalingGroup = new QActionGroup(this);
    m_scalingGroup->setExclusive(true);
    // Scaling Mode 2: Sharp
    m_actScalingSharp = new QAction(tr("Sharp"), this);
    m_actScalingSharp->setCheckable(true);
    // De waarde '0' correspondeert met ModeSharp
    m_actScalingSharp->setData(0);
    m_scalingGroup->addAction(m_actScalingSharp);
    m_scalingMenu->addAction(m_actScalingSharp);
    // Scaling Mode 1: Smooth
    m_actScalingSmooth = new QAction(tr("Smooth"), this);
    m_actScalingSmooth->setCheckable(true);
    // De waarde '1' correspondeert met ModeSmooth
    m_actScalingSmooth->setData(1);
    m_scalingGroup->addAction(m_actScalingSmooth);
    m_scalingMenu->addAction(m_actScalingSmooth);
    // Scaling Mode 3: EPX
    m_actScalingEPX = new QAction(tr("EPX Filter (2x)"), this);
    m_actScalingEPX->setCheckable(true);
    // De waarde '2' correspondeert met ModeEPX
    m_actScalingEPX->setData(2);
    m_scalingGroup->addAction(m_actScalingEPX);
    m_scalingMenu->addAction(m_actScalingEPX);
    connect(m_scalingGroup, &QActionGroup::triggered, this, &MainWindow::onScalingModeChanged);

    if (m_scalingGroup) {
        const QList<QAction*> actions = m_scalingGroup->actions();
        for (int i = 0; i < actions.size(); ++i) {
            QAction* action = actions.at(i);
            if (action && action->data().toInt() == m_scalingMode) {
                action->setChecked(true);
                break;
            }
        }
    }
    videoMenu->addSeparator();

    // Volledig scherm
    m_actFullScreen = new QAction(tr("Full Screen"), this);
    m_actFullScreen->setCheckable(true);
    m_actFullScreen->setChecked(m_startFullScreen);
    m_actFullScreen->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    videoMenu->addAction(m_actFullScreen);
    // Reset Window Size
    m_actResetSize = new QAction(tr("Reset Window Size"), this);
    connect(m_actResetSize, &QAction::triggered, this, &MainWindow::onResetWindowSize);
    videoMenu->addAction(m_actResetSize);
    videoMenu->addSeparator();
    // Show Bezels/Wallpaper
    m_actToggleBezels = new QAction(tr("Show Bezels"), this);
    m_actToggleBezels->setCheckable(true);
    m_actToggleBezels->setChecked(m_useBezels);
    connect(m_actToggleBezels, &QAction::toggled, this, &MainWindow::onToggleBezels);

    videoMenu->addAction(m_actToggleBezels);
    videoMenu->addSeparator();
    // Save Screenshot
    m_actSaveScreenshot = new QAction(tr("Save Screenshot..."), this);
    videoMenu->addAction(m_actSaveScreenshot);

    // --- HARDWARE MENU ---
    QMenu* hardwareMenu = menuBar()->addMenu(tr("Hardware"));

    hardwareMenu->addSeparator();
    m_actHardware = new QAction(tr("Hardware selection"), this);
    hardwareMenu->addAction(m_actHardware);
    connect(m_actHardware, &QAction::triggered, this, &MainWindow::onOpenHardware);
    hardwareMenu->addSeparator();

    m_actDTsound = new QAction(tr("Disc-Tape sounds"), this);
    hardwareMenu->addAction(m_actDTsound);
    m_actDTsound->setCheckable(true);
    m_actDTsound->setChecked(m_useDTsound);
    connect(m_actDTsound, &QAction::toggled, this, &MainWindow::onToggleDTsound);

    hardwareMenu->addSeparator();
    m_actPrinterOutput = hardwareMenu->addAction("Printer Output", this, &MainWindow::onShowPrinterWindow);
    m_actPrinterOutput->setShortcut(QKeySequence("Ctrl+Shift+P"));
    hardwareMenu->addSeparator();

    m_biosSourceMenu = hardwareMenu->addMenu(tr("BIOS Source"));

    m_actColecoBiosSource = m_biosSourceMenu->addAction(tr("Coleco: Internal"));
    m_actEosBiosSource    = m_biosSourceMenu->addAction(tr("EOS: Internal"));
    m_actWriterBiosSource = m_biosSourceMenu->addAction(tr("Writer: Internal"));

    // Maak ze read-only, ze dienen alleen als status-indicator
    m_actColecoBiosSource->setEnabled(false);
    m_actEosBiosSource->setEnabled(false);
    m_actWriterBiosSource->setEnabled(false);

    hardwareMenu->addSeparator();


    // --- OPTIONS MENU ---
    QMenu* optionsMenu = menuBar()->addMenu(tr("Options"));

    m_actToggleSnap = new QAction(tr("Snap to mainwindow"), this);
    m_actToggleSnap->setCheckable(true);
    m_actToggleSnap->setChecked(m_snapWindows);
    connect(m_actToggleSnap, &QAction::toggled, this, &MainWindow::onToggleSnap);
    optionsMenu->addAction(m_actToggleSnap);

    optionsMenu->addSeparator();

    m_adamInputMenu = optionsMenu->addMenu(tr("ADAM game mode"));
    m_adamInputGroup = new QActionGroup(this);
    m_adamInputGroup->setExclusive(true);

    m_actAdamGameOn = new QAction(tr("ENABLED"), this);
    m_actAdamGameOn->setCheckable(true);
    m_actAdamGameOn->setChecked(false);
    m_adamInputGroup->addAction(m_actAdamGameOn);
    m_adamInputMenu->addAction(m_actAdamGameOn);

    m_actAdamGameOff = new QAction(tr("DISABLED"), this);
    m_actAdamGameOff->setCheckable(true);
    m_actAdamGameOff->setChecked(true);
    m_adamInputGroup->addAction(m_actAdamGameOff);
    m_adamInputMenu->addAction(m_actAdamGameOff);

    m_adamInputMenu->setEnabled(false);

    // --- HELP MENU ---
    QMenu* helpMenu = menuBar()->addMenu(tr("Help"));
    m_actWiki = new QAction(tr("Github page"), this);
    helpMenu->addAction(m_actWiki);
    m_actReport = new QAction(tr("Report a bug"), this);
    helpMenu->addAction(m_actReport);
    helpMenu->addSeparator();
    m_actDonate = new QAction(tr("Donate"), this);
    helpMenu->addAction(m_actDonate);
    helpMenu->addSeparator();
    m_actAbout = new QAction(tr("About"), this);
    helpMenu->addAction(m_actAbout);

    // --- CONNECTIES ---
    connect(m_actWiki, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/dvdh1961/ADAMP"));
    });
    connect(m_actReport, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/dvdh1961/ADAMP/issues"));
    });
    connect(m_actDonate, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://www.paypal.com/donate?business=dannyvdh@pandora.be"));
    });

    // Scanlines connectie
    connect(m_scanlinesGroup, &QActionGroup::triggered, this, &MainWindow::onScanlinesModeChanged);
    connect(m_actToggleNTSC, &QAction::triggered, this, &MainWindow::onToggleVideoStandard);
    connect(m_actTogglePAL, &QAction::triggered, this, &MainWindow::onToggleVideoStandard);
    connect(m_actToggleKeyboard, &QAction::toggled, this, &MainWindow::onToggleKeyboard);
    connect(m_cartInfoAction, &QAction::triggered, this, &MainWindow::onOpenCartInfo);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAboutDialog);
    connect(m_actFullScreen, &QAction::toggled, this, &MainWindow::onToggleFullScreen);
    connect(m_actSaveScreenshot, &QAction::triggered,this, &MainWindow::onSaveScreenshot);
    connect(m_actJoypadMapper, &QAction::triggered, this, &MainWindow::onOpenJoypadMapper);
    connect(m_actAdamGameOff, &QAction::triggered, this, &MainWindow::onAdamGameMode);
    connect(m_actAdamGameOn, &QAction::triggered, this, &MainWindow::onAdamGameMode);

    connect(m_powerBtn, &QPushButton::clicked, this, &MainWindow::onPowerBtnClicked);
    connect(m_resetAdamBtn, &QPushButton::clicked, this, &MainWindow::onResetAdamBtnClicked);
    connect(m_resetCartBtn, &QPushButton::clicked, this, &MainWindow::onResetCartBtnClicked);

    onEmuPausedChanged(false);
}

//---------------------------------------------------------------------------------------------
// PREPARE LOGGER DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::setUpLogWindow()
{
    const int w = 770;
    const int h = 500;
    m_logView = new LogWidget(nullptr);
    LogWidget::bindTo(m_logView);
    m_logView->setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    m_logView->setWindowTitle("ADAM+ Debug logger");
    m_logView->resize(w, h);

    m_logView->setAttribute(Qt::WA_DeleteOnClose, false);
    m_logView->hide();
    m_logView->installEventFilter(this);
}

//---------------------------------------------------------------------------------------------
// OPEN/LOAD & SAVE SETTINGS DIALOGS
//---------------------------------------------------------------------------------------------

void MainWindow::onOpenSettings()
{
    if (!m_settingsWindow)
        return;

    // Helper om defaults/relatief -> absoluut te maken
    const QDir appDir(QCoreApplication::applicationDirPath());
    auto resolvePath = [&](QString input, const QString& mediaSubdir) -> QString {
        input = input.trimmed();

        if (input.isEmpty() || input == ".")
            return QDir::cleanPath(appDir.filePath("media/" + mediaSubdir));

        if (QDir::isRelativePath(input))
            input = QDir::cleanPath(appDir.filePath(input));

        return input;
    };

    // 1) Push huidige settings naar dialog
    struct PathBind {
        void (SettingsWindow::*set)(const QString&);
        QString (SettingsWindow::*get)() const;
        QString* target;
        const char* subdir;
    };

    PathBind paths[] = {
        { &SettingsWindow::setRomPath,         &SettingsWindow::romPath,         &m_romPath,         "roms" },
        { &SettingsWindow::setDiskPath,        &SettingsWindow::diskPath,        &m_diskPath,        "disks" },
        { &SettingsWindow::setTapePath,        &SettingsWindow::tapePath,        &m_tapePath,        "tapes" },
        { &SettingsWindow::setStatePath,       &SettingsWindow::statePath,       &m_statePath,       "states" },
        { &SettingsWindow::setBreakpointPath,  &SettingsWindow::breakpointPath,  &m_breakpointPath,  "breakpoints" },
        { &SettingsWindow::setScreenshotPath,  &SettingsWindow::screenshotPath,  &m_screenshotsPath, "screenshots" },
        { &SettingsWindow::setSymbolPath,      &SettingsWindow::symbolPath,      &m_symbolsPath,     "symbols" },
        { &SettingsWindow::setAdamBezelPath,   &SettingsWindow::adamBezelPath,   &m_adamBezelPath,   "bezels" },
        { &SettingsWindow::setCvBezelPath,     &SettingsWindow::cvBezelPath,     &m_cvBezelPath,     "bezels" }
    };

    for (auto& p : paths)
        (m_settingsWindow->*(p.set))(*p.target);

    m_settingsWindow->setColecoBiosPath(m_colecoBiosPath);
    m_settingsWindow->setEosBiosPath(m_eosBiosPath);
    m_settingsWindow->setWriterBiosPath(m_writerBiosPath);
    m_settingsWindow->setAdamStartupPath(m_adamStartupPath);

    // 2) Toon dialog
    if (m_settingsWindow->exec() != QDialog::Accepted)
        return;

    // 3) Pull waarden terug uit dialog + resolve
    for (auto& p : paths) {
        const QString v = (m_settingsWindow->*(p.get))();
        *p.target = resolvePath(v, p.subdir);
    }

    m_colecoBiosPath = m_settingsWindow->colecoBiosPath();
    m_eosBiosPath    = m_settingsWindow->eosBiosPath();
    m_writerBiosPath = m_settingsWindow->writerBiosPath();
    m_adamStartupPath = m_settingsWindow->adamStartupPath();

    saveSettings();
}

void MainWindow::loadSettings()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString iniPath = appDir.filePath("settings.ini");

    QSettings settings(iniPath, QSettings::IniFormat);

    auto resolvePath = [&](const char *key, const QString &mediaSubdir, const QString& def = ".") -> QString {
        QString value = settings.value(key, def).toString().trimmed();

        // Backwards compat: "." of leeg = media/<type> naast EXE
        if (value.isEmpty() || value == ".")
            return QDir::cleanPath(appDir.filePath("media/" + mediaSubdir));

        // Relatief -> absoluut t.o.v. appDir
        if (QDir::isRelativePath(value))
            value = QDir::cleanPath(appDir.filePath(value));

        return value;
    };

    auto readEnum = [&](const char* key, int def) -> int {
        return settings.value(key, def).toInt();
    };

    auto checkActionByData = [&](QActionGroup* group, int value) {
        if (!group) return;
        const auto actions = group->actions();
        for (QAction* a : actions) {
            if (!a) continue;
            if (a->data().toInt() == value) {
                a->setChecked(true);
                break;
            }
        }
    };

    // --- Media paths (met compat + relatieve paden) ---
    m_romPath         = resolvePath("romPath",         "roms");
    m_diskPath        = resolvePath("diskPath",        "disks");
    m_tapePath        = resolvePath("tapePath",        "tapes");
    m_statePath       = resolvePath("statePath",       "states");
    m_breakpointPath  = resolvePath("breakpointPath",  "breakpoints");
    m_screenshotsPath = resolvePath("screenshotPath",  "screenshots");
    m_symbolsPath     = resolvePath("symbolsPath",     "symbols");

    // Bezel paths: geen media default, maar wel relatieve paden naar appDir oplossen
    m_adamBezelPath   = resolvePath("adambezelpath", "", ""); // "" blijft "" / "none"
    m_cvBezelPath     = resolvePath("cvbezelpath",   "", "");

    // --- Video/Machine ---
    m_paletteIndex = settings.value("video/palette", 0).toInt();

    m_vdpType = settings.value("video/vdp", 0).toInt();
    if (m_vdpType != 1)
        m_vdpType = 0;

    m_f18a80SelfTest = settings.value("video/f18a80SelfTest", false).toBool();
    if (m_vdpType != 1)
        m_f18a80SelfTest = false;

    coleco_set_vdp_type(m_vdpType);
    f18a_set_80col_selftest_enabled(m_f18a80SelfTest ? 1 : 0);

    qDebug() << "[VIDEO] Loaded VDP:"
             << (m_vdpType == 1 ? "F18A" : "TMS9928A/TMS9918A")
             << "value =" << m_vdpType;

    m_machineType = settings.value("machine/type", 0).toInt();
    m_machineType = 1; // always start in adam

    m_realhardware     = settings.value("machine/realhardware", false).toBool();

    m_scalingMode     = settings.value("video/scalingMode", 1).toInt();

    m_scanlinesMode   = static_cast<ScanlinesMode>(readEnum("video/scanlinesMode", ScanlinesOff));
    m_colorFilterMode = static_cast<ColorFilterMode>(readEnum("video/colorFilterMode", ColorFilterOff));
    checkActionByData(m_scanlinesGroup, int(m_scanlinesMode));

    m_startFullScreen = settings.value("video/fullscreen", false).toBool();
    m_useBezels       = settings.value("video/useBezels", true).toBool();

    // --- Hardware ---
    m_sgmEnabled      = settings.value("hardware/sgm",  false).toBool();
    m_c80Enabled     = settings.value("hardware/c80", false).toBool();
    m_useDTsound      = settings.value("hardware/useDTsound", true).toBool();

    // --- Controller ---
    m_joystickType    = settings.value("controller/joystickType", 0).toInt();
    m_usePaddleMode   = settings.value("controller/usePaddleMode", false).toBool();

    // Real hardware
    m_ctrlJoys    = settings.value("rhard/rJoys",    false).toBool();
    m_ctrlAdamNet      = settings.value("rhard/rAdamnet",      false).toBool();
    m_ctrlCartridge = settings.value("rhard/rCartridge", false).toBool();

    // --- BIOS ---
    m_colecoBiosPath  = settings.value("bios/coleco", "").toString();
    m_eosBiosPath     = settings.value("bios/eos",    "").toString();
    m_writerBiosPath  = settings.value("bios/writer", "").toString();
    m_adamStartupPath = settings.value("adam/startup", "").toString();

    // --- Window ---
    qDebug() << "[UI] Loaded settings";

    const QByteArray geometry = settings.value("window/geometry").toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    else
        resize(770, 700);

    m_snapWindows = settings.value("window/snap", true).toBool();

    // na m_useDTsound laden:
    if (m_colecoController) {
        QMetaObject::invokeMethod(m_colecoController, "setDTsoundEnabled",
                                  Qt::QueuedConnection, Q_ARG(bool, m_useDTsound));
    }

    if (m_inputWidget) {
        m_inputWidget->setMachineType(m_machineType);
        m_inputWidget->setAdamGameMode(false);  // Standaard keyboard mode
        qDebug() << "[MAINWINDOW] InputWidget configured with loaded settings";
    }

  m_settingsLoaded = true;
}

void MainWindow::saveSettings()
{
    if (!m_allowSaveSettings)
    {
        qDebug() << "[SETTINGS] saveSettings blocked during startup";
        return;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString iniPath = appDir.filePath("settings.ini");

    QSettings settings(iniPath, QSettings::IniFormat);

    // Kleine helper om setValue wat minder repetitief te maken
    auto put = [&](const char* key, const QVariant& v) {
        settings.setValue(key, v);
    };

    // Paths / media
    put("romPath",        m_romPath);
    put("diskPath",       m_diskPath);
    put("tapePath",       m_tapePath);
    put("statePath",      m_statePath);
    put("breakpointPath", m_breakpointPath);
    put("screenshotPath", m_screenshotsPath);
    put("symbolsPath",    m_symbolsPath);

    // Machine / UI
    put("machine/type",   m_machineType);
    put("machine/realhardware", m_realhardware);
    put("video/palette",  m_paletteIndex);
    put("video/vdp",      m_vdpType);
    put("video/f18a80SelfTest", m_f18a80SelfTest);
    put("adambezelpath",  m_adamBezelPath);
    put("cvbezelpath",    m_cvBezelPath);

    // Hardware
    put("hardware/sgm",   m_sgmEnabled);
    put("hardware/c80",  m_c80Enabled);
    put("hardware/useDTsound", m_useDTsound);

    // Controller
    put("controller/joystickType",   m_joystickType);
    put("controller/usePaddleMode",  m_usePaddleMode);

    // Real Hardware
    put("rhard/rJoys",       m_ctrlJoys);
    put("rhard/rAdamnet",         m_ctrlAdamNet);
    put("rhard/rCartridge",    m_ctrlCartridge);

    // Video
    put("video/scalingMode",     m_scalingMode);
    put("video/scanlinesMode",   int(m_scanlinesMode));
    put("video/colorFilterMode", int(m_colorFilterMode));
    put("video/fullscreen",      m_startFullScreen);
    put("video/useBezels",       m_useBezels);

    // BIOS
    put("bios/coleco", m_colecoBiosPath);
    put("bios/eos",    m_eosBiosPath);
    put("bios/writer", m_writerBiosPath);
    put("adam/startup", m_adamStartupPath);

    // Window (zet dit vóór sync)
    put("window/geometry", saveGeometry());
    put("window/snap",     m_snapWindows);

    qDebug() << "[VIDEO] saveSettings BEFORE sync:"
             << "m_vdpType =" << m_vdpType
             << (m_vdpType == 1 ? "F18A" : "TMS9928A/TMS9918A")
             << "file =" << iniPath;

    settings.sync();

    qDebug() << "[VIDEO] saveSettings AFTER sync:"
             << "stored video/vdp =" << settings.value("video/vdp").toInt()
             << "file =" << iniPath;

    QMetaObject::invokeMethod(
        m_colecoController,
        "setDTsoundEnabled",
        Qt::QueuedConnection,
        Q_ARG(bool, m_useDTsound)
        );
}

//---------------------------------------------------------------------------------------------
// SAVE SCREENSHOT DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onSaveScreenshot()
{
    // Basisdir bepalen op basis van setting + appDir
    QDir appDir(QCoreApplication::applicationDirPath());
    QString basePath = m_screenshotsPath.trimmed();

    // Leeg of "." → media/screenshots naast de exe
    if (basePath.isEmpty() || basePath == ".") {
        basePath = appDir.filePath("media/screenshots");
    }
    // Relatief → maak absoluut t.o.v. appDir
    else if (QDir::isRelativePath(basePath)) {
        basePath = appDir.filePath(basePath);
    }

    basePath = QDir::cleanPath(basePath);

    // Directory aanmaken indien nodig
    QDir dir(basePath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Basisnaam voor bestand (afgeleid van ROM)
    QString romBaseName = m_currentRomName;
    if (romBaseName.isEmpty() || romBaseName == "No cart") {
        romBaseName = "screenshot";
    } else {
        QFileInfo fi(romBaseName);
        romBaseName = fi.completeBaseName();
    }

    // Start-directory voor de save dialog
    QString initialPath = dir.absolutePath();

    QString fileName = CustomFileDialog::getSaveFileName(
        this,
        tr("Save Screenshot"),
        initialPath,
        tr("PNG Images (*.png);;All Files (*)"),
        nullptr,
        CustomFileDialog::PathScreenshot,
        QFileDialog::Options(),
        romBaseName
        );

    if (fileName.isEmpty()) {
        return;
    }

    QString finalPath = fileName;
    if (!finalPath.endsWith(".png", Qt::CaseInsensitive)) {
        finalPath += ".png";
    }

    QFileInfo fileInfo(finalPath);
    CustomFileDialog::s_lastSaveDir = fileInfo.absolutePath();

    if (m_screenWidget) {
        QImage screenshot = m_screenWidget->grab().toImage();

        if (screenshot.save(finalPath)) {
            qDebug() << "[UI] Screenshot saved to:" << finalPath;
        } else {
            QMessageBox::warning(
                this,
                tr("Error"),
                tr("Failed to save screenshot to %1.").arg(finalPath)
                );
        }
    }
}

//---------------------------------------------------------------------------------------------
// HARDWARE DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onOpenHardware()
{
    HardwareConfig cur;

    cur.machine = (m_machineType ? MACHINE_ADAM : MACHINE_COLECO); // 1:0
    cur.realhardware = m_realhardware;
    cur.palette = m_paletteIndex;
    cur.vdpType = m_vdpType;
    cur.f18a80SelfTest = m_f18a80SelfTest;

    cur.sgmEnabled  = m_sgmEnabled;
    cur.c80Enabled = m_c80Enabled;

    cur.Joys = m_ctrlJoys;
    cur.AdamNet    = m_ctrlAdamNet;
    cur.Cartridge   = m_ctrlCartridge;

    const int prevPalette = m_paletteIndex;

    HardwareWindow dlg(cur, this);

    dlg.setLoadedMediaDisplayNames(
        m_currentRomName,         // CC (Coleco Cartridge)
        m_currentARomName,        // CA (ADAM Cartridge)
        m_loadedTapeNames[0],     // D1
        m_loadedTapeNames[1],     // D2
        m_loadedDiskNames[0],     // D5
        m_loadedDiskNames[1],     // D6
        m_loadedDiskNames[2]      // D7 (Alleen de eerste 3 disks worden in de tabel getoond)
        );

    if (dlg.exec() == QDialog::Accepted) {
        HardwareConfig chosen = dlg.config();

        // Bewaar oude machine voor vergelijking
        const int oldMachine = cur.machine;
        const int newMachine = chosen.machine;

        m_paletteIndex = chosen.palette;
        m_vdpType = chosen.vdpType;
        m_f18a80SelfTest = chosen.f18a80SelfTest;
        coleco_setpalette(m_paletteIndex);
        //m_sgmEnabled = chosen.sgmEnabled;
        m_realhardware = chosen.realhardware;

        if (oldMachine != newMachine) {
            if (newMachine == MACHINE_ADAM) {
                switchToAdamMode();
            } else {
                switchToColecoMode();
            }
        }

        applyHardwareConfig(chosen);

        saveSettings();
    } else {
        m_paletteIndex = prevPalette;
        coleco_setpalette(m_paletteIndex);
    }
}


void MainWindow::updateHardwareWindowMediaDisplay()
{
    if (m_hardwareWindow) {
        m_hardwareWindow->setLoadedMediaDisplayNames(
            m_currentRomName,          // CC (Coleco Cartridge)
            m_currentARomName,         // CA (ADAM Cartridge)
            m_loadedTapeNames[0],      // D1
            m_loadedTapeNames[1],      // D2
            m_loadedDiskNames[0],      // D5
            m_loadedDiskNames[1],      // D6
            m_loadedDiskNames[2]       // D7 (Disks[2])
            );
    }
}

void MainWindow::onToggleSGM(bool checked)
{
    qDebug() << "[UI] Toggle SGM =" << checked;
    QMetaObject::invokeMethod(m_colecoController, "setSGMEnabled",
                              Qt::QueuedConnection,
                              Q_ARG(bool, checked));
    QMetaObject::invokeMethod(m_colecoController, "resetMachine",
                              Qt::QueuedConnection);
}

void MainWindow::onCartridgeStatusChanged(const QString& colecoName, const QString& adamName)
{
    m_currentRomName = colecoName;  // Coleco Cartridge (CC)
    m_currentARomName = adamName;   // ADAM Cartridge (CA)
    const QString display = (m_machineType == 1) ? m_currentARomName : m_currentRomName;
    updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel, display);

    updateHardwareWindowMediaDisplay();
}

//---------------------------------------------------------------------------------------------
// INFO DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::showAboutDialog()
{
    QDialog aboutDialog(this);
    aboutDialog.setWindowTitle("About ADAM+");
    aboutDialog.setFixedSize(620, 560);

    QVBoxLayout *layout = new QVBoxLayout(&aboutDialog);
    layout->setAlignment(Qt::AlignCenter);

    QLabel *logoLabel = new QLabel(&aboutDialog);
    QPixmap logo(":/images/images/ADAMP.png");
    logoLabel->setPixmap(logo.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(logoLabel);

    QLabel *textLabel = new QLabel(&aboutDialog);

    textLabel->setOpenExternalLinks(true);

    textLabel->setText(QString(
                           "Version %1<br>"  // %1 appVersion
                           "©2025-26 DannyVdH<br>"
                           "<a href='https://github.com/dvdh1961/ADAMP'>VDH Productions</a><br><br>"
                           "This software is free to use for personal, educational, and non-profit purposes<br>"
                           "Some software components are subject to licensing agreements held by the rightful owners<br>"
                           "The ADAM+ emulator is built using the latest available techniques and technologies<br>"
                           "obtainable in 2025. Leveraging deep expertise and the assistance of advanced<br>"
                           "Language Models (LLMs), we can achieve the full potential of our programming<br>"
                           "skills with exceptional speed and accuracy.<br><br>"
                           "The goal is to go even deeper into my ADAM+ hardware project<br>"
                           " — this ADAM+ emulator — <br>"
                           "will go much further in integrating specific hardware components.<br><br>"
                           "Credits goes to all the brilliant coders worldwide!<br>"
                           "A lot of interfacing and parts of code based on the EmulTwo project.<br>"
                           "Parts of ADAM emulation code from Marat Fayzullin’s ColEm project.<br>"
                           "Wavemotion-dave, for improving compatibility issues.<br>"
                           "Parts of EightyOne created by Michael D Wynne.<br>"
                           "Z80 core taken from Juergen Buchmueller.<br>"
                           "AY8910 code from Z81 ©1995–2001 Russell Marks.<br><br>"
                           "And all the ones that were involved and that I forgot to mention.<br><br>"
                           ).arg(appVersion));

    textLabel->setOpenExternalLinks(true);
    textLabel->setWordWrap(true);
    textLabel->setAlignment(Qt::AlignCenter);

    // 1. Laad het lettertype uit je resources of lokale map
    // Pas het pad aan naar waar jouw .ttf staat (bijv. ":/fonts/mijnfont.ttf")
    int fontId = QFontDatabase::addApplicationFont(":/fonts/fonts/luculent.ttf");

    QString family;

    if (fontId != -1) {
        family = QFontDatabase::applicationFontFamilies(fontId).at(0);
        //qDebug() << "[UI] Custom font geladen:" << family;
    } else {
        qDebug() << "[UI] Kon custom font niet laden, fallback naar Roboto";
        family = "Roboto";
    }

    QFont monoFont(family, 10);
    monoFont.setBold(false);

   textLabel->setFont(monoFont);
    layout->addWidget(textLabel);
    layout->addStretch(1);

    QIcon okIcon(":/images/images/OK.png");
    QPixmap okPixmap(":/images/images/OK.png");
    if (okIcon.isNull()) {
        qWarning() << "AboutDialog: Kon OK.png niet laden.";
    }

    QPushButton *okButton = new QPushButton(&aboutDialog);

    okButton->setIcon(okIcon);
    okButton->setIconSize(okPixmap.size());
    okButton->setFixedSize(okPixmap.size());
    okButton->setText("");
    okButton->setFlat(true);
    okButton->setCursor(Qt::PointingHandCursor);
    okButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    connect(okButton, &QPushButton::clicked, &aboutDialog, &QDialog::accept);
    layout->addWidget(okButton, 0, Qt::AlignCenter);

    aboutDialog.exec();
}

//---------------------------------------------------------------------------------------------
// NAMETABLE DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onShowNameTable()
{
        m_ntableWindow->show();
}

//---------------------------------------------------------------------------------------------
// PATTERNTABLE DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onShowPatternTable()
{
        m_patternWindow->show();
}

//---------------------------------------------------------------------------------------------
// SPRITETABLE DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onShowSpriteTable()
{
        m_spriteWindow->show();
}

void MainWindow::onOpenCartInfo()
{
    if (!m_cartInfoDialog) {
        m_cartInfoDialog = new CartridgeInfoDialog(this);
    }

    m_cartInfoDialog->refreshData();

    m_cartInfoDialog->show();
    m_cartInfoDialog->raise();
    m_cartInfoDialog->activateWindow();
}

//---------------------------------------------------------------------------------------------
// VIDEO OPTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::onToggleVideoStandard()
{
    bool isNTSC = m_actToggleNTSC->isChecked();
    qDebug() << "[UI] Video standard set to" << (isNTSC ? "NTSC" : "PAL");

    QMetaObject::invokeMethod(m_colecoController, "setVideoStandard",
                              Qt::QueuedConnection,
                              Q_ARG(bool, isNTSC));
}

void MainWindow::onCycleScalingMode()
{
    m_scalingMode = (m_scalingMode + 1) % 3;

    QString scaleText;
    if (m_scalingMode == 0) {
        scaleText = "Scaling: Sharp";
    } else if (m_scalingMode == 1) {
        scaleText = "Scaling: Smooth";
    } else {
        scaleText = "Scaling: EPX";
    }

    m_actToggleSmoothing->setText(scaleText);

    if (m_screenWidget) {
        m_screenWidget->setScalingMode(static_cast<ScreenWidget::ScalingMode>(m_scalingMode));
    }

    saveSettings();
}

void MainWindow::onScanlinesModeChanged(QAction* action)
{
    ScanlinesMode newMode = static_cast<ScanlinesMode>(action->data().toInt());

    if (m_scanlinesMode == newMode) {
        return;
    }

    m_scanlinesMode = newMode;
    qDebug() << "[UI] Scanlines Mode changed to" << newMode;

    saveSettings();

    if (m_screenWidget) {
        m_screenWidget->setScanlinesMode(newMode);
    }
}

void MainWindow::onColorFilterModeChanged(QAction* action)
{
    ColorFilterMode newMode = static_cast<ColorFilterMode>(action->data().toInt());

    if (m_colorFilterMode == newMode) {
        return;
    }

    m_colorFilterMode = newMode;
    qDebug() << "[UI] Color Filter Mode changed to" << newMode;

    saveSettings();

    if (m_screenWidget) {
        m_screenWidget->setColorFilterMode(newMode);
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_logoLabel1 && event->type() == QEvent::MouseButtonPress) {
        onOpenHardware();
        return true;
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

//---------------------------------------------------------------------------------------------
// VIDEO SET GAMESCREEN
//---------------------------------------------------------------------------------------------

void MainWindow::onFrameReceived(const QImage &frame)
{
    if (!m_screenWidget || frame.isNull()) return;

    m_screenWidget->setFrame(frame);
}

//---------------------------------------------------------------------------------------------
// EMULATOR MAIN THREAD
//---------------------------------------------------------------------------------------------

void MainWindow::setupEmulatorThread()
{
    qDebug() << "[UI] setupEmulatorThread()";

    m_emulatorThread = new QThread(this);
    m_colecoController = new ColecoController();

    // === SET 80-COL VOOR THREAD START ===
    coleco_80col_enabled = m_c80Enabled ? 1 : 0;
    qDebug() << "[UI] Setting coleco_80col_enabled =" << coleco_80col_enabled;

    connect(m_colecoController, &ColecoController::onBiosStatusUpdated,
            this, &MainWindow::onBiosStatusUpdated,
            Qt::QueuedConnection);

    m_colecoController->moveToThread(m_emulatorThread);

    connect(m_colecoController, &ColecoController::biosCFramesDone,
            this, &MainWindow::onBiosCFramesDone,
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::biosAFramesDone,
            this, &MainWindow::onBiosAFramesDone,
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::frameReady,
            this,               &MainWindow::onFrameReceived,
            Qt::QueuedConnection);


    connect(m_colecoController, &ColecoController::frameReady,
            this, &MainWindow::onFramePresented,
            Qt::QueuedConnection);

    connect(m_colecoController, SIGNAL(videoStandardChanged(QString)),
            this, SLOT(setVideoStandard(QString)),
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::fpsUpdated,
            this, &MainWindow::onFpsUpdated,
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::sgmStatusChanged,
            this, &MainWindow::onSgmStatusChanged,
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::emuPausedChanged,
            this, &MainWindow::onEmuPausedChanged,
            Qt::QueuedConnection);

    auto isMediaImage = [](const QString& path) -> bool {
        const QString ext = QFileInfo(path).suffix().toLower();
        return ext == "dsk" || ext == "img" || ext == "ddp";
    };

    if (isMediaImage(m_writerBiosPath)) {
        qWarning() << "[UI][BIOSPATH] Invalid writer BIOS path (media image detected):" << m_writerBiosPath;
        m_writerBiosPath.clear();
    }

    connect(m_emulatorThread, &QThread::started, m_colecoController, [this]() {
        m_colecoController->startWithBios(m_colecoBiosPath, m_eosBiosPath, m_writerBiosPath);
    });

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

    QMetaObject::invokeMethod(m_colecoController, "setSGMEnabled",
                              Qt::QueuedConnection,
                              Q_ARG(bool, m_sgmEnabled));

    connect(
        m_colecoController, &ColecoController::frameReady,
        this,
        [this](const QImage &) {
            QMetaObject::invokeMethod(
                m_colecoController,
                [this]() { coleco_setpalette(m_paletteIndex); },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection
        );

    connect(m_colecoController, &ColecoController::tapeStatusChanged,
            this, &MainWindow::onTapeStatusChanged,
            Qt::QueuedConnection);
    connect(m_colecoController, &ColecoController::diskStatusChanged,
            this, &MainWindow::onDiskStatusChanged,
            Qt::QueuedConnection);
    connect(m_colecoController, &ColecoController::cartridgeStatusChanged, // NIEUW
            this, &MainWindow::onCartridgeStatusChanged,
            Qt::QueuedConnection);

    qDebug() << "[UI] --- EMULATOR RUNNING ---";

}

void MainWindow::onThreadFinished()
{
    qDebug() << "[UI] MainWindow: 'emulationStopped' received.";
    m_emulatorThread->quit();
}

void MainWindow::onFramePresented()
{
    if (m_debugWin && m_debugWin->isVisible()) {
        m_debugWin->updateAllViews();
    }

    if (m_ntableWindow && m_ntableWindow->isVisible()) {
        m_ntableWindow->doRefresh();
    }
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

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);

    if (isMaximized()) {
        return;
    }

    if (m_snapWindows) {
        positionDebugger();
        positionPrinter();
    }
}


//---------------------------------------------------------------------------------------------
// GUI CLOSING EMULATOR FUNCTION
//---------------------------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "[UI] CLOSE Application.";

    if (m_debugWin && m_debugWin->isVisible())
    {
        m_debugWin->close();
        m_debugWin = nullptr;
    }
    if (m_logView && m_logView->isVisible())
    {
        m_logView->close();
        m_logView = nullptr;
    }

    saveSettings();

    QProgressDialog dlg(
        "Closing emulator…\nPlease wait...",
        QString(),
        0, 0,
        this
        );
    dlg.setWindowTitle("Closing...");
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setCancelButton(nullptr);
    dlg.setMinimumDuration(0);
    dlg.show();

    // Laat Qt de dialog tekenen
    qApp->processEvents(QEventLoop::AllEvents, 50);

    // Als de emulator-thread draait: vraag hem om te stoppen
    if (m_emulatorThread && m_emulatorThread->isRunning()) {

        if (m_colecoController) {
            m_colecoController->stopEmulation();   // zet m_running = false;
        }

        // Geef de thread een beetje tijd om netjes uit de lus te komen
        QElapsedTimer timer;
        timer.start();
        while (m_emulatorThread->isRunning() && timer.elapsed() < 5000) {
            qApp->processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }

        if (m_emulatorThread->isRunning()) {
            qWarning() << "[UI] Thread didn't stop in time, forcing terminate.";
            m_emulatorThread->terminate();
            m_emulatorThread->wait();
        }
    }

    const int minVisibleMs = 1000;
    QElapsedTimer visibleTimer;
    visibleTimer.start();
    while (visibleTimer.elapsed() < minVisibleMs) {
        qApp->processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }

    event->accept();
    fflush(nullptr);

    qDebug() << "[UI] --- CLOSE ---";
}

//---------------------------------------------------------------------------------------------
// GUI SELECTOR & INDICATOR EMULATOR STOP/RUN
//---------------------------------------------------------------------------------------------

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

//---------------------------------------------------------------------------------------------
// GUI SELECTOR LOADING SYSTEM ROMS,TAPES & DISKS
//---------------------------------------------------------------------------------------------

void MainWindow::onBiosCFramesDone()
{
    if (m_machineType != 0) return;

    m_isColecoRomLoaded = true;

    // Start de Cartridge Reset knop te knipperen (NIEUW)
    if (m_resetCartBlinkTimer && !m_resetCartBlinkTimer->isActive()) {
        onToggleResetCartBlink(); // Zet direct de eerste blink-status
        m_resetCartBlinkTimer->start(300); // Start knipperen (300ms interval)
    }

    updateMediaMenuState();
    updateMediaStatusLabels();
}

void MainWindow::onOpenColecoRom()
{
    // Hier doe je wat “na het wachten” moet gebeuren, bv ROM laden:
    if (m_machineType != 0) return;

    QString absoluteRomPath = QDir::cleanPath(m_romPath);

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Open COLECO Cartridge ROM"),
        absoluteRomPath,
        tr("COLECO ROM files (*.rom *.bin *.col);;All files (*.*)"),
        nullptr,
        CustomFileDialog::PathRom,
        QFileDialog::Options()
        );

    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    m_ColecoMedia_insert = true;

    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    m_currentARomName = fileInfo.fileName();

    QMetaObject::invokeMethod(m_colecoController, "ColecoCartridge",
                              Qt::QueuedConnection,
                              Q_ARG(QString, filePath));

    QMetaObject::invokeMethod(m_colecoController,
                              "prepareForNewCRomAndPauseOnBios",
                              Qt::QueuedConnection);

}

void MainWindow::onEjectColecoRom()
{
    if (!m_isColecoRomLoaded) return;

    QMetaObject::invokeMethod(m_colecoController, "ejectColecoCartridge",
                              Qt::QueuedConnection);

    m_isColecoRomLoaded = false;
    m_currentARomName.clear();
    updateMediaMenuState();
    updateMediaStatusLabels();
}

void MainWindow::onBiosAFramesDone()
{
    if (m_machineType != 1) return;
    m_isAdamRomLoaded = true;
    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onOpenAdamRom()
{
    if (m_machineType != 1) return;

    QString absoluteRomPath = QDir::cleanPath(m_romPath);

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Open ADAM Cartridge ROM"),
        absoluteRomPath,
        tr("ADAM ROM files (*.rom *.bin);;All files (*.*)"),
        nullptr,
        CustomFileDialog::PathRom,
        QFileDialog::Options()
        );

    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    m_currentARomName = fileInfo.fileName();
    m_isAdamRomLoaded = true;

    m_currentRomName.clear();
    m_isColecoRomLoaded = false;

    QMetaObject::invokeMethod(m_colecoController, "AdamCartridge",
                              Qt::QueuedConnection,
                              Q_ARG(QString, filePath));

    QMetaObject::invokeMethod(m_colecoController,
                              "prepareForNewARomAndPauseOnBios",
                              Qt::QueuedConnection);

}

void MainWindow::onEjectAdamRom()
{
    if (!m_isAdamRomLoaded) return;

    QMetaObject::invokeMethod(m_colecoController, "ejectAdamCartridge",
                              Qt::QueuedConnection);

    m_isAdamRomLoaded = false;
    m_currentARomName.clear();
    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onLoadDisk(int drive)
{
    if (m_machineType != 1) return;

    QString absoluteDiskPath = QDir::cleanPath(m_diskPath);

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Open ADAM Disk Image"),
        absoluteDiskPath,
        tr("ADAM Disk (*.dsk *.img);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathDisk,
        QFileDialog::Options()
        );

    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    //QDir appDir(QCoreApplication::applicationDirPath());
    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    m_loadedDiskNames[drive] = fileInfo.fileName();

    switch (drive) {
    case 0: m_isDiskLoadedA = true; break;
    case 1: m_isDiskLoadedB = true; break;
    case 2: m_isDiskLoadedC = true; break;
    case 3: m_isDiskLoadedD = true; break;
    }

    updateMediaMenuState();
    updateMediaStatusLabels();
    forceStatusBarMediaFlags();
    updateHardwareWindowMediaDisplay();

    QMetaObject::invokeMethod(m_colecoController, "loadDisk",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive),
                              Q_ARG(QString, filePath));

    // Start de ADAM Reset knop te knipperen (NIEUW)
    if (!m_AdamDMedia_insert && !m_AdamTMedia_insert) {
    if (m_resetAdamBlinkTimer && !m_resetAdamBlinkTimer->isActive()) {
        onToggleResetAdamBlink(); // Zet direct de eerste blink-status
        m_resetAdamBlinkTimer->start(300); // Start knipperen (300ms interval)
    }
    }
    m_AdamDMedia_insert = true;
}

void MainWindow::onLoadTape(int drive)
{
    if (m_machineType != 1) return;

    QString absoluteTapePath = QDir::cleanPath(m_tapePath);

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Open ADAM Tape Image"),
        absoluteTapePath,
        tr("ADAM Tape (*.ddp);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathTape,
        QFileDialog::Options()
        );

    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    //QDir appDir(QCoreApplication::applicationDirPath());
    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    m_loadedTapeNames[drive] = fileInfo.fileName();

    switch (drive) {
    case 0: m_isTapeLoadedA = true; break;
    case 1: m_isTapeLoadedB = true; break;
    case 2: m_isTapeLoadedC = true; break;
    case 3: m_isTapeLoadedD = true; break;
    }

    updateMediaMenuState();
    updateMediaStatusLabels();
    forceStatusBarMediaFlags();
    updateHardwareWindowMediaDisplay();

    QMetaObject::invokeMethod(m_colecoController, "loadTape",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive),
                              Q_ARG(QString, filePath));

    // Start de ADAM Reset knop te knipperen (NIEUW)
    if (!m_AdamDMedia_insert && !m_AdamTMedia_insert) {
    if (m_resetAdamBlinkTimer && !m_resetAdamBlinkTimer->isActive()) {
        onToggleResetAdamBlink(); // Zet direct de eerste blink-status
        m_resetAdamBlinkTimer->start(300); // Start knipperen (300ms interval)
     }
    }
    m_AdamTMedia_insert = true;
}

void MainWindow::onEjectDisk(int drive)
{
    qDebug() << "[UI] Eject/Save Disk " << drive + 5;
    QMetaObject::invokeMethod(m_colecoController, "ejectDisk",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive)); // Drive 0

    m_loadedDiskNames[drive].clear();

    switch (drive) {
    case 0: m_isDiskLoadedA = false; break;
    case 1: m_isDiskLoadedB = false; break;
    case 2: m_isDiskLoadedC = false; break;
    case 3: m_isDiskLoadedD = false; break;
    }

    m_AdamDMedia_insert =
        !m_loadedDiskNames[0].isEmpty() ||
        !m_loadedDiskNames[1].isEmpty() ||
        !m_loadedDiskNames[2].isEmpty() ||
        !m_loadedDiskNames[3].isEmpty();

    updateMediaMenuState();
    updateMediaStatusLabels();
    forceStatusBarMediaFlags();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onEjectTape(int drive)
{
    qDebug() << "[UI] Eject/Save Tape " << drive;
    QMetaObject::invokeMethod(m_colecoController, "ejectTape",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive)); // Drive 0

    m_loadedTapeNames[drive].clear();

    switch (drive) {
    case 0: m_isTapeLoadedA = false; break;
    case 1: m_isTapeLoadedB = false; break;
    case 2: m_isTapeLoadedC = false; break;
    case 3: m_isTapeLoadedD = false; break;
    }

    m_AdamTMedia_insert =
        !m_loadedTapeNames[0].isEmpty() ||
        !m_loadedTapeNames[1].isEmpty() ||
        !m_loadedTapeNames[2].isEmpty() ||
        !m_loadedTapeNames[3].isEmpty();

    updateMediaMenuState();
    updateMediaStatusLabels();
    forceStatusBarMediaFlags();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onDiskStatusChanged(int drive, const QString& fileName)
{
    if (drive < 0 || drive >= 4)
        return;

    const QIcon checkIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);

    // Bewaar naam
    m_loadedDiskNames[drive] = fileName;

    // Mapping per drive
    struct DriveUI {
        bool* isLoaded;
        QMenu* menu;
        QLabel* label;
        const char* prefix;   // "D5:", "D6:", ...
    };

    DriveUI ui[4] = {
        { &m_isDiskLoadedA, m_diskMenuA, m_diskLabelA, "D5:" },
        { &m_isDiskLoadedB, m_diskMenuB, m_diskLabelB, "D6:" },
        { &m_isDiskLoadedC, m_diskMenuC, m_diskLabelC, "D7:" },
        { &m_isDiskLoadedD, m_diskMenuD, m_diskLabelD, "D8:" }
    };

    DriveUI& d = ui[drive];

    // Loaded flag + icon
    *d.isLoaded = !fileName.isEmpty();
    if (d.menu)
        d.menu->setIcon(*d.isLoaded ? checkIcon : QIcon());

    // Label met elide
    if (d.label) {
        const QString text = QString("%1 %2").arg(d.prefix, fileName.isEmpty() ? "-" : fileName);
        const QFontMetrics fm(d.label->font());
        d.label->setText(fm.elidedText(text, Qt::ElideRight, d.label->width()));
    }

    updateMediaStatusLabels();
    updateMediaMenuState();
    forceStatusBarMediaFlags();
}

void MainWindow::onTapeStatusChanged(int drive, const QString& fileName)
{
    if (drive < 0 || drive >= 4)
        return;

    const QIcon checkIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);

    // Bewaar naam
    m_loadedTapeNames[drive] = fileName;

    struct DriveUI {
        bool* isLoaded;
        QMenu* menu;
        QLabel* label;
        const char* prefix; // "D1:", "D2:", ...
    };

    DriveUI ui[4] = {
        { &m_isTapeLoadedA, m_tapeMenuA, m_tapeLabelA, "D1:" },
        { &m_isTapeLoadedB, m_tapeMenuB, m_tapeLabelB, "D2:" },
        { &m_isTapeLoadedC, m_tapeMenuC, m_tapeLabelC, "D3:" },
        { &m_isTapeLoadedD, m_tapeMenuD, m_tapeLabelD, "D4:" }
    };

    DriveUI& d = ui[drive];

    // Loaded flag + icon
    *d.isLoaded = !fileName.isEmpty();
    if (d.menu)
        d.menu->setIcon(*d.isLoaded ? checkIcon : QIcon());

    // Label met elide
    if (d.label) {
        const QString text = QString("%1 %2").arg(d.prefix, fileName.isEmpty() ? "-" : fileName);
        const QFontMetrics fm(d.label->font());
        d.label->setText(fm.elidedText(text, Qt::ElideRight, d.label->width()));
    }

    // 1× is genoeg
    updateMediaMenuState();
    updateMediaStatusLabels();
    forceStatusBarMediaFlags();
}

void MainWindow::updateMediaStatusLabels()
{
    const bool isAdam = (m_machineType == MACHINE_ADAM);

    const bool showDisk = isAdam;
    const bool showTape = isAdam;

    QLabel* tapeWidgets[] = {
        m_tapeLabelA, m_sepLabelMedia2a,
        m_tapeLabelB, m_sepLabelMedia2b,
        m_tapeLabelC, m_sepLabelMedia2c,
        m_tapeLabelD, m_sepLabelMedia2d
    };

    QLabel* diskWidgets[] = {
        m_diskLabelA, m_sepLabelMedia1a,
        m_diskLabelB, m_sepLabelMedia1b,
        m_diskLabelC, m_sepLabelMedia1c,
        m_diskLabelD, m_sepLabelMedia1d
    };

    for (QLabel* w : tapeWidgets)
        if (w) w->setVisible(showTape);

    for (QLabel* w : diskWidgets)
        if (w) w->setVisible(showDisk);

    const QString display = isAdam ? m_currentARomName : m_currentRomName;
    updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel, display);
}

void MainWindow::updateMediaMenuState()
{
    const bool isAdam = (m_machineType == MACHINE_ADAM);

    auto setEnabledIf = [](QWidget* w, bool en) {
        if (w) w->setEnabled(en);
    };
    auto setActionEnabledIf = [](QAction* a, bool en) {
        if (a) a->setEnabled(en);
    };

    // ROM menus: Coleco vs ADAM
    setActionEnabledIf(m_openAdamRomAction,   isAdam);
    setActionEnabledIf(m_openColecoRomAction, !isAdam);

    // Als geen ADAM: alles media uit en klaar
    if (!isAdam) {
        QMenu* mediaMenus[] = { m_tapeMenuA, m_tapeMenuB, m_tapeMenuC, m_tapeMenuD,
                               m_diskMenuA, m_diskMenuB, m_diskMenuC, m_diskMenuD };

        QAction* mediaActions[] = {
            m_loadTapeActionA,  m_ejectTapeActionA,
            m_loadTapeActionB,  m_ejectTapeActionB,
            m_loadTapeActionC,  m_ejectTapeActionC,
            m_loadTapeActionD,  m_ejectTapeActionD,
            m_loadDiskActionA,  m_ejectDiskActionA,
            m_loadDiskActionB,  m_ejectDiskActionB,
            m_loadDiskActionC,  m_ejectDiskActionC,
            m_loadDiskActionD,  m_ejectDiskActionD
        };

        for (QMenu* m : mediaMenus)   setEnabledIf(m, false);
        for (QAction* a : mediaActions) setActionEnabledIf(a, false);
        return;
    }

    // ADAM: hier kan je later echte tape<->disk exclusie doen
    const bool canUseTape = true;
    const bool canUseDisk = true;

    // Menus enable/disable
    QMenu* tapeMenus[] = { m_tapeMenuA, m_tapeMenuB, m_tapeMenuC, m_tapeMenuD };
    QMenu* diskMenus[] = { m_diskMenuA, m_diskMenuB, m_diskMenuC, m_diskMenuD };

    for (QMenu* m : tapeMenus) setEnabledIf(m, canUseTape);
    for (QMenu* m : diskMenus) setEnabledIf(m, canUseDisk);

    // Actions + loaded flags in tabellen
    struct SlotActions {
        QAction* load;
        QAction* eject;
        bool*    loaded;
    };

    SlotActions tapeSlots[] = {
        { m_loadTapeActionA, m_ejectTapeActionA, &m_isTapeLoadedA },
        { m_loadTapeActionB, m_ejectTapeActionB, &m_isTapeLoadedB },
        { m_loadTapeActionC, m_ejectTapeActionC, &m_isTapeLoadedC },
        { m_loadTapeActionD, m_ejectTapeActionD, &m_isTapeLoadedD }
    };

    SlotActions diskSlots[] = {
        { m_loadDiskActionA, m_ejectDiskActionA, &m_isDiskLoadedA },
        { m_loadDiskActionB, m_ejectDiskActionB, &m_isDiskLoadedB },
        { m_loadDiskActionC, m_ejectDiskActionC, &m_isDiskLoadedC },
        { m_loadDiskActionD, m_ejectDiskActionD, &m_isDiskLoadedD }
    };

    auto updateSlots = [&](SlotActions* items, int count, bool canUse) {
        for (int i = 0; i < count; ++i) {
            const bool loaded = items[i].loaded ? *items[i].loaded : false;
            setActionEnabledIf(items[i].load,  canUse && !loaded);
            setActionEnabledIf(items[i].eject, canUse && loaded);
        }
    };

    updateSlots(tapeSlots, 4, canUseTape);
    updateSlots(diskSlots, 4, canUseDisk);
}

//---------------------------------------------------------------------------------------------
// PRINTER DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onShowPrinterWindow()
{
    PrintWindow* w = PrintWindow::instance();

    if (!w->isVisible()) {
        w->show();
    }
    w->raise();
    w->activateWindow();

    positionPrinter();
}

void MainWindow::positionPrinter()
{
    PrintWindow* w = PrintWindow::instance();
    if (!w->isVisible()) return;

    if (m_snapWindows) {
        // --- SNAP ON: paste at right side ---
        const int gap = 10;
        int newX = this->x() + this->width() + gap;
        int newY = this->y();
        w->move(newX, newY);

    } else {
        // --- SNAP OFF: Center on mainwindow ---
        int newX = this->x() + (this->width() - w->width()) / 2;
        int newY = this->y() + (this->height() - w->height()) / 2;
        w->move(newX, newY);
    }
}

//---------------------------------------------------------------------------------------------
// SNAP TO MAINSCREEN FUNCTION  (DEBUGGGER / PRINTER) DIALOGS
//---------------------------------------------------------------------------------------------

void MainWindow::onToggleSnap(bool checked)
{
    m_snapWindows = checked;

    if (m_snapWindows) {
        positionDebugger();
        positionPrinter();
    }

    saveSettings();
}

//---------------------------------------------------------------------------------------------
// MAIN SCREEN FUNCTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::onScalingModeChanged(QAction* action)
{
    int newMode = action->data().toInt();

    if (m_scalingMode == newMode) {
        return;
    }

    m_scalingMode = newMode;
    qDebug() << "[UI] Scaling Mode changed to" << newMode;

    saveSettings();

    if (m_screenWidget) {
        m_screenWidget->setScalingMode(static_cast<ScreenWidget::ScalingMode>(newMode));
    }
}

void MainWindow::onToggleFullScreen(bool checked)
{
    m_startFullScreen = checked;

    if (checked) {
        this->setMinimumSize(QSize(0, 0));
        this->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        showMaximized();
        updateFullScreenWallpaper();
        m_wallpaperLabel->show();
        m_screenWidget->setFullScreenMode(true);
    } else {
        showNormal();
        m_wallpaperLabel->hide();
        m_screenWidget->setFullScreenMode(false);
        this->setMinimumSize(770, 700);
    }
}

void MainWindow::onResetWindowSize()
{
    if (isFullScreen() || (m_actFullScreen && m_actFullScreen->isChecked())) {
        if (m_actFullScreen) m_actFullScreen->setChecked(false);
        onToggleFullScreen(false);
    }

    this->resize(770, 700);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (!m_screenWidget || m_screenWidget->height() == 0) return;
    int currentHeight = m_screenWidget->height();
    int gameScreenWidth = (currentHeight * 256) / 192;
    int difference = this->width() - gameScreenWidth;

    bool shouldShow = m_useBezels;// && (isFullScreen() || isMaximized() || (difference > 270));

    if (shouldShow) {
        // --- SHOW BEZELS ---
        if (m_wallpaperLabel) {
            updateFullScreenWallpaper();
            m_wallpaperLabel->setGeometry(this->rect());
            m_wallpaperLabel->show();
            m_wallpaperLabel->lower();
        }
        if (m_screenWidget) {
            m_screenWidget->setFullScreenMode(true);
        }
    } else {
        // --- HIDE BEZELS ---
        if (m_wallpaperLabel) {
            m_wallpaperLabel->hide();
        }
        if (m_screenWidget) {
            m_screenWidget->setFullScreenMode(false);
        }
    }

   centerSplash();

    m_bottomBlackBar->setGeometry(
        0,                  // helemaal links
        this->height() -22,  // onderaan window
        this->width(),      // breedte schaalt mee
        18           // vaste hoogte
        );

    const QString display = (m_machineType == MACHINE_ADAM) ? m_currentARomName : m_currentRomName;
    updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel, display);

}

void MainWindow::onToggleBezels(bool checked)
{
    m_useBezels = checked;

    QResizeEvent re(size(), size());
    resizeEvent(&re);

    saveSettings();
}

void MainWindow::updateFullScreenWallpaper()
{
    if (!m_wallpaperLabel)
        return;

    const bool isAdam = (m_machineType == 1);

    // 1) Bepaal welk pad we willen gebruiken
    const QString bezelPath = isAdam ? m_adamBezelPath : m_cvBezelPath;

    auto loadCustom = [&](const QString& path) -> QPixmap {
        QPixmap px;
        if (path.isEmpty() || path.compare("none", Qt::CaseInsensitive) == 0)
            return px;

        QString absPath = path;
        if (QDir::isRelativePath(absPath)) {
            const QDir appDir(QCoreApplication::applicationDirPath());
            absPath = appDir.absoluteFilePath(absPath);
        }

        const QFileInfo fi(absPath);
        if (fi.exists() && fi.isFile())
            px.load(absPath);

        return px;
    };

    // 2) Probeer custom, anders fallback resource
    QPixmap wallpaper = loadCustom(bezelPath);
    if (wallpaper.isNull()) {
        wallpaper.load(isAdam
                           ? QStringLiteral(":/images/images/wallpaper_adam.png")
                           : QStringLiteral(":/images/images/wallpaper_coleco.png"));
    }

    if (wallpaper.isNull())
        return;

    // 3) Schaal en toon
    m_wallpaperLabel->setPixmap(wallpaper.scaled(
        m_wallpaperLabel->size(),
        Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation));

    m_wallpaperLabel->setAlignment(Qt::AlignCenter);
}

//---------------------------------------------------------------------------------------------
// SAVE&LOAD EMULATOR STATES FUNCTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::onSaveState()
{
 QString absoluteStatePath = QDir::cleanPath(m_statePath);

    QDir statesDir(absoluteStatePath);
    if (!statesDir.exists())
        statesDir.mkpath(".");

    QString baseName = m_currentRomName;
    if (baseName.isEmpty() || baseName == "No cart")
        baseName = "state";

    QFileInfo fi(baseName);
    baseName = fi.completeBaseName();

    QString defaultFile = statesDir.filePath(baseName + ".sta");

    const QString filePath = CustomFileDialog::getSaveFileName(
        this,
        tr("Save State"),
        defaultFile,
        tr("State files (*.sta);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathState,
        QFileDialog::Options()
        );

    if (filePath.isEmpty())
        return;

    QString finalPath = filePath;
    if (!finalPath.endsWith(".sta", Qt::CaseInsensitive))
        finalPath += ".sta";

    QFileInfo fileInfo(finalPath);
    CustomFileDialog::s_lastSaveDir = fileInfo.absolutePath();

    QMetaObject::invokeMethod(
        m_colecoController,
        "saveState",
        Qt::QueuedConnection,
        Q_ARG(QString, finalPath)
        );
}

void MainWindow::onLoadState()
{
    QString absoluteStatePath = QDir::cleanPath(m_statePath);

    QDir statesDir(absoluteStatePath);
    if (!statesDir.exists())
        statesDir.mkpath(".");

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Load State"),
        statesDir.absolutePath(),
        tr("State files (*.sta);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathState,
        QFileDialog::Options()
        );

    if (filePath.isEmpty())
        return;

    QFileInfo fileInfo(filePath);
    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    QFileInfo inInfo(filePath);
    QDir appDir(QCoreApplication::applicationDirPath());
    m_statePath = appDir.relativeFilePath(inInfo.absolutePath());
    saveSettings();

    QMetaObject::invokeMethod(
        m_colecoController,
        "loadState",
        Qt::QueuedConnection,
        Q_ARG(QString, filePath)
        );
}

//---------------------------------------------------------------------------------------------
// JOYSTICK/KEYPAD/KEYBOARD FUNCTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::onOpenJoypadMapper()
{
    JoypadWindow dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        if (m_inputWidget) m_inputWidget->reloadMappings();
    }
}

void MainWindow::onJoystickTypeChanged(QAction* action)
{
    int newType = action->data().toInt();

    if (m_joystickType == newType) {
        return;
    }

    m_joystickType = newType;

    saveSettings();

    if (m_joystick) {
        // When you select Joystick it will poll again
        m_joystick->setJoystickType(m_joystickType);
        m_joystick->stopPolling();
        m_joystick->startPolling(0);
    }
}

void MainWindow::onTogglePaddleMode(bool checked)
{
    m_usePaddleMode = checked;
    qDebug() << "[UI] Paddle Mode set to" << (checked ? "ON" : "OFF");

    // Sla de status op (toe te voegen in save/loadSettings later)
    QSettings settings;
    settings.setValue("controller/usePaddleMode", checked);

    // PUSH DE STATUS NAAR DE BRIDGE (CRUCIAAL)
    // De bridge heeft geen setter nodig; we kunnen de volatile variabele direct zetten.
    extern volatile uint8_t ib_paddle_mode; // Moet geëxporteerd worden in coleco.cpp om te kunnen gebruiken
    ib_paddle_mode = checked ? 1 : 0;

    // Stuur de status naar de InputWidget voor lokale filtering
    if (m_inputWidget) {
        m_inputWidget->setPaddleMode(checked);
    }

    // (Optioneel: Herstart de polling als de setJoystickType dit nodig heeft)
}

void MainWindow::onToggleKeyboard(bool on)
{
    if (on) {
        m_inputWidget->setOverlayVisible(true);
        m_inputWidget->raise();
    } else {
        m_inputWidget->setOverlayVisible(false);
        if (m_screenWidget) m_screenWidget->setFocus(Qt::OtherFocusReason);
    }

    if (m_inputWidget) {
        m_inputWidget->setKeyboardOverlay(on);
    }

}

void MainWindow::onAdamGameMode()
{
    m_adamGameMode = m_actAdamGameOn->isChecked();
    qDebug() << "[UI] ADAM Input Mode:" << (m_adamGameMode ? "Game mode enabled" : "Game mode disabled");
    adamnet_set_game_mode(m_adamGameMode);

    if (m_screenWidget) {
        m_screenWidget->setFocus(Qt::OtherFocusReason);
    }

    if (m_inputWidget) {
        m_inputWidget->setAdamGameMode(m_adamGameMode);
    }

}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // Doorsturen naar InputWidget - die regelt alles
    if (m_inputWidget && m_inputWidget->handleKey(event, true)) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    const int key = event->key();

    // F11/F12 blijven bij MainWindow voor shortcuts
    if (key >= Qt::Key_F11 && key <= Qt::Key_F12) {
        QMainWindow::keyReleaseEvent(event);
        return;
    }

    // Rest naar InputWidget
    if (m_inputWidget && m_inputWidget->handleKey(event, false)) {
        event->accept();
    } else {
        event->ignore();
    }
}

//---------------------------------------------------------------------------------------------
// RESET FUNCTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::powerOff()
{
    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
    m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png"));
    loadExternalBiosRoms();
    // QMetaObject::invokeMethod(m_colecoController, "resethMachine",
    //                            Qt::QueuedConnection);

    // Cartridge ROM loskoppelen bij PowerOff
    QMetaObject::invokeMethod(m_colecoController, "ejectColecoCartridge",
                              Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_colecoController, "ejectAdamCartridge",
                              Qt::QueuedConnection);

    m_isColecoRomLoaded = false;
    m_isAdamRomLoaded = false;
    m_currentRomName.clear();
    m_currentARomName.clear();

    // BUTTON POWER OFF
    qDebug() << "[UI] Button pressed do power off emulator.";
    QMetaObject::invokeMethod(m_colecoController, "powerOffMachine",
                             Qt::QueuedConnection);

    m_resetAdamLocked  = false;
    m_AdamDMedia_insert = false;
    m_AdamTMedia_insert = false;
    forceStatusBarMediaFlags();
    m_ColecoMedia_insert = false;
    m_resetColecoLocked = false;

}

void MainWindow::onResetAdamBtnClicked()
{
    if (!m_resetAdamBtn) return;

    if (m_resetColecoLocked) {
        m_resetColecoLocked  = false;
        m_ColecoMedia_insert = false;
        m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png"));
        // 2. Cartridge ROM loskoppelen
        QMetaObject::invokeMethod(m_colecoController, "ejectColecoCartridge",
                                  Qt::QueuedConnection);
    }

    // Eerst de knipperfunctie stoppen en de iconen resetten (NIEUW)
    if (m_resetAdamBlinkTimer && m_resetAdamBlinkTimer->isActive()) {
        m_resetAdamBlinkTimer->stop();
        m_resetAdamBtn->setChecked(false); // Zorg ervoor dat de knop 'uit' is
        m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png")); // Standaard icoon
    }

    // if (m_resetAdamLocked) return;

    if (m_AdamDMedia_insert || m_AdamTMedia_insert) m_resetAdamLocked = true;
    // 1. HARD RESET uitvoeren (dwingt de ADAM-modus af en reset de CPU/I/O)
    switchToAdamMode();

    // 2. STATUS HERBEVESTIGEN (ADAM ROM)
    if (!m_currentARomName.isEmpty())
    {
        // Absolute pad logica: ga ervan uit dat m_romPath of CustomFileDialog::s_lastOpenDir de basis is
        QString absolutePath = QDir::cleanPath(CustomFileDialog::s_lastOpenDir + QDir::separator() + m_currentARomName);

        QMetaObject::invokeMethod(
            m_colecoController,
            "AdamCartridge",
            Qt::QueuedConnection,
            Q_ARG(QString, absolutePath) // Dwingt de kern de ROM opnieuw te mappen
            );
    }

    // 3. STATUS HERBEVESTIGEN (TAPES)
    for (int drive = 0; drive < 4; ++drive)
    {
        if (!m_loadedTapeNames[drive].isEmpty())
        {
            // We moeten het absolute pad reconstrueren om de load functie aan te roepen
            // We gebruiken CustomFileDialog::s_lastOpenDir als basispad voor de media
            QString absolutePath = QDir::cleanPath(CustomFileDialog::s_lastOpenDir + QDir::separator() + m_loadedTapeNames[drive]);

            QMetaObject::invokeMethod(
                m_colecoController,
                "loadTape",
                Qt::QueuedConnection,
                Q_ARG(int, drive),
                Q_ARG(QString, absolutePath) // Herlaadt de tape (koppelt de FDI structuur opnieuw)
                );
            m_cpm_status = m_cpm_enabled;
        }
    }

    // 4. STATUS HERBEVESTIGEN (DISKS)
    for (int drive = 0; drive < 4; ++drive)
    {
        if (!m_loadedDiskNames[drive].isEmpty())
        {
            QString absolutePath = QDir::cleanPath(CustomFileDialog::s_lastOpenDir + QDir::separator() + m_loadedDiskNames[drive]);

            QMetaObject::invokeMethod(
                m_colecoController,
                "loadDisk",
                Qt::QueuedConnection,
                Q_ARG(int, drive),
                Q_ARG(QString, absolutePath) // Herlaadt de disk (koppelt de FDI structuur opnieuw)
                );
            m_cpm_status = m_cpm_enabled;
        }
    }

    // 5. GUI Updaten (bevestigt de opgeslagen namen)
    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();

    // Visuele feedback: kortstondig de ON-afbeelding tonen
    m_resetAdamBtn->setChecked(true);
    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_on.png"));

    QTimer::singleShot(250, this, [this]() {
        if (m_resetAdamBtn) {
            m_resetAdamBtn->setChecked(false);

            if(m_cpm_status == false){

                if (m_AdamTMedia_insert)
                    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_t_locked_adam_off.png"));
                else if (m_AdamDMedia_insert)
                    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_d_locked_adam_off.png"));
                else
                    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
            }
            else{
                if (m_AdamTMedia_insert)
                    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_tcpm_locked_adam_off.png"));
                else if (m_AdamDMedia_insert)
                    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_dcpm_locked_adam_off.png"));
                else
                    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
            }
        }
    });
}

void MainWindow::onResetCartBtnClicked()
{
    if (!m_resetCartBtn) return;

    if (m_resetAdamLocked) {
    m_resetAdamLocked  = false;
    m_AdamDMedia_insert = false;
    m_AdamTMedia_insert = false;
    m_hardwareWindow->updateAvailability();
    forceStatusBarMediaFlags();
    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
    }

    if (m_resetCartBlinkTimer && m_resetCartBlinkTimer->isActive()) {
        m_resetCartBlinkTimer->stop();
        m_resetCartBtn->setChecked(false); // Zorg ervoor dat de knop 'uit' is
        m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png")); // Standaard icoon
    }


    //if (m_resetColecoLocked) return;
    if (m_ColecoMedia_insert) m_resetColecoLocked = true;

    // Voer Soft Reset (Cartridge Reset) uit
    switchToColecoMode();
    //onReset();
    onSgmStatusChanged(m_sgmEnabled);

    if (!m_currentRomName.isEmpty())
    {
        // Vorm het absolute pad (we gaan ervan uit dat de ROM in m_romPath of CustomFileDialog::s_lastOpenDir staat)
        QString absolutePath = QDir::cleanPath(CustomFileDialog::s_lastOpenDir + QDir::separator() + m_currentRomName);

        // Zelfs als het absolute pad niet 100% klopt, is de naam belangrijk voor de controller logica
        // We roepen de ColecoCartridge functie aan met de opgeslagen naam.
        QMetaObject::invokeMethod(
            m_colecoController,
            "ColecoCartridge",
            Qt::QueuedConnection,
            Q_ARG(QString, absolutePath) // Dit dwingt de kern de ROM opnieuw te mappen
            );

        // Let op: We moeten hier de naam handhaven en NIET wissen.
    }

    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();

    // Visuele feedback blijft hetzelfde
    m_resetCartBtn->setChecked(true);
    m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_on.png"));

    QTimer::singleShot(250, this, [this]() {
        if (m_resetCartBtn) {
            m_resetCartBtn->setChecked(false);
            if (m_ColecoMedia_insert && !m_sgmEnabled)
                m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_c_locked_cartridge_off.png"));
            else if (m_ColecoMedia_insert && m_sgmEnabled)
                m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_csgm_locked_cartridge_off.png"));
            else
                m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png"));
        }
    });
}

void MainWindow::switchToAdamMode()
{
    if (!m_colecoController) return;
    // 1. Maak een NIEUWE configuratie op basis van de HUIDIGE configuratie
    HardwareConfig newCfg;

    // Laad de actuele hardwaresetting van MainWindow in de tijdelijke config
    newCfg.machine = MACHINE_ADAM;
    newCfg.palette = m_paletteIndex;
    newCfg.vdpType = m_vdpType;
    newCfg.f18a80SelfTest = m_f18a80SelfTest;
    newCfg.sgmEnabled = m_sgmEnabled;
    newCfg.c80Enabled = m_c80Enabled;
    newCfg.Joys = m_ctrlJoys;
    newCfg.AdamNet = m_ctrlAdamNet;
    newCfg.Cartridge = m_ctrlCartridge;

    applyHardwareConfig(newCfg);

    // Alleen een echte powerOff doen als er GEEN ADAM media klaarstaat.
    // Anders wissen we bij de eerste reset m_cpm_enabled / m_tdos_enabled
    // en boot CP/M/TDOS terug naar Writer.
    const bool hasAdamMedia =
        m_AdamDMedia_insert ||
        m_AdamTMedia_insert ||
        !m_currentARomName.isEmpty();

    if (emutype == false && !hasAdamMedia)  {
         powerOff();
    }

    emutype = true;

    if (m_inputWidget) {
         m_inputWidget->setMachineType(1);
         //m_inputWidget->setAdamGameMode(false);  // Standaard keyboard mode
         qDebug() << "[MAINWINDOW] EXTRA: Forced InputWidget to ADAM mode";
     }

     updateMediaMenuState();
     updateMediaStatusLabels();
}

void MainWindow::switchToColecoMode()
{
    if (!m_colecoController) return;

    emutype=false;

    onReleaseAll();

    if (m_c80Enabled) m_c80Enabled = false;

    // 1. Maak een NIEUWE configuratie op basis van de HUIDIGE configuratie
    HardwareConfig newCfg;
    newCfg.machine = MACHINE_COLECO;
    newCfg.palette = m_paletteIndex;
    newCfg.vdpType = m_vdpType;
    newCfg.f18a80SelfTest = m_f18a80SelfTest;
    newCfg.sgmEnabled = m_sgmEnabled;
    newCfg.c80Enabled = m_c80Enabled;
    newCfg.Joys = m_ctrlJoys;
    newCfg.AdamNet = m_ctrlAdamNet;
    newCfg.Cartridge = m_ctrlCartridge;
    applyHardwareConfig(newCfg);

    if (m_inputWidget) {
        m_inputWidget->setMachineType(0);
        m_inputWidget->setAdamGameMode(false);  // Standaard keyboard mode
        qDebug() << "[MAINWINDOW] EXTRA: Forced InputWidget to Coleco mode";
    }

    QMetaObject::invokeMethod(
        m_colecoController,
        "setSGMEnabled",
        Qt::QueuedConnection,
        Q_ARG(bool, m_sgmEnabled) // Gebruik de actuele, in applyHardwareConfig ingestelde status
        );

    updateMediaMenuState();
    updateMediaStatusLabels();
}

void MainWindow::applyHardwareConfig(const HardwareConfig& cfg)
{

    m_vdpType = (cfg.vdpType == 1) ? 1 : 0;
    m_f18a80SelfTest = (m_vdpType == 1) ? cfg.f18a80SelfTest : false;

    coleco_set_vdp_type(m_vdpType);
    f18a_set_80col_selftest_enabled(m_f18a80SelfTest ? 1 : 0);

    qDebug() << "[VIDEO] Selected VDP:"
             << (m_vdpType == 1 ? "F18A" : "TMS9928A/TMS9918A")
             << "80col self-test=" << m_f18a80SelfTest;

    // C80 eerst toepassen, vóór resetAdam/resetColeco.
    // Zo ziet TDOS de 80-col hardware alleen als de HardwareWindow-knop echt aan staat.
    m_c80Enabled = cfg.c80Enabled;
    coleco_80col_enabled = cfg.c80Enabled ? 1 : 0;
    if (m_screenWidget) {
        m_screenWidget->set80ColumnMode(cfg.c80Enabled);
    }

    if  (cfg.machine == MACHINE_ADAM)
    {
            QMetaObject::invokeMethod(
                m_colecoController, "resetAdam",
                Qt::QueuedConnection
                );
   }
    if  (cfg.machine == MACHINE_COLECO)
    {
            // COLECO
            QMetaObject::invokeMethod(
                m_colecoController, "resetColeco",
                Qt::QueuedConnection
                );
    }

    m_machineType = cfg.machine;
    m_realhardware = cfg.realhardware;

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

    //const bool desiredSgm = (cfg.machine == MACHINE_ADAM) ? cfg.sgmEnabled : false;
    const bool desiredSgm = cfg.sgmEnabled;

    if (desiredSgm != m_sgmEnabled) {
        m_sgmEnabled = desiredSgm;

        QMetaObject::invokeMethod(
            m_colecoController, "setSGMEnabled",
            Qt::QueuedConnection, Q_ARG(bool, m_sgmEnabled)
            );
    }

    m_ctrlJoys    = cfg.Joys;
    m_ctrlAdamNet      = cfg.AdamNet;
    m_ctrlCartridge = cfg.Cartridge;

    if (m_sysLabel) m_sysLabel->setText(cfg.machine == MACHINE_COLECO ? "COLECO" : "ADAM");

    bool isAdam = (cfg.machine == MACHINE_ADAM);
    if (m_adamInputMenu) m_adamInputMenu->setEnabled(isAdam);

    // C80 is bovenaan al toegepast vóór de reset.

    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();
    updateMediaMenuState();
    updateFullScreenWallpaper();

    if (m_settingsLoaded) {
        saveSettings();
    } else {
        qDebug() << "[VIDEO] skip saveSettings during startup, vdp =" << m_vdpType;
    }
}

void MainWindow::onPowerBtnClicked()
{
    if (m_c80Enabled) m_c80Enabled = false;

    // 1. Maak een NIEUWE configuratie op basis van de HUIDIGE configuratie
    HardwareConfig newCfg;
    newCfg.c80Enabled = m_c80Enabled;


    // Stop ADAM Reset knop knipperen
    if (m_resetAdamBlinkTimer && m_resetAdamBlinkTimer->isActive()) {
        m_resetAdamBlinkTimer->stop();
        if (m_resetAdamBtn) {
            m_resetAdamBtn->setChecked(false);
            m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png")); // Zet standaard icoon terug
        }
    }

    // Stop Cartridge Reset knop knipperen
    if (m_resetCartBlinkTimer && m_resetCartBlinkTimer->isActive()) {
        m_resetCartBlinkTimer->stop();
        if (m_resetCartBtn) {
            m_resetCartBtn->setChecked(false);
            m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png")); // Zet standaard icoon terug
        }
    }

    m_powerBtn->setChecked(true);
    m_powerBtn->setIcon(QIcon(":/images/images/adamp_logo_power_adam_on.png"));

    QTimer::singleShot(250, this, [this]() {
        if (m_powerBtn) {
            m_powerBtn->setChecked(false);
            m_powerBtn->setIcon(QIcon(":/images/images/adamp_logo_power_adam_off.png"));
        }
    });

    onReleaseAll();
    powerOff();
}

void MainWindow::onReleaseAll()
{
    // Coleco Cartridge
    onEjectColecoRom();

    // ADAM Cartridge
    onEjectAdamRom();

    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
    m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png"));

    m_resetAdamLocked  = false;
    m_AdamDMedia_insert = false;
    m_AdamTMedia_insert = false;
    forceStatusBarMediaFlags();
    m_ColecoMedia_insert = false;
    m_resetColecoLocked = false;


    // Tapes (D1-D4)
    for (int drive = 0; drive < 4; ++drive) {
        onEjectTape(drive);
    }

    // Disks (D5-D8)
    for (int drive = 0; drive < 4; ++drive) {
        onEjectDisk(drive);
    }

    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onToggleResetAdamBlink()
{
    if (!m_resetAdamBtn) return;

    // Toggle de checked-status
    bool isChecked = m_resetAdamBtn->isChecked();
    m_resetAdamBtn->setChecked(!isChecked);

    // Wissel tussen de standaard 'off' icoon en de 'blink' icoon
    m_resetAdamBtn->setIcon(QIcon(isChecked
                                      ? ":/images/images/adamp_logo_reset_adam_off.png"
                                      : ":/images/images/adamp_logo_reset_adam_blink.png"));

}

void MainWindow::onToggleResetCartBlink()
{
   if (!m_resetCartBtn) return;

    // Toggle de checked-status
    bool isChecked = m_resetCartBtn->isChecked();
    m_resetCartBtn->setChecked(!isChecked);

    // Wissel tussen de standaard 'off' icoon en de 'blink' icoon
    m_resetCartBtn->setIcon(QIcon(isChecked
                                      ? ":/images/images/adamp_logo_reset_cartridge_off.png"
                                      : ":/images/images/adamp_logo_reset_cartridge_blink.png"));
}

//---------------------------------------------------------------------------------------------
// BIOS FUNCTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::onBiosStatusUpdated(int colecoExt, int eosExt, int writerExt)
{
    if (!m_actColecoBiosSource || !m_actEosBiosSource || !m_actWriterBiosSource)
        return;

    m_actColecoBiosSource->setText(colecoExt ? "Coleco: External" : "Coleco: Internal");
    m_actEosBiosSource->setText(   eosExt    ? "EOS: External"    : "EOS: Internal");
    m_actWriterBiosSource->setText(writerExt ? "Writer: External" : "Writer: Internal");
}

void MainWindow::loadExternalBiosRoms()
{

    // 2. Stuurt de opgeslagen paden van MainWindow naar de controller thread.
    // De controller roept coleco_initialise() aan en reset de emulator.
    QMetaObject::invokeMethod(
        m_colecoController,
        "loadBiosRoms",
        Qt::QueuedConnection,
        Q_ARG(QString, m_colecoBiosPath),
        Q_ARG(QString, m_eosBiosPath),
        Q_ARG(QString, m_writerBiosPath)
        );
}

void MainWindow::onToggleDTsound(bool checked)
{
    m_useDTsound = checked;

    if (m_colecoController) {
        QMetaObject::invokeMethod(
            m_colecoController,
            "setDTsoundEnabled",
            Qt::QueuedConnection,
            Q_ARG(bool, checked)
            );
    }

    saveSettings();
    qDebug() << "[UI] Disc/Tape sounds =" << (checked ? "ON" : "OFF");
}

//---------------------------------------------------------------------------------------------
// OS SYSTEM
//---------------------------------------------------------------------------------------------

void MainWindow::configurePlatformSettings()
{
    qDebug() << "[UI] --- START EMULATOR ---";

#if defined(Q_OS_WIN)
    qDebug() << "[UI] WINDOWS SYSTEM";
#elif defined(Q_OS_LINUX)
    qDebug() << "[UI] LINUX SYSTEM";
#else
    qDebug() << "[UI] UNKNOW SYSTEM";
#endif
}

//---------------------------------------------------------------------------------------------

