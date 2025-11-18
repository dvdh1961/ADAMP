#ifndef CUSTOMFILEDIALOG_H
#define CUSTOMFILEDIALOG_H

#include <QDialog>
#include <QFileSystemModel>
#include "customiconprovider.h"

// Forward declarations om de compileertijd te versnellen
class QTreeView;
class QLineEdit;
class QComboBox;
class QPushButton;

/**
 * @brief Een aangepast bestandsdialoog ter vervanging van QFileDialog.
 *
 * Deze klasse bouwt een bestandsdialoog vanaf nul op met behulp van
 * QFileSystemModel en QTreeView, waardoor volledige controle over de
 * styling en functionaliteit mogelijk is.
 *
 * Het implementeert de bekende static-functies getOpenFileName en getSaveFileName
 * voor een eenvoudige "drop-in" vervanging.
 */
class CustomFileDialog : public QDialog
{
    Q_OBJECT

public:
    enum AcceptMode { AcceptOpen, AcceptSave };

    explicit CustomFileDialog(QWidget *parent = nullptr);

    /**
     * @brief Opent het dialoogvenster om een bestaand bestand te selecteren.
     */
    static QString getOpenFileName(QWidget *parent = nullptr,
                                   const QString &caption = QString(),
                                   const QString &dir = QString(),
                                   const QString &filter = QString());

    /**
     * @brief Opent het dialoogvenster om een bestandsnaam voor opslag te selecteren.
     */
    static QString getSaveFileName(QWidget *parent = nullptr,
                                   const QString &caption = QString(),
                                   const QString &dir = QString(),
                                   const QString &filter = QString());

    /**
     * @brief Retourneert het volledige pad van het geselecteerde bestand.
     */
    QString selectedFile() const;

protected:
    // Interne setup-functies
    void setInitialDirectory(const QString &dir);
    void setNameFilters(const QString &filter);

private slots:
    // Slots voor UI-interactie
    void onTreeViewClicked(const QModelIndex &index);
    void onTreeViewDoubleClicked(const QModelIndex &index);
    void onFilterChanged(const QString &filter);
    void onOkButtonClicked();

private:
    // UI-elementen
    QTreeView       *m_treeView;
    QLineEdit       *m_pathEdit;
    QLineEdit       *m_fileNameEdit;
    QComboBox       *m_filterComboBox;
    QPushButton     *m_okButton;
    QPushButton     *m_cancelButton;

    // Data Model
    QFileSystemModel *m_model;
    CustomIconProvider m_iconProvider; // <-- 2. VOEG DEZE MEMBER TOE

    // Interne status
    AcceptMode m_acceptMode;
    QString    m_selectedFile;
    QStringList m_filterPatterns;
};

#endif // CUSTOMFILEDIALOG_H
