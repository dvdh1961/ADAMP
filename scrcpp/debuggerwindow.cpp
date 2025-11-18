#include "debuggerwindow.h"
#include "colecocontroller.h"
#include "disasm_bridge.h"
#include "coleco.h"
#include "z80.h"
#include "gotoaddressdialog.h"

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
#include <QTabWidget> // <-- Toegevoegd
#include "setbreakpointdialog.h"
#include <algorithm>


DebuggerWindow::DebuggerWindow(QWidget *parent)
    : QMainWindow(parent),
    m_controller(nullptr)
{
    setWindowTitle("ADAM+ Debugger");
    resize(770, 700);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    // Root vertical layout (alles onder elkaar)
    QVBoxLayout *rootLayout = new QVBoxLayout(central);
    QFont tableFont1("Roboto", 10);
    QFont tableFont2("Roboto", 11);
    QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    // --- Helper-lambda voor de *onderste* knoppenbalk ---
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
        button->setFixedSize(pixmap.size()); // Dit maakt ze klein
        button->setText("");
        button->setFlat(true);
        button->setStyleSheet(buttonStyle);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };


    //
    // ======= TOP ROW: DISASSEMBLY (links) | REGISTERS (midden) | RECHTER KOLOM (rechts) =======
    //
    QHBoxLayout *topRow = new QHBoxLayout();

    // --- DISASSEMBLY side (links) ---
    {
        QVBoxLayout *disLayout = new QVBoxLayout();
        QLabel *lblDis = new QLabel("<b>Disassembly (around PC)</b>", this);
        disLayout->addWidget(lblDis);
        m_disasmView = new QTableWidget(0, 3, this);
        m_disasmView->setHorizontalHeaderLabels(QStringList() << "Addr" << "Bytes" << "Instruction");
        m_disasmView->verticalHeader()->hide();
        m_disasmView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_disasmView->setFont(tableFont1);
        m_disasmView->setMinimumHeight(442);
        m_disasmView->setMinimumWidth(260);
        m_disasmView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_disasmView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_disasmView->setColumnWidth(0, 60);
        m_disasmView->setColumnWidth(1, 80);
        m_disasmView->setColumnWidth(2, 100);
        disLayout->addWidget(m_disasmView, 1);
        topRow->addLayout(disLayout, 1);
    }

    // --- REGISTERS side (midden) ---
    {
        QVBoxLayout *regLayout = new QVBoxLayout();
        QLabel *lblRegs = new QLabel("<b>Registers</b>", this);
        regLayout->addWidget(lblRegs);
        m_regTable = new QTableWidget(0, 2, this);
        m_regTable->setHorizontalHeaderLabels(QStringList() << "Register" << "Value");
        m_regTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_regTable->verticalHeader()->hide();
        m_regTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_regTable->setFont(tableFont2);
        m_regTable->setMinimumWidth(140);
        m_regTable->setMaximumWidth(160);
        regLayout->addWidget(m_regTable, 1);
        topRow->addLayout(regLayout, 0);
    }

    // --- Rechter Kolom (Flags, Stack, Breakpoints) ---
    {
        QVBoxLayout *rightColumnLayout = new QVBoxLayout();

        // --- 1. Flags Table (Bovenaan) ---
        rightColumnLayout->addWidget(new QLabel("<b>Flags</b>", this));
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
        rightColumnLayout->addWidget(m_flagsTable, 0); // Stretch 0


        // --- 2. Stack Table (Midden, vult de rest) ---
        rightColumnLayout->addWidget(new QLabel("<b>Stack (Top)</b>", this));
        m_stackTable = new QTableWidget(7, 4, this);
        m_stackTable->setHorizontalHeaderLabels(QStringList() << "Addr 0..7" << "Val" << "Addr 8..14" << "Val");
        m_stackTable->verticalHeader()->hide();
        m_stackTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_stackTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_stackTable->setFont(fixedFont);
        m_stackTable->setFocusPolicy(Qt::NoFocus);
        m_stackTable->setSelectionMode(QAbstractItemView::NoSelection);
        rightColumnLayout->addWidget(m_stackTable, 1 /* stretch 1 */);


        // --- 3. TAB WIDGET (Breakpoint / Patches) ---
        m_rightTabWidget = new QTabWidget(this);
        m_rightTabWidget->setFont(tableFont1); // Kleinere font voor de tabs
        m_rightTabWidget->setFixedHeight(140); // Vaste hoogte

        // --- TAB 1: Breakpoints ---
        {
            QWidget *bpTab = new QWidget(m_rightTabWidget);
            QVBoxLayout *bpTabLayout = new QVBoxLayout(bpTab);
            bpTabLayout->setContentsMargins(4, 4, 4, 4); // Binnen-marge
            bpTabLayout->setSpacing(4);

            // 3a. Breakpoint Lijst
            m_bpList = new QListWidget(this);
            m_bpList->setFont(fixedFont);
            m_bpList->setSelectionMode(QAbstractItemView::SingleSelection);
            m_bpList->setFixedHeight(80); // Vaste hoogte voor de lijst zelf
            bpTabLayout->addWidget(m_bpList, 0); // Stretch 0

            // 3b. Breakpoint Knoppen
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

            m_bpEnableButton = new QPushButton("Enabled",this);
            m_bpEnableButton->setCheckable(true);
            m_bpEnableButton->setChecked(true);
            m_bpEnableButton->setStyleSheet(
                "background-color: #009900; color: white; font-weight: bold; padding: 2px 5px;"
                );

            bpButtons->addWidget(m_bpAddButton);
            bpButtons->addWidget(m_bpDelButton);
            bpButtons->addWidget(m_bpEditButton);
            bpButtons->addWidget(m_bpLoadButton);
            bpButtons->addWidget(m_bpSaveButton);
            bpButtons->addStretch();
            bpButtons->addWidget(m_bpEnableButton);

            bpTabLayout->addLayout(bpButtons, 0); // Stretch 0

            m_rightTabWidget->addTab(bpTab, tr("Breakpoints"));

            // Connecteer breakpoint signalen
            connect(m_bpEnableButton, &QPushButton::clicked,
                    this, &DebuggerWindow::onToggleBreakpoints);
            connect(m_bpAddButton, &QPushButton::clicked, this, &DebuggerWindow::onBpAdd);
            connect(m_bpDelButton, &QPushButton::clicked, this, &DebuggerWindow::onBpDel);
            connect(m_bpEditButton, &QPushButton::clicked, this, &DebuggerWindow::onBpEdit);
            connect(m_bpList, &QListWidget::itemDoubleClicked, this, &DebuggerWindow::onBpEdit);
            connect(m_bpLoadButton, &QPushButton::clicked, this, &DebuggerWindow::onBpLoad);
            connect(m_bpSaveButton, &QPushButton::clicked, this, &DebuggerWindow::onBpSave);
        }

        // --- TAB 2: Patches ---
        {
            QWidget *patchTab = new QWidget(m_rightTabWidget);
            QVBoxLayout *patchLayout = new QVBoxLayout(patchTab);
            patchLayout->setContentsMargins(4, 4, 4, 4);

            QLabel *patchLabel = new QLabel(tr("Patch functionality coming soon."), patchTab);
            patchLabel->setAlignment(Qt::AlignCenter);
            patchLayout->addWidget(patchLabel);

            m_rightTabWidget->addTab(patchTab, tr("Patches"));
        }

        // Voeg het hele tab-widget toe aan de rechterkolom
        rightColumnLayout->addWidget(m_rightTabWidget, 0 /* stretch 0 */);

        // Voeg de hele rechterkolom toe
        topRow->addLayout(rightColumnLayout, 1);
    }

    rootLayout->addLayout(topRow, 1);

    //
    // ======= MEMORY DUMP (volledige breedte onderaan) =======
    //
    {
        QLabel *lblMem = new QLabel("<b>Memory Dump</b>", this);
        rootLayout->addWidget(lblMem);
        m_memTable = new QTableWidget(0, 18, this);
        QStringList headers;
        headers << "Addr";
        for (int i = 0; i < 16; ++i) headers << QString("%1").arg(i, 2, 16, QChar('0')).toUpper();
        headers << "ASCII";
        m_memTable->setHorizontalHeaderLabels(headers);
        m_memTable->verticalHeader()->hide();
        m_memTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_memTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_memTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_memTable->setAlternatingRowColors(true);
        m_memTable->setFont(fixedFont);
        m_memTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        m_memTable->verticalHeader()->setDefaultSectionSize(20);
        m_memTable->setColumnWidth(0, 60);
        for (int i = 1; i <= 16; ++i) m_memTable->setColumnWidth(i, 28);
        m_memTable->setColumnWidth(17, 120);
        rootLayout->addWidget(m_memTable, 1);
    }

    //
    // ======= MEMORY CONTROLS =======
    //
    {
        QHBoxLayout *memControlsLayout = new QHBoxLayout();
        memControlsLayout->addWidget(new QLabel("Bron:", this));
        m_memSourceComboBox = new QComboBox(this);
        m_memSourceComboBox->addItems({"Z80 CPU Space", "TMS VDP VRAM", "Main RAM", "SGM RAM", "EEPROM", "SRAM"});
        memControlsLayout->addWidget(m_memSourceComboBox);
        memControlsLayout->addSpacing(20);
        memControlsLayout->addWidget(new QLabel("Adres:", this));
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
        m_memAddrHomeButton->setToolTip("Ga naar adres 0");
        memControlsLayout->addWidget(m_memAddrHomeButton);
        memControlsLayout->addStretch();
        rootLayout->addLayout(memControlsLayout);

        connect(m_memSourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DebuggerWindow::onMemSourceChanged);
        connect(m_memAddrEdit, &QLineEdit::returnPressed, this, &DebuggerWindow::onMemAddrChanged);
        connect(m_memAddrPrevButton, &QPushButton::clicked, this, &DebuggerWindow::onMemAddrPrev);
        connect(m_memAddrNextButton, &QPushButton::clicked, this, &DebuggerWindow::onMemAddrNext);
        connect(m_memAddrHomeButton, &QPushButton::clicked, this, &DebuggerWindow::onMemAddrHome);
    }

    //
    // ======= DEBUGGER CONTROLS (knoppen) =======
    //
    QHBoxLayout *buttons = new QHBoxLayout;

    // --- De lambda wordt hier gebruikt ---
    QPushButton *btnStep        = createImageButton(":/images/images/STEP.png");
    QPushButton *btnRun         = createImageButton(":/images/images/RUN.png");
    QPushButton *btnBreak       = createImageButton(":/images/images/BREAK.png");
    QPushButton *btnRefresh     = createImageButton(":/images/images/REFRESH.png");
    QPushButton *btnGotoAddr    = createImageButton(":/images/images/GOTO.png");
    QPushButton *btnSinglestep  = createImageButton(":/images/images/SSTEP.png");

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
    connect(btnRefresh, &QPushButton::clicked, this, &DebuggerWindow::updateAllViews);
    connect(btnRun, &QPushButton::clicked, this, [this](){
        if (m_controller) m_controller->resumeEmulation();
        if (m_disasmView) {
            m_disasmView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    });
    buttons->addWidget(btnStep);
    buttons->addWidget(btnRun);
    buttons->addWidget(btnBreak);
    buttons->addWidget(btnGotoAddr);
    buttons->addWidget(btnSinglestep);
    buttons->addStretch();
    buttons->addWidget(btnRefresh);
    rootLayout->addLayout(buttons, 0);

    updateAllViews();
}

