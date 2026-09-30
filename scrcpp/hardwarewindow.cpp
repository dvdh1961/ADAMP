#include "hardwarewindow.h"
#include "printwindow.h"
#include "mcu2_gateway.h"

#include "CORE/cv.h"

#include <QApplication>
#include <QGroupBox>
#include <QToolButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QStyle>
#include <QColor>
#include <QToolTip>
#include <QButtonGroup>
#include <QScreen>

#include <QPushButton>
#include <QIcon>
#include <QPixmap>
#include <QDebug>
#include <QMessageBox>

bool HardwareWindow::m_sgmSelectionState = false;
bool HardwareWindow::m_c80SelectionState = false;

HardwareWindow::HardwareWindow(const HardwareConfig& initial, QWidget *parent)
    : QDialog(parent), m_initial(initial), m_result(initial)
{
    setWindowTitle("Hardware");
    setModal(true);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    buildUi();
    loadFromConfig(initial);
    updatePaletteSwatches();

    m_btnPrinter->setChecked(PrintWindow::instance()->isVisible());
    updateAvailability();

    setFixedSize(920,520);
}

void HardwareWindow::buildUi()
{
    m_loading = true;
    {
        QPalette pal = QToolTip::palette();
        pal.setColor(QPalette::ToolTipBase, QColor("#dcdcdc"));
        pal.setColor(QPalette::ToolTipText, Qt::black);
        QToolTip::setPalette(pal);
    }

    auto makeIconToggleButton = [&](QToolButton* btn, const QString& iconPath,
                                    const QString& tooltip, bool exclusive) {
        btn->setIcon(QIcon(iconPath));
        btn->setIconSize(QSize(165,113));
        btn->setCheckable(true);
        btn->setAutoExclusive(exclusive);
        btn->setToolTip(tooltip);
        btn->setMinimumSize(165,113);
        btn->setMaximumSize(165,113);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    };

    auto makeLabeledButton = [&](QToolButton* btn, const QString& iconPath,
                                 const QString& text, bool exclusive) -> QWidget*
    {
        makeIconToggleButton(btn, iconPath, text, exclusive);
        auto *wrap = new QWidget(this);
        auto *vl = new QVBoxLayout(wrap);
        vl->setContentsMargins(0,0,0,0);
        vl->setSpacing(4);

        vl->addWidget(btn, 0, Qt::AlignHCenter);

        auto *lbl = new QLabel(text, wrap);
        lbl->setAlignment(Qt::AlignHCenter);
        lbl->setWordWrap(true);
        lbl->setStyleSheet("QLabel{ font-size: 11px; }");
        lbl->setMinimumWidth(btn->minimumWidth());
        lbl->setMaximumWidth(btn->maximumWidth());
        vl->addWidget(lbl, 0, Qt::AlignHCenter);
        return wrap;
    };

    // === Machine ===
    m_groupMachine = new QGroupBox("Machine", this);
    m_btnColeco  = new QToolButton(m_groupMachine);
    m_btnAdam    = new QToolButton(m_groupMachine);
    m_btnAdamP   = new QToolButton(m_groupMachine);

    auto *layMac = new QHBoxLayout;
    layMac->addWidget(makeLabeledButton(m_btnColeco,  ":/images/images/machine_coleco.png",  "ColecoVision", true));
    layMac->addWidget(makeLabeledButton(m_btnAdam,    ":/images/images/machine_adam.png",    "ADAM",         true));

    //layMac->addWidget(makeLabeledButton(m_btnAdamP,   ":/images/images/machine_adamp.png",   "ADAMP",        true));
    QWidget* adamPWidget =
        makeLabeledButton(m_btnAdamP,
                          ":/images/images/machine_atari.png",
                          "Expansion ATARI 2600",
                          true);

    layMac->addWidget(adamPWidget);

    // Zoek de label binnen dat widget en bewaar hem
    m_lblAdamP = adamPWidget->findChild<QLabel*>();
    layMac->addStretch(1);
    m_groupMachine->setLayout(layMac);

    m_machineGroup = new QButtonGroup(m_groupMachine);
    m_machineGroup->setExclusive(true);
    m_machineGroup->addButton(m_btnColeco, static_cast<int>(MACHINE_COLECO));
    m_machineGroup->addButton(m_btnAdam,   static_cast<int>(MACHINE_ADAM));
    m_machineGroup->addButton(m_btnAdamP,  static_cast<int>(MACHINE_ATARI2600));

    connect(m_machineGroup, &QButtonGroup::idClicked,
            this, [this](int){ onMachineChanged(); });
    // === Additional Hardware ===
    m_groupAddHw = new QGroupBox("Additional Hardware", this);
    m_btnSGM  = new QToolButton(m_groupAddHw);
    m_btn80C = new QToolButton(m_groupAddHw);
    m_btnPrinter = new QToolButton(m_groupAddHw);

    auto *layHw = new QHBoxLayout;
    layHw->addWidget(makeLabeledButton(m_btnSGM,  ":/images/images/hw_sgm.png",  "Opcode SGM",       false));
    layHw->addWidget(makeLabeledButton(m_btn80C, ":/images/images/hw_c80.png", "80 Terminal", false));

    auto *wrapPrinter = makeLabeledButton(m_btnPrinter, ":/images/images/hw_printer.png", "Printer Output", false);
    layHw->addWidget(wrapPrinter);

    layHw->addStretch(1);
    m_groupAddHw->setLayout(layHw);

    connect(m_btnSGM,  &QToolButton::toggled, this, &HardwareWindow::onToggleSGM);
    connect(m_btnSGM,  &QToolButton::clicked, this, &HardwareWindow::updateAvailability);
    connect(m_btn80C,  &QToolButton::toggled, this, &HardwareWindow::onToggleC80);
    connect(m_btn80C,  &QToolButton::clicked, this, &HardwareWindow::updateAvailability);
    connect(m_btnPrinter, &QToolButton::clicked, this, &HardwareWindow::onPrinterClicked);

    // === Experimental physical ADAMnet backend ===
    // The proven Disk 1 and FujiNet devices can use the physical MCU2 backend. The selection
    // is activated on the next ADAM reset so an active DCB can never change
    // backend halfway. Software images remain the unchanged default path.
    m_groupAdamNet = new QGroupBox("ADAMnet (Experimental)", this);
    m_cboDisk1Backend = new QComboBox(m_groupAdamNet);
    m_cboDisk1Backend->addItem("ADAM drives: Software images", false);
    m_cboDisk1Backend->addItem("Disk 1 + FujiNet: MCU2 hardware", true);
    m_lblMcu2Status = new QLabel("MCU2 not tested", m_groupAdamNet);
    m_lblMcu2Status->setWordWrap(true);
    m_btnTestMcu2 = new QPushButton("Test MCU2 connection", m_groupAdamNet);
    m_chkFujiNetDirectRom = new QCheckBox(
        tr("Fast native ROM loading via PC network"), m_groupAdamNet);
    m_chkFujiNetDirectRom->setToolTip(tr(
        "Use the FujiNet host and path to download native Coleco ROMs directly. "
        "If that is unavailable, ADAMP automatically uses the physical D5 route."));

    auto *layAdamNet = new QGridLayout;
    layAdamNet->addWidget(m_cboDisk1Backend, 0, 0, 1, 2);
    layAdamNet->addWidget(m_btnTestMcu2, 1, 0);
    layAdamNet->addWidget(m_lblMcu2Status, 1, 1);
    layAdamNet->addWidget(m_chkFujiNetDirectRom, 2, 0, 1, 2);
    layAdamNet->setColumnStretch(1, 1);
    m_groupAdamNet->setLayout(layAdamNet);
    connect(m_btnTestMcu2, &QPushButton::clicked,
            this, &HardwareWindow::onTestMcu2);
    connect(m_cboDisk1Backend, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateAvailability(); });

    // === Video ===
    m_groupVideo = new QGroupBox("Video", this);
    auto *lblPal  = new QLabel("Palette", m_groupVideo);
    auto *lblVdp  = new QLabel("VDP", m_groupVideo);

    m_cboPalette = new QComboBox(m_groupVideo);
    m_cboPalette->addItems({"Coleco", "TMS9918", "MSX", "Grayscale"});

    m_cboVdp = new QComboBox(m_groupVideo);
    m_cboVdp->addItem("TMS9928A / TMS9918A", VDP_TMS);
    m_cboVdp->addItem("F18A", VDP_F18A);
    m_cboVdp->addItem("PICO9918", VDP_PICO9918);

    //m_chkF18a80SelfTest = new QCheckBox("F18A 80-column self-test", m_groupVideo);
   // m_chkF18a80SelfTest->setToolTip("Shows the internal F18A 80-column diagnostic screen. Only useful when VDP is F18A.");

    auto *layVidTop = new QGridLayout;
    layVidTop->addWidget(lblVdp,       0, 0);
    layVidTop->addWidget(m_cboVdp,     0, 1);
    layVidTop->addWidget(lblPal,       1, 0);
    layVidTop->addWidget(m_cboPalette,1, 1);

    // 16 kleur-swatch
    auto *palLayout = new QGridLayout();
    palLayout->setHorizontalSpacing(0);
    palLayout->setVerticalSpacing(2);
    palLayout->setContentsMargins(6, 8, 6, 6);

    const int swatchSize = 20;
    for (int i = 0; i < 16; ++i) {
        m_paletteSwatches[i] = new QLabel(m_groupVideo);
        m_paletteSwatches[i]->setFixedSize(swatchSize, swatchSize);
        m_paletteSwatches[i]->setStyleSheet("background-color: black; border: 1px solid #808080;");
        m_paletteSwatches[i]->setAlignment(Qt::AlignCenter);
        m_paletteSwatches[i]->setToolTip(QString("Palette index %1").arg(i));
        palLayout->addWidget(m_paletteSwatches[i], 0, i);

        QLabel *lblNum = new QLabel(QString::number(i), m_groupVideo);
        lblNum->setStyleSheet("color: white; font: 8pt 'Consolas';");
        lblNum->setAlignment(Qt::AlignHCenter);
        palLayout->addWidget(lblNum, 1, i);
    }
    palLayout->setColumnStretch(16, 1);

    auto *layVidMain = new QVBoxLayout;
    layVidMain->addLayout(layVidTop);
    layVidMain->addLayout(palLayout);
    m_groupVideo->setLayout(layVidMain);

    int mh = m_groupMachine->sizeHint().height();
    m_groupVideo->setMinimumHeight(mh);
    m_groupVideo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    connect(m_cboPalette, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &HardwareWindow::onPaletteChanged);

    connect(m_cboVdp, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){
                const bool isF18A = vdpHasF18A(m_cboVdp->currentData().toInt());
                //m_chkF18a80SelfTest->setEnabled(isF18A);
                //if (!isF18A)
                   // m_chkF18a80SelfTest->setChecked(false);

                updateAvailability();
            });

    updatePaletteSwatches();

    // === Emulation ===
    m_groupEmu = new QGroupBox("Selected System: ColecoVision", this);

    // Image
    QLabel* imgEmu = new QLabel(m_groupEmu);
    imgEmu->setPixmap(QPixmap(":/images/images/Hardware.png"));
    imgEmu->setAlignment(Qt::AlignHCenter);

    // CSS voor de donkergrijze randkleur
    const QString borderColor = "#404040";
    const QString baseBorderStyle = QString("1px solid %1").arg(borderColor);

    // Tabel voor beschrijvingen
    QGridLayout* layEmuGrid = new QGridLayout;
    layEmuGrid->setContentsMargins(10, 15, 10, 10);
    layEmuGrid->setSpacing(0); // Zorgt dat de randen naadloos aansluiten

    // STRETCH FACTOR
    layEmuGrid->setColumnStretch(0, 1); // Code Label (Smalle kolom)
    layEmuGrid->setColumnStretch(1, 7); // Beschrijving Label (Brede kolom)

    // --- Headers (Rij 0) ---
    QLabel *lblHwCol = new QLabel("Dev", m_groupEmu);
    QLabel *lblDescCol = new QLabel("Program/Game Description", m_groupEmu);

    // Stijl voor de linker header (Kolom 0): top, bottom, right, left border
    lblHwCol->setStyleSheet(
        QString("font-weight: bold; padding: 4px; border: %1;")
            .arg(baseBorderStyle)
        );

    // Stijl voor de rechter header (Kolom 1): top, bottom, right border (geen left border om overlap te voorkomen)
    lblDescCol->setStyleSheet(
        QString("font-weight: bold; padding: 4px; border-top: %1; border-bottom: %1; border-right: %1;")
            .arg(baseBorderStyle)
        );

    layEmuGrid->addWidget(lblHwCol, 0, 0);
    layEmuGrid->addWidget(lblDescCol, 0, 1);

    auto createEmuLabel = [&](const QString& defaultText) -> QLabel* {
        QLabel *lbl = new QLabel(defaultText, m_groupEmu);
        lbl->setWordWrap(true);
        lbl->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        // Stijl voor de rechterkolom (Kolom 1): top, right, bottom border (geen left border)
        lbl->setStyleSheet(
            QString("font-size: 10px; padding: 4px; border-top: %1; border-right: %1; border-bottom: %1;")
                .arg(baseBorderStyle)
            );
        return lbl;
    };

    auto addTableRow = [&](int row, const QString& hwCode, QLabel*& lblEmu, QLabel*& lblRow) {
        QLabel *lblHw = new QLabel(hwCode, m_groupEmu);
        lblRow = lblHw;
        lblHw->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        // Stijl voor de linkerkolom (Kolom 0): volledige rand, behalve dubbele border in het midden.
        lblHw->setStyleSheet(
            QString("font-size: 10px; padding: 4px; border-top: %1; border-left: %1; border-right: %1; border-bottom: %1;")
                .arg(baseBorderStyle)
            );

        layEmuGrid->addWidget(lblHw, row, 0);
        layEmuGrid->addWidget(lblEmu, row, 1);
    };


    // Rijen toevoegen en labels initialiseren met default waarden
    m_lblEmuCC = createEmuLabel("No coleco cartridge");
    addTableRow(1, "CV ROM", m_lblEmuCC, m_lblRowCC);

    m_lblEmuCA = createEmuLabel("No adam cartridge");
    addTableRow(2, "AD ROM", m_lblEmuCA, m_lblRowCA);

    m_lblEmuD1 = createEmuLabel("No tape");
    addTableRow(3, "TAPE D1", m_lblEmuD1, m_lblRowD1);

    m_lblEmuD2 = createEmuLabel("No tape");
    addTableRow(4, "TAPE D2", m_lblEmuD2, m_lblRowD2);

    m_lblEmuD5 = createEmuLabel("No disc");
    addTableRow(5, "DISK D5", m_lblEmuD5, m_lblRowD5);

    m_lblEmuD6 = createEmuLabel("No disc");
    addTableRow(6, "DISK D6", m_lblEmuD6, m_lblRowD6);

    m_lblEmuVCS = createEmuLabel("No atari cartridge");
    addTableRow(7, "VCS", m_lblEmuVCS, m_lblRowVCS);

    // Emu Layout (combineert image, widgets en grid)
    auto *layEmu = new QVBoxLayout;
    layEmu->addWidget(imgEmu, 0, Qt::AlignHCenter);
    layEmu->addLayout(layEmuGrid);
    layEmu->addStretch(1);
    m_groupEmu->setLayout(layEmu);

    // === Buttons ===
    QIcon okIcon(":/images/images/OK.png");
    QIcon cancelIcon(":/images/images/CANCEL.png");
    QPixmap okPixmap(":/images/images/OK.png");
    QPixmap cancelPixmap(":/images/images/CANCEL.png");

    if (okIcon.isNull()) { qWarning() << "HardwareWindow: Kon OK.png niet laden."; }
    if (cancelIcon.isNull()) { qWarning() << "HardwareWindow: Kon CANCEL.png niet laden."; }

    QString buttonStyle =
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }";

    QPushButton* okButton = new QPushButton(this);
    okButton->setIcon(okIcon);
    okButton->setIconSize(okPixmap.size());
    okButton->setFixedSize(okPixmap.size());
    okButton->setText("");
    okButton->setFlat(true);
    okButton->setStyleSheet(buttonStyle);

    QPushButton* cancelButton = new QPushButton(this);
    cancelButton->setIcon(cancelIcon);
    cancelButton->setIconSize(cancelPixmap.size());
    cancelButton->setFixedSize(cancelPixmap.size());
    cancelButton->setText("");
    cancelButton->setFlat(true);
    cancelButton->setStyleSheet(buttonStyle);

    connect(okButton, &QPushButton::clicked, this, &HardwareWindow::onOk);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    // === Hoofd-layout ===
    auto *colLeft  = new QVBoxLayout;
    colLeft->addWidget(m_groupMachine);
    colLeft->addWidget(m_groupAddHw);
    colLeft->addWidget(m_groupAdamNet);
    colLeft->addStretch(1);

    auto *colRight = new QVBoxLayout;
    colRight->addWidget(m_groupVideo);
    colRight->addWidget(m_groupEmu, 1);

    auto *rowMain = new QHBoxLayout;
    rowMain->addLayout(colLeft, 1);
    rowMain->addLayout(colRight, 1);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch(1);
    btnLayout->addWidget(okButton);
    btnLayout->addWidget(cancelButton);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(rowMain, 1);
    mainLayout->addLayout(btnLayout);
    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    m_loading = false;

    m_btnSGM->setCheckable(true);

    checkRealAdamP();

}

