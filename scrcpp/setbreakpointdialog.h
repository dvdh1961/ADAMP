#ifndef SETBREAKPOINTDIALOG_H
#define SETBREAKPOINTDIALOG_H

#include <QDialog>
#include <QString>

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
    void onTypeChanged(const QString &type);
    void onAddrCondChanged(const QString &addrCond);

    void onOkClicked();
    void onCancelClicked();

private:
    void parseInputString(const QString &input);
    QString buildOutputString() const;
    void setupUi();
    void setAllControlsVisible(bool visible);
    void updateHelpText(const QString &type);

    QComboBox *m_typeCombo;
    QComboBox *m_addrCondCombo;
    QComboBox *m_valueCondCombo;

    QLineEdit *m_addr1Edit;
    QComboBox *m_registerCombo;
    QComboBox *m_flagCombo;
    QLineEdit *m_addr2Edit;

    QLabel *m_typeLabel;
    QLabel *m_addrCondLabel;
    QLabel *m_addr1Label;
    QLabel *m_valueCondLabel;
    QLabel *m_valueLabel;

    QTextEdit *m_helpEdit;

    QPushButton *m_okButton;
    QPushButton *m_cancelButton;

    const QStringList m_addrCondList = {"=", "->"};
    const QStringList m_valueCondList = {"=", "<>", "<=", "=>"};
    QStringList m_registerList;
    QStringList m_flagList;

    QString m_resultString;
};

#endif
