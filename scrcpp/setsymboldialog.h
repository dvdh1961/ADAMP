#ifndef SETSYMBOLDIALOG_H
#define SETSYMBOLDIALOG_H

#include <QDialog>
#include <QString>

class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QTextEdit;

class SetSymbolDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SetSymbolDialog(QWidget *parent = nullptr);
    static QString getSymbolString(QWidget *parent, const QString &currentValue = "");

private slots:
    void onTypeChanged(const QString &type);
    void onOkClicked();
    void onCancelClicked();

private:
    void parseInputString(const QString &input);
    QString buildOutputString() const;
    void setupUi();
    void updateHelpText(const QString &type);
    static QString formatSymbolString(const QString& input);

    QComboBox *m_typeCombo;
    QLineEdit *m_addrEdit;
    QLineEdit *m_labelEdit;

    QLabel *m_typeLabel;
    QLabel *m_addrLabel;
    QLabel *m_labelLabel;

    QTextEdit *m_helpEdit;

    QPushButton *m_okButton;
    QPushButton *m_cancelButton;

    QStringList m_typeList = {"EXEC", "CALL", "JUMP", "DATA", "TEXT", "PORT"};
    QString m_resultString;
};

#endif // SETSYMBOLDIALOG_H