void HardwareWindow::loadFromConfig(const HardwareConfig& c)
{
    // Machine
    m_btnColeco ->setChecked(c.machine == MACHINE_COLECO);
    m_btnAdam   ->setChecked(c.machine == MACHINE_ADAM);
    m_btnAdamP  ->setChecked(c.machine == MACHINE_ATARI2600);

    // Video
    //m_cboDisplay->setCurrentIndex(qBound(0, c.renderMode, m_cboDisplay->count()-1));
    m_cboPalette->setCurrentIndex(qBound(0, c.palette,    m_cboPalette->count()-1));

    const int vdpIdx = m_cboVdp->findData(vdpHasF18A(c.vdpType) ? c.vdpType : VDP_TMS);
    m_cboVdp->setCurrentIndex(vdpIdx >= 0 ? vdpIdx : 0);
   // m_chkF18a80SelfTest->setChecked(c.f18a80SelfTest && c.vdpType == VDP_F18A);
   // m_chkF18a80SelfTest->setEnabled(c.vdpType == VDP_F18A);

    if (!m_loading) { // Zorg dat dit alleen gebeurt bij het openen van de dialoog
        HardwareWindow::m_sgmSelectionState = c.sgmEnabled;
    }
    // Additional hardware
    m_btnSGM->setChecked(HardwareWindow::m_sgmSelectionState);

    /* Keep the Hardware window in sync with the live emulator state.
     * 80 Terminal is only selectable when F18A is selected.
     * When CP/M80 is switched back to 40C from the ScreenWidget popup,
     * coleco_80col_enabled is cleared immediately. The saved HardwareConfig
     * can still contain c80Enabled=true, so do not blindly show the button ON.
     */
    const bool isF18A = vdpHasF18A(c.vdpType);
    const bool liveC80Enabled = (isF18A && c.c80Enabled && (coleco_80col_enabled != 0));
    HardwareWindow::m_c80SelectionState = liveC80Enabled;
    m_btn80C->setChecked(liveC80Enabled);

    const int adamNetIndex = m_cboDisk1Backend->findData(c.AdamNet);
    m_cboDisk1Backend->setCurrentIndex(adamNetIndex >= 0 ? adamNetIndex : 0);
    m_chkFujiNetDirectRom->setChecked(c.fujiNetDirectRom);
    updateAvailability();
}

