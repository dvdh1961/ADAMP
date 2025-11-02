#ifndef HARDWAREWINDOW_H
#define HARDWAREWINDOW_H

#include <QDialog>
#include <QString>
#include <QButtonGroup>

class QGroupBox;
class QToolButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QDialogButtonBox;

// Machine types
enum MachineType {
    MACHINE_COLECO  = 0,
    MACHINE_ADAMP   = 1,
    MACHINE_ADAM    = 2
};

// Config die door het venster wordt bewerkt/geretourneerd
struct HardwareConfig {
    // Machine
    MachineType machine = MACHINE_COLECO;

    // Video
    int  ntsc = 1;          // 1=NTSC (60), 0=PAL (50)
    int  palette = 0;       // 0..n (UI-keuze)
    int  renderMode = 0;    // “Display Driver” combobox

    // Emulation
    bool startInDebug = false;
    bool biosNoDelay  = false;
    bool patchBiosPAL = false;

    // Additional hardware
    bool sgmEnabled  = false;   // Opcode SGM
    bool f18aEnabled = false;   // F18A

    // Controllers
    bool steeringWheel = false;
    bool rollerCtrl    = false;
    bool superAction   = false;

    // BIOS paden (placeholder)
    QString biosColeco = "Internal";
    QString biosEOS    = "Internal";
    QString biosWriter = "Internal";
    };

class HardwareWindow : public QDialog
{
    Q_OBJECT
public:
    explicit HardwareWindow(const HardwareConfig& initial, QWidget *parent = nullptr);
    HardwareConfig config() const;

private slots:
    void onMachineChanged();
    void onOk();
    void onPaletteChanged(int idx);

private:
    // helpers
    void buildUi();
    void loadFromConfig(const HardwareConfig& c);
    HardwareConfig readFromUi() const;
    void updateAvailability();
    void updatePaletteSwatches();

    // UI widgets
    // Machine
    QGroupBox*   m_groupMachine = nullptr;
    QToolButton* m_btnColeco  = nullptr;
    QToolButton* m_btnAdam    = nullptr;
    QToolButton* m_btnAdamP   = nullptr;
    QButtonGroup* m_machineGroup = nullptr;

    // Additional Controller
    QGroupBox*   m_groupCtrl = nullptr;
    QToolButton* m_btnSteering = nullptr;
    QToolButton* m_btnRoller   = nullptr;
    QToolButton* m_btnSuperAction = nullptr;
    QButtonGroup* m_ctrlGroup = nullptr;

    // Additional Hardware
    QGroupBox*   m_groupAddHw = nullptr;
    QToolButton* m_btnSGM  = nullptr;
    QToolButton* m_btnF18A = nullptr;

    // Video
    QGroupBox*   m_groupVideo = nullptr;
    QComboBox*   m_cboDisplay = nullptr;
    QComboBox*   m_cboPalette = nullptr;
    QLabel*      m_paletteSwatches[16] = { nullptr };  // 16 kleurblokjes

    // Emulation
    QGroupBox*   m_groupEmu = nullptr;
    QCheckBox*   m_chkStartDebug   = nullptr;
    QCheckBox*   m_chkNoDelayBios  = nullptr;
    QCheckBox*   m_chkPatchBiosPAL = nullptr;
    QComboBox*   m_cboFrequency    = nullptr; // NTSC/PAL

    // Buttons
    QDialogButtonBox* m_btnBox = nullptr;

    // state
    HardwareConfig m_initial;
    HardwareConfig m_result;

    bool m_loading = false;        // voorkomt core-calls tijdens opbouwen
};

#endif // HARDWAREWINDOW_H
