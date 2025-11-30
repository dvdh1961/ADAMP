#include "customfiledialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTreeView>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QStringList>
#include <QHeaderView>
#include <QSettings>
#include <QCoreApplication>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileSystemModel>

QString CustomFileDialog::s_lastOpenDir;
QString CustomFileDialog::s_lastSaveDir;

CustomFileDialog::CustomFileDialog(QWidget *parent)
    : QDialog(parent), m_acceptMode(AcceptOpen)
{
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    m_model = new QFileSystemModel(this);
    m_model->setRootPath(QDir::rootPath());
    m_model->setIconProvider(&m_iconProvider);

    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    m_model->setHeaderData(0, Qt::Horizontal, "Bestandsnaam");
    m_model->setHeaderData(1, Qt::Horizontal, "Grootte");

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setFont(QFont("Roboto", 10));

    m_treeView->setIconSize(QSize(32, 32));

    m_treeView->setRootIndex(m_model->index(QDir::homePath()));

    m_treeView->setColumnHidden(1, false);
    m_treeView->setColumnHidden(2, true);
    m_treeView->setColumnHidden(3, true);
    m_treeView->header()->setVisible(true);

    m_treeView->setSortingEnabled(true);
    m_treeView->sortByColumn(0, Qt::AscendingOrder);

    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_treeView->setColumnWidth(0, 500);
    m_treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    m_treeView->setAlternatingRowColors(true);

    QColor m_rowLight = QColor(60, 60, 60);
    QColor m_rowDark  = QColor(40, 40, 40);
    QColor m_selActiveBg = QColor(51, 153, 255);
    QColor m_selActiveText = QColor(Qt::black);
    QColor m_selInactiveBg = QColor(217, 217, 217);
    QColor m_selInactiveText = QColor(Qt::black);

    m_treeView->setStyleSheet(QString(
                                  "QTreeView {"
                                  "    background-color: %1;"
                                  "    border: none;"
                                  "}"
                                  "QTreeView::item:alternate {"
                                  "    background-color: %2;"
                                  "}"
                                  "QTreeView::item:selected:active {"
                                  "    background-color: %3;"
                                  "    color: %4;"
                                  "}"
                                  "QTreeView::item:selected:!active {"
                                  "    background-color: %5;"
                                  "    color: %6;"
                                  "}"
                                  ).arg(m_rowLight.name(),
                                       m_rowDark.name(),
                                       m_selActiveBg.name(),
                                       m_selActiveText.name(),
                                       m_selInactiveBg.name(),
                                       m_selInactiveText.name()
                                       ));
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setReadOnly(true);
    m_pathEdit->setText(QDir::homePath());

    m_fileNameEdit = new QLineEdit(this);

    m_filterComboBox = new QComboBox(this);

    m_okButton = new QPushButton(this);
    m_cancelButton = new QPushButton(this);
    m_upButton = new QPushButton(this);

    QIcon okIcon(":/images/images/OK.png");
    QIcon cancelIcon(":/images/images/CANCEL.png");
    QPixmap okPixmap(":/images/images/OK.png");
    QPixmap cancelPixmap(":/images/images/CANCEL.png");

    if (okIcon.isNull()) { qWarning() << "CustomFileDialog: Kon OK.png niet laden."; }
    if (cancelIcon.isNull()) { qWarning() << "CustomFileDialog: Kon CANCEL.png niet laden."; }

    QSize okSize = okPixmap.size();
    QSize cancelSize = cancelPixmap.size();

    m_okButton->setIcon(okIcon);
    m_okButton->setIconSize(okSize);
    m_okButton->setFixedSize(okSize);
    m_okButton->setText("");
    m_okButton->setFlat(true);
    m_okButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    m_cancelButton->setIcon(cancelIcon);
    m_cancelButton->setIconSize(cancelSize);
    m_cancelButton->setFixedSize(cancelSize);
    m_cancelButton->setText("");
    m_cancelButton->setFlat(true);
    m_cancelButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    QIcon upIcon(":/images/images/UP.png");
    m_upButton->setIcon(upIcon.isNull() ? QIcon::fromTheme("go-up") : upIcon);
    m_upButton->setIconSize(QSize(18, 18));
    m_upButton->setText("");
    m_upButton->setToolTip("Ga één map omhoog tot de ingestelde hoofdmap");
    m_upButton->setFixedSize(QSize(28, 28));
    m_upButton->setFlat(true);

    m_upButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 1px; padding-left: 1px; }"
        );

    m_createDirButton = new QPushButton(this);
    m_deleteDirButton = new QPushButton(this);

    QIcon createDirIcon(":/images/images/NEWDIR.png");
    QIcon deleteDirIcon(":/images/images/DELDIR.png");

    QSize iconSize(18, 18);
    QSize fixedSize(28, 28);
    QString buttonStyle = "QPushButton { border: none; background: transparent; }"
                          "QPushButton:pressed { padding-top: 1px; padding-left: 1px; }";

    // CreateDir Button
    m_createDirButton->setIcon(createDirIcon.isNull() ? QIcon::fromTheme("folder-new") : createDirIcon);
    m_createDirButton->setIconSize(iconSize);
    m_createDirButton->setFixedSize(fixedSize);
    m_createDirButton->setText("");
    m_createDirButton->setFlat(true);
    m_createDirButton->setToolTip(tr("Maak een nieuwe map aan in de huidige locatie"));
    m_createDirButton->setStyleSheet(buttonStyle);

    // DeleteDir Button
    m_deleteDirButton->setIcon(deleteDirIcon.isNull() ? QIcon::fromTheme("edit-delete") : deleteDirIcon);
    m_deleteDirButton->setIconSize(iconSize);
    m_deleteDirButton->setFixedSize(fixedSize);
    m_deleteDirButton->setText("");
    m_deleteDirButton->setFlat(true);
    m_deleteDirButton->setToolTip(tr("Verwijder de geselecteerde map of bestand (let op: permanent)"));
    m_deleteDirButton->setStyleSheet(buttonStyle);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    mainLayout->addWidget(new QLabel("Map:", this));

    QHBoxLayout *pathAndButtonLayout = new QHBoxLayout();
    pathAndButtonLayout->setContentsMargins(0, 0, 0, 0);

    pathAndButtonLayout->addWidget(m_pathEdit, 1);

    pathAndButtonLayout->addWidget(m_createDirButton);
    pathAndButtonLayout->addWidget(m_deleteDirButton);
    pathAndButtonLayout->addWidget(m_upButton);

    mainLayout->addLayout(pathAndButtonLayout);

    mainLayout->addWidget(m_treeView, 1);

    QGridLayout *formLayout = new QGridLayout();
    formLayout->addWidget(new QLabel("Bestandsnaam:", this), 0, 0);
    formLayout->addWidget(m_fileNameEdit, 0, 1);
    formLayout->addWidget(new QLabel("Filter:", this), 1, 0);
    formLayout->addWidget(m_filterComboBox, 1, 1);
    mainLayout->addLayout(formLayout);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_treeView, &QTreeView::clicked, this, &CustomFileDialog::onTreeViewClicked);
    connect(m_treeView, &QTreeView::doubleClicked, this, &CustomFileDialog::onTreeViewDoubleClicked);
    connect(m_filterComboBox, &QComboBox::currentTextChanged, this, &CustomFileDialog::onFilterChanged);
    connect(m_okButton, &QPushButton::clicked, this, &CustomFileDialog::onOkButtonClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_upButton, &QPushButton::clicked, this, &CustomFileDialog::onUpButtonClicked);
    connect(m_createDirButton, &QPushButton::clicked, this, &CustomFileDialog::onCreateDirClicked);
    connect(m_deleteDirButton, &QPushButton::clicked, this, &CustomFileDialog::onDeleteDirClicked);

    resize(700, 500);
}

