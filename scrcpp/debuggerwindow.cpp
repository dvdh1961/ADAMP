#include "debuggerwindow.h"
#include "colecocontroller.h"
#include "disasm_bridge.h"
#include "coleco.h"
#include "z80.h"
#include "gotoaddressdialog.h"
#include "tms9928a.h" // Noodzakelijk voor tms.VR[i] access

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QFontDatabase>
#include <QScrollBar>
#include <QTimer>
#include <QCloseEvent>
#include <QDebug>
#include <QComboBox>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QIcon>
#include <QListWidget>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>
#include <QCoreApplication>
#include <QTabWidget>
#include "setbreakpointdialog.h"
#include <algorithm>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>
#include "setbreakpointdialog.h"
#include "setsymboldialog.h"
#include "memoryeditdialog.h"

// Externe declaratie voor VDP-status structuur
extern tTMS9981A tms;

static BreakpointType breakpointTypeFromString(const QString& text)
{
    QString upper = text.trimmed().toUpper();

    if (upper.startsWith("REG "))   return BreakpointType::BP_REG_VAL;
    if (upper.startsWith("FLAG "))  return BreakpointType::BP_FLAG_VAL;
    if (upper.startsWith("MEM "))   return BreakpointType::BP_MEM_VAL;

    if (upper.startsWith("RD "))    return BreakpointType::BP_READ;
    if (upper.startsWith("WR "))    return BreakpointType::BP_WRITE;

    if (upper.startsWith("INH "))   return BreakpointType::BP_IO_IN;
    if (upper.startsWith("INL "))   return BreakpointType::BP_IO_IN;
    if (upper.startsWith("IN "))    return BreakpointType::BP_IO_IN;

    if (upper.startsWith("OUTH "))  return BreakpointType::BP_IO_OUT;
    if (upper.startsWith("OUTL "))  return BreakpointType::BP_IO_OUT;
    if (upper.startsWith("OUT "))   return BreakpointType::BP_IO_OUT;

    if (upper.startsWith("CLK "))   return BreakpointType::BP_CLOCK;
    if (upper.startsWith("EXE "))   return BreakpointType::BP_EXECUTE;

    bool ok = false;
    text.toUShort(&ok, 16);
    if (ok)
        return BreakpointType::BP_EXECUTE;

    return BreakpointType::BP_EXECUTE;
}


// --- Program tab LED helpers (no header changes needed) ---
static inline int programTabIndex(const DebuggerWindow* self)
{
    if (!self || !self->m_disasmTabWidget) return -1;
    // "Program" is the tab we add to m_disasmTabWidget
    // (we use the current index of the first/only tab; fallback to 0).
    if (self->m_disasmTabWidget->count() == 0) return -1;
    return 0;
}

static void setProgramLedIcon(DebuggerWindow* self, const QIcon& icon)
{
    if (!self || !self->m_disasmTabWidget) return;
    const int idx = programTabIndex(self);
    if (idx < 0) return;
    self->m_disasmTabWidget->setTabIcon(idx, icon);
}

static QTimer* ensurePauseBlinkTimer(DebuggerWindow* self)
{
    if (!self) return nullptr;
    auto timer = self->findChild<QTimer*>("dbg_pauseBlinkTimer");
    if (!timer) {
        timer = new QTimer(self);
        timer->setObjectName("dbg_pauseBlinkTimer");
        timer->setInterval(400);

        // store blink phase on the window (no member needed)
        self->setProperty("dbg_pauseBlinkPhase", false);

        QObject::connect(timer, &QTimer::timeout, self, [self]() {
            if (!self->m_disasmTabWidget) return;

            const bool phase = self->property("dbg_pauseBlinkPhase").toBool();
            self->setProperty("dbg_pauseBlinkPhase", !phase);

            setProgramLedIcon(self, phase
                ? QIcon(":/images/images/LED_GRAY.png")  // grijs
                : QIcon(":/images/images/LED_YELLOW.png")  // geel
            );
        });
    }
    return timer;
}

static void stopPauseBlink(DebuggerWindow* self)
{
    if (!self) return;
    if (auto t = self->findChild<QTimer*>("dbg_pauseBlinkTimer")) {
        t->stop();
    }
    self->setProperty("dbg_pauseBlinkPhase", false);
}

