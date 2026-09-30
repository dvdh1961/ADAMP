#include "customfiledialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTreeView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QHeaderView>
#include <QSettings>
#include <QCoreApplication>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QUrl>
#include <QDateTime>
#include <QFile>
#include <QShowEvent>
#include <QScrollBar>
#include <QPersistentModelIndex>

QString CustomFileDialog::s_lastOpenDir;
QString CustomFileDialog::s_lastSaveDir;

CustomFileDialog::CustomFileDialog(QWidget *parent)
    : QDialog(parent), m_acceptMode(AcceptOpen)
{
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    m_model = new QFileSystemModel(this);
    m_model->setRootPath(QDir::rootPath());
    m_model->setIconProvider(&m_iconProvider);
    m_model->setReadOnly(false);

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    // Pixel scrolling allows PositionAtCenter to place a row at the actual
    // viewport centre.  The default item scrolling can only align whole rows
    // and was also prone to losing the requested position after sorting.
    m_treeView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Hide the Size, Type, and Date columns
    //m_treeView->hideColumn(1); // Hides Size
    m_treeView->hideColumn(2); // Hides Type
    m_treeView->hideColumn(3); // Hides Date Modified
   m_treeView->header()->setStretchLastSection(true);

    m_treeView->setRootIsDecorated(false);
    m_treeView->setItemsExpandable(false);
    m_treeView->setDragDropOverwriteMode(false);

    m_treeView->viewport()->setAcceptDrops(true);
    // Accept files dragged in from Explorer, but never start an internal drag
    // from the file list itself.  Internal dragging made it possible to move
    // or copy the previously selected disk while navigating folders.
    m_treeView->setDragEnabled(false);
    m_treeView->setDropIndicatorShown(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragDrop);
    m_treeView->viewport()->installEventFilter(this);

    m_treeView->setIconSize(QSize(32, 32));
    m_treeView->setSortingEnabled(true);
    m_treeView->sortByColumn(0, Qt::AscendingOrder);
    m_treeView->header()->setSortIndicator(0, Qt::AscendingOrder);
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_treeView->setColumnWidth(0, 500);
    m_treeView->setAlternatingRowColors(true);

    int fontId = QFontDatabase::addApplicationFont(":/fonts/fonts/luculent.ttf");
    QString family = (fontId != -1) ? QFontDatabase::applicationFontFamilies(fontId).at(0) : "Roboto";
    m_treeView->setFont(QFont(family, 12));
    m_treeView->setStyleSheet("QTreeView { background-color: #3C3C3C; border: 2px solid black; color: white; }"
                              "QTreeView::item:alternate { background-color: #282828; }"
                              "QTreeView::item:selected { background-color: #3399FF; color: black; }");

    m_pathEdit = new QLineEdit(this); m_pathEdit->setReadOnly(true);
    m_fileNameEdit = new QLineEdit(this);
    m_filterComboBox = new QComboBox(this); m_filterComboBox->setCursor(Qt::PointingHandCursor);

    m_okButton = new QPushButton(this); m_cancelButton = new QPushButton(this);
    m_upButton = new QPushButton(this); m_createDirButton = new QPushButton(this); m_deleteDirButton = new QPushButton(this);

    QSize bigBtnSize(86, 57);
    m_okButton->setIcon(QIcon(":/images/images/OK.png"));
    m_okButton->setFixedSize(bigBtnSize); m_okButton->setIconSize(bigBtnSize);
    m_okButton->setFlat(true); m_okButton->setCursor(Qt::PointingHandCursor);
    m_okButton->setStyleSheet("background: transparent; border: none;");

    m_cancelButton->setIcon(QIcon(":/images/images/CANCEL.png"));
    m_cancelButton->setFixedSize(bigBtnSize); m_cancelButton->setIconSize(bigBtnSize);
    m_cancelButton->setFlat(true); m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setStyleSheet("background: transparent; border: none;");

    QSize smallBtnSize(28, 28);
    m_upButton->setIcon(QIcon(":/images/images/UP.png"));
    m_upButton->setFixedSize(smallBtnSize); m_upButton->setIconSize(smallBtnSize);
    m_upButton->setFlat(true); m_upButton->setCursor(Qt::PointingHandCursor);

    m_createDirButton->setIcon(QIcon(":/images/images/NEWDIR.png"));
    m_createDirButton->setFixedSize(smallBtnSize); m_createDirButton->setIconSize(smallBtnSize);
    m_createDirButton->setFlat(true); m_createDirButton->setCursor(Qt::PointingHandCursor);

    m_deleteDirButton->setIcon(QIcon(":/images/images/DELDIR.png"));
    m_deleteDirButton->setFixedSize(smallBtnSize); m_deleteDirButton->setIconSize(smallBtnSize);
    m_deleteDirButton->setFlat(true); m_deleteDirButton->setCursor(Qt::PointingHandCursor);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->addWidget(m_pathEdit, 1); topLayout->addWidget(m_createDirButton); topLayout->addWidget(m_deleteDirButton); topLayout->addWidget(m_upButton);
    mainLayout->addWidget(new QLabel("Map:")); mainLayout->addLayout(topLayout); mainLayout->addWidget(m_treeView, 1);
    QGridLayout *formLayout = new QGridLayout();
    formLayout->addWidget(new QLabel("Filename:"), 0, 0); formLayout->addWidget(m_fileNameEdit, 0, 1);
    formLayout->addWidget(new QLabel("Filetype:"), 1, 0); formLayout->addWidget(m_filterComboBox, 1, 1);
    mainLayout->addLayout(formLayout);
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch(); bottomLayout->addWidget(m_okButton); bottomLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(bottomLayout);

    connect(m_treeView, &QTreeView::clicked, this, &CustomFileDialog::onTreeViewClicked);
    connect(m_treeView, &QTreeView::doubleClicked, this, &CustomFileDialog::onTreeViewDoubleClicked);
    connect(m_filterComboBox, &QComboBox::currentTextChanged, this, &CustomFileDialog::onFilterChanged);
    connect(m_okButton, &QPushButton::clicked, this, &CustomFileDialog::onOkButtonClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_upButton, &QPushButton::clicked, this, &CustomFileDialog::onUpButtonClicked);
    connect(m_createDirButton, &QPushButton::clicked, this, &CustomFileDialog::onCreateDirClicked);
    connect(m_deleteDirButton, &QPushButton::clicked, this, &CustomFileDialog::onDeleteDirClicked);
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString &path) {
        if (QDir::cleanPath(path) == QDir::cleanPath(m_pathEdit->text())) {
            QTimer::singleShot(0,this,[this](){
                restoreLastSelection();
                if(isVisible()&&!m_restoreFilePath.isEmpty())m_treeView->setFocus(Qt::OtherFocusReason);
            });
        }
    });
    connect(m_model, &QAbstractItemModel::layoutChanged, this, [this](){
        if(isVisible()&&!m_restoreFilePath.isEmpty())
            QTimer::singleShot(0,this,&CustomFileDialog::centerRestoredSelection);
    });

    setFixedSize(700, 510);
}

