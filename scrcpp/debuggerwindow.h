#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include <QCloseEvent>
#include <QMutex>
#include <QList>
#include <QString>
#include <QListWidget>

// Forward declare om cirkeldependencys te vermijden
class ColecoController;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTabWidget; // <-- Toegevoegd

// Slaat één breakpoint op, met zijn definitie-string
// en zijn individuele enabled/disabled (checkbox) staat.
struct DebuggerBreakpoint {
    QString definition;
    bool enabled = true;

    // Operator voor sorteren op de 'definition' string
    bool operator<(const DebuggerBreakpoint& other) const {
        return definition.compare(other.definition, Qt::CaseInsensitive) < 0;
    }
};


class DebuggerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DebuggerWindow(QWidget *parent = nullptr);
    void setController(ColecoController *controller);
    void setBreakpointPath(const QString &path); // Setter voor het pad

signals:
    void requestStepCPU();
    void requestRunCPU();
    void requestBreakCPU();

public slots:
    void updateAllViews();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // Slots voor de memory controls
    void onMemSourceChanged(int index);
    void onMemAddrChanged();
    void onMemAddrPrev();
    void onMemAddrNext();
    void onMemAddrHome();

    // Slots voor Breakpoints
    void onBpAdd();
    void onBpDel();
    void onBpEdit();
    void onToggleBreakpoints();
    void nxtSStep();

    // Slot voor custom checkbox
    void onBpCheckboxToggled(bool checked);

    // Slots voor Load/Save
    void onBpLoad();
    void onBpSave();

private:
    // UI componenten
    QTableWidget *m_regTable    = nullptr;
    QTableWidget *m_disasmView  = nullptr;
    QTableWidget *m_memTable    = nullptr;

    // UI voor Memory View
    QComboBox   *m_memSourceComboBox = nullptr;
    QLineEdit   *m_memAddrEdit = nullptr;
    QPushButton *m_memAddrPrevButton = nullptr;
    QPushButton *m_memAddrNextButton = nullptr;
    QPushButton *m_memAddrHomeButton = nullptr;

    // UI voor Rechterkolom
    QTableWidget *m_flagsTable  = nullptr;
    QTableWidget *m_stackTable  = nullptr;
    QTabWidget   *m_rightTabWidget = nullptr; // <-- Tab widget
    QListWidget  *m_bpList      = nullptr;
    QPushButton  *m_bpAddButton = nullptr;
    QPushButton  *m_bpDelButton = nullptr;
    QPushButton  *m_bpEditButton = nullptr;
    QPushButton  *m_bpLoadButton = nullptr;
    QPushButton  *m_bpSaveButton = nullptr;
    QPushButton  *m_bpEnableButton = nullptr;

    // Link naar de emulator controller
    ColecoController *m_controller = nullptr;

    // interne helpers voor refresh
    void updateRegisters();
    void updateDisassembly();
    void updateMemoryDump();
    void updateBreakpointList();
    void updateFlags();
    void updateStack();
    void syncBreakpointsToCore();
    void gotoAddress();
    void fillDisassemblyAround(uint16_t addr);

    // Helper om data te lezen van de geselecteerde bron
    uint8_t readMemoryByte(uint32_t address);

    // Interne status
    uint32_t m_memDumpStartAddr = 0;
    int m_currentMemSourceIndex = 0;
    QList<DebuggerBreakpoint> m_breakpoints;
    bool m_breakpointsEnabled = true;
    QString m_breakpointPath = "media/breakpoints"; // Default relatief pad

    //QMutex m_breakpointMutex;
};