DebuggerWindow::DebuggerWindow(QWidget *parent)
    : QMainWindow(parent),
    m_controller(nullptr)
{
    setWindowTitle("ADAM+ Debugger");

    int x = 1080, y = 800;
    this->setMinimumSize(x, y);
    resize(x, y);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout *rootLayout = new QVBoxLayout(central);
    QFont tableFont1("Roboto", 10);
    QFont tableFont2("Roboto", 11);
    QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QFont smallFont("Roboto", 8);

    QString buttonStyle =
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }";

    auto createImageButton = [&](const QString &resourcePath) -> QPushButton* {
        QIcon icon(resourcePath);
        QPixmap pixmap(resourcePath);
        if (icon.isNull()) { qWarning() << "DebuggerWindow: Kon icoon niet laden:" << resourcePath; }
        QPushButton *button = new QPushButton(this);
        button->setIcon(icon);
        button->setIconSize(pixmap.size());
        button->setFixedSize(pixmap.size());
        button->setText("");
        button->setFlat(true);
        button->setStyleSheet(buttonStyle);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };


    QHBoxLayout *topRow = new QHBoxLayout();

    // --- 1. DISASSEMBLY (Program Tab Widget) ---
    {
        m_disasmTabWidget = new QTabWidget(this);
        m_disasmTabWidget->setFont(tableFont1);
        m_disasmTabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_disasmTabWidget->setMinimumWidth(260);

        QWidget *disasmTab = new QWidget(m_disasmTabWidget);
        QVBoxLayout *disLayout = new QVBoxLayout(disasmTab);
        disLayout->setContentsMargins(4, 4, 4, 4);

        // WIJZIGING 1: 5 Kolommen in plaats van 4
        m_disasmView = new QTableWidget(0, 5, this);
        // WIJZIGING 2: Nieuwe header voor de Label-injectie
        m_disasmView->setHorizontalHeaderLabels(QStringList() << "Label" << "Addr" << "Code" << "Mnemonic" << "Target");
        m_disasmView->verticalHeader()->hide();
        m_disasmView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_disasmView->setFont(tableFont1);
        m_disasmView->setMinimumHeight(442);
        m_disasmView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_disasmView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // WIJZIGING 3: Kolombreedtes aanpassen
        m_disasmView->setColumnWidth(0, 160); // Label
        m_disasmView->setColumnWidth(1, 50);  // Adres
        m_disasmView->setColumnWidth(2, 80);  // Code
        m_disasmView->setColumnWidth(3, 100); // Mnemonic
        m_disasmView->setColumnWidth(4, 160); // Target Label (NIEUW)
        m_disasmView->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_disasmView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_disasmView->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(m_disasmView, &QTableWidget::customContextMenuRequested,
                this, &DebuggerWindow::onDisasmContextMenu);

        disLayout->addWidget(m_disasmView, 1);
        m_disasmTabWidget->addTab(disasmTab, tr("Program"));


// --- LED naast "Program" tab (stop by default) ---
m_disasmTabWidget->setIconSize(QSize(14, 14));
m_disasmTabWidget->setTabIcon(m_disasmTabWidget->indexOf(disasmTab), QIcon(":/images/images/LED_GRAY.png"));
        topRow->addWidget(m_disasmTabWidget, 1); // Neemt de meeste horizontale stretch
    }

    // --- 2. REGISTERS (Z80 Tab Widget) ---
    {
        m_regsTabWidget = new QTabWidget(this);
        m_regsTabWidget->setFont(tableFont1);
        m_regsTabWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        m_regsTabWidget->setMinimumWidth(140);
        m_regsTabWidget->setMaximumWidth(140);

        QWidget *regTab = new QWidget(m_regsTabWidget);
        QVBoxLayout *regLayout = new QVBoxLayout(regTab);
        regLayout->setContentsMargins(4, 4, 4, 4);

        m_regTable = new QTableWidget(0, 2, this);
        m_regTable->setHorizontalHeaderLabels(QStringList() << "Reg" << "Value");
        m_regTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_regTable->verticalHeader()->hide();
        m_regTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_regTable->setFont(tableFont2);
        m_regTable->setMinimumWidth(120);
        m_regTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_regTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_regTable->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(m_regTable, &QTableWidget::customContextMenuRequested,
                this, &DebuggerWindow::onRegistersContextMenu);

        regLayout->addWidget(m_regTable, 1);
        m_regsTabWidget->addTab(regTab, tr("Z80"));

        topRow->addWidget(m_regsTabWidget, 0); // Vaste breedte
    }

    // --- 3. FLAGS, STACK, en VDP Regs Kolom ---
    {
        // Wrapper voor de vlaggen/stack en VDP registers, die de breedte van 320px vastlegt.
        QVBoxLayout *rightTabsWrapper = new QVBoxLayout();
        rightTabsWrapper->setContentsMargins(0, 0, 0, 0);

        // 3.1 Flags/Stack Tab Widget
        m_flagsStackTabWidget = new QTabWidget(this);
        m_flagsStackTabWidget->setFont(tableFont1);
        m_flagsStackTabWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        m_flagsStackTabWidget->setMinimumWidth(320);
        m_flagsStackTabWidget->setMaximumWidth(320);

        QWidget *fsTab = new QWidget(m_flagsStackTabWidget);
        QVBoxLayout *fsLayout = new QVBoxLayout(fsTab);
        fsLayout->setContentsMargins(4, 4, 4, 4);

        // Flags Table
        m_flagsTable = new QTableWidget(1, 6, this);
        m_flagsTable->setHorizontalHeaderLabels(QStringList() << "S" << "Z" << "H" << "P/V" << "N" << "C");
        m_flagsTable->verticalHeader()->hide();
        m_flagsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_flagsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_flagsTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_flagsTable->setFont(fixedFont);
        m_flagsTable->setFocusPolicy(Qt::NoFocus);
        m_flagsTable->setSelectionMode(QAbstractItemView::NoSelection);
        int flagsHeight = (m_flagsTable->horizontalHeader()->height() + 2) + (1 * 20);
        m_flagsTable->setMinimumHeight(flagsHeight);
        m_flagsTable->setMaximumHeight(flagsHeight);
        fsLayout->addWidget(m_flagsTable, 0);

        // Stack Table
        m_stackTable = new QTableWidget(7, 4, this);
        m_stackTable->setHorizontalHeaderLabels(QStringList() << "Addr 0..7" << "Val" << "Addr 8..14" << "Val");
        m_stackTable->verticalHeader()->hide();
        m_stackTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_stackTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_stackTable->setFont(fixedFont);
        m_stackTable->setFocusPolicy(Qt::NoFocus);
        m_stackTable->setSelectionMode(QAbstractItemView::NoSelection);
        fsLayout->addWidget(m_stackTable, 1 /* stretch 1 */);

        m_flagsStackTabWidget->addTab(fsTab, tr("Flags/Stack"));

        rightTabsWrapper->addWidget(m_flagsStackTabWidget, 1); // Flags/Stack krijgt stretch 1


        // --- 3.2 VDP Registers Tab Widget ---
        m_vdpRegsTabWidget = new QTabWidget(this);
        m_vdpRegsTabWidget->setFont(tableFont1);

        // ** VASTE HOOGTE INSTELLING **
        m_vdpRegsTabWidget->setFixedHeight(209);
        m_vdpRegsTabWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        // ******************************

        m_vdpRegsTabWidget->setMinimumWidth(320);
        m_vdpRegsTabWidget->setMaximumWidth(320);

        QWidget *vdpTab = new QWidget(m_vdpRegsTabWidget);
        QVBoxLayout *vdpLayout = new QVBoxLayout(vdpTab);
        vdpLayout->setContentsMargins(4, 4, 4, 4);

        // VDP Registers Tabel (4 rijen x 4 kolommen: R0-Waarde R1-Waarde ...)
        m_vdpRegTable = new QTableWidget(6, 4, this);
        m_vdpRegTable->setHorizontalHeaderLabels(QStringList() << "Reg" << "Val" << "Reg" << "Val");
        m_vdpRegTable->verticalHeader()->hide();
        m_vdpRegTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_vdpRegTable->setFont(tableFont1);
        m_vdpRegTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_vdpRegTable->verticalHeader()->setDefaultSectionSize(25);
        m_vdpRegTable->setFocusPolicy(Qt::NoFocus);
        m_vdpRegTable->setSelectionMode(QAbstractItemView::NoSelection);

        vdpLayout->addWidget(m_vdpRegTable, 1); // Stretch 1
        m_vdpRegsTabWidget->addTab(vdpTab, tr("VDP Regs"));

        rightTabsWrapper->addWidget(m_vdpRegsTabWidget, 0); // VDP Regs krijgt stretch 0 (vaste hoogte)

        topRow->addLayout(rightTabsWrapper, 0); // De Wrapper heeft een vaste breedte
    }

    rootLayout->addLayout(topRow, 1);


    // Initialisatie van het Tab Widget voor Breakpoints/Patches (apart gehouden)
    m_rightTabWidget = new QTabWidget(this);
    m_rightTabWidget->setFont(tableFont1);
    m_rightTabWidget->setMinimumWidth(320);
    m_rightTabWidget->setMaximumWidth(320);


    // --- Breakpoints ---
    {
        QWidget *bpTab = new QWidget(m_rightTabWidget);
        QVBoxLayout *bpTabLayout = new QVBoxLayout(bpTab);
        bpTabLayout->setContentsMargins(4, 4, 4, 4);
        bpTabLayout->setSpacing(4);

        // Breakpoint Lijst (vult resterende ruimte)
        m_bpList = new QListWidget(this);
        m_bpList->setFont(fixedFont);
        m_bpList->setSelectionMode(QAbstractItemView::SingleSelection);

        // Vult de resterende ruimte
        bpTabLayout->addWidget(m_bpList, 1);

        // Breakpoint Knoppen
        QHBoxLayout *bpButtons = new QHBoxLayout();
        bpButtons->setSpacing(4);
        QString bpBtnStyle = "QPushButton { padding: 2px 5px; }";

        m_bpAddButton = new QPushButton(tr("Add"), this);
        m_bpAddButton->setStyleSheet(bpBtnStyle);

        m_bpDelButton = new QPushButton(tr("Delete"), this);
        m_bpDelButton->setStyleSheet(bpBtnStyle);

        m_bpEditButton = new QPushButton(tr("Edit"), this);
        m_bpEditButton->setStyleSheet(bpBtnStyle);

        m_bpLoadButton = new QPushButton(tr("Load"), this);
        m_bpLoadButton->setStyleSheet(bpBtnStyle);

        m_bpSaveButton = new QPushButton(tr("Save"), this);
        m_bpSaveButton->setStyleSheet(bpBtnStyle);

        m_bpEnableButton = new QPushButton(tr("Break ON"), this);
        m_bpEnableButton->setCheckable(true);

        // Interpretatie:
        //  - unchecked  (false) = ON  (rood)
        //  - checked    (true)  = OFF (groen)
        m_bpEnableButton->setChecked(false);

        // master-flag overeen laten komen met rood = aan
        m_breakpointsEnabled = true;

        m_bpEnableButton->setStyleSheet(
            "background-color: #888800; color: white; font-weight: bold; padding: 2px 5px;"
            );

        bpButtons->addWidget(m_bpAddButton);
        bpButtons->addWidget(m_bpDelButton);
        bpButtons->addWidget(m_bpEditButton);
        bpButtons->addWidget(m_bpLoadButton);
        bpButtons->addWidget(m_bpSaveButton);
        bpButtons->addStretch();
        bpButtons->addWidget(m_bpEnableButton);

        // Knoppen-layout (blijft compact onderaan)
        bpTabLayout->addLayout(bpButtons, 0);

        m_rightTabWidget->addTab(bpTab, tr("Breakpoints"));

        // Connect breakpoint signalen
        connect(m_bpEnableButton, &QPushButton::clicked,
                this, &DebuggerWindow::onToggleBreakpoints);
        connect(m_bpAddButton, &QPushButton::clicked, this, &DebuggerWindow::onBpAdd);
        connect(m_bpDelButton, &QPushButton::clicked, this, &DebuggerWindow::onBpDel);
        connect(m_bpEditButton, &QPushButton::clicked, this, &DebuggerWindow::onBpEdit);
        connect(m_bpList, &QListWidget::itemDoubleClicked, this, &DebuggerWindow::onBpEdit);
        connect(m_bpLoadButton, &QPushButton::clicked, this, &DebuggerWindow::onBpLoad);
        connect(m_bpSaveButton, &QPushButton::clicked, this, &DebuggerWindow::onBpSave);
    }

    // --- Symbols ---
    {
        QWidget *symbolsTab = new QWidget(m_rightTabWidget);
        QVBoxLayout *symbolsLayout = new QVBoxLayout(symbolsTab);
        symbolsLayout->setContentsMargins(4, 4, 4, 4);
        symbolsLayout->setSpacing(4);

        // Symbolen Lijst
        m_symList = new QListWidget(this); // NIEUW: Symbolen Lijst
        m_symList->setFont(fixedFont);
        m_symList->setSelectionMode(QAbstractItemView::SingleSelection);

        symbolsLayout->addWidget(m_symList, 1);

        // Symbolen Knoppen (zonder Enable/Disable knop)
        QHBoxLayout *symButtons = new QHBoxLayout();
        symButtons->setSpacing(4);
        QString symBtnStyle = "QPushButton { padding: 2px 5px; }";

        m_symAddButton = new QPushButton(tr("Add"), this); // NIEUW
        m_symAddButton->setStyleSheet(symBtnStyle);

        m_symDelButton = new QPushButton(tr("Delete"), this); // NIEUW
        m_symDelButton->setStyleSheet(symBtnStyle);

        m_symEditButton = new QPushButton(tr("Edit"), this); // NIEUW
        m_symEditButton->setStyleSheet(symBtnStyle);

        m_symLoadButton = new QPushButton(tr("Load"), this); // NIEUW
        m_symLoadButton->setStyleSheet(symBtnStyle);

        m_symSaveButton = new QPushButton(tr("Save"), this); // NIEUW
        m_symSaveButton->setStyleSheet(symBtnStyle);

        m_symEnableButton = new QPushButton(tr("Sym ON"), this); // NIEUW
        m_symEnableButton->setCheckable(true);
        m_symEnableButton->setChecked(false); // unchecked = ON (rood)
        m_symbolsEnabled = true; // master-flag AAN
        m_symEnableButton->setStyleSheet(
            "background-color: #888800; color: white; font-weight: bold; padding: 2px 5px;"
            );

        symButtons->addWidget(m_symAddButton);
        symButtons->addWidget(m_symDelButton);
        symButtons->addWidget(m_symEditButton);
        symButtons->addWidget(m_symLoadButton);
        symButtons->addWidget(m_symSaveButton);
        symButtons->addStretch(); // Geen Enable/Disable knop hier
        symButtons->addWidget(m_symEnableButton);

        symbolsLayout->addLayout(symButtons, 0);

        m_rightTabWidget->addTab(symbolsTab, tr("Symbols"));

        // Connect symbol signalen
        connect(m_symAddButton, &QPushButton::clicked, this, &DebuggerWindow::onSymAdd);
        connect(m_symDelButton, &QPushButton::clicked, this, &DebuggerWindow::onSymDel);
        connect(m_symEditButton, &QPushButton::clicked, this, &DebuggerWindow::onSymEdit);
        connect(m_symList, &QListWidget::itemDoubleClicked, this, &DebuggerWindow::onSymEdit);
        connect(m_symLoadButton, &QPushButton::clicked, this, &DebuggerWindow::onSymLoad);
        connect(m_symSaveButton, &QPushButton::clicked, this, &DebuggerWindow::onSymSave);
        connect(m_symEnableButton, &QPushButton::clicked, this, &DebuggerWindow::onToggleSymbols);
    }

    // ======= BOTTOM ROW (MEMORY DUMP TABS LINKS / BREAKPOINTS RECHTS) =======
    {
        QHBoxLayout *bottomRow = new QHBoxLayout();

        // --- 1. Memory Tab Widget (Links) ---

        m_memSourceTabWidget = new QTabWidget(this);
        m_memSourceTabWidget->setFont(tableFont1);
        m_memSourceTabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        // --- De enige Memory Tab ---
        {
            QWidget *memTab = new QWidget(m_memSourceTabWidget);
            QVBoxLayout *memTabLayout = new QVBoxLayout(memTab);
            memTabLayout->setContentsMargins(4, 4, 4, 4);
            memTabLayout->setSpacing(4);

            // Memory Dump Tabel
            m_memTable = new QTableWidget(0, 19, this);
            QStringList headers;
            headers << ""; // Adres
            for (int i = 0; i < 16; ++i) headers << QString("%1").arg(i, 2, 16, QChar('0')).toUpper();
            headers << ""; // Nieuwe lege scheidingskolom
            headers << "ASCII"; // ASCII
            m_memTable->setHorizontalHeaderLabels(headers);

            m_memTable->verticalHeader()->hide();
            m_memTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
            m_memTable->setSelectionMode(QAbstractItemView::SingleSelection);
            m_memTable->setSelectionBehavior(QAbstractItemView::SelectRows);
            m_memTable->setAlternatingRowColors(true);
            m_memTable->setFont(fixedFont);

            m_memTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
            m_memTable->verticalHeader()->setDefaultSectionSize(20);
            m_memTable->setColumnWidth(0, 40); // Adres
            for (int i = 1; i <= 16; ++i) m_memTable->setColumnWidth(i, 28); // Hex Bytes

            // WIJZIGING: Kolom 17 (de nieuwe lege kolom) de breedte geven
            m_memTable->setColumnWidth(17, 40); // Lege scheiding van 40 pixels

            // WIJZIGING: Kolom 18 is nu de ASCII-kolom
            m_memTable->setColumnWidth(18, 120);
            memTabLayout->addWidget(m_memTable, 1); // Stretch 1

            // Memory Controls
            QHBoxLayout *memControlsLayout = new QHBoxLayout();
            memControlsLayout->addWidget(new QLabel("Source:", this));

            // Combobox
            m_memSourceComboBox = new QComboBox(this);
            m_memSourceComboBox->addItems({"Z80 CPU Space", "TMS VDP VRAM", "Main RAM", "SGM RAM", "EEPROM", "SRAM"});
            m_memSourceComboBox->setMinimumWidth(200);
            memControlsLayout->addWidget(m_memSourceComboBox);

            memControlsLayout->addStretch();

            memControlsLayout->addSpacing(20);
            memControlsLayout->addWidget(new QLabel("Address:", this));

            m_memAddrPrevButton = new QPushButton("<", this);
            m_memAddrPrevButton->setFixedSize(24, 24);
            m_memAddrPrevButton->setFont(fixedFont);
            memControlsLayout->addWidget(m_memAddrPrevButton);

            m_memAddrEdit = new QLineEdit("0000", this);
            m_memAddrEdit->setFont(fixedFont);
            m_memAddrEdit->setFixedWidth(60);
            m_memAddrEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9a-fA-F]{1,5}"), this));
            memControlsLayout->addWidget(m_memAddrEdit);

            m_memAddrNextButton = new QPushButton(">", this);
            m_memAddrNextButton->setFixedSize(24, 24);
            m_memAddrNextButton->setFont(fixedFont);
            memControlsLayout->addWidget(m_memAddrNextButton);

            m_memAddrHomeButton = new QPushButton(this);
            m_memAddrHomeButton->setText("-0-");
            m_memAddrHomeButton->setFont(fixedFont);
            m_memAddrHomeButton->setFixedSize(24, 24);
            memControlsLayout->addWidget(m_memAddrHomeButton);

            // --- Copy memory range button ---
            m_memCopyRangeButton = new QPushButton(tr("Copy range"), this);
            m_memCopyRangeButton->setFixedHeight(24);
            memControlsLayout->addSpacing(12);
            memControlsLayout->addWidget(m_memCopyRangeButton);

            memTabLayout->addLayout(memControlsLayout, 0);

            // Voeg de Memory Tab toe aan het Tab Widget
            m_memSourceTabWidget->addTab(memTab, tr("Memory"));
        }

        bottomRow->addWidget(m_memSourceTabWidget, 1); // Neemt de meeste breedte

        // --- 2. Breakpoints/Symbols Tab Wrapper (Rechts) ---
        QVBoxLayout *bpWrapper = new QVBoxLayout();
        bpWrapper->setContentsMargins(4, 0, 0, 0);

        m_rightTabWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        m_rightTabWidget->setMinimumHeight(100);

        bpWrapper->addWidget(m_rightTabWidget, 1);

        bottomRow->addLayout(bpWrapper, 0); // Vaste breedte (stretch 0)

        // Voeg de Bottom Row toe aan de Root Layout
        rootLayout->addLayout(bottomRow, 1);

        // Koppel de controls aan de lay-out
        connect(m_memSourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DebuggerWindow::onMemSourceChanged);
        connect(m_memAddrEdit, &QLineEdit::returnPressed, this, &DebuggerWindow::onMemAddrChanged);
        connect(m_memAddrPrevButton, &QPushButton::clicked, this, &DebuggerWindow::onMemAddrPrev);
        connect(m_memAddrNextButton, &QPushButton::clicked, this, &DebuggerWindow::onMemAddrNext);
        connect(m_memAddrHomeButton, &QPushButton::clicked, this, &DebuggerWindow::onMemAddrHome);
        connect(m_memCopyRangeButton,&QPushButton::clicked,this,&DebuggerWindow::onCopyMemoryRange);
    }

    // Koppel het dubbelklik signaal aan de nieuwe slot
    connect(m_memTable, &QTableWidget::cellDoubleClicked,
            this, &DebuggerWindow::onMemTableDoubleClicked);

    // ======= DEBUGGER CONTROLS (knoppen) =======
    QHBoxLayout *buttons = new QHBoxLayout;

    btnStep        = createImageButton(":/images/images/STEP.png");
    btnRun         = createImageButton(":/images/images/RUN.png");
    btnBreak       = createImageButton(":/images/images/BREAK.png");
    //btnRefresh     = createImageButton(":/images/images/REFRESH.png");
    btnGotoAddr    = createImageButton(":/images/images/GOTO.png");
    btnSinglestep  = createImageButton(":/images/images/SSTEP.png");
    btnStepOver    = createImageButton(":/images/images/STEPOVER.png");

    connect(btnBreak, &QPushButton::clicked, this, [this](){
        if (m_controller) m_controller->pauseEmulation();
        if (m_disasmView) {
            m_disasmView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        }
    });
    connect(btnStep, &QPushButton::clicked, this, [this](){
        if (m_controller) m_controller->stepOnce();
        if (m_disasmView) {
            m_disasmView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        }
    });
    connect(btnBreak, &QPushButton::clicked, this, [this](){
        emit requestBreakCPU();
        if (m_disasmView) {
            m_disasmView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        }
    });
    connect(btnGotoAddr, &QPushButton::clicked, this, &DebuggerWindow::gotoAddress);
    connect(btnSinglestep, &QPushButton::clicked, this, &DebuggerWindow::nxtSStep);
    //connect(btnRefresh, &QPushButton::clicked, this, &DebuggerWindow::updateAllViews);
    connect(btnRun, &QPushButton::clicked, this, [this](){
        if (m_controller) {DebugBridge::instance().clearLastBreakPC(); m_controller->resumeEmulation();}
        if (m_disasmView) {
            m_disasmView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    });
    connect(btnStepOver, &QPushButton::clicked, this, &DebuggerWindow::onStepOver);

    buttons->addWidget(btnSinglestep);
    buttons->addWidget(btnStep);
    buttons->addWidget(btnStepOver);
    buttons->addWidget(btnGotoAddr);
    buttons->addWidget(btnRun);
    buttons->addStretch();
    buttons->addWidget(btnBreak);
    //buttons->addStretch();
    //buttons->addWidget(btnRefresh);
    rootLayout->addLayout(buttons, 0);

    updateAllViews();
}