void CustomFileDialog::updateFileSystemFilter(const QString &currentPath) {
    QFileInfo currentFi(currentPath);
    QFileInfo limitFi(m_limitPath);
    QString cur = currentFi.canonicalFilePath();
    QString lim = limitFi.canonicalFilePath();

    if (cur == lim || QDir(cur).isRoot()) {
        m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    } else {
        m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDot);
    }

    m_treeView->selectionModel()->clearSelection();
    m_treeView->setCurrentIndex(QModelIndex());
    QCoreApplication::processEvents();
    m_treeView->viewport()->update();
}

void CustomFileDialog::onTreeViewDoubleClicked(const QModelIndex &index) {
    if (!index.isValid()) return;

    if (m_model->isDir(index)) {
        QString path = m_model->filePath(index);

        QTimer::singleShot(0, this, [this, path]() {
            QModelIndex newRoot = m_model->index(path);
            if (newRoot.isValid()) {
                m_restoreFilePath.clear();
                m_fileNameEdit->clear();
                m_treeView->setRootIndex(newRoot);
                m_pathEdit->setText(path);
                saveCurrentDirectory();
                updateFileSystemFilter(path);
                m_treeView->doItemsLayout();
                m_treeView->scrollToTop();
            }
        });
    } else {
        onTreeViewClicked(index);
        onOkButtonClicked();
    }
}

