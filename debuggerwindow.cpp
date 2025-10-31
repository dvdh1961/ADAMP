#include "debuggerwindow.h"
#include "colecocontroller.h"
#include "disasm_bridge.h"
#include "coleco.h"

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

DebuggerWindow::DebuggerWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("ADAM+ Debugger");
    resize(700, 700);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    // Root vertical layout (alles onder elkaar)
    QVBoxLayout *rootLayout = new QVBoxLayout(central);
    QFont tableFont1("Roboto", 10);
    QFont tableFont2("Roboto", 12);

    //
    // ======= TOP ROW: REGISTERS (links) + DISASSEMBLY (rechts) =======
    //
    QHBoxLayout *topRow = new QHBoxLayout();

    // --- REGISTERS side ---
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
        m_regTable->setMinimumWidth(200); // fixe breedte
        m_regTable->setMaximumWidth(260); // zodat disasm genoeg ruimte krijgt
        regLayout->addWidget(m_regTable, /*stretch*/1);

        topRow->addLayout(regLayout, /*stretch*/0);
    }

    // --- DISASSEMBLY side ---
    {
        QVBoxLayout *disLayout = new QVBoxLayout();

        QLabel *lblDis = new QLabel("<b>Disassembly (around PC)</b>", this);
        disLayout->addWidget(lblDis);

        m_disasmView = new QTableWidget(0, 3, this);
        m_disasmView->setHorizontalHeaderLabels(QStringList() << "Addr" << "Bytes" << "Instruction");
        m_disasmView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_disasmView->verticalHeader()->hide();
        m_disasmView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_disasmView->setFont(tableFont1);

        disLayout->addWidget(m_disasmView, /*stretch*/1);

        topRow->addLayout(disLayout, /*stretch*/1);
    }

    rootLayout->addLayout(topRow, /*stretch*/2);

    //
    // ======= MEMORY DUMP (volledige breedte onderaan) =======
    //
    {
        QLabel *lblMem = new QLabel("<b>Memory Dump (16 bytes/line)</b>", this);
        rootLayout->addWidget(lblMem);

        m_memTable = new QTableWidget(0, 18, this);

        QStringList headers;
        headers << "Addr";
        for (int i = 0; i < 16; ++i)
            headers << QString("%1").arg(i, 2, 16, QChar('0')).toUpper();
        headers << "ASCII";

        m_memTable->setHorizontalHeaderLabels(headers);
        m_memTable->verticalHeader()->hide();
        m_memTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_memTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_memTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_memTable->setAlternatingRowColors(true);
        m_memTable->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_memTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        m_memTable->verticalHeader()->setDefaultSectionSize(20);
        m_memTable->setColumnWidth(0, 60);
        for (int i = 1; i <= 16; ++i)
            m_memTable->setColumnWidth(i, 28);
        m_memTable->setColumnWidth(17, 120);

        rootLayout->addWidget(m_memTable, /*stretch*/2);
    }

    //
    // ======= BUTTON ROW =======
    //
    QHBoxLayout *buttons = new QHBoxLayout;
    QPushButton *btnStep    = new QPushButton("Step", this);
    QPushButton *btnRun     = new QPushButton("Run", this);
    QPushButton *btnBreak   = new QPushButton("Break", this);
    QPushButton *btnRefresh = new QPushButton("Refresh", this);

    // Emits (als je elders nog iets wil opvangen)
    connect(btnStep,  &QPushButton::clicked, this, [this](){ emit requestStepCPU();  });
    connect(btnRun,   &QPushButton::clicked, this, [this](){ emit requestRunCPU();   });
    connect(btnBreak, &QPushButton::clicked, this, [this](){ emit requestBreakCPU(); });
    connect(btnRefresh, &QPushButton::clicked, this, &DebuggerWindow::updateAllViews);

    // Directe koppeling naar controller-acties, maar nu met null-check
    connect(btnBreak, &QPushButton::clicked, this, [this](){
        qDebug() << "[DebuggerWindow] Break clicked";
        if (m_controller) {
            m_controller->pauseEmulation();
        } else {
            qDebug() << " -> m_controller is null";
        }
    });

    connect(btnRun, &QPushButton::clicked, this, [this](){
        qDebug() << "[DebuggerWindow] Run clicked";
        if (m_controller) {
            m_controller->resumeEmulation();
        } else {
            qDebug() << " -> m_controller is null";
        }
    });

    connect(btnStep, &QPushButton::clicked, this, [this](){
        qDebug() << "[DebuggerWindow] Step clicked";
        if (m_controller) {
            m_controller->stepOnce();
        } else {
            qDebug() << " -> m_controller is null";
        }
    });

    buttons->addWidget(btnStep);
    buttons->addWidget(btnRun);
    buttons->addWidget(btnBreak);
    buttons->addStretch();
    buttons->addWidget(btnRefresh);

    rootLayout->addLayout(buttons, /*stretch*/0);

    //
    // ======= AUTOREFRESH TIMER =======
    //
    QTimer *refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &DebuggerWindow::updateAllViews);
    refreshTimer->start(500);

    updateAllViews();
}

/* ============================================================
   Koppel de controller (moet gebeuren na constructie!)
   ============================================================ */
void DebuggerWindow::setController(ColecoController *controller)
{
    m_controller = controller;

    // (optioneel) meteen initiale state van de knoppen zetten,
    // maar knoppen zelf hebben we lokaal (btnRun/etc) niet als member,
    // dus dat laten we voorlopig. Als je dat toch wil,
    // maak btnRun/btnBreak/btnStep members.
}

/* ============================================================
   PUBLIC SLOT: alles refreshen
   ============================================================ */
