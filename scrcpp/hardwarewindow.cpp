#include "hardwarewindow.h"
#include "printwindow.h"

#include "coleco.h"

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

bool HardwareWindow::m_sgmSelectionState = false;

static QWidget* makeHSpacer(QWidget* parent=nullptr) {
    auto *w = new QWidget(parent);
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return w;
}

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
    layMac->addWidget(makeLabeledButton(m_btnAdamP,   ":/images/images/machine_adamp.png",   "ADAMP",        true));
    layMac->addStretch(1);
    m_groupMachine->setLayout(layMac);

    m_machineGroup = new QButtonGroup(m_groupMachine);
    m_machineGroup->setExclusive(true);
    m_machineGroup->addButton(m_btnColeco, static_cast<int>(MACHINE_COLECO));
    m_machineGroup->addButton(m_btnAdamP,  static_cast<int>(MACHINE_ADAMP));
    m_machineGroup->addButton(m_btnAdam,   static_cast<int>(MACHINE_ADAM));

    connect(m_machineGroup, &QButtonGroup::idClicked,
            this, [this](int){ onMachineChanged(); });

    // === Additional Controller ===
    m_groupCtrl = new QGroupBox("Additional Controller", this);
    m_btnSteering    = new QToolButton(m_groupCtrl);
    m_btnRoller      = new QToolButton(m_groupCtrl);
    m_btnSuperAction = new QToolButton(m_groupCtrl);

    // Steering
    auto *wrapSteer = makeLabeledButton(m_btnSteering, ":/images/images/ctrl_sterring.png", "Steering Wheel", false);

    // Roller & SuperAction
    auto *wrapRoll  = makeLabeledButton(m_btnRoller, ":/images/images/ctrl_roller.png", "Roller Controller", false);
    auto *wrapAct   = makeLabeledButton(m_btnSuperAction, ":/images/images/ctrl_superaction.png", "Super Action", false);

    m_btnRoller->setAutoExclusive(false);
    m_btnSuperAction->setAutoExclusive(false);

    m_ctrlGroup = new QButtonGroup(m_groupCtrl);
    m_ctrlGroup->addButton(m_btnRoller, 1);
    m_ctrlGroup->addButton(m_btnSuperAction, 2);
    m_ctrlGroup->setExclusive(false);

    auto *layCtrl = new QHBoxLayout;
    layCtrl->addWidget(wrapSteer);
    layCtrl->addWidget(wrapRoll);
    layCtrl->addWidget(wrapAct);
    layCtrl->addWidget(makeHSpacer());
    m_groupCtrl->setLayout(layCtrl);

    connect(m_btnSteering, &QToolButton::clicked, this, &HardwareWindow::updateAvailability);

    connect(m_btnRoller, &QToolButton::toggled, this, [this](bool on){
        if (on) m_btnSuperAction->setChecked(false); // exclusief als 'on'
        updateAvailability();
    });

    connect(m_btnSuperAction, &QToolButton::toggled, this, [this](bool on){
        if (on) m_btnRoller->setChecked(false);      // exclusief als 'on'
        updateAvailability();
    });

    // === Additional Hardware ===
    m_groupAddHw = new QGroupBox("Additional Hardware", this);
    m_btnSGM  = new QToolButton(m_groupAddHw);
    m_btnF18A = new QToolButton(m_groupAddHw);
    m_btnPrinter = new QToolButton(m_groupAddHw);

    auto *layHw = new QHBoxLayout;
    layHw->addWidget(makeLabeledButton(m_btnSGM,  ":/images/images/hw_sgm.png",  "Opcode SGM",       false));
    layHw->addWidget(makeLabeledButton(m_btnF18A, ":/images/images/hw_f18a.png", "F18A VGA Adapter", false));

    auto *wrapPrinter = makeLabeledButton(m_btnPrinter, ":/images/images/hw_printer.png", "Printer Output", false);
    layHw->addWidget(wrapPrinter);

    layHw->addStretch(1);
    m_groupAddHw->setLayout(layHw);

    connect(m_btnSGM,  &QToolButton::toggled, this, &HardwareWindow::onToggleSGM);
    connect(m_btnSGM,  &QToolButton::clicked, this, &HardwareWindow::updateAvailability);
    connect(m_btnF18A, &QToolButton::clicked, this, &HardwareWindow::updateAvailability);
    connect(m_btnPrinter, &QToolButton::clicked, this, &HardwareWindow::onPrinterClicked);

    // === Video ===
    m_groupVideo = new QGroupBox("Video", this);
    auto *lblDisp = new QLabel("Display Driver", m_groupVideo);
    auto *lblPal  = new QLabel("Palette", m_groupVideo);

    m_cboDisplay = new QComboBox(m_groupVideo);
    m_cboDisplay->addItems({"GDI", "OpenGL", "Software"});

    m_cboPalette = new QComboBox(m_groupVideo);
    m_cboPalette->addItems({"Coleco", "TMS9918", "MSX", "Grayscale"});

    auto *layVidTop = new QGridLayout;
    layVidTop->addWidget(lblDisp,      0, 0);
    layVidTop->addWidget(m_cboDisplay, 0, 1);
    layVidTop->addWidget(lblPal,       1, 0);
    layVidTop->addWidget(m_cboPalette, 1, 1);
    layVidTop->setColumnStretch(1, 1);

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

    updatePaletteSwatches();

    // === Emulation ===
    m_groupEmu = new QGroupBox("Hardware", this);

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
    layEmuGrid->setColumnStretch(1, 8); // Beschrijving Label (Brede kolom)

    // --- Headers (Rij 0) ---
    QLabel *lblHwCol = new QLabel("Code", m_groupEmu);
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

    auto addTableRow = [&](int row, const QString& hwCode, QLabel*& lblEmu) {
        QLabel *lblHw = new QLabel(hwCode, m_groupEmu);
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
    addTableRow(1, "CC", m_lblEmuCC);

    m_lblEmuCA = createEmuLabel("No adam cartridge");
    addTableRow(2, "CA", m_lblEmuCA);

    m_lblEmuD1 = createEmuLabel("No tape");
    addTableRow(3, "D1", m_lblEmuD1);

    m_lblEmuD2 = createEmuLabel("No tape");
    addTableRow(4, "D2", m_lblEmuD2);

    m_lblEmuD5 = createEmuLabel("No disc");
    addTableRow(5, "D5", m_lblEmuD5);

    m_lblEmuD6 = createEmuLabel("No disc");
    addTableRow(6, "D6", m_lblEmuD6);

    m_lblEmuD7 = createEmuLabel("No disc");
    addTableRow(7, "D7", m_lblEmuD7);

    // Emu Layout (combineert image, widgets en grid)
    auto *layEmu = new QVBoxLayout;
    layEmu->addWidget(imgEmu, 0, Qt::AlignHCenter);
    layEmu->addWidget(m_chkStartDebug);
    layEmu->addWidget(m_chkNoDelayBios);
    layEmu->addWidget(m_chkPatchBiosPAL);
    layEmu->addWidget(m_cboFrequency);
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
    colLeft->addWidget(m_groupCtrl);
    colLeft->addWidget(m_groupAddHw);
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

}

