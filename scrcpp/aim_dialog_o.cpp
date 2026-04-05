#include "aim_dialog.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QStyle>
#include <QVBoxLayout>

AdamImageManagerDialog::AdamImageManagerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("ADAM Image Manager");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(980, 520);
    setMinimumSize(900, 500);

    m_pcRootPath = QDir::homePath();

    setupUi();
    setupMenus();
    setupConnections();
    applyPalette();
    refreshPcView();
    refreshState();
}

AdamImageManagerDialog::~AdamImageManagerDialog() = default;

void AdamImageManagerDialog::setBackend(AdamImageManagerBackend *backend)
{
    m_backend = backend;
    refreshImageView();
    refreshState();
}

void AdamImageManagerDialog::setPcRootPath(const QString &path)
{
    if (path.isEmpty())
        return;

    m_pcRootPath = QDir::cleanPath(path);
    refreshPcView();
}

QString AdamImageManagerDialog::pcRootPath() const
{
    return m_pcRootPath;
}

void AdamImageManagerDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    refreshPcView();
    refreshImageView();
    refreshState();
}

void AdamImageManagerDialog::setupUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);

    m_menuBar = new QMenuBar(this);
    root->setMenuBar(m_menuBar);

    QHBoxLayout *topButtons = new QHBoxLayout();
    m_aboutButton = new QPushButton("About", this);
    m_openButton = new QPushButton("...", this);
    m_pcButton = new QPushButton("PC", this);
    m_upButton = new QPushButton("UP", this);
    m_browsePcButton = new QPushButton("...", this);

    for (QPushButton *btn : {m_aboutButton, m_openButton, m_pcButton, m_upButton, m_browsePcButton}) {
        btn->setMinimumHeight(28);
    }

    topButtons->addWidget(m_aboutButton);
    topButtons->addStretch(1);
    topButtons->addWidget(m_openButton);
    topButtons->addSpacing(16);
    topButtons->addWidget(m_pcButton);
    topButtons->addStretch(1);
    topButtons->addWidget(m_upButton);
    topButtons->addWidget(m_browsePcButton);
    root->addLayout(topButtons);

    QGridLayout *header = new QGridLayout();
    header->setHorizontalSpacing(10);
    header->setVerticalSpacing(4);

    QLabel *volumeLabel = new QLabel("Volume Name:", this);
    QLabel *dirSizeLabel = new QLabel("Dir Size:", this);
    QLabel *filenameLabel = new QLabel("Filename:", this);

    m_volumeNameEdit = new QLineEdit(this);
    m_volumeNameEdit->setReadOnly(true);
    m_dirSizeValue = new QLabel("0 / 0", this);
    m_filenameValue = new QLabel("No image loaded", this);

    header->addWidget(volumeLabel, 0, 0);
    header->addWidget(m_volumeNameEdit, 0, 1);
    header->addWidget(dirSizeLabel, 0, 2);
    header->addWidget(m_dirSizeValue, 0, 3);
    header->addWidget(filenameLabel, 0, 4);
    header->addWidget(m_filenameValue, 0, 5);
    header->setColumnStretch(1, 1);
    header->setColumnStretch(5, 2);
    root->addLayout(header);

    QHBoxLayout *listsRow = new QHBoxLayout();
    listsRow->setSpacing(12);

    m_imageList = new QListWidget(this);
    m_pcList = new QListWidget(this);
    m_imageList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pcList->setSelectionMode(QAbstractItemView::SingleSelection);

    QWidget *middleButtons = new QWidget(this);
    QVBoxLayout *middleLayout = new QVBoxLayout(middleButtons);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->addStretch();

    m_importButton = new QPushButton(QString::fromUtf8("◀"), this);
    m_exportButton = new QPushButton(QString::fromUtf8("▶"), this);
    m_deleteButton = new QPushButton(QString::fromUtf8("🗑"), this);

    for (QPushButton *btn : {m_importButton, m_exportButton, m_deleteButton}) {
        btn->setFixedSize(38, 30);
        middleLayout->addWidget(btn, 0, Qt::AlignHCenter);
    }

    middleLayout->addStretch();

    listsRow->addWidget(m_imageList, 1);
    listsRow->addWidget(middleButtons, 0);
    listsRow->addWidget(m_pcList, 1);
    root->addLayout(listsRow, 1);

    QHBoxLayout *bottomInfo = new QHBoxLayout();
    QLabel *blocksLeftLabel = new QLabel("BLOCKS LEFT:", this);
    m_blocksLeftValue = new QLabel("0", this);
    bottomInfo->addWidget(blocksLeftLabel);
    bottomInfo->addWidget(m_blocksLeftValue);
    bottomInfo->addStretch(1);
    root->addLayout(bottomInfo);

    QHBoxLayout *bottomButtons = new QHBoxLayout();
    bottomButtons->setSpacing(8);

    m_quitButton = new QPushButton("QUIT", this);
    m_crunchButton = new QPushButton("Crunch", this);
    m_blockCopyButton = new QPushButton("Block Copy", this);
    m_volumeButton = new QPushButton("Volume", this);
    m_renameButton = new QPushButton("Rename", this);
    m_attributesButton = new QPushButton("Attributes", this);
    m_newImageButton = new QPushButton("New Image", this);

    bottomButtons->addWidget(m_quitButton);
    bottomButtons->addStretch(1);
    bottomButtons->addWidget(m_crunchButton);
    bottomButtons->addWidget(m_blockCopyButton);
    bottomButtons->addWidget(m_volumeButton);
    bottomButtons->addWidget(m_renameButton);
    bottomButtons->addWidget(m_attributesButton);
    bottomButtons->addWidget(m_newImageButton);
    root->addLayout(bottomButtons);

    m_statusLabel = new QLabel("Ready", this);
    root->addWidget(m_statusLabel);
}

