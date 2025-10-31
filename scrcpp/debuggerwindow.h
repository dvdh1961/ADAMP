#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include <QCloseEvent>

// Forward declare om cirkeldependencys te vermijden
class ColecoController;

// DebuggerWindow toont registers, disassembly, memory dump
// en laat je pauzeren/runnen/steppen in de emulator.
class DebuggerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DebuggerWindow(QWidget *parent = nullptr);

    // Koppelt de emulatorcontroller zodat de knoppen kunnen werken
    void setController(ColecoController *controller);

signals:
    void requestStepCPU();
    void requestRunCPU();
    void requestBreakCPU();

public slots:
    void updateAllViews();

protected:
    // Wanneer user de debugger sluit -> emulator moet terug runnen
    void closeEvent(QCloseEvent *event) override;

private:
    // UI componenten
    QTableWidget *m_regTable    = nullptr;
    QTableWidget *m_disasmView  = nullptr;
    QTableWidget *m_memTable    = nullptr;

    // Link naar de emulator controller (mag nullptr zijn tot setController wordt geroepen)
    ColecoController *m_controller = nullptr;

    // interne helpers voor refresh
    void updateRegisters();
    void updateDisassembly();
    void updateMemoryDump();
};
