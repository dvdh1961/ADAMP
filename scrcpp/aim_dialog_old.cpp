#include "aim_dialog.h"
#include "customfiledialog.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

AimDialog::AimDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("ADAM Image Manager"));
    resize(1040, 620);
    setModal(false);

    setupUi();
    setupMenus();
    setupConnections();
    applyStyle();
    refreshPcView();
    refreshImageView();
    refreshInfoBar();
    setStatusText(tr("Ready."));
}

void AimDialog::setPcRootPath(const QString &path)
{
    m_pcRootPath = QDir::cleanPath(path);
    refreshPcView();
}

void AimDialog::setDiskRootPath(const QString &path)
{
    m_diskRootPath = QDir::cleanPath(path);
}

void AimDialog::setTapeRootPath(const QString &path)
{
    m_tapeRootPath = QDir::cleanPath(path);
}

void AimDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    m_menuBar = new QMenuBar(this);
    mainLayout->setMenuBar(m_menuBar);

    auto *topButtons = new QHBoxLayout;
    topButtons->setSpacing(6);

    m_aboutButton     = new QPushButton(tr("About"), this);
    m_openDskButton   = new QPushButton(tr("Open DSK"), this);
    m_openDdpButton   = new QPushButton(tr("Open DDP"), this);
    m_pcButton        = new QPushButton(tr("PC"), this);
    m_upButton        = new QPushButton(tr("Up"), this);
    m_browsePcButton  = new QPushButton(tr("..."), this);

    topButtons->addWidget(m_aboutButton);
    topButtons->addSpacing(10);
    topButtons->addWidget(m_openDskButton);
    topButtons->addWidget(m_openDdpButton);
    topButtons->addSpacing(10);
    topButtons->addWidget(m_pcButton);
    topButtons->addStretch();
    topButtons->addWidget(m_upButton);
    topButtons->addWidget(m_browsePcButton);

    mainLayout->addLayout(topButtons);

    auto *infoBar = new QGridLayout;
    infoBar->setHorizontalSpacing(8);
    infoBar->setVerticalSpacing(6);

    auto *volumeLabel = new QLabel(tr("Volume Name:"), this);
    m_volumeNameEdit = new QLineEdit(this);
    m_volumeNameEdit->setPlaceholderText(tr("Volume"));

    auto *dirLabel = new QLabel(tr("Dir Size:"), this);
    m_dirSizeValue = new QLabel(QStringLiteral("-"), this);
    m_dirSizeValue->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_dirSizeValue->setMinimumWidth(90);

    auto *fileLabel = new QLabel(tr("Filename:"), this);
    m_filenameValue = new QLabel(QStringLiteral("-"), this);
    m_filenameValue->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

    infoBar->addWidget(volumeLabel,      0, 0);
    infoBar->addWidget(m_volumeNameEdit, 0, 1);
    infoBar->addWidget(dirLabel,         0, 2);
    infoBar->addWidget(m_dirSizeValue,   0, 3);
    infoBar->addWidget(fileLabel,        0, 4);
    infoBar->addWidget(m_filenameValue,  0, 5);
    infoBar->setColumnStretch(1, 1);
    infoBar->setColumnStretch(5, 2);

    mainLayout->addLayout(infoBar);

    auto *center = new QHBoxLayout;
    center->setSpacing(8);

    m_imageGroup = new QGroupBox(tr("Image Directory"), this);
    auto *imageLayout = new QVBoxLayout(m_imageGroup);
    imageLayout->setContentsMargins(6, 10, 6, 6);
    m_imageList = new QListWidget(m_imageGroup);
    imageLayout->addWidget(m_imageList);

    auto *middleButtons = new QVBoxLayout;
    middleButtons->setSpacing(6);
    middleButtons->addStretch();

    m_importButton = new QPushButton(tr("->"), this);
    m_exportButton = new QPushButton(tr("<-"), this);
    m_deleteButton = new QPushButton(tr("Del"), this);

    m_importButton->setToolTip(tr("Import selected PC file into image"));
    m_exportButton->setToolTip(tr("Export selected image file to PC folder"));
    m_deleteButton->setToolTip(tr("Delete selected image entry"));

    middleButtons->addWidget(m_importButton);
    middleButtons->addWidget(m_exportButton);
    middleButtons->addWidget(m_deleteButton);
    middleButtons->addStretch();

    m_pcGroup = new QGroupBox(tr("PC Folder"), this);
    auto *pcLayout = new QVBoxLayout(m_pcGroup);
    pcLayout->setContentsMargins(6, 10, 6, 6);
    m_pcList = new QListWidget(m_pcGroup);
    pcLayout->addWidget(m_pcList);

    center->addWidget(m_imageGroup, 1);
    center->addLayout(middleButtons);
    center->addWidget(m_pcGroup, 1);

    mainLayout->addLayout(center, 1);

    auto *blocksLayout = new QHBoxLayout;
    blocksLayout->setSpacing(6);
    blocksLayout->addWidget(new QLabel(tr("Blocks Left:"), this));
    m_blocksLeftValue = new QLabel(QStringLiteral("-"), this);
    m_blocksLeftValue->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_blocksLeftValue->setMinimumWidth(90);
    blocksLayout->addWidget(m_blocksLeftValue);
    blocksLayout->addStretch();
    mainLayout->addLayout(blocksLayout);

    auto *bottomButtons = new QHBoxLayout;
    bottomButtons->setSpacing(6);

    m_quitButton       = new QPushButton(tr("Quit"), this);
    m_crunchButton     = new QPushButton(tr("Crunch"), this);
    m_blockCopyButton  = new QPushButton(tr("Block Copy"), this);
    m_volumeButton     = new QPushButton(tr("Volume"), this);
    m_renameButton     = new QPushButton(tr("Rename"), this);
    m_attributesButton = new QPushButton(tr("Attributes"), this);
    m_newImageButton   = new QPushButton(tr("New Image"), this);

    bottomButtons->addWidget(m_quitButton);
    bottomButtons->addWidget(m_crunchButton);
    bottomButtons->addWidget(m_blockCopyButton);
    bottomButtons->addStretch();
    bottomButtons->addWidget(m_volumeButton);
    bottomButtons->addWidget(m_renameButton);
    bottomButtons->addWidget(m_attributesButton);
    bottomButtons->addWidget(m_newImageButton);

    mainLayout->addLayout(bottomButtons);

    m_status = new QLabel(this);
    m_status->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    mainLayout->addWidget(m_status);
}

