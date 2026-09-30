#ifndef ATARIDEBUGGERWINDOW_H
#define ATARIDEBUGGERWINDOW_H
#include <QMainWindow>
#include <QVariantMap>
class QCloseEvent;
class ColecoController; class QLabel; class QLineEdit; class QListWidget; class QTableWidget; class QTabWidget;
class QTimer;
class AtariDebuggerWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit AtariDebuggerWindow(QWidget* parent=nullptr);
    void setController(ColecoController* controller); void refresh();
private slots:
    void updateState(const QVariantMap& state); void addBreakpoint(); void removeBreakpoint();
    void goToMemoryAddress(); void previousMemoryLine(); void nextMemoryLine();
    void homeMemory(); void copyMemoryRange();
private:
    ColecoController* m_controller=nullptr; QLabel* m_status=nullptr;
    QTableWidget* m_disassembly=nullptr; QTableWidget* m_registers=nullptr; QTableWidget* m_flags=nullptr;
    QTableWidget* m_tiaRiot=nullptr; QTableWidget* m_memory=nullptr; QTabWidget* m_programTabs=nullptr;
    QLineEdit* m_breakpointEdit=nullptr; QListWidget* m_breakpointList=nullptr;
    QLineEdit* m_memoryAddressEdit=nullptr;
    QTimer* m_liveRefreshTimer=nullptr;
    bool m_instructionRun=false;
    quint16 m_disassemblyBase=0;
    bool m_disassemblyBaseValid=false;
    int m_traceCursor=0;
    QByteArray m_memoryBytes;
    void sendBreakpoints();
protected:
    void closeEvent(QCloseEvent* event) override;
};
#endif