HardwareConfig HardwareWindow::readFromUi() const
{
    HardwareConfig c;

    // Machine
    if      (m_btnAdam->isChecked())  c.machine = MACHINE_ADAM;
    else if (m_btnAdamP->isChecked()) c.machine = MACHINE_ATARI2600;
    else                              c.machine = MACHINE_COLECO;

    c.realhardware = false;

    // Video
    c.palette = m_cboPalette->currentIndex();
    c.vdpType = m_cboVdp ? m_cboVdp->currentData().toInt() : VDP_TMS;
    if (!vdpHasF18A(c.vdpType))
        c.vdpType = VDP_TMS;
   // c.f18a80SelfTest = (c.vdpType == VDP_F18A) && m_chkF18a80SelfTest && m_chkF18a80SelfTest->isChecked();

    // Additional hardware
    c.sgmEnabled = m_btnColeco->isChecked() && m_btnSGM->isChecked();
    c.c80Enabled = m_btnAdam->isChecked() && vdpHasF18A(c.vdpType) && m_btn80C->isChecked();

    // External joystick/cartridge backends remain unused.  AdamNet now stores
    // the experimental Disk 1 backend choice; software is always the default.
    c.Joys = false;
    c.AdamNet = m_cboDisk1Backend && m_cboDisk1Backend->currentData().toBool();
    c.fujiNetDirectRom = c.AdamNet && m_chkFujiNetDirectRom
        && m_chkFujiNetDirectRom->isChecked();
    c.Cartridge = false;

    return c;
}

