#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include <QCloseEvent>
#include <QMutex>
#include <QList>
#include <QString>
#include <QListWidget>

class ColecoController;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTabWidget;

struct DebuggerBreakpoint {
    QString definition;
    bool enabled = true;

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
    // NIEUWE INTERFACES VOOR BESTANDSOPSLAG/LADING
    QList<DebuggerBreakpoint> getBreakpointDefinitions() const;
    void setBreakpointDefinitions(const QList<DebuggerBreakpoint>& list);

signals:
    void requestStepCPU();
    void requestRunCPU();
    void requestBreakCPU();
    void requestBpLoad();
    void requestBpSave();

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

    void onBpAdd();
    void onBpDel();
    void onBpEdit();
    void onToggleBreakpoints();
    void nxtSStep();

    void onBpCheckboxToggled(bool checked);

    void onBpLoad();
    void onBpSave();

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
    QTabWidget   *m_rightTabWidget = nullptr; // <-- Tab widget
    QListWidget  *m_bpList      = nullptr;
    QPushButton  *m_bpAddButton = nullptr;
    QPushButton  *m_bpDelButton = nullptr;
    QPushButton  *m_bpEditButton = nullptr;
    QPushButton  *m_bpLoadButton = nullptr;
    QPushButton  *m_bpSaveButton = nullptr;
    QPushButton  *m_bpEnableButton = nullptr;

    ColecoController *m_controller = nullptr;

    void updateRegisters();
    void updateDisassembly();
    void updateMemoryDump();
    void updateBreakpointList();
    void updateFlags();
    void updateStack();
    void syncBreakpointsToCore();
    void gotoAddress();
    void fillDisassemblyAround(uint16_t addr);

    uint8_t readMemoryByte(uint32_t address);

    uint32_t m_memDumpStartAddr = 0;
    int m_currentMemSourceIndex = 0;
    QList<DebuggerBreakpoint> m_breakpoints;
    bool m_breakpointsEnabled = true;
    QString m_breakpointPath = "media/breakpoints";

};