void DebuggerWindow::setController(ColecoController *controller)
{
    if (m_controller) {
        // Verbreek oude verbindingen
        disconnect(m_controller, nullptr, this, nullptr);
    }

    m_controller = controller;

    // Nieuwe verbindingen
    if (m_controller) {
        // [Andere verbindingen...]

        // DE CRUCIALE KOPPELING: Wanneer de emulator stopt (door een breakpoint)
        // moet de GUI alle vensters vernieuwen.
        connect(m_controller, &ColecoController::emulationStopped,
                this, &DebuggerWindow::updateAllViews);

        connect(m_controller, &ColecoController::emulationStopped,
                this, [this]() {
                    stopPauseBlink(this);
                    setProgramLedIcon(this, QIcon(":/images/images/LED_GRAY.png"));
                });
        // Optioneel: Voor de knoppen (Run/Step/Break) status:
        connect(m_controller, &ColecoController::emuPausedChanged,
                this, &DebuggerWindow::onEmuPausedChanged); // *U moet deze slot toevoegen*

        // Fallback: if breakpoints pause the emulator without emitting emuPausedChanged,
        // detect it via DebugBridge::lastBreakPC() and refresh UI.
        m_breakPollTimer = new QTimer(this);
        m_breakPollTimer->setInterval(50);
        connect(m_breakPollTimer, &QTimer::timeout, this, [this]() {
            const uint16_t hitPc = DebugBridge::instance().lastBreakPC();
            if (hitPc != 0 && !m_forcedPausedUi) {
                m_forcedPausedUi = true;
                onEmuPausedChanged(true);
                updateAllViews();
            }
        });
        m_breakPollTimer->start();

    }
}

void DebuggerWindow::updateAllViews()
{
    updateRegisters();
    updateDisassembly();
    updateMemoryDump();
    updateFlags();
    updateStack();
    updateVdpRegisters();
}

void DebuggerWindow::updateRegisters()
{
    if (!m_regTable) return;
    struct RegEntry { const char* name; uint16_t value; };
    RegEntry regs[] = {
                       {"PC", Z80.pc.w.l}, {"SP", Z80.sp.w.l},
                       {"AF", Z80.af.w.l}, {"BC", Z80.bc.w.l},
                       {"DE", Z80.de.w.l}, {"HL", Z80.hl.w.l},
                       {"IX", Z80.ix.w.l}, {"IY", Z80.iy.w.l},
                       {"AF'", Z80.af2.w.l}, {"BC'", Z80.bc2.w.l},
                       {"DE'", Z80.de2.w.l}, {"HL'", Z80.hl2.w.l},
                       {"I", Z80.i}, {"R", Z80.r},
                       };
    m_regTable->setRowCount(0);
    for (auto &r : regs) {
        int row = m_regTable->rowCount();
        m_regTable->insertRow(row);
        auto nameItem = new QTableWidgetItem(r.name);
        auto valItem  = new QTableWidgetItem(
            QString("$%1").arg(r.value, (r.name == "I" || r.name == "R") ? 2 : 4, 16, QChar('0')).toUpper());
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        valItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (row == 0) { nameItem->setForeground(QColor("#4FC3F7")); valItem->setForeground(QColor("#4FC3F7")); }
        else if (row == 1) { nameItem->setForeground(QColor("#BF8040")); valItem->setForeground(QColor("#BF8040")); }
        else if (row > 11) { nameItem->setForeground(QColor("#BFBF00")); valItem->setForeground(QColor("#BFBF00")); }
        else { nameItem->setForeground(QColor("#9E9E9E")); valItem->setForeground(QColor("#FFFFFF")); }
        m_regTable->setItem(row, 0, nameItem);
        m_regTable->setItem(row, 1, valItem);
    }
}