QString CustomFileDialog::getSaveFileName(QWidget *parent, const QString &caption,
                                          const QString &dir, const QString &filter,
                                          QString *selectedFilter,
                                          PathType type,
                                          QFileDialog::Options options,
                                          const QString& romBaseName)
{
    CustomFileDialog dlg(parent);
    dlg.m_acceptMode = AcceptSave;
    dlg.m_pathType = type;
    dlg.setWindowTitle(caption);

    dlg.loadLastVisitedPath(dir, AcceptSave);

    dlg.setNameFilters(filter);

    if (type == PathScreenshot && dlg.m_fileNameEdit->text().isEmpty()) {

        QString base = romBaseName;
        if (base.isEmpty() || base == "No cart" || base == "state" || base == "screenshot") {
            base = "screenshot";
        }

        QDateTime now = QDateTime::currentDateTime();

        QString defaultName = QString("%1_%2.png").arg(base).arg(now.toSecsSinceEpoch());

        dlg.m_fileNameEdit->setText(defaultName);
    }

    if (dlg.exec() == QDialog::Accepted) {
        dlg.saveLastVisitedPath();
        return dlg.selectedFile();
    }
    return QString();
}

QString CustomFileDialog::getOpenFileName(QWidget *parent, const QString &caption,
                                          const QString &dir, const QString &filter,
                                          QString *selectedFilter,
                                          PathType type,
                                          QFileDialog::Options options)
{
    CustomFileDialog dlg(parent);
    dlg.m_acceptMode = AcceptOpen;
    dlg.m_pathType = type;
    dlg.setWindowTitle(caption);
    dlg.setInitialDirectory(dir);
    dlg.setNameFilters(filter);
    dlg.loadLastVisitedPath(dir, AcceptOpen);

    if (dlg.exec() == QDialog::Accepted) {
        dlg.saveLastVisitedPath();
        return dlg.selectedFile();
    }
    return QString();
}