void DebuggerWindow::updateAllViews()
{
    updateRegisters();
    updateDisassembly();
    updateMemoryDump();
}

/* ============================================================
   REGISTERS
   ============================================================ */
void DebuggerWindow::updateRegisters()
{
    if (!m_regTable) return;

    struct RegEntry { const char* name; uint16_t value; };
    RegEntry regs[] = {
                       {"PC", z80_get_pc()}, {"SP", z80_get_sp()},
                       {"AF", z80_get_af()}, {"BC", z80_get_bc()},
                       {"DE", z80_get_de()}, {"HL", z80_get_hl()},
                       {"IX", z80_get_ix()}, {"IY", z80_get_iy()},
                       };

    m_regTable->setRowCount(0);
    for (auto &r : regs)
    {
        int row = m_regTable->rowCount();
        m_regTable->insertRow(row);

        auto nameItem = new QTableWidgetItem(r.name);
        auto valItem  = new QTableWidgetItem(
            QString("$%1").arg(r.value, 4, 16, QChar('0')).toUpper());

        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        valItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        m_regTable->setItem(row, 0, nameItem);
        if (row == 0) {
            nameItem->setForeground(QColor("#4FC3F7"));  // lichtblauw
            valItem->setForeground(QColor("#4FC3F7"));   // lichtblauw
        } else {
            nameItem->setForeground(QColor("#9E9E9E"));  // subtiel grijs
            nameItem->setForeground(QColor("#FFFFFF"));  // white? (check: dit overschrijft vorige lijn)
        }
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

    // Huidige PC (program counter)
    const unsigned short pc = z80_get_pc();

    // Toon bytes rond PC
    const int MAX_LINES = 64;
    const int CONTEXT_BACK = 0x20; // +-32 bytes terug als dat kan

    unsigned short startAddr =
        (pc > CONTEXT_BACK) ? (pc - CONTEXT_BACK) : 0x0000;

    int pcRow = -1;
    unsigned short cur = startAddr;

    for (int line = 0; line < MAX_LINES; ++line)
    {
        int oplen = 0;
        QString instr = disasmOneAt(cur, oplen);
        if (oplen <= 0 || oplen > 4) oplen = 1;

        QString bytesStr;
        for (int b = 0; b < oplen; ++b) {
            bytesStr += QString("%1 ").arg(
                                          coleco_ReadByte(cur + b),
                                          2, 16, QChar('0')
                                          ).toUpper();
        }
        bytesStr = bytesStr.trimmed();

        int row = m_disasmView->rowCount();
        m_disasmView->insertRow(row);

        auto addrItem = new QTableWidgetItem(
            QString("%1").arg(cur, 4, 16, QChar('0')).toUpper());
        addrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        addrItem->setForeground(QColor("#4FC3F7"));  // lichtblauw

        auto bytesItem = new QTableWidgetItem(bytesStr);
        bytesItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        bytesItem->setForeground(QColor("#81C784")); // groen

        auto instrItem = new QTableWidgetItem(instr);
        instrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        instrItem->setForeground(QColor("#E0E0E0")); // lichtgrijs

        m_disasmView->setItem(row, 0, addrItem);
        m_disasmView->setItem(row, 1, bytesItem);
        m_disasmView->setItem(row, 2, instrItem);

        if (cur == pc) {
            pcRow = row;
        }

        cur = cur + (unsigned short)oplen;
    }

    m_disasmView->resizeRowsToContents();

    if (pcRow >= 0)
    {
        m_disasmView->setSelectionMode(QAbstractItemView::NoSelection);
        m_disasmView->setFocusPolicy(Qt::NoFocus);

        m_disasmView->scrollToItem(
            m_disasmView->item(pcRow, 0),
            QAbstractItemView::PositionAtCenter
            );

        QColor redLine(200, 30, 30);
        QBrush bg(redLine);
        QBrush fg(Qt::white);

        for (int col = 0; col < m_disasmView->columnCount(); ++col)
        {
            if (QTableWidgetItem *it = m_disasmView->item(pcRow, col))
            {
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
    const int maxAddr = 0x200; // eerste 0x200 bytes tonen

    int row = 0;
    for (int base = 0; base < maxAddr; base += bytesPerLine)
    {
        m_memTable->insertRow(row);

        auto addrItem = new QTableWidgetItem(
            QString("%1").arg(base, 4, 16, QChar('0')).toUpper());
        addrItem->setForeground(Qt::cyan);
        addrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_memTable->setItem(row, 0, addrItem);

        QString ascii;
        for (int i = 0; i < bytesPerLine; ++i)
        {
            uint8_t b = coleco_ReadByte(base + i);

            auto itm = new QTableWidgetItem(
                QString("%1").arg(b, 2, 16, QChar('0')).toUpper());
            itm->setTextAlignment(Qt::AlignCenter);
            itm->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

            m_memTable->setItem(row, 1 + i, itm);

            ascii += (b >= 32 && b <= 126) ? QChar(b) : '.';
        }

        auto ascItem = new QTableWidgetItem(ascii);
        ascItem->setForeground(Qt::darkGray);
        ascItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_memTable->setItem(row, 17, ascItem);

        ++row;
    }

    m_memTable->resizeRowsToContents();
}

/* ============================================================
   CLOSE EVENT
   ============================================================ */
void DebuggerWindow::closeEvent(QCloseEvent *event)
{
    // Als debugger dichtgaat → emulator moet terug runnen
    if (m_controller) {
        m_controller->resumeEmulation();
    }
emit requestRunCPU();
    QMainWindow::closeEvent(event);
}