void HardwareWindow::loadFromConfig(const HardwareConfig& c)
{
    // Machine
    m_btnColeco ->setChecked(c.machine == MACHINE_COLECO);
    m_btnAdam   ->setChecked(c.machine == MACHINE_ADAM);
    m_btnAdamP  ->setChecked(c.machine == MACHINE_ADAMP);

    // Video
    m_cboDisplay->setCurrentIndex(qBound(0, c.renderMode, m_cboDisplay->count()-1));
    m_cboPalette->setCurrentIndex(qBound(0, c.palette,    m_cboPalette->count()-1));

    if (!m_loading) { // Zorg dat dit alleen gebeurt bij het openen van de dialoog
        HardwareWindow::m_sgmSelectionState = c.sgmEnabled;
    }
    // Additional hardware
    m_btnSGM->setChecked(HardwareWindow::m_sgmSelectionState);
    m_btnF18A->setChecked(c.f18aEnabled);

    // Controllers
    m_btnSteering->setChecked(c.steeringWheel);
    m_btnRoller->setChecked(c.rollerCtrl);
    m_btnSuperAction->setChecked(c.superAction);

    if (m_btnRoller->isChecked() && m_btnSuperAction->isChecked()) {
        m_btnSuperAction->setChecked(false);
    }
    updateAvailability();
}