QString CustomFileDialog::selectedFile() const
{
    return QDir(m_pathEdit->text()).filePath(m_fileNameEdit->text());
}

// customfiledialog.cpp

void CustomFileDialog::setInitialDirectory(const QString &dir)
{
    QString path = dir;
    QString fileName;

    // We gebruiken de statische (niet-persistente) variabelen voor tijdelijk geheugen:
    QString savedPath = (m_acceptMode == AcceptOpen) ? s_lastOpenDir : s_lastSaveDir;

    if (!savedPath.isEmpty() && QDir(savedPath).exists()) {
        // Gebruik het TIJDELIJK OPGESLAGEN pad als de dialoog al een keer is geopend in deze sessie
        path = savedPath;
    } else if (!dir.isEmpty()) {
        // Anders: Gebruik het pad van de MainWindow (onze root)
        QFileInfo fi(dir);

        if (fi.isDir()) {
            path = fi.absoluteFilePath();
        } else {
            path = fi.absolutePath();
            fileName = fi.fileName(); // Om de bestandsnaam in te vullen bij save/open
        }
    }

    if (!QDir(path).exists()) {
        path = QDir::homePath();
        fileName.clear();
    }

    QModelIndex index = m_model->index(path);
    if (index.isValid()) {
        m_treeView->setRootIndex(index);
        m_treeView->scrollTo(index);
        m_pathEdit->setText(path);
    }

    if (!fileName.isEmpty() && m_acceptMode == AcceptSave) {
        m_fileNameEdit->setText(fileName);
    }

    // De initiële map (m_initialPath) wordt de root voor de 'Up'-knop
    m_initialPath = path;
}

void CustomFileDialog::setNameFilters(const QString &filter)
{
    QStringList filters = filter.split(";;");
    m_filterComboBox->addItems(filters);
    onFilterChanged(filters.at(0));
}

void CustomFileDialog::onTreeViewClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    QString filePath = m_model->filePath(index);

    if (m_model->isDir(index)) {
        m_fileNameEdit->clear();
    } else {
        QFileInfo fi(filePath);
        m_fileNameEdit->setText(fi.fileName());
    }
}

void CustomFileDialog::onTreeViewDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    if (m_model->isDir(index)) {
        m_treeView->setRootIndex(index);
        m_pathEdit->setText(m_model->filePath(index));
        m_fileNameEdit->clear();
        saveLastVisitedPath();
    } else {
        onTreeViewClicked(index);
        onOkButtonClicked();
    }
}

