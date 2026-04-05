#ifndef ADAMIMAGEMANAGERDIALOG_H
#define ADAMIMAGEMANAGERDIALOG_H

#include <QDialog>
#include <QDateTime>
#include <QString>
#include <QVector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QLineEdit;
class QSplitter;
class QMenuBar;
class QAction;
class QFileInfo;

struct AdamImageEntry
{
    QString name;
    QString type;
    int blocks = 0;
    int startBlock = 0;
    bool locked = false;
    QDateTime timestamp;
    qint64 sizeBytes = 0;
};

class AdamImageManagerBackend
{
public:
    virtual ~AdamImageManagerBackend() = default;

    virtual bool openImage(const QString &filePath, QString *errorString = nullptr) = 0;
    virtual bool saveImage(QString *errorString = nullptr) = 0;
    virtual bool saveImageAs(const QString &filePath, QString *errorString = nullptr) = 0;
    virtual bool createNewImage(const QString &filePath,
                                const QString &volumeName,
                                int totalBlocks,
                                QString *errorString = nullptr) = 0;

    virtual QString imagePath() const = 0;
    virtual QString volumeName() const = 0;
    virtual int totalBlocks() const = 0;
    virtual int usedBlocks() const = 0;
    virtual QVector<AdamImageEntry> entries() const = 0;

    virtual bool importFromPc(const QStringList &pcFiles,
                              QString *errorString = nullptr) = 0;
    virtual bool exportToPc(const QString &entryName,
                            const QString &targetDir,
                            QString *errorString = nullptr) = 0;
    virtual bool removeEntry(const QString &entryName,
                             QString *errorString = nullptr) = 0;
    virtual bool renameEntry(const QString &oldName,
                             const QString &newName,
                             QString *errorString = nullptr) = 0;
    virtual bool setEntryLocked(const QString &entryName,
                                bool locked,
                                QString *errorString = nullptr) = 0;
    virtual bool crunchImage(QString *errorString = nullptr) = 0;
    virtual bool blockCopyToImage(const QString &sourceEntry,
                                  const QString &destEntry,
                                  QString *errorString = nullptr) = 0;
};

class AdamImageManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AdamImageManagerDialog(QWidget *parent = nullptr);
    ~AdamImageManagerDialog() override;

    void setBackend(AdamImageManagerBackend *backend);
    void setPcRootPath(const QString &path);
    QString pcRootPath() const;

signals:
    void requestMountImage(const QString &filePath);
    void requestCreateBlankImage(const QString &filePath, const QString &volumeName, int totalBlocks);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onOpenImage();
    void onSaveImage();
    void onSaveImageAs();
    void onNewImage();
    void onBrowsePcFolder();
    void onNavigatePcUp();

    void onImportToImage();
    void onExportToPc();
    void onDeleteEntry();
    void onRenameEntry();
    void onAttributesEntry();
    void onCrunchImage();
    void onBlockCopy();
    void onCloseRequested();

    void onPcSelectionChanged();
    void onImageSelectionChanged();
    void onPcItemDoubleClicked(QListWidgetItem *item);

private:
    void setupUi();
    void setupMenus();
    void setupConnections();
    void applyPalette();

    void refreshPcView();
    void refreshImageView();
    void refreshState();
    void setStatusMessage(const QString &message);

    QString cleanEntryDisplayName(const AdamImageEntry &entry) const;
    QString currentImageEntryName() const;
    bool ensureBackend(const char *actionName);

private:
    AdamImageManagerBackend *m_backend = nullptr;
    QString m_pcRootPath;

    QMenuBar *m_menuBar = nullptr;
    QAction *m_actOpen = nullptr;
    QAction *m_actSave = nullptr;
    QAction *m_actSaveAs = nullptr;
    QAction *m_actNew = nullptr;
    QAction *m_actClose = nullptr;

    QPushButton *m_aboutButton = nullptr;
    QPushButton *m_openButton = nullptr;
    QPushButton *m_pcButton = nullptr;
    QPushButton *m_upButton = nullptr;
    QPushButton *m_browsePcButton = nullptr;

    QLineEdit *m_volumeNameEdit = nullptr;
    QLabel *m_dirSizeValue = nullptr;
    QLabel *m_filenameValue = nullptr;
    QLabel *m_blocksLeftValue = nullptr;
    QLabel *m_statusLabel = nullptr;

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
};

#endif // ADAMIMAGEMANAGERDIALOG_H