void CustomFileDialog::onUpButtonClicked() {
    QFileInfo currentFi(m_pathEdit->text());
    QFileInfo limitFi(m_limitPath);
    if (currentFi.canonicalFilePath() == limitFi.canonicalFilePath()) return;

    QDir dir(m_pathEdit->text());
    if (dir.cdUp()) {
        QString path = dir.absolutePath();
        QTimer::singleShot(0, this, [this, path]() {
            m_restoreFilePath.clear();
            m_fileNameEdit->clear();
            m_treeView->setRootIndex(m_model->index(path));
            m_pathEdit->setText(path);
            saveCurrentDirectory();
            updateFileSystemFilter(path);
            m_treeView->doItemsLayout();
            m_treeView->scrollToTop();
        });
    }
}

void CustomFileDialog::onTreeViewClicked(const QModelIndex &index) {
    if (!index.isValid()) return;
    if (!m_model->isDir(index)) {
        m_fileNameEdit->setText(m_model->fileName(index));
    } else {
        m_fileNameEdit->clear();
    }
}

void CustomFileDialog::loadLastVisitedPath(const QString &initialDir, AcceptMode mode) {
    QSettings settings(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
    m_limitPath = QFileInfo(initialDir.isEmpty() ? QDir::homePath() : initialDir).absoluteFilePath();

    QString key = keyFromPathType(m_pathType, mode);
    QString path = (m_pathType == PathInjected)
                       ? m_limitPath
                       : settings.value(key, m_limitPath).toString();
    if (!QDir(path).exists()) path = m_limitPath;

    m_restoreFilePath=settings.value(selectionKeyFromPathType(m_pathType,mode)).toString();
    if(QFileInfo(m_restoreFilePath).absolutePath()!=QFileInfo(path).absoluteFilePath())
        m_restoreFilePath.clear();

    m_treeView->setRootIndex(m_model->index(path));
    m_pathEdit->setText(path);
    updateFileSystemFilter(path);
    QTimer::singleShot(0,this,&CustomFileDialog::restoreLastSelection);
}

void CustomFileDialog::saveLastVisitedPath() {
    QSettings settings(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
    QString key = keyFromPathType(m_pathType, m_acceptMode);
    settings.setValue(key, m_pathEdit->text());
    if(!m_fileNameEdit->text().isEmpty())
        settings.setValue(selectionKeyFromPathType(m_pathType,m_acceptMode),selectedFile());
}

void CustomFileDialog::saveCurrentDirectory()
{
    QSettings settings(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
    settings.setValue(keyFromPathType(m_pathType,m_acceptMode),m_pathEdit->text());
}

void CustomFileDialog::restoreLastSelection()
{
    if(m_restoreFilePath.isEmpty()||!QFileInfo::exists(m_restoreFilePath))return;
    const QModelIndex index=m_model->index(m_restoreFilePath);
    if(!index.isValid())return;
    m_treeView->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
    m_treeView->selectionModel()->select(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
    m_fileNameEdit->setText(QFileInfo(m_restoreFilePath).fileName());
    m_treeView->doItemsLayout();
    centerRestoredSelection();
}

void CustomFileDialog::centerRestoredSelection()
{
    if(m_restoreFilePath.isEmpty())return;
    const QPersistentModelIndex index(m_model->index(m_restoreFilePath));
    if(!index.isValid())return;

    m_treeView->scrollTo(index,QAbstractItemView::EnsureVisible);
    // First make the row visible, then measure where Qt really placed it.
    // With ScrollPerPixel the measured offset can be applied directly to the
    // scrollbar, independent of row height, font or icon size.
    QTimer::singleShot(0,this,[this,index](){
        if(!index.isValid())return;
        const QRect rowRect=m_treeView->visualRect(index);
        if(!rowRect.isValid())return;
        const int wantedY=m_treeView->viewport()->rect().center().y();
        const int delta=rowRect.center().y()-wantedY;
        QScrollBar *bar=m_treeView->verticalScrollBar();
        bar->setValue(qBound(bar->minimum(),bar->value()+delta,bar->maximum()));
        m_treeView->viewport()->update();
    });
}

void CustomFileDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // QFileSystemModel builds and sorts the visible rows asynchronously.  A
    // selection restored before show() has no final scrollbar geometry yet.
    // Repeat after the dialog is visible, then once more after Qt has laid out
    // the rows so the selected game is genuinely centred and owns focus.
    const auto restoreVisibleSelection=[this](){
        restoreLastSelection();
        if(!m_restoreFilePath.isEmpty())m_treeView->setFocus(Qt::OtherFocusReason);
    };
    // QFileSystemModel loads quickly, but sorting and scrollbar geometry can
    // settle a little later.  Reapply at each relevant stage; the last pass
    // guarantees that the selected row remains centred.
    QTimer::singleShot(0,this,restoreVisibleSelection);
    QTimer::singleShot(100,this,restoreVisibleSelection);
    QTimer::singleShot(300,this,restoreVisibleSelection);
}

void CustomFileDialog::onOkButtonClicked() {
    if (!m_fileNameEdit->text().isEmpty()) {
        saveLastVisitedPath();
        accept();
    }
}

bool CustomFileDialog::isFileAccepted(const QString &fileName) {
    QString suffix = QFileInfo(fileName).suffix().toLower();
    if (m_pathType == PathRom) return (suffix == "col" || suffix == "bin" || suffix == "rom");
    if (m_pathType == PathAtariRom) return (suffix == "a26" || suffix == "bin" || suffix == "rom");
    if (m_pathType == PathDisk) return (suffix == "dsk");
    if (m_pathType == PathTape) return (suffix == "dpp");
    return true;
}

bool CustomFileDialog::eventFilter(QObject *obj, QEvent *event) {
    if (obj == m_treeView->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            QDragMoveEvent *dmEvent = static_cast<QDragMoveEvent*>(event);
            if (dmEvent->mimeData()->hasUrls() && isFileAccepted(dmEvent->mimeData()->urls().at(0).toLocalFile())) {
                dmEvent->acceptProposedAction(); return true;
            }
            dmEvent->ignore(); return true;
        }
        else if (event->type() == QEvent::Drop) {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData *mimeData = dropEvent->mimeData();
            if (mimeData->hasUrls()) {
                QString sourcePath = mimeData->urls().at(0).toLocalFile();
                if (!isFileAccepted(sourcePath)) return true;
                QFileInfo sourceFi(sourcePath);
                QModelIndex targetIndex = m_treeView->indexAt(dropEvent->pos());
                QString destDir = (targetIndex.isValid() && m_model->isDir(targetIndex)) ? m_model->filePath(targetIndex) : m_pathEdit->text();
                QString destPath = QDir(destDir).filePath(sourceFi.fileName());
                if (sourceFi.exists()) {
                    bool success = sourcePath.startsWith(m_model->rootPath()) ? QFile::rename(sourcePath, destPath) : QFile::copy(sourcePath, destPath);
                    if (success) { m_fileNameEdit->setText(sourceFi.fileName()); dropEvent->acceptProposedAction(); }
                }
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}

QString CustomFileDialog::getOpenFileName(QWidget *parent, const QString &caption, const QString &dir, const QString &filter, QString *selectedFilter, PathType type, QFileDialog::Options options) {
    CustomFileDialog dlg(parent);
    dlg.m_acceptMode = AcceptOpen; dlg.m_pathType = type; dlg.setWindowTitle(caption);
    dlg.loadLastVisitedPath(dir, AcceptOpen); dlg.setNameFilters(filter);
    return (dlg.exec() == QDialog::Accepted) ? dlg.selectedFile() : QString();
}

QString CustomFileDialog::getSaveFileName(QWidget *parent, const QString &caption, const QString &dir, const QString &filter, QString *selectedFilter, PathType type, QFileDialog::Options options, const QString& romBaseName) {
    CustomFileDialog dlg(parent);
    dlg.m_acceptMode = AcceptSave; dlg.m_pathType = type; dlg.setWindowTitle(caption);
    dlg.loadLastVisitedPath(dir, AcceptSave); dlg.setNameFilters(filter);
    if (type == PathScreenshot && dlg.m_fileNameEdit->text().isEmpty()) {
        QString base = (romBaseName.isEmpty() || romBaseName == "No cart") ? "screenshot" : romBaseName;
        dlg.m_fileNameEdit->setText(QString("%1_%2.png").arg(base).arg(QDateTime::currentDateTime().toSecsSinceEpoch()));
    }
    return (dlg.exec() == QDialog::Accepted) ? dlg.selectedFile() : QString();
}

QString CustomFileDialog::selectedFile() const { return QDir(m_pathEdit->text()).filePath(m_fileNameEdit->text()); }

QString CustomFileDialog::keyFromPathType(PathType type, AcceptMode mode) const {
    QString base;
    switch (type) {
    case PathRom: base = "Rom"; break; case PathAtariRom: base = "AtariRom"; break;
    case PathDisk: base = "Disk"; break; case PathTape: base = "Tape"; break;
    case PathState: base = "State"; break; case PathScreenshot: base = "Screenshot"; break; case PathSymbol: base = "Symbol"; break;
    case PathInjected: base = "Injected"; break;
    default: base = (mode == AcceptOpen) ? "DefaultOpen" : "DefaultSave"; break;
    }
    return "CustomFileDialog/Last" + base + "Dir";
}

QString CustomFileDialog::selectionKeyFromPathType(PathType type,AcceptMode mode) const
{
    QString key=keyFromPathType(type,mode);
    if(key.endsWith("Dir"))key.chop(3);
    return key+"File";
}

void CustomFileDialog::setNameFilters(const QString &filter) {
    QStringList filters = filter.split(";;");
    m_filterComboBox->clear(); m_filterComboBox->addItems(filters);
    if (!filters.isEmpty()) onFilterChanged(filters.at(0));
}

void CustomFileDialog::onFilterChanged(const QString &filter) {
    int start = filter.indexOf("(*"), end = filter.indexOf(")", start);
    if (start != -1 && end != -1) m_filterPatterns = filter.mid(start + 1, end - start - 1).split(" ", Qt::SkipEmptyParts);
    m_model->setNameFilters((m_filterPatterns.contains("*.*") || m_filterPatterns.isEmpty()) ? QStringList() : m_filterPatterns);
    QTimer::singleShot(0,this,&CustomFileDialog::restoreLastSelection);
}

void CustomFileDialog::onCreateDirClicked() {
    bool ok; QString name = QInputDialog::getText(this, "Make directory", "Name:", QLineEdit::Normal, "New_Directory", &ok);
    if (ok && !name.isEmpty()) QDir(m_pathEdit->text()).mkdir(name);
}

void CustomFileDialog::onDeleteDirClicked() {
    QModelIndex idx = m_treeView->currentIndex();
    if (idx.isValid() && QMessageBox::question(this, "Delete", "Really?") == QMessageBox::Yes) {
        m_model->isDir(idx) ? QDir().rmdir(m_model->filePath(idx)) : QFile::remove(m_model->filePath(idx));
    }
}

void CustomFileDialog::resetLastVisitedPaths() { s_lastOpenDir.clear(); s_lastSaveDir.clear(); }
void CustomFileDialog::setInitialDirectory(const QString &dir) { Q_UNUSED(dir); }