void HardwareWindow::updateAvailability()
{
    if (!m_btnColeco->isChecked() && !m_btnAdam->isChecked() && !m_btnAdamP->isChecked()) {
        m_btnColeco->setChecked(true);
    }

    const bool isAdam = m_btnAdam->isChecked();
    const bool isAtari2600 = m_btnAdamP->isChecked();
    const bool isF18A = (m_cboVdp && vdpHasF18A(m_cboVdp->currentData().toInt()));
    const bool c80Available = isAdam && isF18A;

    if (isAdam || isAtari2600) {

        m_btnSGM->setChecked(false);
        m_btnSGM->setEnabled(false);

    } else {
        m_btnSGM->setEnabled(true);
    }

    m_btn80C->setEnabled(c80Available);
    if (!c80Available) {
        m_btn80C->setChecked(false);
        HardwareWindow::m_c80SelectionState = false;
    }

    m_btnPrinter->setEnabled(true);
    m_groupAdamNet->setEnabled(isAdam);
    if (m_chkFujiNetDirectRom)
        m_chkFujiNetDirectRom->setEnabled(isAdam && m_cboDisk1Backend
            && m_cboDisk1Backend->currentData().toBool());
    m_cboPalette->setEnabled(!isAtari2600);
    m_cboVdp->setEnabled(!isAtari2600);

    auto setBorder = [](QToolButton* b){
        b->setStyleSheet(b->isChecked()
                         ? "QToolButton{border:3px solid #00ccff; border-radius:6px;}"
                           "QToolButton:hover{border:3px solid #33ddff;}"
                         : "QToolButton{border:1px solid #444; border-radius:6px;}"
                           "QToolButton:hover{border:1px solid #777;}");
    };

    setBorder(m_btnColeco);
    setBorder(m_btnAdam);

    setBorder(m_btnAdamP);

    setBorder(m_btnSGM);
    setBorder(m_btn80C);
    setBorder(m_btnPrinter);
}

