#pragma once

#include <QDialog>
#include <cstdint>
#include <QTableWidget>
#include <QStyledItemDelegate>

class QLineEdit;
class QLabel;
class QPushButton;
class QClipboard; // Niet strikt nodig hier, maar goed voor context

class MemoryEditDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Toont de geheugenbewerkingsdialoog.
     */
    static bool editMemoryLine(QWidget *parent, uint32_t address, uint8_t *data16);

private:
    explicit MemoryEditDialog(QWidget *parent, uint32_t address, const uint8_t *data16);

    void setupUi();
    void loadData(const uint8_t *data16);
    void retrieveData(uint8_t *data16);

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onCopyClicked();
    void onPasteClicked();

private:
    uint32_t m_address;
    uint8_t m_data[16];

    // UI Elementen
    QLabel      *m_addressLabel;
    QLineEdit   *m_addressEdit;
    QTableWidget *m_byteTable;

    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    QPushButton *m_copyButton;
    QPushButton *m_pasteButton;

};