void DebuggerWindow::updateDisassembly()
{
    if (!m_disasmView) return;

    // FONT DEFINITIES
    QFont labelFont("Roboto", 7);   // Kolom 0 en 4
    QFont instructionFont("Roboto", 10); // Kolom 1,2,3
    const QColor symbolColor = QColor("#FFA500");

    m_disasmView->setRowCount(0);

    //const uint16_t pc = Z80.pc.w.l;

    uint16_t pc = DebugBridge::instance().lastBreakPC();
    if (pc == 0)
        pc = Z80.pc.w.l;


    // qDebug() << "updateDisassembly lastBreakPC="
    //          << QString::number(DebugBridge::instance().lastBreakPC(),16)
    //          << "cpuPC=" << QString::number(Z80.pc.w.l,16);

    const int MAX_LINES        = 64;
    const int LINES_BEFORE_PC  = MAX_LINES / 2;
    const uint16_t SEARCH_BACK_BYTES = 0x0200; // zoek maximaal 0x200 bytes terug

    // ---------------------------------------------------------
    // 1) Bepaal startadres o.b.v. AANTAL INSTRUCTIES, niet bytes
    // ---------------------------------------------------------
    uint16_t searchStart = (pc > SEARCH_BACK_BYTES) ? pc - SEARCH_BACK_BYTES : 0x0000;

    QVector<uint16_t> instrAddrs;
    instrAddrs.reserve(256);

    uint16_t cur = searchStart;
    int pcIndex = -1;

    while (cur <= pc && cur <= 0xFFFF) {
        uint16_t thisAddr = cur;
        int oplen = 0;
        (void)disasmOneAt(cur, oplen);

        // Safety: forceer altijd vooruitgang
        if (oplen <= 0 || oplen > 4) {
            oplen = 1;
        }

        instrAddrs.push_back(thisAddr);

        if (thisAddr == pc) {
            pcIndex = instrAddrs.size() - 1;
        }

        // Bescherm tegen overflow/wrap
        if (thisAddr == 0xFFFF)
            break;

        cur = thisAddr + static_cast<uint16_t>(oplen);
        if (cur == thisAddr) {
            ++cur;
        }
        if (cur == 0) { // wrap rond 0xFFFF→0
            break;
        }

        if (thisAddr > pc) {
            break;
        }
    }

    uint16_t startAddr = 0x0000;

    if (pcIndex >= 0 && !instrAddrs.isEmpty()) {
        int startIndex = pcIndex - LINES_BEFORE_PC;
        if (startIndex < 0) startIndex = 0;
        if (startIndex >= instrAddrs.size()) startIndex = instrAddrs.size() - 1;
        startAddr = instrAddrs[startIndex];
    } else {
        // Fallback: oude gedrag als PC niet netjes in de lijst zit
        const int CONTEXT_BACK_BYTES = 100;
        startAddr = (pc > CONTEXT_BACK_BYTES) ? (pc - CONTEXT_BACK_BYTES) : 0x0000;
    }

    // ---------------------------------------------------------
    // 2) Tabel vullen vanaf startAddr, PC-rij markeren
    // ---------------------------------------------------------
    int pcRow = -1;
    cur = startAddr;

    for (int line = 0; line < MAX_LINES; ++line) {
        int oplen = 0;
        QString instr = disasmOneAt(cur, oplen);

        if (oplen <= 0 || oplen > 4) {
            oplen = 1;
            instr = QString("DB $%1").arg(coleco_ReadByte(cur), 2, 16, QChar('0')).toUpper();
        }

        // Label opzoeken
        QString label = getSymbolLabelForAddress(cur);

        // Bytes string opbouwen
        QString bytesStr;
        for (int b = 0; b < oplen; ++b) {
            bytesStr += QString("%1 ").arg(coleco_ReadByte(cur + b), 2, 16, QChar('0')).toUpper();
        }
        bytesStr = bytesStr.trimmed();

        int row = m_disasmView->rowCount();
        m_disasmView->insertRow(row);

        // Kolom 0: Label
        QTableWidgetItem *lblItem;
        if (m_symbolsEnabled && !label.isEmpty()) {
            lblItem = new QTableWidgetItem(label);
        } else {
            lblItem = new QTableWidgetItem("");
        }
        lblItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        lblItem->setForeground(symbolColor);
        lblItem->setFont(labelFont);

        // Kolom 1: Adres
        auto addrItem = new QTableWidgetItem(QString("%1").arg(cur, 4, 16, QChar('0')).toUpper());
        addrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        addrItem->setForeground(QColor("#4FC3F7"));
        addrItem->setFont(instructionFont);

        // Kolom 2: Bytes
        auto bytesItem = new QTableWidgetItem(bytesStr);
        bytesItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        bytesItem->setForeground(QColor("#81C784"));
        bytesItem->setFont(instructionFont);

        // Kolom 3 + 4: Mnemonic + Target label
        QString targetLabelText;
        int tagIndex = instr.indexOf("$$");
        if (m_symbolsEnabled && tagIndex != -1) {
            QString targetAddrStr = instr.mid(tagIndex - 4, 4);
            bool ok;
            uint16_t targetAddr = targetAddrStr.toUShort(&ok, 16);

            instr.chop(2); // "$$" weg

            if (ok) {
                QString targetLabel = getSymbolLabelForAddress(targetAddr);
                if (!targetLabel.isEmpty()) {
                    targetLabelText = "< " + targetLabel;
                }
            }
        }

        auto instrItem = new QTableWidgetItem(instr);
        instrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        instrItem->setForeground(QColor("#E0E0E0"));
        instrItem->setFont(instructionFont);

        QTableWidgetItem *targetItem = new QTableWidgetItem(targetLabelText);
        targetItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        targetItem->setForeground(symbolColor);
        targetItem->setFont(labelFont);

        m_disasmView->setItem(row, 0, lblItem);
        m_disasmView->setItem(row, 1, addrItem);
        m_disasmView->setItem(row, 2, bytesItem);
        m_disasmView->setItem(row, 3, instrItem);
        m_disasmView->setItem(row, 4, targetItem);

        // PC gevonden binnen deze instructie?
        if (cur <= pc && pc < cur + static_cast<uint16_t>(oplen)) {
            pcRow = row;
        }

        uint16_t next = cur + static_cast<uint16_t>(oplen);
        if (next <= cur) { // protect tegen overflow/wrap
            break;
        }
        cur = next;
        if (cur > 0xFFFF) {
            break;
        }
    }

    // ---------------------------------------------------------
    // 3) Scrollen + highlighten van de PC-rij
    // ---------------------------------------------------------
    if (pcRow >= 0) {
        m_disasmView->scrollToItem(m_disasmView->item(pcRow, 0),
                                   QAbstractItemView::PositionAtCenter);

        QColor redLine(200, 30, 30);
        QBrush bg(redLine);
        QBrush fg(Qt::white);

        for (int col = 0; col < 5; ++col) {
            if (QTableWidgetItem *it = m_disasmView->item(pcRow, col)) {
                it->setBackground(bg);
                it->setForeground(fg);
            }
        }
    }
}


void DebuggerWindow::updateMemoryDump()
{
    if (!m_memTable) return;

    // De Memory Dump is altijd zichtbaar, we gebruiken de m_currentMemSourceIndex.

    m_memTable->setRowCount(0);
    const int bytesPerLine = 16;
    const int numLines = 32;
    uint32_t currentAddr = m_memDumpStartAddr;
    for (int line = 0; line < numLines; ++line) {
        uint32_t base = currentAddr + (line * bytesPerLine);
        m_memTable->insertRow(line);
        auto addrItem = new QTableWidgetItem(QString("%1").arg(base, 4, 16, QChar('0')).toUpper());
        addrItem->setForeground(Qt::cyan);
        addrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_memTable->setItem(line, 0, addrItem);
        QString ascii;
        for (int i = 0; i < bytesPerLine; ++i) {
            uint8_t b = readMemoryByte(base + i);
            auto itm = new QTableWidgetItem(QString("%1").arg(b, 2, 16, QChar('0')).toUpper());
            itm->setTextAlignment(Qt::AlignCenter);
            itm->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_memTable->setItem(line, 1 + i, itm);
            ascii += (b >= 32 && b <= 126) ? QChar(b) : '.';
        }

        // We laten m_memTable->setItem(line, 17, NULL) weg, omdat de kolom leeg moet blijven.
        // De kolom is al een lege QTableWidgetItem omdat we deze niet expliciet instellen.

        auto ascItem = new QTableWidgetItem(ascii);
        ascItem->setForeground(Qt::darkGray);
        ascItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        // WIJZIGING: Plaats de ASCII-weergave in kolom 18
        m_memTable->setItem(line, 18, ascItem);
    }
}

void DebuggerWindow::updateVdpRegisters()
{
    if (!m_vdpRegTable) return;

    // We hebben 4 rijen voor R0-R7 + 2 rijen voor status = 6 rijen
    if (m_vdpRegTable->rowCount() != 6) {
        m_vdpRegTable->setRowCount(6);
    }

    // Helper functie om items in te stellen
    auto setItem = [&](int r, int c, const QString& regText, const QString& valText, QColor regCol, QColor valCol) {
        QTableWidgetItem *regItem = m_vdpRegTable->item(r, c);
        if (!regItem) {
            regItem = new QTableWidgetItem();
            regItem->setFlags(Qt::ItemIsEnabled);
            m_vdpRegTable->setItem(r, c, regItem);
        }
        regItem->setText(regText);
        regItem->setForeground(regCol);

        QTableWidgetItem *valItem = m_vdpRegTable->item(r, c + 1);
        if (!valItem) {
            valItem = new QTableWidgetItem();
            valItem->setFlags(Qt::ItemIsEnabled);
            m_vdpRegTable->setItem(r, c + 1, valItem);
        }
        valItem->setText(valText);
        valItem->setForeground(valCol);
    };

    // Stijlen
    QColor regCol = QColor("#A0A0A0");
    QColor valCol = Qt::white;
    // Gebruik de bestaande definitie voor de font
    QFont fixedFont("Roboto", 11);

    // VUL RIJEN 0 T/M 3: TMS VDP REGISTERS R0 T/M R7
    for (int r = 0; r < 4; ++r) {
        // Linker paar: R0 t/m R3 (kolommen 0 en 1)
        int regIndexL = r;
        uint8_t valL = tms.VR[regIndexL];

        setItem(r, 0,
                QString("R%1").arg(regIndexL),
                QString("$%1").arg(valL, 2, 16, QChar('0')).toUpper(),
                regCol, valCol);

        // Rechter paar: R4 t/m R7 (kolommen 2 en 3)
        int regIndexR = r + 4;
        uint8_t valR = tms.VR[regIndexR];

        setItem(r, 2,
                QString("R%1").arg(regIndexR),
                QString("$%1").arg(valR, 2, 16, QChar('0')).toUpper(),
                regCol, valCol);
    }

    // VUL RIJEN 4 EN 5: EXTRA STATUSWAARDEN

    // RIJ 4: SR (S0) en VRAM Mask (VRMP)
    // De VRAM Maskerwaarde is afgeleid van R1, gedefinieerd in tms9928a.h
    uint32_t vramMask = TMS9918_VRAMMask;
    QColor statusValCol = QColor("#FF8C00");

    // Kolom 0/1: S0 (SR)
    setItem(4, 0,
            "S0",
            QString("$%1").arg(tms.SR, 2, 16, QChar('0')).toUpper(),
            regCol, statusValCol);

    // Kolom 2/3: VRMP (VRAM Mask)
    setItem(4, 2,
            "VRMP",
            QString("$%1").arg(vramMask, 4, 16, QChar('0')).toUpper(),
            regCol, statusValCol);


    // RIJ 5: CurLine (SCAN) en ScanLines (LNTM)

    // Kolom 0/1: SCAN (CurLine)
    setItem(5, 0,
            "SCAN",
            QString("%1").arg(tms.CurLine, 3, 10, QChar('0')), // Weergegeven in decimaal
            regCol, statusValCol);

    // Kolom 2/3: LNTM (ScanLines)
    setItem(5, 2,
            "LNTM",
            QString("%1").arg(tms.ScanLines, 3, 10, QChar('0')), // Weergegeven in decimaal
            regCol, statusValCol);
}