void HardwareWindow::onTestMcu2()
{
    m_btnTestMcu2->setEnabled(false);
    m_lblMcu2Status->setText("Testing MCU2...");
    QApplication::processEvents();

    const Mcu2Gateway::ProbeResult result = Mcu2Gateway::probe();
    m_mcu2ProbeSucceeded = result.connected;
    m_lblMcu2Status->setText(result.message);
    m_lblMcu2Status->setStyleSheet(result.connected
        ? "QLabel{color:#55dd77;}" : "QLabel{color:#ff7777;}");
    m_btnTestMcu2->setEnabled(true);

    if (!result.connected && m_cboDisk1Backend->currentData().toBool())
        m_cboDisk1Backend->setCurrentIndex(0);
}

void HardwareWindow::onToggleC80(bool checked)
{
    const bool isF18A = (m_cboVdp && vdpHasF18A(m_cboVdp->currentData().toInt()));
    if (!isF18A) {
        m_c80SelectionState = false;
        if (m_btn80C)
            m_btn80C->setChecked(false);
        return;
    }

    m_c80SelectionState = checked;
}

void HardwareWindow::onToggleSGM(bool checked)
{
    m_sgmSelectionState = checked;
}

void HardwareWindow::onPrinterClicked()
{
    PrintWindow* w = PrintWindow::instance();

    if (m_btnPrinter->isChecked()) {
        w->show();
        w->raise();
        w->activateWindow();

        QWidget* mainWin = parentWidget();
        if (mainWin)
        {
            const QRect mainGeom = mainWin->frameGeometry();
            const QRect avail    = screen()->availableGeometry();
            QPoint pos(mainGeom.right() + 10, mainGeom.top());
            QSize  sz  = w->size();

            if (pos.x() + sz.width() > avail.right())
                pos.setX(qMax(avail.left(), mainGeom.left() - 10 - sz.width()));
            if (pos.y() + sz.height() > avail.bottom())
                pos.setY(qMax(avail.top(), avail.bottom() - sz.height()));

            w->move(pos);
        }
    } else {
        w->hide();
    }

    updateAvailability();
}