void AdamImageManagerDialog::setupMenus()
{
    QMenu *fileMenu = m_menuBar->addMenu("&File");
    m_actOpen = fileMenu->addAction("Open image...");
    m_actSave = fileMenu->addAction("Save image");
    m_actSaveAs = fileMenu->addAction("Save image as...");
    fileMenu->addSeparator();
    m_actNew = fileMenu->addAction("New image...");
    fileMenu->addSeparator();
    m_actClose = fileMenu->addAction("Close");
}

void AdamImageManagerDialog::setupConnections()
{
    connect(m_aboutButton, &QPushButton::clicked, this, [this]() {
        QMessageBox::about(this,
                           "About ADAM Image Manager",
                           "Qt example dialog for ADAMP_EMU.\n\n"
                           "Layout inspired by the classic ADAM Image Manager UI.\n"
                           "The backend interface is intentionally separated so you can wire in\n"
                           "your own DSK/DDP/EOS/CP-M image code without turning the dialog into spaghetti.");
    });

    connect(m_openButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onOpenImage);
    connect(m_pcButton, &QPushButton::clicked, this, &AdamImageManagerDialog::refreshPcView);
    connect(m_upButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onNavigatePcUp);
    connect(m_browsePcButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onBrowsePcFolder);

    connect(m_importButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onImportToImage);
    connect(m_exportButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onExportToPc);
    connect(m_deleteButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onDeleteEntry);

    connect(m_quitButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onCloseRequested);
    connect(m_crunchButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onCrunchImage);
    connect(m_blockCopyButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onBlockCopy);
    connect(m_volumeButton, &QPushButton::clicked, this, [this]() {
        if (!m_backend) {
            setStatusMessage("No image backend attached.");
            return;
        }

        bool ok = false;
        const QString newName = QInputDialog::getText(this,
                                                      "Volume name",
                                                      "Volume name:",
                                                      QLineEdit::Normal,
                                                      m_backend->volumeName(),
                                                      &ok).trimmed();
        if (!ok || newName.isEmpty())
            return;

        m_volumeNameEdit->setText(newName);
        setStatusMessage("Volume name changed in UI. Persist it in your backend implementation.");
    });
    connect(m_renameButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onRenameEntry);
    connect(m_attributesButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onAttributesEntry);
    connect(m_newImageButton, &QPushButton::clicked, this, &AdamImageManagerDialog::onNewImage);

    connect(m_pcList, &QListWidget::itemDoubleClicked, this, &AdamImageManagerDialog::onPcItemDoubleClicked);
    connect(m_pcList, &QListWidget::itemSelectionChanged, this, &AdamImageManagerDialog::onPcSelectionChanged);
    connect(m_imageList, &QListWidget::itemSelectionChanged, this, &AdamImageManagerDialog::onImageSelectionChanged);

    connect(m_actOpen, &QAction::triggered, this, &AdamImageManagerDialog::onOpenImage);
    connect(m_actSave, &QAction::triggered, this, &AdamImageManagerDialog::onSaveImage);
    connect(m_actSaveAs, &QAction::triggered, this, &AdamImageManagerDialog::onSaveImageAs);
    connect(m_actNew, &QAction::triggered, this, &AdamImageManagerDialog::onNewImage);
    connect(m_actClose, &QAction::triggered, this, &AdamImageManagerDialog::onCloseRequested);
}

