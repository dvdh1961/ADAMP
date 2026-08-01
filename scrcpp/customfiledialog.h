#ifndef CUSTOMFILEDIALOG_H
#define CUSTOMFILEDIALOG_H

#include "customiconprovider.h" // Include de aparte klasse
#include <QDialog>
#include <QFileSystemModel>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QTimer>

class QTreeView;
class QLineEdit;
class QComboBox;
class QPushButton;

class CustomFileDialog : public QDialog
{
    Q_OBJECT

public:
    enum AcceptMode { AcceptOpen, AcceptSave };
    enum PathType { PathDefault = 0, PathRom, PathDisk, PathTape, PathState, PathScreenshot, PathSymbol, PathInjected };

    explicit CustomFileDialog(QWidget *parent = nullptr);

    static QString getOpenFileName(QWidget *parent = nullptr, const QString &caption = QString(), const QString &dir = QString(), const QString &filter = QString(), QString *selectedFilter = nullptr, PathType type = PathDefault, QFileDialog::Options options = QFileDialog::Options());
    static QString getSaveFileName(QWidget *parent = nullptr, const QString &caption = QString(), const QString &dir = QString(), const QString &filter = QString(), QString *selectedFilter = nullptr, PathType type = PathDefault, QFileDialog::Options options = QFileDialog::Options(), const QString& romBaseName = QString());

    QString selectedFile() const;
    static QString s_lastOpenDir;
    static QString s_lastSaveDir;
    static void resetLastVisitedPaths();

protected:
    void setInitialDirectory(const QString &dir);
    void setNameFilters(const QString &filter);
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onTreeViewClicked(const QModelIndex &index);
    void onTreeViewDoubleClicked(const QModelIndex &index);
    void onFilterChanged(const QString &filter);
    void onOkButtonClicked();
    void onUpButtonClicked();
    void onCreateDirClicked();
    void onDeleteDirClicked();

private:
    QTreeView       *m_treeView;
    QLineEdit       *m_pathEdit;
    QLineEdit       *m_fileNameEdit;
    QComboBox       *m_filterComboBox;
    QPushButton     *m_okButton;
    QPushButton     *m_cancelButton;
    QPushButton     *m_upButton;
    QPushButton     *m_createDirButton;
    QPushButton     *m_deleteDirButton;

    QFileSystemModel *m_model;
    CustomIconProvider m_iconProvider; // De instantie van je aparte klasse

    AcceptMode m_acceptMode;
    QString    m_selectedFile;
    QStringList m_filterPatterns;
    QString    m_limitPath;
    PathType   m_pathType = PathDefault;

    void loadLastVisitedPath(const QString &initialDir, AcceptMode mode);
    void saveLastVisitedPath();
    void updateFileSystemFilter(const QString &currentPath);
    QString keyFromPathType(PathType type, AcceptMode mode) const;
    bool isFileAccepted(const QString &fileName);
};

#endif