HardwareConfig HardwareWindow::readFromUi() const
{
    HardwareConfig c;

    // Machine
    if      (m_btnAdam->isChecked())   c.machine = MACHINE_ADAM;
    else if (m_btnAdamP->isChecked())  c.machine = MACHINE_ADAMP;
    else                               c.machine = MACHINE_COLECO;

    // Video
    c.renderMode = m_cboDisplay->currentIndex();
    c.palette    = m_cboPalette->currentIndex();

    // Additional hardware
    c.sgmEnabled  = !m_btnAdam->isChecked() && m_btnSGM->isChecked();
    c.f18aEnabled = m_btnF18A->isChecked();

    c.steeringWheel = m_btnSteering->isChecked();
    c.rollerCtrl    = m_btnRoller->isChecked();
    c.superAction   = m_btnSuperAction->isChecked();

    return c;
}

void HardwareWindow::updateAvailability()
{
    if (!m_btnColeco->isChecked() && !m_btnAdamP->isChecked() && !m_btnAdam->isChecked()) {
        m_btnColeco->setChecked(true);
    }

    const bool isColeco  = m_btnColeco->isChecked();
    const bool isAdamP   = m_btnAdamP->isChecked();
    const bool isAdam    = m_btnAdam->isChecked();

    if (isAdam) {

        HardwareWindow::m_sgmSelectionState = m_btnSGM->isChecked();
        m_btnSGM->setEnabled(false);

    } else {
        m_btnSGM->setEnabled(true);
        m_btnSGM->setChecked(HardwareWindow::m_sgmSelectionState);
    }
    m_btnF18A->setEnabled(true);
    m_btnPrinter->setEnabled(true);

    const bool padControllers = isColeco || isAdamP;
    m_btnSteering->setEnabled(padControllers);
    m_btnRoller->setEnabled(padControllers);
    m_btnSuperAction->setEnabled(padControllers);
    if (!padControllers) {
        m_btnSteering->setChecked(false);
        m_btnRoller->setChecked(false);
        m_btnSuperAction->setChecked(false);
    }

    auto setBorder = [](QToolButton* b){
        b->setStyleSheet(b->isChecked()
                         ? "QToolButton{border:3px solid #00ccff; border-radius:6px;}"
                           "QToolButton:hover{border:3px solid #33ddff;}"
                         : "QToolButton{border:1px solid #444; border-radius:6px;}"
                           "QToolButton:hover{border:1px solid #777;}");
    };

    setBorder(m_btnColeco);
    setBorder(m_btnAdamP);
    setBorder(m_btnAdam);
    setBorder(m_btnSteering);
    setBorder(m_btnRoller);
    setBorder(m_btnSuperAction);
    setBorder(m_btnSGM);
    setBorder(m_btnF18A);
    setBorder(m_btnPrinter);
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
    updateAvailability();
}

void HardwareWindow::onOk()
{
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
    const QString& colecoCartridgeName,
    const QString& adamCartridgeName,
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

    setLabelStatus(m_lblEmuCC, colecoCartridgeName, "No coleco cartridge");
    setLabelStatus(m_lblEmuCA, adamCartridgeName, "No adam cartridge");
    setLabelStatus(m_lblEmuD1, tape1Name, "No tape");
    setLabelStatus(m_lblEmuD2, tape2Name, "No tape");
    setLabelStatus(m_lblEmuD5, disc1Name, "No disc");
    setLabelStatus(m_lblEmuD6, disc2Name, "No disc");
    setLabelStatus(m_lblEmuD7, disc3Name, "No disc");
}