void AimDialog::setupMenus()
{
    QMenu *fileMenu = m_menuBar->addMenu(tr("&File"));
    m_actOpenDsk = fileMenu->addAction(tr("Open DSK..."));
    m_actOpenDdp = fileMenu->addAction(tr("Open DDP..."));
    m_actRefreshPc = fileMenu->addAction(tr("Refresh PC Folder"));
    fileMenu->addSeparator();
    m_actClose = fileMenu->addAction(tr("Close"));
}

void AimDialog::setupConnections()
{
    connect(m_aboutButton, &QPushButton::clicked, this, [this]() {
        QMessageBox::about(this,
                           tr("About ADAM Image Manager"),
                           tr("ADAM Image Manager dialog for ADAMP_EMU.\n\n"
                              "This rebuild restores the full classic layout while keeping the same Qt style as the emulator."));
    });

    connect(m_openDskButton, &QPushButton::clicked, this, &AimDialog::onOpenDsk);
    connect(m_openDdpButton, &QPushButton::clicked, this, &AimDialog::onOpenDdp);
    connect(m_pcButton,      &QPushButton::clicked, this, &AimDialog::onRefreshPc);
    connect(m_upButton,      &QPushButton::clicked, this, &AimDialog::onNavigateUp);
    connect(m_browsePcButton,&QPushButton::clicked, this, &AimDialog::onBrowsePc);

    connect(m_actOpenDsk,    &QAction::triggered, this, &AimDialog::onOpenDsk);
    connect(m_actOpenDdp,    &QAction::triggered, this, &AimDialog::onOpenDdp);
    connect(m_actRefreshPc,  &QAction::triggered, this, &AimDialog::onRefreshPc);
    connect(m_actClose,      &QAction::triggered, this, &QDialog::close);

    connect(m_pcList, &QListWidget::itemDoubleClicked, this, &AimDialog::onPcItemDoubleClicked);
    connect(m_pcList, &QListWidget::itemSelectionChanged, this, &AimDialog::onPcSelectionChanged);
    connect(m_imageList, &QListWidget::itemSelectionChanged, this, &AimDialog::onImageSelectionChanged);

    connect(m_importButton, &QPushButton::clicked, this, &AimDialog::onImportToImage);
    connect(m_exportButton, &QPushButton::clicked, this, &AimDialog::onExportToPc);
    connect(m_deleteButton, &QPushButton::clicked, this, &AimDialog::onDeleteEntry);

    connect(m_quitButton, &QPushButton::clicked, this, &QDialog::close);
    connect(m_crunchButton, &QPushButton::clicked, this, &AimDialog::onCrunchImage);
    connect(m_blockCopyButton, &QPushButton::clicked, this, &AimDialog::onBlockCopy);
    connect(m_volumeButton, &QPushButton::clicked, this, &AimDialog::onVolumeLabelClicked);
    connect(m_renameButton, &QPushButton::clicked, this, &AimDialog::onRenameEntry);
    connect(m_attributesButton, &QPushButton::clicked, this, &AimDialog::onAttributesEntry);
    connect(m_newImageButton, &QPushButton::clicked, this, &AimDialog::onNewImage);
}