void HardwareWindow::updatePaletteSwatches()
{
    if (!m_paletteSwatches[0] || !m_cboPalette) return;

    const int bank  = qBound(0, m_cboPalette->currentIndex(), 5);
    const int base  = bank * 16 * 3;

    for (int i = 0; i < 16; ++i) {
        const int off = base + i * 3;
        const int r = TMS9918A_palette[off + 0];
        const int g = TMS9918A_palette[off + 1];
        const int b = TMS9918A_palette[off + 2];

        m_paletteSwatches[i]->setStyleSheet(
            QString("background-color: rgb(%1,%2,%3); border: 1px solid #808080;")
                .arg(r).arg(g).arg(b)
            );
    }
}

void HardwareWindow::onPaletteChanged(int idx)
{
    Q_UNUSED(idx);
    updatePaletteSwatches();
}

void HardwareWindow::onMachineChanged()
{
    if (m_btnSGM->isEnabled()) {
        m_sgmSelectionState = m_btnSGM->isChecked();
    }

    qDebug() << "Machine changed:"
             << (m_btnAdamP->isChecked() ? "ATARI 2600" :
                 (m_btnAdam->isChecked() ? "ADAM" : "COLECO"));

    const int machine = m_btnAdamP->isChecked() ? MACHINE_ATARI2600 :
                        (m_btnAdam->isChecked() ? MACHINE_ADAM : MACHINE_COLECO);

    emit machineChanged(machine);

    updateMediaVisibility(static_cast<MachineType>(machine));
    updateAvailability();
}