uint8_t DebuggerWindow::readMemoryByte(uint32_t address)
{
    uint32_t addr = address;
    switch (m_currentMemSourceIndex) {
    case 0: return coleco_ReadByte(addr & 0xFFFF);
    case 1: return VDP_Memory[addr & 0x3FFF];
    case 2: return RAM_Memory[addr & 0x1FFFF];
    case 3: return RAM_Memory[addr & 0x7FFF];
    case 4: return SRAM_Memory[addr & 0x7FFF];
    case 5: return RAM_Memory[0xE000 + (addr & 0x07FF)];
    default: return 0xFF;
    }
}

void DebuggerWindow::updateFlags()
{
    if (!m_flagsTable) return;

    uint8_t f = Z80.af.b.l;

    /*
    S - Sign Flag (Teken-flag)
        Deze flag wordt gezet (1) als het resultaat van een berekening negatief is.
        Technisch gezien is dit een kopie van de meest significante bit (bit 7) van het resultaat.

    Z - Zero Flag (Nul-flag)
        Deze flag wordt gezet (1) als het resultaat van een berekening exact nul is.
        Dit is een van de meest gebruikte flags, bijvoorbeeld om te controleren of twee waarden gelijk zijn (door ze van elkaar af te trekken).

    H - Half Carry Flag (Halve Carry-flag)
        Deze flag wordt gebruikt voor BCD-berekeningen (Binary-Coded Decimal), wat een manier is om decimale getallen (0-9) op te slaan.
        De flag wordt gezet (1) als er een 'overdracht' (carry) was van bit 3 naar bit 4 tijdens een optelling, of een 'leen' (borrow) van bit 4 naar bit 3 bij een aftrekking.

    P/V - Parity/Overflow Flag (Pariteit/Overloop-flag)
        Deze flag heeft een dubbele functie:
            Parity (P): Na logische operaties (zoals AND, OR, XOR) geeft deze flag de pariteit van het resultaat aan. Hij wordt gezet (1) als het resultaat een even aantal '1'-bits bevat.
            Overflow (V): Na rekenkundige operaties (zoals optellen, aftrekken) geeft deze flag aan of er een overflow heeft plaatsgevonden. Dit gebeurt als het resultaat van een berekening met getallen met een teken (+/-) te groot of te klein is om in één byte te passen (bijv. 127 + 2).

    N - Add/Subtract Flag (Optel/Aftrek-flag)
        Deze flag wordt, net als de H-flag, voornamelijk gebruikt voor BCD-berekeningen.
        De flag wordt gezet (1) als de laatste operatie een aftrekking was.
        De flag wordt gereset (0) als de laatste operatie een optelling was.

    C - Carry Flag (Draag-flag)
        Deze flag wordt gezet (1) als een optelling resulteert in een getal dat groter is dan wat in 8 bits past (een 'carry' uit bit 7).
        Bij een aftrekking wordt hij gezet (1) als er 'geleend' moest worden (d.w.z. als je een groter getal van een kleiner getal aftrekt, zoals 5 - 10).
        Deze flag is essentieel voor het uitvoeren van berekeningen met getallen die groter zijn dan 8 bits (bijvoorbeeld 16-bits of 32-bits getallen).
    */

    const int displayMasks[] = { 0x80, 0x40, 0x10, 0x04, 0x02, 0x01 }; // S, Z, H, P/V, N, C
    QColor activeColor = Qt::green;
    QColor inactiveColor = QColor("#555555");

    for (int j = 0; j < 6; ++j)
    {
        bool active = (f & displayMasks[j]);

        QTableWidgetItem* valItem = m_flagsTable->item(0, j);
        if (!valItem) {
            valItem = new QTableWidgetItem();
            valItem->setFlags(Qt::ItemIsEnabled);
            valItem->setTextAlignment(Qt::AlignCenter);
            m_flagsTable->setItem(0, j, valItem);
        }

        valItem->setText(active ? "1" : "0");
        valItem->setForeground(active ? activeColor : inactiveColor);
    }
}

