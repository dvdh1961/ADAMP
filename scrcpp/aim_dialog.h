#ifndef AIM_DIALOG_H
#define AIM_DIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QTableWidget>
#include <QVector>
#include "fdidisk.h"

class QPushButton;
class QLabel;
class QLineEdit;
class QHBoxLayout;
class QVBoxLayout;
class QWidget;
class QStatusBar; // Toegevoegd

class AimDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AimDialog(QWidget *parent = nullptr);
    ~AimDialog();

    void setDiskRootPath(const QString &path);
    void setTapeRootPath(const QString &path);

    enum Side { SideL, SideR };
    enum ImageType { ImageNone, ImageDsk, ImageDdp };

protected:
    // Event filter om klikken in de tabel op te vangen
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onModePc();
    void onModeAdam();
    void onOpenDskL();
    void onOpenDdpL();
    void onOpenDskR();
    void onOpenDdpR();
    void onNewImgR();
    void onPC();
    void onCopyL();
    void onCopyR();
    void onDelete();
    void onAttrib();
    void onRename();
    void onCrunch();
    void onQuit();
    void onClearL();
    void onClearR();
    void onBlockCopy();
    void onEditVolumeNameL();
    void onEditVolumeNameR();
    void onTableContextMenuL(const QPoint &pos);
    void onTableContextMenuR(const QPoint &pos);

private:
    struct EosEntry {
        QString name;
        QString typeChar;
        quint16 used = 0;
        quint16 lcount = 0;
        quint8 attribute = 0;
        quint16 sblock = 0;
        quint16 allocd = 0;
        quint8 day = 0, month = 0, year = 0;

        bool isVolume() const { return (sblock == 0xAA55u); }
        bool isSentinel() const { return (attribute & 0x01) != 0 && !isVolume(); }
        bool isSystem() const { return (attribute & 0x08) != 0; }
        bool isDeleted() const { return (attribute & 0x04) != 0; }
    };

    void setupUi();
    void setupConnections();
    void configureTableStyle(QTableWidget *table);
    void updateTableFocus(Side side);
    QHBoxLayout* createStatusGroup(Side side, QLineEdit*& maxOut, QLineEdit*& usedOut, QLineEdit*& freeOut, QLineEdit*& typeOut);

    bool openImageFile(const QString &filePath, ImageType type, Side side);
    bool parseDirectory(Side side);
    void repopulateTable(Side side);
    QByteArray readAdamBlock(int blockNum, Side side);
    bool saveEOSfile(Side side);

    AimDialog::EosEntry entryFromBytes(const uchar *raw, bool volume, Side side) const;
    QString formatFileName(const uchar *raw, quint8 attr, QString &typeOut) const;
    QString formatVolumeName(const uchar *raw) const;
    int getMaxBlocks(Side side) const;

    QLineEdit *m_volEditL = nullptr, *m_volEditR = nullptr;
    QLabel *m_dirSizeLabelL = nullptr, *m_dirSizeLabelR = nullptr;
    QLineEdit *m_dirSizeEditL = nullptr, *m_dirSizeEditR = nullptr;
    QTableWidget *m_tableL = nullptr, *m_tableR = nullptr;

    QPushButton *m_btnLabelAdam = nullptr;
    QPushButton *m_btnOpenDskL = nullptr, *m_btnOpenDdpL = nullptr;
    QPushButton *m_btnCopyL = nullptr, *m_btnCopyR = nullptr;
    QPushButton *m_btnModePc = nullptr, *m_btnModeAdam = nullptr;
    QPushButton *m_btnBrowsPc = nullptr;
    QPushButton *m_btnNewImgR = nullptr, *m_btnOpenDskR = nullptr, *m_btnOpenDdpR = nullptr;
    QPushButton *m_btnDel = nullptr, *m_btnAttrib = nullptr, *m_btnRename = nullptr;
    QPushButton *m_btnCrunch = nullptr, *m_btnBlockCopy = nullptr;
    QPushButton *m_btnQuit = nullptr;
    QPushButton *m_btnClearL = nullptr;
    QPushButton *m_btnClearR = nullptr;

    // Footer Stats
    QWidget *m_statusWidgetR = nullptr;
    QWidget *m_pcStatusWidgetR = nullptr;
    QLineEdit *m_pcFileCountEditR = nullptr;

    QLineEdit *m_maxL = nullptr, *m_usedL = nullptr, *m_freeL = nullptr;
    QLineEdit *m_maxR = nullptr, *m_usedR = nullptr, *m_freeR = nullptr;
    QLineEdit *m_typeLabelL = nullptr, *m_typeLabelR = nullptr;

    QStatusBar *m_statusBar = nullptr; // Toegevoegd

    FDIDisk m_fdiL, m_fdiR;
    QVector<EosEntry> m_entriesL, m_entriesR;
    int m_dirBlocksL = 0, m_dirBlocksR = 0;
    ImageType m_typeL = ImageNone, m_typeR = ImageNone;
    QString m_diskRootPath, m_tapeRootPath, m_pathL, m_pathR;
    bool updateVolumeName(Side side, const QString &newName);
    bool updateAllBlocksLeftEntries(Side side, quint16 startBlock, quint16 freeBlocks);
    bool editFileAtRow(Side side, int row);
    bool writeAdamBlock(int blockNum, Side side, const QByteArray &blockData);
};

#endif