void CustomFileDialog::onFilterChanged(const QString &filter)
{
    m_filterPatterns.clear();
    int start = filter.indexOf("(*");
    int end = filter.indexOf(")", start);
    if (start != -1 && end != -1) {
        QString patterns = filter.mid(start + 1, end - start - 1);
        m_filterPatterns = patterns.split(" ", Qt::SkipEmptyParts);
    }

    if (m_filterPatterns.contains("*.*") || m_filterPatterns.isEmpty()) {
        m_model->setNameFilters(QStringList());
    } else {
        m_model->setNameFilters(m_filterPatterns);
    }
}

void CustomFileDialog::onOkButtonClicked()
{
    if (m_fileNameEdit->text().isEmpty()) {
        return;
    }

    m_selectedFile = selectedFile();
    QFileInfo fi(m_selectedFile);

    if (m_acceptMode == AcceptSave && !m_filterPatterns.isEmpty() && !m_filterPatterns.contains("*.*")) {
        bool extensionMatch = false;
        QString suffix = fi.suffix();
        for (const QString &pattern : m_filterPatterns) {
            if (pattern.endsWith(suffix, Qt::CaseInsensitive)) {
                extensionMatch = true;
                break;
            }
        }
        if (!extensionMatch && fi.suffix().isEmpty()) {
            QString defaultExt = m_filterPatterns.at(0).mid(1);
            m_selectedFile += defaultExt;
        }
    }

    if (m_acceptMode == AcceptOpen) {
        if (!fi.exists() || !fi.isFile()) {
            return;
        }
    }

    accept();
}

void CustomFileDialog::onUpButtonClicked()
{
    QString currentPath = m_pathEdit->text();

    if (QDir::cleanPath(currentPath) == QDir::cleanPath(m_initialPath)) {
        return;
    }

    QDir currentDir(currentPath);

    if (!currentDir.cdUp()) {
        return;
    }

    QString parentPath = currentDir.absolutePath();

    if (!parentPath.startsWith(QDir::cleanPath(m_initialPath), Qt::CaseInsensitive)) {
        parentPath = m_initialPath;
    }

    QModelIndex index = m_model->index(parentPath);

    if (index.isValid()) {
        m_treeView->setRootIndex(index);
        m_treeView->scrollTo(index);
        m_pathEdit->setText(parentPath);
        m_fileNameEdit->clear();
        saveLastVisitedPath();
    }
}

QString CustomFileDialog::keyFromPathType(PathType type, AcceptMode mode) const
{
    QString base;
    switch (type) {
    case PathRom:       base = "Rom"; break;
    case PathDisk:      base = "Disk"; break;
    case PathTape:      base = "Tape"; break;
    case PathState:     base = "State"; break;
    case PathScreenshot: base = "Screenshot"; break;
    case PathDefault:
    default:            base = (mode == AcceptOpen) ? "DefaultOpen" : "DefaultSave"; break;
    }
    return "CustomFileDialog/Last" + base + "Dir";
}

// customfiledialog.cpp