void HardwareWindow::updateMediaVisibility(MachineType activeMachine)
{
    const bool colecoActive = activeMachine == MACHINE_COLECO;
    const bool adamActive = activeMachine == MACHINE_ADAM;
    const bool atariActive = activeMachine == MACHINE_ATARI2600;

    m_groupEmu->setTitle(QString("Selected System: %1").arg(
        colecoActive ? "ColecoVision" :
        adamActive ? "ADAM" : "Expansion ATARI 2600"));

    auto showRow = [](QLabel* code, QLabel* value, bool visible) {
        if (code) code->setVisible(visible);
        if (value) value->setVisible(visible);
    };
    showRow(m_lblRowCC, m_lblEmuCC, colecoActive);
    showRow(m_lblRowCA, m_lblEmuCA, adamActive);
    showRow(m_lblRowD1, m_lblEmuD1, adamActive);
    showRow(m_lblRowD2, m_lblEmuD2, adamActive);
    showRow(m_lblRowD5, m_lblEmuD5, adamActive);
    showRow(m_lblRowD6, m_lblEmuD6, adamActive);
    showRow(m_lblRowVCS, m_lblEmuVCS, atariActive);
}

void HardwareWindow::onOk()
{
    /* Never store the experimental hardware selection based on an assumption.
     * A successful live PING/VERSION probe is required in this dialog.  The
     * software backend therefore remains the guaranteed escape route.
     */
    if (m_cboDisk1Backend->currentData().toBool() && !m_mcu2ProbeSucceeded) {
        onTestMcu2();
        if (!m_mcu2ProbeSucceeded) {
            QMessageBox::warning(
                this,
                "MCU2 not available",
                "Disk 1 remains on the software backend because the MCU2 "
                "connection test did not succeed."
            );
            m_cboDisk1Backend->setCurrentIndex(0);
            return;
        }
    }

    m_result = readFromUi();
    accept();
}