void AimDialog::applyStyle()
{
    setStyleSheet(QString());

    const auto buttons = findChildren<QPushButton*>();
    for (QPushButton *button : buttons) {
        if (!button) {
            continue;
        }
        button->setMinimumHeight(28);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    m_importButton->setMinimumWidth(52);
    m_exportButton->setMinimumWidth(52);
    m_deleteButton->setMinimumWidth(52);

    const auto labels = findChildren<QLabel*>();
    for (QLabel *label : labels) {
        if (!label) {
            continue;
        }
        label->setTextInteractionFlags(Qt::NoTextInteraction);
    }

    if (m_imageList) {
        m_imageList->setSelectionMode(QAbstractItemView::SingleSelection);
        m_imageList->setUniformItemSizes(true);
    }
    if (m_pcList) {
        m_pcList->setSelectionMode(QAbstractItemView::SingleSelection);
        m_pcList->setUniformItemSizes(true);
    }
}

void AimDialog::refreshPcView()
{
    if (m_pcRootPath.isEmpty()) {
        m_pcRootPath = QDir::homePath();
    }

    QDir dir(m_pcRootPath);
    if (!dir.exists()) {
        dir = QDir(QDir::homePath());
        m_pcRootPath = dir.absolutePath();
    }

    m_pcList->clear();

    const QFileInfoList list = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                 QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);

    for (const QFileInfo &info : list) {
        QString text = info.fileName();
        if (info.isDir()) {
            text = QStringLiteral("[%1]").arg(text);
        }
        auto *item = new QListWidgetItem(text, m_pcList);
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setData(Qt::UserRole + 1, info.isDir());
        item->setToolTip(info.absoluteFilePath());
    }

    m_pcGroup->setTitle(tr("PC Folder: %1").arg(QFileInfo(m_pcRootPath).fileName().isEmpty()
                                                 ? m_pcRootPath
                                                 : QFileInfo(m_pcRootPath).fileName()));

    refreshInfoBar();
}

void AimDialog::refreshImageView()
{
    m_imageList->clear();

    if (m_currentImagePath.isEmpty()) {
        m_imageGroup->setTitle(tr("Image Directory"));
        refreshInfoBar();
        return;
    }

    addPlaceholderImageEntries(m_currentImagePath);
    m_imageGroup->setTitle(tr("Image: %1").arg(QFileInfo(m_currentImagePath).fileName()));
    refreshInfoBar();
}

void AimDialog::refreshInfoBar()
{
    const QFileInfo imageInfo(m_currentImagePath);

    if (m_currentImagePath.isEmpty()) {
        m_dirSizeValue->setText(QStringLiteral("0"));
        m_filenameValue->setText(QStringLiteral("-"));
        m_blocksLeftValue->setText(QStringLiteral("-"));
        if (m_volumeNameEdit->text().isEmpty()) {
            m_volumeNameEdit->setText(QStringLiteral("-"));
        }
        return;
    }

    m_dirSizeValue->setText(QString::number(directoryEntryCount()));

    QListWidgetItem *currentItem = m_imageList->currentItem();
    if (currentItem) {
        m_filenameValue->setText(currentItem->text());
    } else {
        m_filenameValue->setText(imageInfo.fileName());
    }

    const qint64 pseudoBlocks = qMax<qint64>(0, 320 - directoryEntryCount() * 4);
    m_blocksLeftValue->setText(QString::number(pseudoBlocks));

    if (m_volumeNameEdit->text().trimmed().isEmpty() || m_volumeNameEdit->text() == QStringLiteral("-")) {
        m_volumeNameEdit->setText(imageInfo.completeBaseName().left(16).toUpper());
    }
}