void AdamImageManagerDialog::applyPalette()
{
    setStyleSheet(
        "QDialog { background: #000000; color: #00ff52; }"
        "QMenuBar, QMenu { background: #101010; color: #e8e8e8; }"
        "QMenu::item:selected { background: #2a4f7a; }"
        "QPushButton { background: #d8d8d8; color: black; border: 1px solid #404040; padding: 4px 10px; }"
        "QPushButton:disabled { color: #808080; background: #bcbcbc; }"
        "QLineEdit, QListWidget { background: #efefef; color: black; border: 2px solid #9a9a9a; }"
        "QLabel { color: #00ff52; }"
    );
}

void AdamImageManagerDialog::refreshPcView()
{
    QDir dir(m_pcRootPath);
    if (!dir.exists())
        dir = QDir::home();

    m_pcRootPath = dir.absolutePath();
    m_pcList->clear();

    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                    QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &fi : entries) {
        QString label;
        if (fi.isDir()) {
            label = QString("[%1]").arg(fi.fileName());
        } else {
            label = QString("%1  (%2 bytes)").arg(fi.fileName()).arg(fi.size());
        }

        QListWidgetItem *item = new QListWidgetItem(label, m_pcList);
        item->setData(Qt::UserRole, fi.absoluteFilePath());
        item->setData(Qt::UserRole + 1, fi.isDir());
    }

    setStatusMessage(QString("PC folder: %1").arg(m_pcRootPath));
    refreshState();
}

void AdamImageManagerDialog::refreshImageView()
{
    m_imageList->clear();

    if (!m_backend) {
        m_volumeNameEdit->clear();
        m_filenameValue->setText("No image loaded");
        m_dirSizeValue->setText("0 / 0");
        m_blocksLeftValue->setText("0");
        refreshState();
        return;
    }

    const QVector<AdamImageEntry> items = m_backend->entries();
    for (const AdamImageEntry &entry : items) {
        const QString display = QString("%1   %2   %3 blk   start %4%5")
                                    .arg(cleanEntryDisplayName(entry), -16)
                                    .arg(entry.type, -6)
                                    .arg(entry.blocks, 4)
                                    .arg(entry.startBlock, 4)
                                    .arg(entry.locked ? "   [L]" : "");

        QListWidgetItem *item = new QListWidgetItem(display, m_imageList);
        item->setData(Qt::UserRole, entry.name);
        item->setToolTip(QString("%1\nType: %2\nBlocks: %3\nBytes: %4")
                             .arg(entry.name)
                             .arg(entry.type)
                             .arg(entry.blocks)
                             .arg(entry.sizeBytes));
    }

    m_volumeNameEdit->setText(m_backend->volumeName());
    m_filenameValue->setText(QFileInfo(m_backend->imagePath()).fileName());
    m_dirSizeValue->setText(QString("%1 / %2")
                                .arg(items.size())
                                .arg(m_backend->totalBlocks()));
    m_blocksLeftValue->setText(QString::number(qMax(0, m_backend->totalBlocks() - m_backend->usedBlocks())));
    refreshState();
}

void AdamImageManagerDialog::refreshState()
{
    const bool hasBackend = (m_backend != nullptr);
    const bool hasImageSelection = !currentImageEntryName().isEmpty();
    const bool hasPcSelection = !m_pcList->selectedItems().isEmpty();
    const bool pcSelectionIsDir = hasPcSelection && m_pcList->selectedItems().first()->data(Qt::UserRole + 1).toBool();

    m_actSave->setEnabled(hasBackend);
    m_actSaveAs->setEnabled(hasBackend);

    m_importButton->setEnabled(hasBackend && hasPcSelection && !pcSelectionIsDir);
    m_exportButton->setEnabled(hasBackend && hasImageSelection);
    m_deleteButton->setEnabled(hasBackend && hasImageSelection);
    m_crunchButton->setEnabled(hasBackend);
    m_blockCopyButton->setEnabled(hasBackend && hasImageSelection);
    m_volumeButton->setEnabled(hasBackend);
    m_renameButton->setEnabled(hasBackend && hasImageSelection);
    m_attributesButton->setEnabled(hasBackend && hasImageSelection);
}

void AdamImageManagerDialog::setStatusMessage(const QString &message)
{
    m_statusLabel->setText(message);
}

QString AdamImageManagerDialog::cleanEntryDisplayName(const AdamImageEntry &entry) const
{
    return entry.name.trimmed();
}