void DebuggerWindow::updateStack()
{
    if (!m_stackTable) return;

    uint16_t sp = Z80.sp.w.l;
    QColor spColor = Qt::cyan;
    QColor defaultColor = Qt::white;

    auto setItem = [&](int r, int c, const QString& text, QColor col) {
        QTableWidgetItem* item = m_stackTable->item(r, c);
        if (!item) {
            item = new QTableWidgetItem();
            item->setFlags(Qt::ItemIsEnabled);
            m_stackTable->setItem(r, c, item);
        }
        item->setText(text);
        item->setForeground(col);
    };

    if (m_stackTable->rowCount() != 7) {
        m_stackTable->setRowCount(7);
    }

    uint16_t addrs[14] = {
        sp, static_cast<uint16_t>(sp + 2), static_cast<uint16_t>(sp + 4),
        static_cast<uint16_t>(sp + 6), static_cast<uint16_t>(sp + 8),
        static_cast<uint16_t>(sp + 10), static_cast<uint16_t>(sp + 12),
        static_cast<uint16_t>(sp + 14), static_cast<uint16_t>(sp + 16),
        static_cast<uint16_t>(sp + 18), static_cast<uint16_t>(sp + 20),
        static_cast<uint16_t>(sp + 22), static_cast<uint16_t>(sp + 24),
        static_cast<uint16_t>(sp + 26)
    };
    uint16_t words[14];
    for(int i=0; i<14; ++i) {
        uint8_t lo = coleco_ReadByte(addrs[i]);
        uint8_t hi = coleco_ReadByte(addrs[i] + 1);
        words[i] = (hi << 8) | lo;
    }

    setItem(0, 0, QString("%1").arg(addrs[0], 4, 16, QChar('0')).toUpper(), spColor);
    setItem(0, 1, QString("%1").arg(words[0], 4, 16, QChar('0')).toUpper(), spColor);
    setItem(0, 2, QString("%1").arg(addrs[7], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(0, 3, QString("%1").arg(words[7], 4, 16, QChar('0')).toUpper(), defaultColor);

    setItem(1, 0, QString("%1").arg(addrs[1], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(1, 1, QString("%1").arg(words[1], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(1, 2, QString("%1").arg(addrs[8], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(1, 3, QString("%1").arg(words[8], 4, 16, QChar('0')).toUpper(), defaultColor);

    setItem(2, 0, QString("%1").arg(addrs[2], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(2, 1, QString("%1").arg(words[2], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(2, 2, QString("%1").arg(addrs[9], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(2, 3, QString("%1").arg(words[9], 4, 16, QChar('0')).toUpper(), defaultColor);

    setItem(3, 0, QString("%1").arg(addrs[3], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(3, 1, QString("%1").arg(words[3], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(3, 2, QString("%1").arg(addrs[10], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(3, 3, QString("%1").arg(words[10], 4, 16, QChar('0')).toUpper(), defaultColor);

    setItem(4, 0, QString("%1").arg(addrs[4], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(4, 1, QString("%1").arg(words[4], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(4, 2, QString("%1").arg(addrs[11], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(4, 3, QString("%1").arg(words[11], 4, 16, QChar('0')).toUpper(), defaultColor);

    setItem(5, 0, QString("%1").arg(addrs[5], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(5, 1, QString("%1").arg(words[5], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(5, 2, QString("%1").arg(addrs[12], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(5, 3, QString("%1").arg(words[12], 4, 16, QChar('0')).toUpper(), defaultColor);

    setItem(6, 0, QString("%1").arg(addrs[6], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(6, 1, QString("%1").arg(words[6], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(6, 2, QString("%1").arg(addrs[13], 4, 16, QChar('0')).toUpper(), defaultColor);
    setItem(6, 3, QString("%1").arg(words[13], 4, 16, QChar('0')).toUpper(), defaultColor);
}

void DebuggerWindow::nxtSStep()
{
    if (m_disasmView) {
        m_disasmView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }
    m_controller->sstepOnce();
    updateRegisters();
    updateDisassembly();
    updateMemoryDump();
    updateFlags();
    updateStack();
}

void DebuggerWindow::gotoAddress()
{
    uint16_t currentPC = Z80.pc.w.l;
    uint16_t addr = 0;

    if (m_disasmView) {
        m_disasmView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }

    // Pauzeer de emulator voor de adreswijziging
    if (m_controller) {
        m_controller->pauseEmulation();
    }

    if (!GotoAddressDialog::getAddress(this, currentPC, addr)) {
        return;
    }

    // --- FIX: Stel de PC direct en exact in op het gevraagde adres ---
    Z80.pc.w.l = addr; // PC = Gevraagde adres

    // VERWIJDERD: m_controller->sstepOnce();
    // We roepen GEEN uitvoeringsfunctie aan, zodat de instructie NIET wordt uitgevoerd.

    // Vernieuw de weergave om de nieuwe PC-locatie te tonen
    //m_controller->sstepOnce();
    updateRegisters();
    updateDisassembly();
    updateMemoryDump();
    updateFlags();
    updateStack();
}

void DebuggerWindow::fillDisassemblyAround(uint16_t addr)
{
    if (!m_disasmView) return;
    m_disasmView->setRowCount(0);
    const int MAX_LINES = 64;
    const int CONTEXT_BACK = 0x20;
    uint16_t startAddr = (addr > CONTEXT_BACK) ? (addr - CONTEXT_BACK) : 0x0000;
    uint16_t cur = startAddr;
    int pcRow = -1;
    for (int line = 0; line < MAX_LINES; ++line) {
        int oplen = 0;
        QString instr = disasmOneAt(cur, oplen);
        if (oplen <= 0 || oplen > 4) oplen = 1;
        QString bytesStr;
        for (int b = 0; b < oplen; ++b) {
            bytesStr += QString("%1 ").arg(coleco_ReadByte(cur + b), 2, 16, QChar('0')).toUpper();
        }
        bytesStr = bytesStr.trimmed();
        int row = m_disasmView->rowCount();
        m_disasmView->insertRow(row);
        m_disasmView->setItem(row, 0, new QTableWidgetItem(QString("%1").arg(cur, 4, 16, QChar('0')).toUpper()));
        m_disasmView->setItem(row, 1, new QTableWidgetItem(bytesStr));
        m_disasmView->setItem(row, 2, new QTableWidgetItem(instr));
        if (cur == addr)
            pcRow = row;
        cur += oplen;
    }
    if (pcRow >= 0) {
        m_disasmView->scrollToItem(m_disasmView->item(pcRow, 0),
                                   QAbstractItemView::PositionAtCenter);
    }
}

void DebuggerWindow::onMemSourceChanged(int index)
{
    m_currentMemSourceIndex = index;
    m_memDumpStartAddr = 0;
    m_memAddrEdit->setText("0000");
    updateMemoryDump();
}
void DebuggerWindow::onMemAddrChanged()
{
    bool ok;
    uint32_t newAddr = m_memAddrEdit->text().toUInt(&ok, 16);
    if (ok) {
        m_memDumpStartAddr = newAddr & 0xFFFFFFF0;
    }
    m_memAddrEdit->setText(QString("%1").arg(m_memDumpStartAddr, 4, 16, QChar('0')).toUpper());
    updateMemoryDump();
}
void DebuggerWindow::onMemAddrPrev()
{
    const int bytesPerLine = 16;
    if (m_memDumpStartAddr >= bytesPerLine) {
        m_memDumpStartAddr -= bytesPerLine;
    } else {
        m_memDumpStartAddr = 0;
    }
    m_memAddrEdit->setText(QString("%1").arg(m_memDumpStartAddr, 4, 16, QChar('0')).toUpper());
    updateMemoryDump();
}
void DebuggerWindow::onMemAddrNext()
{
    const int bytesPerLine = 16;
    uint32_t maxAddr = 0xFFFF;
    if (m_currentMemSourceIndex == 2) {
        maxAddr = 0x1FFFF; // 128K
    }
    if (m_memDumpStartAddr < (maxAddr - bytesPerLine)) {
        m_memDumpStartAddr += bytesPerLine;
    } else {
        m_memDumpStartAddr = maxAddr & 0xFFFFFFF0;
    }
    m_memAddrEdit->setText(QString("%1").arg(m_memDumpStartAddr, 4, 16, QChar('0')).toUpper());
    updateMemoryDump();
}
void DebuggerWindow::onMemAddrHome()
{
    m_memDumpStartAddr = 0;
    m_memAddrEdit->setText("0000");
    updateMemoryDump();
}

void DebuggerWindow::syncBreakpointsToCore()
{
    // Neem alle GUI-breakpoints over
    QList<CoreBreakpoint> guiBreakpoints = getBreakpointDefinitions();

    // Master bepaalt of ze effectief "enabled" zijn
    for (CoreBreakpoint &bp : guiBreakpoints) {
        bp.enabled = m_breakpointsEnabled && bp.enabled;

        const QString t = bp.definition_text.trimmed().toUpper();

        // Let op: eerst de meer specifieke tokens
        if      (t.startsWith("WR "))  bp.type = BreakpointType::BP_WRITE;
        else if (t.startsWith("RD "))  bp.type = BreakpointType::BP_READ;
        else if (t.startsWith("OUT"))  bp.type = BreakpointType::BP_IO_OUT;
        else if (t.startsWith("IN"))   bp.type = BreakpointType::BP_IO_IN;
        // (optioneel: EXEC/MEM/CLK/etc als jij die ook via text bepaalt)
    }

    qDebug() << "[DebuggerWindow] syncBreakpointsToCore: masterEnabled="
             << m_breakpointsEnabled
             << "defs=" << guiBreakpoints.size();

    DebugBridge::instance().syncBreakpoints(guiBreakpoints);
}

void DebuggerWindow::updateBreakpointList()
{
    QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    m_bpList->blockSignals(true);
    m_bpList->clear();

    for (const DebuggerBreakpoint& bp : m_breakpoints) {
        QListWidgetItem* item = new QListWidgetItem(m_bpList);

        QWidget* itemWidget = new QWidget();
        QHBoxLayout* layout = new QHBoxLayout(itemWidget);
        layout->setContentsMargins(2, 0, 4, 0);
        layout->setSpacing(4);

        QLabel* label = new QLabel(bp.definition_text);
        label->setFont(fixedFont);

        bool isEffectivelyEnabled = m_breakpointsEnabled && bp.enabled;

        label->setStyleSheet(QString("color: %1").arg(isEffectivelyEnabled ? "white" : "#555555"));

        QCheckBox* checkBox = new QCheckBox();
        checkBox->setChecked(bp.enabled);

        checkBox->setProperty("bpDefinition", bp.definition_text);

        connect(checkBox, &QCheckBox::toggled, this, &DebuggerWindow::onBpCheckboxToggled);

        layout->addWidget(label);
        layout->addStretch(1);
        layout->addWidget(checkBox);

        item->setSizeHint(itemWidget->sizeHint());
        m_bpList->addItem(item);
        m_bpList->setItemWidget(item, itemWidget);
    }

    m_bpList->blockSignals(false);
}

void DebuggerWindow::onBpAdd()
{
    qDebug() << "[DebuggerWindow] onBpAdd() called.";
    QString bpString = SetBreakpointDialog::getBreakpointString(this);
    qDebug() << "[DebuggerWindow] getBreakpointString returned:" << bpString;

    if (bpString.isEmpty()) {
        qDebug() << "[DebuggerWindow] String is empty. Aborting.";
        return;
    }

    auto it = std::find_if(m_breakpoints.begin(), m_breakpoints.end(),
                           [&bpString](const DebuggerBreakpoint& bp) {
                               return bp.definition_text == bpString;
                           });

    if (it == m_breakpoints.end()) {
        qDebug() << "[DebuggerWindow] Adding new breakpoint:" << bpString;

        DebuggerBreakpoint newBp;
        newBp.enabled = true;
        newBp.definition_text = bpString;
        newBp.type = breakpointTypeFromString(bpString);

        m_breakpoints.append(newBp);
        updateBreakpointList();
        syncBreakpointsToCore();
    } else {
        qDebug() << "[DebuggerWindow] Breakpoint already exists, not adding duplicate.";
    }
}

void DebuggerWindow::onBpDel()
{
    int row = m_bpList->currentRow();
    if (row < 0 || row >= m_breakpoints.size()) return;

    m_breakpoints.removeAt(row);
    updateBreakpointList();

    if (row < m_bpList->count()) {
        m_bpList->setCurrentRow(row);
    } else if (m_bpList->count() > 0) {
        m_bpList->setCurrentRow(row - 1);
    }

    syncBreakpointsToCore();
}

void DebuggerWindow::onBpEdit()
{
    int row = m_bpList->currentRow();
    if (row < 0 || row >= m_breakpoints.size()) return;

    QString currentAddr = m_breakpoints.at(row).definition_text;
    bool originalState = m_breakpoints.at(row).enabled;

    QString bpString = SetBreakpointDialog::getBreakpointString(this, currentAddr);
    if (bpString.isEmpty() || bpString == currentAddr) return;

    bool needsSync = false;

    auto it = std::find_if(m_breakpoints.begin(), m_breakpoints.end(),
                           [&bpString](const DebuggerBreakpoint& bp) {
                               return bp.definition_text == bpString;
                           });

    if (it == m_breakpoints.end()) {
        m_breakpoints[row].definition_text = bpString;
        m_breakpoints[row].type = breakpointTypeFromString(bpString);
        m_breakpoints[row].enabled = originalState;

        std::sort(m_breakpoints.begin(), m_breakpoints.end());
        updateBreakpointList();

        auto newIt = std::find_if(m_breakpoints.begin(), m_breakpoints.end(),
                                  [&bpString](const DebuggerBreakpoint& bp) {
                                      return bp.definition_text == bpString;
                                  });
        if (newIt != m_breakpoints.end()) {
            m_bpList->setCurrentRow(std::distance(m_breakpoints.begin(), newIt));
        }
        needsSync = true;
    } else if (bpString != currentAddr) {
        m_breakpoints.removeAt(row);
        updateBreakpointList();
        needsSync = true;
    }

    if (needsSync) {
        syncBreakpointsToCore();
    }
}

void DebuggerWindow::onToggleBreakpoints()
{
    bool checked = m_bpEnableButton->isChecked();

    // Button checked = OFF (groen), unchecked = ON (rood)
    m_breakpointsEnabled = !checked;

    if (m_breakpointsEnabled) {
        // AAN → rood
        m_bpEnableButton->setText(tr("Break ON"));
        m_bpEnableButton->setStyleSheet(
            "background-color: #888800; color: white; font-weight: bold; padding: 2px 5px;"
            );

        // Als je bij inschakelen meteen wil pauzeren:
        // emit requestBreakCPU();
    } else {
        // UIT → groen
        m_bpEnableButton->setText(tr("Break OFF"));
        m_bpEnableButton->setStyleSheet(
            "background-color: #009900; color: white; font-weight: bold; padding: 2px 5px;"
            );
    }

    updateBreakpointList();
    syncBreakpointsToCore();
}

void DebuggerWindow::onBpCheckboxToggled(bool checked)
{
    QCheckBox* checkBox = qobject_cast<QCheckBox*>(sender());
    if (!checkBox) return;

    QString bpDef = checkBox->property("bpDefinition").toString();
    if (bpDef.isEmpty()) return;

    auto it = std::find_if(m_breakpoints.begin(), m_breakpoints.end(),
                           [&bpDef](const DebuggerBreakpoint& bp) {
                               return bp.definition_text == bpDef;
                           });

    if (it != m_breakpoints.end()) {
        it->enabled = checked;
    } else {
        qWarning() << "onBpCheckboxToggled: Could not find breakpoint:" << bpDef;
        return;
    }

    QWidget* itemWidget = checkBox->parentWidget();
    if (itemWidget) {
        QLabel* label = itemWidget->findChild<QLabel*>();
        if (label) {
            bool isEffectivelyEnabled = m_breakpointsEnabled && checked;
            label->setStyleSheet(QString("color: %1").arg(isEffectivelyEnabled ? "white" : "#555555"));
        }
    }

    syncBreakpointsToCore();
}

void DebuggerWindow::onBpLoad()
{
    emit requestBpLoad();
}

void DebuggerWindow::onBpSave()
{
    emit requestBpSave();
}

void DebuggerWindow::closeEvent(QCloseEvent *event)
{
    if (m_controller) {
        DebugBridge::instance().clearLastBreakPC();
        m_controller->resumeEmulation();
    }
    emit requestRunCPU();
    QMainWindow::closeEvent(event);
}

QList<DebuggerBreakpoint> DebuggerWindow::getBreakpointDefinitions() const
{
    return m_breakpoints;
}

void DebuggerWindow::setBreakpointDefinitions(const QList<DebuggerBreakpoint>& list)
{
    m_breakpoints = list;
    std::sort(m_breakpoints.begin(), m_breakpoints.end());
    updateBreakpointList();
    syncBreakpointsToCore();
    QMessageBox::information(this, tr("Success"), tr("Breakpoints loaded."));
}

void DebuggerWindow::onDisasmContextMenu(const QPoint &pos)
{
    if (!m_disasmView) return;

    QTableWidgetItem *item = m_disasmView->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QAction *copyAct = menu.addAction(tr("Copy selection to clipboard"));
    QAction *chosen = menu.exec(m_disasmView->viewport()->mapToGlobal(pos));
    if (chosen != copyAct)
        return;

    QList<int> rowList;
    for (QTableWidgetItem *sel : m_disasmView->selectedItems()) {
        int r = sel->row();
        if (!rowList.contains(r))
            rowList.append(r);
    }
    if (rowList.isEmpty())
        return;

    std::sort(rowList.begin(), rowList.end());

    QStringList lines;
    for (int row : rowList) {
        QString label, addr, bytes, instr, target;

        // Kolom 0: Label
        if (auto it = m_disasmView->item(row, 0)) label = it->text();
        // Kolom 1: Adres
        if (auto it = m_disasmView->item(row, 1)) addr  = it->text();
        // Kolom 2: Code (Bytes)
        if (auto it = m_disasmView->item(row, 2)) bytes = it->text();
        // Kolom 3: Mnemonic (Instructie)
        if (auto it = m_disasmView->item(row, 3)) instr = it->text();
        // Kolom 4: Target Label (NIEUW)
        if (auto it = m_disasmView->item(row, 4)) target = it->text();

        // Mooie, simpele tekstvorm met alle 5 kolommen.
        // Gebruik vaste breedtes voor alignatie:
        // Label (10), Adres (4), Bytes (15), Instructie (15), Target
        lines << QString("%1  %2  %3  %4  %5")
                     .arg(label, -10)
                     .arg(addr, -4)
                     .arg(bytes, -15)
                     .arg(instr, -15)
                     .arg(target);
    }

    QClipboard *cb = QGuiApplication::clipboard();
    cb->setText(lines.join('\n'));
}

void DebuggerWindow::onRegistersContextMenu(const QPoint &pos)
{
    if (!m_regTable)
        return;

    // Enkel menu tonen als er op een cel/rij geklikt is
    QTableWidgetItem *clickedItem = m_regTable->itemAt(pos);
    if (!clickedItem)
        return;

    QMenu menu(this);
    QAction *copyAct = menu.addAction(tr("Copy registers to clipboard"));
    QAction *chosen = menu.exec(m_regTable->viewport()->mapToGlobal(pos));
    if (chosen != copyAct)
        return;

    // Alle unieke geselecteerde rijen verzamelen
    QList<int> rows;
    for (QTableWidgetItem *selItem : m_regTable->selectedItems()) {
        int r = selItem->row();
        if (!rows.contains(r))
            rows.append(r);
    }

    if (rows.isEmpty()) {
        rows.append(clickedItem->row());
    }

    std::sort(rows.begin(), rows.end());

    QStringList lines;
    for (int r : rows) {
        QString regName;
        QString regValue;

        if (auto it = m_regTable->item(r, 0))
            regName = it->text();
        if (auto it = m_regTable->item(r, 1))
            regValue = it->text();

        if (!regName.isEmpty())
            lines << QString("%1 = %2").arg(regName, regValue);
    }

    if (lines.isEmpty())
        return;

    QClipboard *cb = QGuiApplication::clipboard();
    cb->setText(lines.join('\n'));
}

void DebuggerWindow::onEmuPausedChanged(bool paused)
{
    if (!paused)
        m_forcedPausedUi = false;

    if (btnRun)
        btnRun->setEnabled(paused);

    if (btnStep)
        btnStep->setEnabled(paused);

   // if (btnBreak)
   //     btnBreak->setEnabled(!paused);

    if (btnGotoAddr)
        btnGotoAddr->setEnabled(paused);

    if (btnSinglestep)
        btnSinglestep->setEnabled(paused);

    if (btnStepOver)
        btnStepOver->setEnabled(paused);

    if (paused) {
        updateAllViews();
    }


// --- Program-tab LED update ---
// paused = breakpoint/step mode => blink (grijs/geel)
// !paused = running => groen
if (paused) {
    ensurePauseBlinkTimer(this)->start();
    setProgramLedIcon(this, QIcon(":/images/images/LED_YELLOW.png"));
} else {
    stopPauseBlink(this);
    setProgramLedIcon(this, QIcon(":/images/images/LED_GREEN.png"));
}
}

void DebuggerWindow::writeMemoryByte(uint32_t address, uint8_t data)
{
    uint32_t addr = address & 0xFFFF; // Zorg voor 16-bits adresbereik

    switch (m_currentMemSourceIndex) {
    case 0: // Z80 Mapped Memory: FORCE WRITE
        if (addr < 0x2000) {
            // 0x0000 - 0x1FFF (BIOS) - Werkt nu
            BIOS_Memory[addr] = data;
        } else if (addr < 0x6000) {
            // 0x2000 - 0x5FFF (Cartridge ROM)
            // Gebruik hier de functie die u heeft laten werken voor 0x1700
            // Als Cartridge ROM een andere array is, moet u die hier patchen.
            // Voor nu laten we coleco_writebyte staan als fallback voor bankswitching
            coleco_writebyte(addr, data);
        } else if (addr < 0x8000) {
            // 0x6000 - 0x7FFF (Standaard RAM) - Werkt al
            RAM_Memory[addr - 0x6000] = data; // Directe patch, geef de offset aan
        } else {
            // *** OPLOSSING VOOR $C040 (0x8000 - 0xFFFF) ***
            // Dit is het uitgebreide RAM-bereik (ADAM RAM).
            // We patchen hier de fysieke RAM array direct.
            // Dit negeert I/O registers en bankswitching.
            // De index is hier waarschijnlijk de Z80-adres zelf:
            RAM_Memory[addr] = data;
        }
        break;

    case 1: VDP_Memory[addr & 0x3FFF] = data; break;
    case 2: RAM_Memory[addr & 0x1FFFF] = data; break;
    case 3: RAM_Memory[addr & 0x7FFF] = data; break;
    case 4: SRAM_Memory[addr & 0x7FFF] = data; break;
    case 5: RAM_Memory[0xE000 + (addr & 0x07FF)] = data; break;
    default: break;
    }
}

void DebuggerWindow::onMemTableDoubleClicked(int row, int /*column*/)
{
    if (!m_memTable || row < 0 || row >= m_memTable->rowCount()) {
        return;
    }

    // De Memory Dump is altijd zichtbaar, we gebruiken de m_currentMemSourceIndex.

    const int bytesPerLine = 16;
    uint32_t baseAddr = m_memDumpStartAddr + (row * bytesPerLine);

    uint8_t data16[bytesPerLine];
    for (int i = 0; i < bytesPerLine; ++i) {
        data16[i] = readMemoryByte(baseAddr + i);
    }

    bool dataChanged = MemoryEditDialog::editMemoryLine(this, baseAddr, data16);

    if (dataChanged) {
        for (int i = 0; i < bytesPerLine; ++i) {

            writeMemoryByte(baseAddr + i, data16[i]);
        }
        QTimer::singleShot(10, this, &DebuggerWindow::updateAllViews);
    }
}

QList<DebuggerSymbol> DebuggerWindow::getSymbolDefinitions() const
{
    return m_symbols;
}

void DebuggerWindow::setSymbolDefinitions(const QList<DebuggerSymbol>& list)
{
    m_symbols = list;
    std::sort(m_symbols.begin(), m_symbols.end());
    updateSymbolsList();
    updateDisassembly(); // BELANGRIJK: Vernieuw de disassembler
}

void DebuggerWindow::updateSymbolsList()
{
    QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    m_symList->blockSignals(true);
    m_symList->clear();

    for (const DebuggerSymbol& sym : m_symbols) {
        QListWidgetItem* item = new QListWidgetItem(m_symList);

        QWidget* itemWidget = new QWidget();
        QHBoxLayout* layout = new QHBoxLayout(itemWidget);
        layout->setContentsMargins(2, 0, 4, 0);
        layout->setSpacing(8); // Meer ruimte

        // Symbool Definitie: Type, Adres, Label
        QString display = QString("%1: $%2 -> %3")
                              .arg(sym.type, 4, QChar(' '))
                              .arg(sym.address, 4, 16, QChar('0')).toUpper()
                              .arg(sym.label);

        QLabel* label = new QLabel(display);
        label->setFont(fixedFont);

        // Geef een kleur aan symbolen (b.v. Geel)
        label->setStyleSheet("color: yellow");

        layout->addWidget(label);
        layout->addStretch(1); // Geen checkbox

        item->setSizeHint(itemWidget->sizeHint());
        m_symList->addItem(item);
        m_symList->setItemWidget(item, itemWidget);
    }

    m_symList->blockSignals(false);
}

// Functie om de ruwe string te parsen naar een DebuggerSymbol
static DebuggerSymbol parseSymbolDefinition(const QString& definition)
{
    DebuggerSymbol sym;
    sym.definition_text = definition;
    sym.address = 0; // Standaard
    sym.type = "EXEC"; // Standaard

    QStringList parts = definition.split(':');
    if (parts.size() >= 3) {
        // Formaat: TYPE:ADRES:LABEL
        sym.type = parts[0].trimmed().toUpper();

        bool ok = false;
        // ADRES (moet een hexadecimaal string zijn)
        sym.address = parts[1].trimmed().toUShort(&ok, 16);
        if (!ok) sym.address = 0;

        // LABEL
        sym.label = parts[2].trimmed();

    } else if (parts.size() >= 2) {
        // Formaat: ADRES:LABEL (Type = EXEC)
        bool ok = false;
        sym.address = parts[0].trimmed().toUShort(&ok, 16);
        if (!ok) sym.address = 0;
        sym.label = parts[1].trimmed();
        sym.type = "EXEC";
    }

    // Zorg ervoor dat het label altijd wordt opgeslagen als de volledige definitie.
    // Dit zorgt ervoor dat bewerken en opslaan werkt.
    if (sym.label.isEmpty() && !sym.definition_text.isEmpty()) {
        sym.label = sym.definition_text;
    }

    return sym;
}

void DebuggerWindow::onSymAdd()
{
    // Gebruik de nieuwe Symbol Dialoog om de definitiestring op te halen
    QString symString = SetSymbolDialog::getSymbolString(this, tr("CALL:C000:GameStart"));

    if (symString.isEmpty()) {
        qDebug() << "[DebuggerWindow] Symboolstring is leeg. Afgebroken.";
        return;
    }

    // 1. Controleer op duplicaten op basis van de volledige definitie string
    auto it = std::find_if(m_symbols.begin(), m_symbols.end(),
                           [&symString](const DebuggerSymbol& sym) {
                               return sym.definition_text == symString;
                           });

    if (it == m_symbols.end()) {
        // 2. Parsen van de definitie om de structuur te vullen
        QStringList parts = symString.split(':');

        DebuggerSymbol newSym;
        newSym.definition_text = symString;

        // Parsing is nu eenvoudiger en gebaseerd op de gevalideerde string
        if (parts.size() >= 3) {
            newSym.type = parts[0];
            bool ok = false;
            // Adres is al 4-cijferig en gevalideerd door formatSymbolString
            newSym.address = parts[1].toUShort(&ok, 16);
            // Label is de rest van de string
            newSym.label = parts.mid(2).join(':');
        } else {
            qWarning() << "[DebuggerWindow] Fout bij parsen van gevalideerd symbool:" << symString;
            return; // Mocht dit toch gebeuren, stop
        }

        m_symbols.append(newSym);
        std::sort(m_symbols.begin(), m_symbols.end());
        updateSymbolsList();
        qDebug() << "[DebuggerWindow] Nieuw symbool toegevoegd:" << symString;

    } else {
        qDebug() << "[DebuggerWindow] Symbool bestaat al, geen duplicaat toegevoegd.";
    }
}

void DebuggerWindow::onSymDel()
{
    int row = m_symList->currentRow();
    if (row < 0 || row >= m_symbols.size()) return;

    m_symbols.removeAt(row);
    updateSymbolsList();

    if (row < m_symList->count()) {
        m_symList->setCurrentRow(row);
    } else if (m_symList->count() > 0) {
        m_symList->setCurrentRow(row - 1);
    }
}

void DebuggerWindow::onSymEdit()
{
    int row = m_symList->currentRow();
    if (row < 0 || row >= m_symbols.size()) return;

    QString currentDef = m_symbols.at(row).definition_text;

    // Gebruik de nieuwe Symbol Dialoog met de huidige waarde als startpunt
    QString newSymString = SetSymbolDialog::getSymbolString(this, currentDef);

    if (newSymString.isEmpty() || newSymString == currentDef) return;

    // 1. Controleer of de nieuwe definitie al bestaat
    auto it = std::find_if(m_symbols.begin(), m_symbols.end(),
                           [&newSymString](const DebuggerSymbol& sym) {
                               return sym.definition_text == newSymString;
                           });

    // 2. Parsen van de nieuwe definitie
    QStringList parts = newSymString.split(':');
    DebuggerSymbol newSym;

    // Parsing is nu eenvoudiger en gebaseerd op de gevalideerde string
    if (parts.size() >= 3) {
        newSym.definition_text = newSymString;
        newSym.type = parts[0];
        bool ok = false;
        newSym.address = parts[1].toUShort(&ok, 16);
        newSym.label = parts.mid(2).join(':');
    } else {
        qWarning() << "[DebuggerWindow] Fout bij parsen na bewerken:" << newSymString;
        return;
    }

    if (it == m_symbols.end()) {
        // De nieuwe definitie is uniek: vervang de oude en sorteer
        m_symbols[row] = newSym;
        std::sort(m_symbols.begin(), m_symbols.end());
        updateSymbolsList();

        // Selecteer de nieuwe locatie in de lijst
        auto newIt = std::find_if(m_symbols.begin(), m_symbols.end(),
                                  [&newSymString](const DebuggerSymbol& sym) {
                                      return sym.definition_text == newSymString;
                                  });
        if (newIt != m_symbols.end()) {
            m_symList->setCurrentRow(std::distance(m_symbols.begin(), newIt));
        }
    } else if (newSymString != currentDef) {
        // Duplicaat gevonden, maar het is niet de oude (dus een andere index). Verwijder de oude.
        m_symbols.removeAt(row);
        updateSymbolsList();
    }
}

void DebuggerWindow::onSymLoad()
{
    // Emitteren naar MainWindow om het padbeheer af te handelen
    emit requestSymLoad();
}

void DebuggerWindow::onSymSave()
{
    // Emitteren naar MainWindow om het padbeheer af te handelen
    emit requestSymSave();
}

void DebuggerWindow::onToggleSymbols()
{
    if (!m_symEnableButton) return;

    bool checked = m_symEnableButton->isChecked();

    // Button checked = OFF (groen), unchecked = ON (rood)
    m_symbolsEnabled = !checked;

    if (m_symbolsEnabled) {
        // AAN → rood
        m_symEnableButton->setText(tr("Sym ON"));
        m_symEnableButton->setStyleSheet(
            "background-color: #888800; color: white; font-weight: bold; padding: 2px 5px;"
            );
    } else {
        // UIT → groen
        m_symEnableButton->setText(tr("Sym OFF"));
        m_symEnableButton->setStyleSheet(
            "background-color: #009900; color: white; font-weight: bold; padding: 2px 5px;"
            );
    }

    // Aangezien symbolen de disassembler en mogelijk de memory dump beïnvloeden,
    // moeten we deze views updaten.
    updateSymbolsList();
    updateDisassembly();
    // updateMemoryDump(); // Optioneel, afhankelijk van hoe symbolen in dump getoond worden
}

QString DebuggerWindow::getSymbolLabelForAddress(uint16_t address) const
{
    if (!m_symbolsEnabled) return QString();

    for (const DebuggerSymbol& sym : m_symbols) {
        if (sym.address == address) {
            return sym.label;
        }
    }
    return QString();
}

void DebuggerWindow::onStepOver()
{
    if (!m_controller) return;

    if (btnStepOver) {
        btnStepOver->setEnabled(false);
    }

    uint16_t pc = Z80.pc.w.l;
    uint8_t opcode = coleco_ReadByte(pc);
    //uint16_t original_pc = Z80.pc.w.l;

    int nextInstructionLength = 1; // Standaard voor alle 1-byte instructies

    // --- 1. INSTRUCTIE-LENGTE BEREKENING ---

    // 4-byte Instructies (FD prefix: LD IY,nnnn)
    if (opcode == 0xFD) {
        uint8_t nextOpcode = coleco_ReadByte(pc + 1);
        if (nextOpcode == 0x21 || nextOpcode == 0x01 || nextOpcode == 0x11 || nextOpcode == 0x31) {
            nextInstructionLength = 4;
        }
    }

    // Standaard 3-byte Instructies: LD rr, nn (rr = BC, DE, HL, SP)
    else if (opcode == 0x01 || opcode == 0x11 || opcode == 0x21 || opcode == 0x31) {
        nextInstructionLength = 3;
    }
    // 2-byte Instructies: JR's, DJNZ, etc.
    else if ((opcode & 0x07) == 0x06 || (opcode & 0x07) == 0x0E ||
             opcode == 0x10 || opcode == 0x20 || opcode == 0x28 || opcode == 0x30 || opcode == 0x38) {
        nextInstructionLength = 2;
    }

    // 3-byte Instructies: ALLE JUMP/CALL instructies en 16-bit adres instructies.
    else if (opcode == 0xC2 || opcode == 0xC4 || opcode == 0xCA || opcode == 0xCC ||
             opcode == 0xD2 || opcode == 0xD4 || opcode == 0xDA || opcode == 0xDC ||
             opcode == 0xE2 || opcode == 0xE4 || opcode == 0xEA || opcode == 0xEC ||
             opcode == 0xF2 || opcode == 0xF4 || opcode == opcode == 0xFA || opcode == 0xFC ||
             opcode == 0xC3 || opcode == 0xCD ||
             opcode == 0x2A || opcode == 0x3A || opcode == 0x22 || opcode == 0x32)
    {
        nextInstructionLength = 3;
    }

    // --- 2. SPRONG UITVOEREN ---
    uint16_t targetPC = pc + nextInstructionLength;

    // ** DEFINITIEVE VISUELE STABILISATIE FIX **
    // Voeg 3 extra bytes toe als de instructie 1 byte lang is (zoals CPL, LD D,B).
    // Dit dwingt de GUI om de weergave te centreren.
    //if (nextInstructionLength == 1) {
    //    targetPC += 3;
    //}

    // NIEUW LOGGING: Wat is de verwachte sprong?
    //qDebug() << "[STEPOVER] Start PC:" << QString("%1").arg(original_pc, 4, 16).toUpper()
    //         << "Length:" << nextInstructionLength
    //         << "-> Target PC:" << QString("%1").arg(targetPC, 4, 16).toUpper();

    Z80.pc.w.l = targetPC;

    updateAllViews();

    if (btnStepOver) {
        btnStepOver->setEnabled(true);
    }

    if (m_disasmView) {
        m_disasmView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }
}

void DebuggerWindow::onCopyMemoryRange()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Copy memory range to clipboard"));
    dlg.setModal(true);
    dlg.setFixedSize(320, 160);

    QVBoxLayout *vl = new QVBoxLayout(&dlg);

    QGridLayout *gl = new QGridLayout();
    QLabel *lblStart = new QLabel(tr("Start (hex):"), &dlg);
    QLabel *lblEnd   = new QLabel(tr("End (hex):"), &dlg);

    QLineEdit *edStart = new QLineEdit(&dlg);
    QLineEdit *edEnd   = new QLineEdit(&dlg);

    edStart->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edEnd->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto *hexVal = new QRegularExpressionValidator(
        QRegularExpression("[0-9a-fA-F]{1,4}$"), &dlg);

    edStart->setValidator(hexVal);
    edEnd->setValidator(hexVal);

    edStart->setMaxLength(4);
    edEnd->setMaxLength(4);

    edStart->setText(QString("%1").arg(m_memDumpStartAddr & 0xFFFF, 4, 16, QChar('0')).toUpper());
    edEnd->setText(QString("%1").arg((m_memDumpStartAddr + 0x1FF) & 0xFFFF, 4, 16, QChar('0')).toUpper());

    gl->addWidget(lblStart, 0, 0);
    gl->addWidget(edStart,  0, 1);
    gl->addWidget(lblEnd,   1, 0);
    gl->addWidget(edEnd,    1, 1);

    vl->addLayout(gl);

    // OK / Cancel (gebruik jouw bestaande PNG resource namen)
    QHBoxLayout *hl = new QHBoxLayout();
    hl->addStretch();

    QPixmap okPix(":/images/images/OK.png");
    QPixmap cancelPix(":/images/images/CANCEL.png");

    QPushButton *btnOk     = new QPushButton(&dlg);
    QPushButton *btnCancel = new QPushButton(&dlg);

    // Icon zetten
    btnOk->setIcon(QIcon(okPix));
    btnCancel->setIcon(QIcon(cancelPix));

    // Echte PNG grootte gebruiken
    if (!okPix.isNull())
    {
        btnOk->setIconSize(okPix.size());
        btnOk->setFixedSize(okPix.size());
    }

    if (!cancelPix.isNull())
    {
        btnCancel->setIconSize(cancelPix.size());
        btnCancel->setFixedSize(cancelPix.size());
    }

    // Zelfde stijl als je andere image buttons
    btnOk->setFlat(true);
    btnCancel->setFlat(true);

    btnOk->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    btnCancel->setStyleSheet(btnOk->styleSheet());

    btnOk->setCursor(Qt::PointingHandCursor);
    btnCancel->setCursor(Qt::PointingHandCursor);

    hl->addWidget(btnOk);
    hl->addWidget(btnCancel);

    vl->addLayout(hl);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    connect(btnOk, &QPushButton::clicked, &dlg, [&]() {

        bool ok1=false, ok2=false;
        uint32_t start = edStart->text().toUInt(&ok1, 16);
        uint32_t end   = edEnd->text().toUInt(&ok2, 16);

        if (!ok1 || !ok2)
            return;

        // Forceer 16-bit bereik
        if (start > 0xFFFF || end > 0xFFFF)
            return;

        if (end < start)
            std::swap(start, end);

        QString out;
        const int bytesPerLine = 16;

        for (uint32_t a = start; a <= end; a += bytesPerLine)
        {
            out += QString("%1: ").arg(a & 0xFFFFF, 5, 16, QChar('0')).toUpper();

            for (int i=0; i<bytesPerLine; ++i)
            {
                uint32_t cur = a + i;
                if (cur > end) break;

                uint8_t b = readMemoryByte(cur);
                out += QString("%1 ").arg(b, 2, 16, QChar('0')).toUpper();
            }

            out += "\n";
        }

        QGuiApplication::clipboard()->setText(out);
        dlg.accept();
    });

    dlg.exec();
}