/* ============================================================
   SETTER VOOR PATH
   ============================================================ */
void DebuggerWindow::setBreakpointPath(const QString &path)
{
    m_breakpointPath = path;
}


/* ============================================================
   Koppel de controller
   ============================================================ */
void DebuggerWindow::setController(ColecoController *controller)
{
    m_controller = controller;
}

/* ============================================================
   PUBLIC SLOT: alles refreshen
   ============================================================ */
void DebuggerWindow::updateAllViews()
{
    updateRegisters();
    updateDisassembly();
    updateMemoryDump();
    updateFlags();
    updateStack();
}

/* ============================================================
   REGISTERS
   ============================================================ */
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
        else { nameItem->setForeground(QColor("#9E9E9E")); valItem->setForeground(QColor("#FFFFFF")); }
        m_regTable->setItem(row, 0, nameItem);
        m_regTable->setItem(row, 1, valItem);
    }
}

/* ============================================================
   DISASSEMBLY (PC highlight in rood)
   ============================================================ */
void DebuggerWindow::updateDisassembly()
{
    if (!m_disasmView) return;
    m_disasmView->setRowCount(0);
    const unsigned short pc = Z80.pc.w.l;
    const int MAX_LINES = 64;
    const int CONTEXT_BACK = 0x20;
    unsigned short startAddr = (pc > CONTEXT_BACK) ? (pc - CONTEXT_BACK) : 0x0000;
    int pcRow = -1;
    unsigned short cur = startAddr;
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
        auto addrItem = new QTableWidgetItem(QString("%1").arg(cur, 4, 16, QChar('0')).toUpper());
        addrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        addrItem->setForeground(QColor("#4FC3F7"));
        auto bytesItem = new QTableWidgetItem(bytesStr);
        bytesItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        bytesItem->setForeground(QColor("#81C784"));
        auto instrItem = new QTableWidgetItem(instr);
        instrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        instrItem->setForeground(QColor("#E0E0E0"));
        m_disasmView->setItem(row, 0, addrItem);
        m_disasmView->setItem(row, 1, bytesItem);
        m_disasmView->setItem(row, 2, instrItem);
        if (cur == pc) { pcRow = row; }
        cur = cur + (unsigned short)oplen;
    }
    if (pcRow >= 0) {
        m_disasmView->setSelectionMode(QAbstractItemView::NoSelection);
        m_disasmView->setFocusPolicy(Qt::NoFocus);
        m_disasmView->scrollToItem(m_disasmView->item(pcRow, 0), QAbstractItemView::PositionAtCenter);
        QColor redLine(200, 30, 30);
        QBrush bg(redLine);
        QBrush fg(Qt::white);
        for (int col = 0; col < m_disasmView->columnCount(); ++col) {
            if (QTableWidgetItem *it = m_disasmView->item(pcRow, col)) {
                it->setBackground(bg);
                it->setForeground(fg);
            }
        }
    }
}

