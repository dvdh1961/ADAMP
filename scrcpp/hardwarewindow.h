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

struct HardwareConfig {
    MachineType machine = MACHINE_COLECO;

    int  ntsc = 1;          // 1=NTSC , 0=PAL
    int  palette = 0;
    int  renderMode = 0;

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
    void updateLoadedMedia(const QString& mediaName);

    void setLoadedMediaDisplayNames(
        const QString& colecoCartridgeName,
        const QString& adamCartridgeName,
        const QString& tape1Name,
        const QString& tape2Name,
        const QString& disc1Name,
        const QString& disc2Name,
        const QString& disc3Name
        );

private slots:
    void onMachineChanged();
    void onOk();
    void onPaletteChanged(int idx);
    void onPrinterClicked();
    void onToggleSGM(bool checked);

private:
    void buildUi();
    void loadFromConfig(const HardwareConfig& c);
    HardwareConfig readFromUi() const;
    void updateAvailability();
    void updatePaletteSwatches();

    QGroupBox*   m_groupMachine = nullptr;
    QToolButton* m_btnColeco  = nullptr;
    QToolButton* m_btnAdam    = nullptr;
    QToolButton* m_btnAdamP   = nullptr;
    QButtonGroup* m_machineGroup = nullptr;

    QGroupBox*   m_groupCtrl = nullptr;
    QToolButton* m_btnSteering = nullptr;
    QToolButton* m_btnRoller   = nullptr;
    QToolButton* m_btnSuperAction = nullptr;
    QButtonGroup* m_ctrlGroup = nullptr;

    QGroupBox*   m_groupAddHw = nullptr;
    QToolButton* m_btnSGM  = nullptr;
    QToolButton* m_btnF18A = nullptr;
    QToolButton* m_btnPrinter = nullptr;

    QGroupBox*   m_groupVideo = nullptr;
    QComboBox*   m_cboDisplay = nullptr;
    QComboBox*   m_cboPalette = nullptr;
    QLabel*      m_paletteSwatches[16] = { nullptr };

    QGroupBox*   m_groupEmu = nullptr;
    QCheckBox*   m_chkStartDebug   = nullptr;
    QCheckBox*   m_chkNoDelayBios  = nullptr;
    QCheckBox*   m_chkPatchBiosPAL = nullptr;
    QComboBox*   m_cboFrequency    = nullptr;

    QLabel* m_lblEmuCC = nullptr; // CC (Coleco Cartridge)
    QLabel* m_lblEmuCA = nullptr; // CA (Adam Cartridge)
    QLabel* m_lblEmuD1 = nullptr; // D1 (Tape 1)
    QLabel* m_lblEmuD2 = nullptr; // D2 (Tape 2)
    QLabel* m_lblEmuD5 = nullptr; // D5 (Disc 1)
    QLabel* m_lblEmuD6 = nullptr; // D6 (Disc 2)
    QLabel* m_lblEmuD7 = nullptr; // D7 (Disc 3)

    QDialogButtonBox* m_btnBox = nullptr;

    HardwareConfig m_initial;
    HardwareConfig m_result;

    bool m_loading = false;
    static bool m_sgmSelectionState;
};

#endif