QString AdamImageManagerDialog::currentImageEntryName() const
{
    QListWidgetItem *item = m_imageList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

bool AdamImageManagerDialog::ensureBackend(const char *actionName)
{
    if (m_backend)
        return true;

    QMessageBox::information(this,
                             "Backend missing",
                             QString("%1 needs an AdamImageManagerBackend implementation.\n"
                                     "The dialog is ready; the filesystem brain still needs coffee.")
                                 .arg(QString::fromLatin1(actionName)));
    return false;
}

void AdamImageManagerDialog::onOpenImage()
{
    const QString filePath = QFileDialog::getOpenFileName(this,
                                                          "Open ADAM image",
                                                          m_pcRootPath,
                                                          "ADAM images (*.dsk *.ddp *.img *.adm);;All files (*.*)");
    if (filePath.isEmpty())
        return;

    if (!ensureBackend("Open image")) {
        emit requestMountImage(filePath);
        return;
    }

    QString error;
    if (!m_backend->openImage(filePath, &error)) {
        QMessageBox::warning(this, "Open failed", error.isEmpty() ? "Unable to open image." : error);
        return;
    }

    refreshImageView();
    setStatusMessage(QString("Loaded image: %1").arg(QFileInfo(filePath).fileName()));
}

void AdamImageManagerDialog::onSaveImage()
{
    if (!ensureBackend("Save image"))
        return;

    QString error;
    if (!m_backend->saveImage(&error)) {
        QMessageBox::warning(this, "Save failed", error.isEmpty() ? "Unable to save image." : error);
        return;
    }

    setStatusMessage("Image saved.");
}

void AdamImageManagerDialog::onSaveImageAs()
{
    if (!ensureBackend("Save image as"))
        return;

    const QString filePath = QFileDialog::getSaveFileName(this,
                                                          "Save ADAM image as",
                                                          m_backend->imagePath(),
                                                          "ADAM images (*.dsk *.ddp *.img);;All files (*.*)");
    if (filePath.isEmpty())
        return;

    QString error;
    if (!m_backend->saveImageAs(filePath, &error)) {
        QMessageBox::warning(this, "Save failed", error.isEmpty() ? "Unable to save image." : error);
        return;
    }

    refreshImageView();
    setStatusMessage(QString("Saved image as: %1").arg(QFileInfo(filePath).fileName()));
}

void AdamImageManagerDialog::onNewImage()
{
    bool ok = false;
    const QString volume = QInputDialog::getText(this,
                                                 "New image",
                                                 "Volume name:",
                                                 QLineEdit::Normal,
                                                 "NEWDISK",
                                                 &ok).trimmed();
    if (!ok || volume.isEmpty())
        return;

    const int blocks = QInputDialog::getInt(this,
                                            "New image",
                                            "Total blocks:",
                                            320,
                                            8,
                                            65535,
                                            1,
                                            &ok);
    if (!ok)
        return;

    const QString filePath = QFileDialog::getSaveFileName(this,
                                                          "Create ADAM image",
                                                          QDir(m_pcRootPath).filePath(volume + ".dsk"),
                                                          "ADAM images (*.dsk *.ddp *.img);;All files (*.*)");
    if (filePath.isEmpty())
        return;

    if (!ensureBackend("Create new image")) {
        emit requestCreateBlankImage(filePath, volume, blocks);
        return;
    }

    QString error;
    if (!m_backend->createNewImage(filePath, volume, blocks, &error)) {
        QMessageBox::warning(this, "Create failed", error.isEmpty() ? "Unable to create image." : error);
        return;
    }

    refreshImageView();
    setStatusMessage(QString("New image created: %1").arg(QFileInfo(filePath).fileName()));
}

void AdamImageManagerDialog::onBrowsePcFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Choose PC folder", m_pcRootPath);
    if (dir.isEmpty())
        return;

    m_pcRootPath = dir;
    refreshPcView();
}

void AdamImageManagerDialog::onNavigatePcUp()
{
    QDir dir(m_pcRootPath);
    if (dir.cdUp()) {
        m_pcRootPath = dir.absolutePath();
        refreshPcView();
    }
}

void AdamImageManagerDialog::onImportToImage()
{
    if (!ensureBackend("Import to image"))
        return;

    QListWidgetItem *item = m_pcList->currentItem();
    if (!item)
        return;

    const QString filePath = item->data(Qt::UserRole).toString();
    const bool isDir = item->data(Qt::UserRole + 1).toBool();
    if (isDir)
        return;

    QString error;
    if (!m_backend->importFromPc({filePath}, &error)) {
        QMessageBox::warning(this, "Import failed", error.isEmpty() ? "Unable to import file." : error);
        return;
    }

    refreshImageView();
    setStatusMessage(QString("Imported %1 into image.").arg(QFileInfo(filePath).fileName()));
}

