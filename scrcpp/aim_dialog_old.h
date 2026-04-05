#ifndef AIM_DIALOG_H
#define AIM_DIALOG_H

#include <QDialog>
#include <QListWidget>

class QPushButton;
class QLabel;
class QLineEdit;
class QMenuBar;
class QAction;
class QListWidgetItem;
class QGroupBox;

class AimDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AimDialog(QWidget *parent = nullptr);

    void setPcRootPath(const QString &path);
    void setDiskRootPath(const QString &path);
    void setTapeRootPath(const QString &path);

    QString pcRootPath() const { return m_pcRootPath; }
    QString currentImagePath() const { return m_currentImagePath; }

signals:
    void dskSelected(const QString &filePath);
    void ddpSelected(const QString &filePath);

private slots:
    void onOpenDsk();
    void onOpenDdp();
    void onBrowsePc();
    void onNavigateUp();
    void onRefreshPc();
    void onPcItemDoubleClicked(QListWidgetItem *item);
    void onPcSelectionChanged();
    void onImageSelectionChanged();
    void onImportToImage();
    void onExportToPc();
    void onDeleteEntry();
    void onRenameEntry();
    void onAttributesEntry();
    void onCrunchImage();
    void onBlockCopy();
    void onVolumeLabelClicked();
    void onNewImage();

private:
    void setupUi();
    void setupMenus();
    void setupConnections();
    void refreshPcView();
    void refreshImageView();
    void refreshInfoBar();
    void applyStyle();
    void setStatusText(const QString &text);
    void setCurrentImagePath(const QString &filePath);
    void addPlaceholderImageEntries(const QString &filePath);
    int directoryEntryCount() const;

private:
    QString m_pcRootPath;
    QString m_diskRootPath;
    QString m_tapeRootPath;
    QString m_currentImagePath;

    QMenuBar *m_menuBar = nullptr;

    QAction *m_actOpenDsk = nullptr;
    QAction *m_actOpenDdp = nullptr;
    QAction *m_actRefreshPc = nullptr;
    QAction *m_actClose = nullptr;

    QPushButton *m_aboutButton = nullptr;
    QPushButton *m_openDskButton = nullptr;
    QPushButton *m_openDdpButton = nullptr;
    QPushButton *m_pcButton = nullptr;
    QPushButton *m_upButton = nullptr;
    QPushButton *m_browsePcButton = nullptr;

    QLineEdit *m_volumeNameEdit = nullptr;
    QLabel *m_dirSizeValue = nullptr;
    QLabel *m_filenameValue = nullptr;
    QLabel *m_blocksLeftValue = nullptr;

    QGroupBox *m_imageGroup = nullptr;
    QGroupBox *m_pcGroup = nullptr;

    QListWidget *m_imageList = nullptr;
    QListWidget *m_pcList = nullptr;

    QPushButton *m_importButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_deleteButton = nullptr;

    QPushButton *m_quitButton = nullptr;
    QPushButton *m_crunchButton = nullptr;
    QPushButton *m_blockCopyButton = nullptr;
    QPushButton *m_volumeButton = nullptr;
    QPushButton *m_renameButton = nullptr;
    QPushButton *m_attributesButton = nullptr;
    QPushButton *m_newImageButton = nullptr;

    QLabel *m_status = nullptr;
};

#endif