/* ============================================================
   MEMORY DUMP (hex + ASCII)
   ============================================================ */
void DebuggerWindow::updateMemoryDump()
{
    if (!m_memTable) return;
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
        auto ascItem = new QTableWidgetItem(ascii);
        ascItem->setForeground(Qt::darkGray);
        ascItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_memTable->setItem(line, 17, ascItem);
    }
}

/* ============================================================
   HELPER: Memory Read
   ============================================================ */
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

/* ============================================================
   NIEUWE HELPERS: Flags en Stack
   ============================================================ */

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

    // Helper lambda om een cel in te vullen
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
    if (m_controller) {
        m_controller->pauseEmulation();
    }
    if (!GotoAddressDialog::getAddress(this, currentPC, addr)) {
        return;
    }
    Z80.pc.w.l = addr-1;
    m_controller->sstepOnce();
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

/* ============================================================
   SLOTS voor Memory Controls
   ============================================================ */
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
    if (m_currentMemSourceIndex == 2) { // Main RAM
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

/* ============================================================
   AANGEPASTE SLOTS voor Breakpoints
   ============================================================ */

void DebuggerWindow::syncBreakpointsToCore()
{
    if (!m_controller) return;

    QStringList activeBreakpoints;

    if (m_breakpointsEnabled) {
        for (const DebuggerBreakpoint& bp : m_breakpoints) {
            if (bp.enabled) {
                activeBreakpoints.append(bp.definition);
            }
        }
    }
    debug_sync_breakpoints(m_controller, activeBreakpoints); // (ga er vanuit dat dit bestaat)
}

void DebuggerWindow::updateBreakpointList()
{
    QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    m_bpList->blockSignals(true);
    m_bpList->clear(); // Dit wist alle items *en* de bijbehorende widgets

    for (const DebuggerBreakpoint& bp : m_breakpoints) {
        // 1. Maak het 'item' dat als container in de lijst dient
        QListWidgetItem* item = new QListWidgetItem(m_bpList);

        // 2. Maak onze custom widget die de inhoud van de rij wordt
        QWidget* itemWidget = new QWidget();
        QHBoxLayout* layout = new QHBoxLayout(itemWidget);
        layout->setContentsMargins(2, 0, 4, 0); // (links, top, rechts, bottom)
        layout->setSpacing(4);

        // 3. Maak het label met de breakpoint-tekst
        QLabel* label = new QLabel(bp.definition);
        label->setFont(fixedFont);

        bool isEffectivelyEnabled = m_breakpointsEnabled && bp.enabled;

        label->setStyleSheet(QString("color: %1").arg(isEffectivelyEnabled ? "white" : "#555555"));

        // 4. Maak de checkbox
        QCheckBox* checkBox = new QCheckBox();
        checkBox->setChecked(bp.enabled);

        // 5. BEWAAR DE 'ID' (de definitie string) in de checkbox
        checkBox->setProperty("bpDefinition", bp.definition);

        // 6. Verbind het 'toggled' signaal
        connect(checkBox, &QCheckBox::toggled, this, &DebuggerWindow::onBpCheckboxToggled);

        // 7. Voeg de widgets toe aan de layout
        layout->addWidget(label);     // Tekst links
        layout->addStretch(1);        // Rekbare ruimte in het midden
        layout->addWidget(checkBox);  // Checkbox rechts

        // 8. Koppel de widget aan het item en voeg toe aan de lijst
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
                               return bp.definition == bpString;
                           });

    if (it == m_breakpoints.end()) {
        qDebug() << "[DebuggerWindow] Adding new breakpoint:" << bpString;
        m_breakpoints.append({bpString, true});
        std::sort(m_breakpoints.begin(), m_breakpoints.end());
        updateBreakpointList();
        syncBreakpointsToCore();
    } else {
        qDebug() << "[DebuggerWindow] Breakpoint already exists. Ignoring.";
    }
}


