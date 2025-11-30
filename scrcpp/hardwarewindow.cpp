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
    m_groupEmu = new QGroupBox("Emulation", this);

    auto *layEmu = new QVBoxLayout;
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
    auto *colLeft  = new QVBoxLayout;   // Machine / Controllers / Hardware
    colLeft->addWidget(m_groupMachine);
    colLeft->addWidget(m_groupCtrl);
    colLeft->addWidget(m_groupAddHw);
    colLeft->addStretch(1);

    auto *colRight = new QVBoxLayout;   // Video / Emulation
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

    // Additional hardware
    m_btnSGM->setChecked(c.sgmEnabled);
    m_btnF18A->setChecked(c.f18aEnabled);

    // Controllers
    m_btnSteering->setChecked(c.steeringWheel);
    m_btnRoller->setChecked(c.rollerCtrl);
    m_btnSuperAction->setChecked(c.superAction);

    if (m_btnRoller->isChecked() && m_btnSuperAction->isChecked()) {
        m_btnSuperAction->setChecked(false);
    }
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
    c.sgmEnabled  = m_btnSGM->isChecked();
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
        m_btnSGM->setEnabled(false);
        m_btnSGM->setChecked(false);
    } else {
        m_btnSGM->setEnabled(true);
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