void AdamImageManagerDialog::onExportToPc()
{
    if (!ensureBackend("Export to PC"))
        return;

    const QString entryName = currentImageEntryName();
    if (entryName.isEmpty())
        return;

    QString error;
    if (!m_backend->exportToPc(entryName, m_pcRootPath, &error)) {
        QMessageBox::warning(this, "Export failed", error.isEmpty() ? "Unable to export entry." : error);
        return;
    }

    refreshPcView();
    setStatusMessage(QString("Exported %1 to %2").arg(entryName, m_pcRootPath));
}

void AdamImageManagerDialog::onDeleteEntry()
{
    if (!ensureBackend("Delete entry"))
        return;

    const QString entryName = currentImageEntryName();
    if (entryName.isEmpty())
        return;

    if (QMessageBox::question(this,
                              "Delete entry",
                              QString("Delete '%1' from image?").arg(entryName)) != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_backend->removeEntry(entryName, &error)) {
        QMessageBox::warning(this, "Delete failed", error.isEmpty() ? "Unable to delete entry." : error);
        return;
    }

    refreshImageView();
    setStatusMessage(QString("Deleted %1").arg(entryName));
}

void AdamImageManagerDialog::onRenameEntry()
{
    if (!ensureBackend("Rename entry"))
        return;

    const QString oldName = currentImageEntryName();
    if (oldName.isEmpty())
        return;

    bool ok = false;
    const QString newName = QInputDialog::getText(this,
                                                  "Rename entry",
                                                  "New name:",
                                                  QLineEdit::Normal,
                                                  oldName,
                                                  &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == oldName)
        return;

    QString error;
    if (!m_backend->renameEntry(oldName, newName, &error)) {
        QMessageBox::warning(this, "Rename failed", error.isEmpty() ? "Unable to rename entry." : error);
        return;
    }

    refreshImageView();
    setStatusMessage(QString("Renamed %1 to %2").arg(oldName, newName));
}

void AdamImageManagerDialog::onAttributesEntry()
{
    if (!ensureBackend("Change attributes"))
        return;

    const QString entryName = currentImageEntryName();
    if (entryName.isEmpty())
        return;

    const bool currentlyLocked = m_imageList->currentItem()->text().contains("[L]");

    QString error;
    if (!m_backend->setEntryLocked(entryName, !currentlyLocked, &error)) {
        QMessageBox::warning(this, "Attributes failed", error.isEmpty() ? "Unable to update attributes." : error);
        return;
    }

    refreshImageView();
    setStatusMessage(QString("%1 is now %2")
                         .arg(entryName, !currentlyLocked ? "locked" : "unlocked"));
}

void AdamImageManagerDialog::onCrunchImage()
{
    if (!ensureBackend("Crunch image"))
        return;

    QString error;
    if (!m_backend->crunchImage(&error)) {
        QMessageBox::warning(this, "Crunch failed", error.isEmpty() ? "Unable to crunch image." : error);
        return;
    }

    refreshImageView();
    setStatusMessage("Image crunched.");
}

void AdamImageManagerDialog::onBlockCopy()
{
    if (!ensureBackend("Block copy"))
        return;

    const QString source = currentImageEntryName();
    if (source.isEmpty())
        return;

    bool ok = false;
    const QString dest = QInputDialog::getText(this,
                                               "Block copy",
                                               "Destination entry name:",
                                               QLineEdit::Normal,
                                               source + "_COPY",
                                               &ok).trimmed();
    if (!ok || dest.isEmpty())
        return;

    QString error;
    if (!m_backend->blockCopyToImage(source, dest, &error)) {
        QMessageBox::warning(this, "Block copy failed", error.isEmpty() ? "Unable to copy entry." : error);
        return;
    }

    refreshImageView();
    setStatusMessage(QString("Copied %1 to %2").arg(source, dest));
}

void AdamImageManagerDialog::onCloseRequested()
{
    close();
}

void AdamImageManagerDialog::onPcSelectionChanged()
{
    refreshState();
}

void AdamImageManagerDialog::onImageSelectionChanged()
{
    refreshState();
}

void AdamImageManagerDialog::onPcItemDoubleClicked(QListWidgetItem *item)
{
    if (!item)
        return;

    if (!item->data(Qt::UserRole + 1).toBool())
        return;

    m_pcRootPath = item->data(Qt::UserRole).toString();
    refreshPcView();
}