void DebuggerWindow::onBpDel()
{
    int row = m_bpList->currentRow();
    if (row < 0 || row >= m_breakpoints.size()) return;

    m_breakpoints.removeAt(row);
    updateBreakpointList(); // Bouwt de lijst opnieuw op

    // Update selectie
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

    QString currentAddr = m_breakpoints.at(row).definition;
    bool originalState = m_breakpoints.at(row).enabled;

    QString bpString = SetBreakpointDialog::getBreakpointString(this, currentAddr);
    if (bpString.isEmpty() || bpString == currentAddr) return;

    bool needsSync = false;

    auto it = std::find_if(m_breakpoints.begin(), m_breakpoints.end(),
                           [&bpString](const DebuggerBreakpoint& bp) {
                               return bp.definition == bpString;
                           });

    if (it == m_breakpoints.end()) {
        m_breakpoints[row].definition = bpString;
        m_breakpoints[row].enabled = originalState;

        std::sort(m_breakpoints.begin(), m_breakpoints.end());
        updateBreakpointList();

        auto newIt = std::find_if(m_breakpoints.begin(), m_breakpoints.end(),
                                  [&bpString](const DebuggerBreakpoint& bp) {
                                      return bp.definition == bpString;
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

    if(needsSync) {
        syncBreakpointsToCore();
    }
}

void DebuggerWindow::onToggleBreakpoints()
{
    m_breakpointsEnabled = m_bpEnableButton->isChecked();

    if (m_breakpointsEnabled) {
        m_bpEnableButton->setText("Enabled");
        m_bpEnableButton->setStyleSheet(
            "background-color: #009900; color: white; font-weight: bold; padding: 2px 5px;"
            );
    } else {
        m_bpEnableButton->setText("Disabled");
        m_bpEnableButton->setStyleSheet(
            "background-color: #CC0000; color: white; font-weight: bold; padding: 2px 5px;"
            );

        // Als we de breakpoints uitschakelen terwijl de CPU
        // mogelijk op een breakpoint is gestopt, wis dan de
        // 'break'-vlag in de C-core.
        // Zonder dit zal de "Run"-knop niet werken.
        emit requestBreakCPU();
        //coleco_clear_debug_flags();
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
                               return bp.definition == bpDef;
                           });

    if (it != m_breakpoints.end()) {
        it->enabled = checked;
    } else {
        qWarning() << "onBpCheckboxToggled: Kon breakpoint niet vinden:" << bpDef;
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


// --- SLOTS VOOR LADEN EN OPSLAAN ---

void DebuggerWindow::onBpLoad()
{
    // Maak een absoluut pad
    QString absoluteBpPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + m_breakpointPath);

    QDir dir(absoluteBpPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Load Breakpoints"),
        absoluteBpPath, // Gebruik het absolute pad
        tr("Breakpoint Files (*.txt);;All Files (*)")
        );

    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not open file for reading:\n%1").arg(file.errorString()));
        return;
    }

    QTextStream in(&file);
    m_breakpoints.clear();

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        bool enabled = true;
        QString definition = line;

        if (line.startsWith("D ")) {
            enabled = false;
            definition = line.mid(2).trimmed();
        } else if (line.startsWith("E ")) {
            enabled = true;
            definition = line.mid(2).trimmed();
        }

        if (!definition.isEmpty()) {
            m_breakpoints.append({definition, enabled});
        }
    }

    file.close();

    std::sort(m_breakpoints.begin(), m_breakpoints.end());
    updateBreakpointList();
    syncBreakpointsToCore();

    QMessageBox::information(this, tr("Success"), tr("Breakpoints loaded."));
}

void DebuggerWindow::onBpSave()
{
    QString absoluteBpPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + m_breakpointPath);

    QDir dir(absoluteBpPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString defaultFile = dir.filePath("my_breakpoints.txt");

    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Save Breakpoints"),
        defaultFile,
        tr("Breakpoint Files (*.txt);;All Files (*)")
        );

    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not open file for writing:\n%1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);
    out << "# ADAM+ Breakpoint File\n";
    out << "# Format: [E/D] <Definition>\n";
    out << "# E = Enabled, D = Disabled (default is E)\n\n";

    for (const DebuggerBreakpoint& bp : m_breakpoints) {
        out << (bp.enabled ? "E " : "D ") << bp.definition << "\n";
    }

    file.close();

    QMessageBox::information(this, tr("Success"), tr("Breakpoints saved."));
}


/* ============================================================
   CLOSE EVENT
   ============================================================ */
void DebuggerWindow::closeEvent(QCloseEvent *event)
{
    if (m_controller) {
        m_controller->resumeEmulation();
    }
    emit requestRunCPU();
    QMainWindow::closeEvent(event);
}