void AimDialog::setStatusText(const QString &text)
{
    if (m_status) {
        m_status->setText(text);
    }
}

void AimDialog::setCurrentImagePath(const QString &filePath)
{
    m_currentImagePath = filePath;
    m_volumeNameEdit->setText(QFileInfo(filePath).completeBaseName().left(16).toUpper());
    refreshImageView();
}

void AimDialog::addPlaceholderImageEntries(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();

    QStringList entries;
    if (suffix == QStringLiteral("dsk") || suffix == QStringLiteral("img")) {
        entries << QStringLiteral("BOOT      SYS   08 blocks")
                << QStringLiteral("DIR       CAT   04 blocks")
                << QStringLiteral("README    TXT   03 blocks")
                << QStringLiteral("GAME      COM   16 blocks");
    } else {
        entries << QStringLiteral("TAPEHDR   DDP   02 blocks")
                << QStringLiteral("PROGRAM1  BAS   06 blocks")
                << QStringLiteral("DATAFILE  DAT   05 blocks");
    }

    for (const QString &entry : entries) {
        auto *item = new QListWidgetItem(entry, m_imageList);
        item->setToolTip(entry);
    }
}

int AimDialog::directoryEntryCount() const
{
    return m_imageList ? m_imageList->count() : 0;
}

void AimDialog::onOpenDsk()
{
    const QString startDir = m_diskRootPath.isEmpty() ? m_pcRootPath : m_diskRootPath;

    const QString file = CustomFileDialog::getOpenFileName(
        this,
        tr("Open DSK"),
        startDir,
        tr("ADAM Disk Images (*.dsk *.img);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathDisk);

    if (file.isEmpty()) {
        return;
    }

    setCurrentImagePath(file);
    setStatusText(tr("Loaded DSK image: %1").arg(QFileInfo(file).fileName()));
    emit dskSelected(file);
}

void AimDialog::onOpenDdp()
{
    const QString startDir = m_tapeRootPath.isEmpty() ? m_pcRootPath : m_tapeRootPath;

    const QString file = CustomFileDialog::getOpenFileName(
        this,
        tr("Open DDP"),
        startDir,
        tr("ADAM Tape Images (*.ddp);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathTape);

    if (file.isEmpty()) {
        return;
    }

    setCurrentImagePath(file);
    setStatusText(tr("Loaded DDP image: %1").arg(QFileInfo(file).fileName()));
    emit ddpSelected(file);
}

void AimDialog::onBrowsePc()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this,
        tr("Select PC Folder"),
        m_pcRootPath.isEmpty() ? QDir::homePath() : m_pcRootPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (folder.isEmpty()) {
        return;
    }

    m_pcRootPath = QDir::cleanPath(folder);
    refreshPcView();
    setStatusText(tr("PC folder changed to: %1").arg(m_pcRootPath));
}

void AimDialog::onNavigateUp()
{
    QDir dir(m_pcRootPath.isEmpty() ? QDir::homePath() : m_pcRootPath);
    dir.cdUp();
    m_pcRootPath = dir.absolutePath();
    refreshPcView();
    setStatusText(tr("Moved to: %1").arg(m_pcRootPath));
}

void AimDialog::onRefreshPc()
{
    refreshPcView();
    setStatusText(tr("PC folder refreshed."));
}

void AimDialog::onPcItemDoubleClicked(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    const bool isDir = item->data(Qt::UserRole + 1).toBool();
    const QString path = item->data(Qt::UserRole).toString();
    if (!isDir) {
        return;
    }

    m_pcRootPath = path;
    refreshPcView();
    setStatusText(tr("Opened folder: %1").arg(path));
}

void AimDialog::onPcSelectionChanged()
{
    QListWidgetItem *item = m_pcList->currentItem();
    if (!item) {
        return;
    }
    m_filenameValue->setText(item->text());
}

void AimDialog::onImageSelectionChanged()
{
    QListWidgetItem *item = m_imageList->currentItem();
    if (!item) {
        return;
    }
    m_filenameValue->setText(item->text());
}

void AimDialog::onImportToImage()
{
    if (m_currentImagePath.isEmpty()) {
        QMessageBox::information(this, tr("Import"), tr("Open a DSK or DDP image first."));
        return;
    }

    QListWidgetItem *item = m_pcList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Import"), tr("Select a PC file first."));
        return;
    }

    if (item->data(Qt::UserRole + 1).toBool()) {
        QMessageBox::information(this, tr("Import"), tr("Folders cannot be imported yet."));
        return;
    }

    new QListWidgetItem(QFileInfo(item->data(Qt::UserRole).toString()).fileName(), m_imageList);
    refreshInfoBar();
    setStatusText(tr("Imported placeholder entry from PC into image list."));
}

void AimDialog::onExportToPc()
{
    QListWidgetItem *item = m_imageList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Export"), tr("Select an image entry first."));
        return;
    }

    setStatusText(tr("Export placeholder for '%1' to '%2'.").arg(item->text(), m_pcRootPath));
}

void AimDialog::onDeleteEntry()
{
    QListWidgetItem *item = m_imageList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Delete"), tr("Select an image entry first."));
        return;
    }

    delete item;
    refreshInfoBar();
    setStatusText(tr("Image entry removed from placeholder list."));
}