HardwareConfig HardwareWindow::config() const
{
    return m_result;
}

void HardwareWindow::updateLoadedMedia(const QString& cartridgeName)
{
    if (m_lblEmuCC) {
        m_lblEmuCC->setText(cartridgeName.isEmpty() ? "No coleco cartridge" : cartridgeName);
    }
}

// In hardwarewindow.cpp, implementatie van de setter methode:

void HardwareWindow::setLoadedMediaDisplayNames(
    MachineType activeMachine,
    const QString& colecoCartridgeName,
    const QString& adamCartridgeName,
    const QString& atariCartridgeName,
    const QString& tape1Name,
    const QString& tape2Name,
    const QString& disc1Name,
    const QString& disc2Name,
    const QString& disc3Name)
{
    // Functie om de juiste weergavetekst en kleur te bepalen
    auto setLabelStatus = [&](QLabel* label, const QString& name, const QString& defaultText) {
        if (!label) return;

        bool isLoaded = !name.isEmpty() && name != defaultText;

        // Bepaal de tekst: naam als geladen, defaultText anders
        QString displayText = isLoaded ? name : defaultText;

        // Bepaal de kleur: Groen voor geladen, Donkergrijs voor default
        // #A0A0A0 is lichtgrijs (default), #50C878 is groen (loaded)
        const QString textColor = isLoaded ? "#50C878" : "#A0A0A0";

        // De basis border style uit buildUi()
        const QString borderColor = "#404040";
        const QString baseBorderStyle = QString("1px solid %1").arg(borderColor);

        // De labelstijl toepassen (behoudt de rand, voegt kleur toe)
        label->setStyleSheet(
            QString("font-size: 10px; padding: 4px; border-top: %1; border-right: %1; border-bottom: %1; color: %2;")
                .arg(baseBorderStyle).arg(textColor)
            );

        label->setText(displayText);
    };

    // Stijl voor de linkerkolom (Code, die geen kleurstijl nodig heeft, maar wel de rand)
    // We moeten deze opnieuw definiëren in de buildUi context,
    // maar voor de beschrijvingskolom is dit voldoende.

    // Noot: We gaan ervan uit dat de QLabel van Kolom 0 (Code) zijn stijl behoudt
    // en dat Kolom 1 (Beschrijving) deze statusmethode gebruikt.

    updateMediaVisibility(activeMachine);

    setLabelStatus(m_lblEmuCC, colecoCartridgeName, "No coleco cartridge");
    setLabelStatus(m_lblEmuCA, adamCartridgeName, "No adam cartridge");
    setLabelStatus(m_lblEmuVCS, atariCartridgeName, "No atari cartridge");
    setLabelStatus(m_lblEmuD1, tape1Name, "No tape");
    setLabelStatus(m_lblEmuD2, tape2Name, "No tape");
    setLabelStatus(m_lblEmuD5, disc1Name, "No disc");
    setLabelStatus(m_lblEmuD6, disc2Name, "No disc");
    setLabelStatus(m_lblEmuD7, disc3Name, "No disc");
}

void HardwareWindow::checkRealAdamP()
{
    m_btnAdamP->setEnabled(true);
    if (m_lblAdamP)
        m_lblAdamP->setText("Expansion ATARI 2600");
}
