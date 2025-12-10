#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include <QCloseEvent>
#include <QMutex>
#include <QList>
#include <QString>
#include <QListWidget>

#include "debug_bridge.h"

class ColecoController;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTabWidget;

using DebuggerBreakpoint = CoreBreakpoint;

struct DebuggerSymbol {
    QString type;           // b.v. "call", "data", "text"
    uint16_t address;       // 16-bit adres
    QString label;          // Omschrijving/label
    QString definition_text; // De volledige ruwe definitie string (b.v. "call:0020:start")

    bool operator<(const DebuggerSymbol& other) const {
        return address < other.address;
    }
};

class DebuggerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DebuggerWindow(QWidget *parent = nullptr);
    void setController(ColecoController *controller);
    QList<DebuggerBreakpoint> getBreakpointDefinitions() const;
    void setBreakpointDefinitions(const QList<DebuggerBreakpoint>& list);
    QList<DebuggerSymbol> getSymbolDefinitions() const;
    void setSymbolDefinitions(const QList<DebuggerSymbol>& list);

signals:
    void requestStepCPU();
    void requestRunCPU();
    void requestBreakCPU();
    void requestBpLoad();
    void requestBpSave();
    void requestSymLoad();
    void requestSymSave();
    void requestStepOver(uint16_t returnAddress);

public slots:
    void updateAllViews();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onMemSourceChanged(int index);
    void onMemAddrChanged();
    void onMemAddrPrev();
    void onMemAddrNext();
    void onMemAddrHome();
    void onDisasmContextMenu(const QPoint &pos);
    void onRegistersContextMenu(const QPoint &pos);
    void onBpAdd();
    void onBpDel();
    void onBpEdit();
    void onMemTableDoubleClicked(int row, int column);
    void onToggleBreakpoints();
    void nxtSStep();
    void onStepOver();

    void onBpCheckboxToggled(bool checked);
    void onToggleSymbols();

    void onBpLoad();
    void onBpSave();
    void onSymAdd();
    void onSymDel();
    void onSymEdit();
    void onSymLoad();
    void onSymSave();

    void onEmuPausedChanged(bool paused);

private:
    QTableWidget *m_regTable    = nullptr;
    QTableWidget *m_disasmView  = nullptr;
    QTableWidget *m_memTable    = nullptr;

    QComboBox   *m_memSourceComboBox = nullptr;
    QLineEdit   *m_memAddrEdit = nullptr;
    QPushButton *m_memAddrPrevButton = nullptr;
    QPushButton *m_memAddrNextButton = nullptr;
    QPushButton *m_memAddrHomeButton = nullptr;

    QTableWidget *m_flagsTable  = nullptr;
    QTableWidget *m_stackTable  = nullptr;
    QTabWidget   *m_rightTabWidget = nullptr;
    QTabWidget   *m_mainBottomTabWidget = nullptr;
    QTabWidget   *m_memSourceTabWidget = nullptr;
    QTabWidget   *m_disasmTabWidget = nullptr;
    QTabWidget   *m_regsTabWidget = nullptr;
    QTabWidget   *m_flagsStackTabWidget = nullptr;
    QListWidget  *m_bpList      = nullptr;
    QListWidget  *m_symList     = nullptr;
    QTableWidget *m_vdpRegTable = nullptr;
    QTabWidget   *m_vdpRegsTabWidget = nullptr;
    QPushButton  *m_bpAddButton = nullptr;

    QPushButton *btnStep        = nullptr;
    QPushButton *btnRun         = nullptr;
    QPushButton *btnBreak       = nullptr;
    QPushButton *btnGotoAddr    = nullptr;
    QPushButton *btnSinglestep  = nullptr;
    QPushButton *btnRefresh     = nullptr;
    QPushButton *btnStepOver    = nullptr;

    QPushButton  *m_bpDelButton = nullptr;
    QPushButton  *m_bpEditButton = nullptr;
    QPushButton  *m_bpLoadButton = nullptr;
    QPushButton  *m_bpSaveButton = nullptr;
    QPushButton  *m_bpEnableButton = nullptr;

    QPushButton  *m_symAddButton = nullptr;
    QPushButton  *m_symDelButton = nullptr;
    QPushButton  *m_symEditButton = nullptr;
    QPushButton  *m_symLoadButton = nullptr;
    QPushButton  *m_symSaveButton = nullptr;

    QPushButton  *m_symEnableButton = nullptr;

    ColecoController *m_controller = nullptr;

    void updateRegisters();
    void updateDisassembly();
    void updateMemoryDump();
    void updateBreakpointList();
    void updateSymbolsList();
    void updateFlags();
    void updateStack();
    void updateVdpRegisters();
    void syncBreakpointsToCore();
    void gotoAddress();
    void fillDisassemblyAround(uint16_t addr);

    uint8_t readMemoryByte(uint32_t address);
    void writeMemoryByte(uint32_t address, uint8_t value);
    QString getSymbolLabelForAddress(uint16_t address) const;

    uint32_t m_memDumpStartAddr = 0;
    int m_currentMemSourceIndex = 0;
    QList<DebuggerBreakpoint> m_breakpoints;
    QList<DebuggerSymbol> m_symbols;
    bool m_breakpointsEnabled = true;
    QString m_breakpointPath = "media/breakpoints";
    bool m_symbolsEnabled = true;
    int m_scrollAttempts = 0;
};