void AimDialog::onRenameEntry()
{
    QListWidgetItem *item = m_imageList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Rename"), tr("Select an image entry first."));
        return;
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(this,
                                                  tr("Rename entry"),
                                                  tr("New name:"),
                                                  QLineEdit::Normal,
                                                  item->text(),
                                                  &ok).trimmed();
    if (!ok || newName.isEmpty()) {
        return;
    }

    item->setText(newName);
    refreshInfoBar();
    setStatusText(tr("Entry renamed."));
}

void AimDialog::onAttributesEntry()
{
    QListWidgetItem *item = m_imageList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Attributes"), tr("Select an image entry first."));
        return;
    }

    QMessageBox::information(this,
                             tr("Attributes"),
                             tr("Attributes editor placeholder for '%1'.\n\n"
                                "This is where locked/read-only flags can be implemented next.").arg(item->text()));
}

void AimDialog::onCrunchImage()
{
    QMessageBox::information(this,
                             tr("Crunch"),
                             tr("Crunch placeholder.\n\n"
                                "Hook this button to your real DSK/DDP free-block compaction logic."));
    setStatusText(tr("Crunch requested."));
}

void AimDialog::onBlockCopy()
{
    QMessageBox::information(this,
                             tr("Block Copy"),
                             tr("Block Copy placeholder.\n\n"
                                "Hook this into your future image sector/block copy implementation."));
    setStatusText(tr("Block Copy requested."));
}

void AimDialog::onVolumeLabelClicked()
{
    bool ok = false;
    const QString current = m_volumeNameEdit->text().trimmed();
    const QString newLabel = QInputDialog::getText(this,
                                                   tr("Volume label"),
                                                   tr("Volume name:"),
                                                   QLineEdit::Normal,
                                                   current,
                                                   &ok).trimmed();
    if (!ok || newLabel.isEmpty()) {
        return;
    }

    m_volumeNameEdit->setText(newLabel.left(16).toUpper());
    setStatusText(tr("Volume label updated in dialog."));
}

void AimDialog::onNewImage()
{
    bool ok = false;
    const QString volume = QInputDialog::getText(this,
                                                 tr("New Image"),
                                                 tr("Volume name:"),
                                                 QLineEdit::Normal,
                                                 QStringLiteral("NEWDISK"),
                                                 &ok).trimmed();
    if (!ok || volume.isEmpty()) {
        return;
    }

    const QString baseDir = m_diskRootPath.isEmpty() ? m_pcRootPath : m_diskRootPath;
    const QString file = CustomFileDialog::getSaveFileName(
        this,
        tr("Create DSK Image"),
        baseDir,
        tr("ADAM Disk Images (*.dsk);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathDisk);

    if (file.isEmpty()) {
        return;
    }

    setCurrentImagePath(file);
    m_volumeNameEdit->setText(volume.left(16).toUpper());
    setStatusText(tr("New image placeholder created: %1").arg(QFileInfo(file).fileName()));
}
