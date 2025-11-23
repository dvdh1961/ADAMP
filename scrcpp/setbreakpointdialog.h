#ifndef SETBREAKPOINTDIALOG_H
#define SETBREAKPOINTDIALOG_H

#include <QDialog>
#include <QString>

// Forward declarations
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QTextEdit;

class SetBreakpointDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SetBreakpointDialog(QWidget *parent = nullptr);
    static QString getBreakpointString(QWidget *parent, const QString &currentValue = "");

private slots:
    // Update de UI-elementen als het Type verandert
    void onTypeChanged(const QString &type);
    // Update de UI-elementen als de Adres-Conditie verandert
    void onAddrCondChanged(const QString &addrCond);

    // Slots voor de nieuwe image knoppen
    void onOkClicked();
    void onCancelClicked();

private:
    // Helpers
    void parseInputString(const QString &input);
    QString buildOutputString() const;
    void setupUi();
    void setAllControlsVisible(bool visible);
    void updateHelpText(const QString &type);

    // UI Elementen
    QComboBox *m_typeCombo;
    QComboBox *m_addrCondCombo;
    QComboBox *m_valueCondCombo;

    QLineEdit *m_addr1Edit;
    // --- NIEUW ---
    QComboBox *m_registerCombo; // Ter vervanging van m_addr1Edit voor REG
    QComboBox *m_flagCombo;     // Ter vervanging van m_addr1Edit voor FLAG
    // --- EINDE NIEUW ---
    QLineEdit *m_addr2Edit;

    QLabel *m_typeLabel;
    QLabel *m_addrCondLabel;
    QLabel *m_addr1Label;
    QLabel *m_valueCondLabel;
    QLabel *m_valueLabel;

    // Help/memo box
    QTextEdit *m_helpEdit;

    QPushButton *m_okButton;
    QPushButton *m_cancelButton;

    // Constante lijsten
    const QStringList m_addrCondList = {"=", "->"};
    const QStringList m_valueCondList = {"=", "<>", "<=", "=>"};
    // --- NIEUW ---
    QStringList m_registerList;
    QStringList m_flagList;
    // --- EINDE NIEUW ---

    // Resultaat
    QString m_resultString;
};

#endif // SETBREAKPOINTDIALOG_H