void CustomFileDialog::loadLastVisitedPath(const QString &initialDir, AcceptMode mode)
{
    // initialDir is het ABSOLUTE pad geladen uit MainWindow::loadSettings().
    // Dit is onze harde DEFAULT/ROOT.

    QSettings settings(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
    QString key = keyFromPathType(m_pathType, mode);

    const QString homePath = QDir::homePath();

    // 1. Probeer de laatst bezochte map uit QSettings te lezen
    // We gebruiken NIET initialDir als default value in QSettings::value,
    // maar een lege string. Dit garandeert dat we ALLEEN de opgeslagen waarde lezen.
    QString savedPath = settings.value(key, QString()).toString();

    QString path;

    if (!savedPath.isEmpty() && QDir(savedPath).exists()) {
        // Gevonden: Gebruik het pad dat de gebruiker de laatste keer bezocht.
        path = savedPath;
    } else {
        // Niet gevonden of ongeldig: Gebruik de initiële/standaard map van MainWindow.
        // Als initialDir leeg is (wat niet zou moeten gebeuren), val terug op HomePath.
        path = initialDir.isEmpty() ? homePath : initialDir;

        // Zorg dat de fallback een geldige, bestaande map is
        if (!QDir(path).exists()) {
            path = homePath;
        }
    }

    // 2. Pas het model aan
    QModelIndex index = m_model->index(path);
    if (index.isValid()) {
        m_treeView->setRootIndex(index);
        m_treeView->scrollTo(index);
        m_pathEdit->setText(path);
        // m_fileNameEdit->clear() is hier niet nodig, dit wordt elders behandeld.
    } else {
        // Als het pad om de een of andere reden niet geldig is, val terug op de HomePath root.
        QModelIndex rootIndex = m_model->index(homePath);
        if (rootIndex.isValid()) {
            m_treeView->setRootIndex(rootIndex);
            m_pathEdit->setText(homePath);
        }
    }
}

void CustomFileDialog::saveLastVisitedPath()
{
}

void CustomFileDialog::onCreateDirClicked()
{
    QString currentPath = m_pathEdit->text();
    QDir currentDir(currentPath);

    bool ok;
    QString dirName = QInputDialog::getText(
        this,
        tr("Make new folder"),
        tr("Foldername:"),
        QLineEdit::Normal,
        tr("New_Folder"),
        &ok);

    if (ok && !dirName.isEmpty()) {
        QString newDirPath = currentDir.filePath(dirName);

        if (currentDir.mkdir(dirName)) {

            QModelIndex currentDirIndex = m_model->index(currentPath);
            m_model->data(currentDirIndex, Qt::DisplayRole);

            emit m_model->dataChanged(currentDirIndex, currentDirIndex);

            QModelIndex index = m_model->index(newDirPath);
            if (index.isValid()) {
                m_treeView->setCurrentIndex(index);
                m_treeView->scrollTo(index);
            }
        } else {
            QMessageBox::warning(this, tr("Fault"),
                                 tr("Cannot create the folder '%1'. Check permissions!").arg(dirName));
        }
    }
}

void CustomFileDialog::onDeleteDirClicked()
{
    QModelIndex currentIndex = m_treeView->currentIndex();
    if (!currentIndex.isValid()) {
        QMessageBox::information(this, tr("Selection Required"),
                                 tr("Select a folder or file to delete."));
        return;
    }

    QString itemPath = m_model->filePath(currentIndex);
    QFileInfo fileInfo(itemPath);

    if (QMessageBox::question(
            this,
            tr("Confirm Deletion"),
            tr("Are you sure you want to permanently delete '%1'?").arg(fileInfo.fileName()),
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::No)
    {
        return;
    }

    QDir dir;
    bool success = false;
    QString errorMsg;

    QString currentPath = m_pathEdit->text();
    QModelIndex currentDirIndex = m_model->index(currentPath);

    if (m_model->isDir(currentIndex)) {
        success = dir.rmdir(itemPath);
        if (!success) {
            QDir targetDir(itemPath);
            if (!targetDir.entryList(QDir::NoDotAndDotDot).isEmpty()) {
                errorMsg = tr("Folder is not empty and cannot be deleted..");
            } else {
                errorMsg = tr("Cannot delete the empty folder. Check permissions!");
            }
        }

    } else {
        success = dir.remove(itemPath);
        if (!success) {
            errorMsg = tr("Cannot delete the file. Check permissions!");
        }
    }

    if (success) {
        qDebug() << "Removed:" << itemPath;

        m_model->data(currentDirIndex, Qt::DisplayRole);
        emit m_model->dataChanged(currentDirIndex, currentDirIndex);
    } else {
        QMessageBox::warning(this, tr("Fault"),
                             tr("Can not remove '%1'. %2").arg(fileInfo.fileName()).arg(errorMsg));
    }
}

void CustomFileDialog::resetLastVisitedPaths()
{
    s_lastOpenDir.clear();
    s_lastSaveDir.clear();
}
