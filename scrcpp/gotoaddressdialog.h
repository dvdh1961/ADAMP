#ifndef GOTOADDRESSDIALOG_H
#define GOTOADDRESSDIALOG_H

#include <QDialog>
#include <QString>
#include <cstdint>

class QLineEdit;
class QLabel;
class QPushButton;

class GotoAddressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GotoAddressDialog(QWidget *parent = nullptr);

    /**
     * Eenvoudige helper:
     * - toont dialoog
     * - pre-filled met currentPC
     * - geeft true terug bij OK, false bij Cancel
     * - outAddr bevat het gekozen adres
     */
    static bool getAddress(QWidget *parent,
                           uint16_t currentPC,
                           uint16_t &outAddr);

private slots:
    void onOkClicked();
    void onCancelClicked();

private:
    void setupUi();

    QLineEdit   *m_addrEdit = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    uint16_t     m_resultAddr = 0;
};

#endif // GOTOADDRESSDIALOG_H
