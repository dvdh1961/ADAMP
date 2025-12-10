#include "setsymboldialog.h"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QDebug>
#include <QIcon>
#include <QPixmap>
#include <QTextEdit>
#include <QFrame>
#include <QMessageBox> // Added QMessageBox for warning

// Functie om de output string te formatteren en te valideren
// OPMERKING: De scope resolutie is toegevoegd om de linkerfout op te lossen.
QString SetSymbolDialog::formatSymbolString(const QString& input)
{
    QString s = input.toUpper().trimmed();
    s.replace(QRegularExpression("\\s+"), " ");
    QStringList parts = s.split(':');

    if (parts.size() < 3) return QString(); // Moet TYPE:ADRES:LABEL zijn

    QString type = parts[0].trimmed();
    QString address = parts[1].trimmed();
    QString label = parts[2].trimmed();

    if (type.isEmpty() || address.isEmpty() || label.isEmpty()) return QString();

    bool ok;
    // Valideer adres (moet 16-bit hex zijn)
    address.toUShort(&ok, 16);
    if (!ok) return QString();

    // Adres opvullen tot 4 hex karakters
    address = address.rightJustified(4, '0');

    // De output is altijd TYPE:ADRES:LABEL
    return QString("%1:%2:%3").arg(type, address, label);
}

SetSymbolDialog::SetSymbolDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

void SetSymbolDialog::setupUi()
{
    setWindowTitle(tr("Set Symbol Label"));
    resize(400, 200);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGridLayout *gridLayout = new QGridLayout();

    m_typeLabel   = new QLabel(tr("Type:"), this);
    m_addrLabel   = new QLabel(tr("Address (Hex):"), this);
    m_labelLabel  = new QLabel(tr("Label/Desc:"), this);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems(m_typeList);

    m_addrEdit = new QLineEdit(this);
    // Adres mag enkel hexadecimale karakters bevatten (max. 4)
    m_addrEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9a-fA-F]{1,4}"), this));

    m_labelEdit = new QLineEdit(this);

    // Rijen toevoegen aan de grid
    gridLayout->addWidget(m_typeLabel,  0, 0);
    gridLayout->addWidget(m_typeCombo,  0, 1);
    gridLayout->addWidget(m_addrLabel,  1, 0);
    gridLayout->addWidget(m_addrEdit,   1, 1);
    gridLayout->addWidget(m_labelLabel, 2, 0);
    gridLayout->addWidget(m_labelEdit,  2, 1);

    mainLayout->addLayout(gridLayout);

    m_helpEdit = new QTextEdit(this);
    m_helpEdit->setReadOnly(true);
    m_helpEdit->setMinimumHeight(60);
    m_helpEdit->setWordWrapMode(QTextOption::WordWrap);
    mainLayout->addWidget(m_helpEdit);

    mainLayout->addStretch();

    // Knoppen (overgenomen van SetBreakpointDialog)
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QIcon okIcon(":/images/images/OK.png");
    QIcon cancelIcon(":/images/images/CANCEL.png");
    QPixmap okPixmap(":/images/images/OK.png");
    QPixmap cancelPixmap(":/images/images/CANCEL.png");

    // Maximale grootte berekenen voor de knoppen
    int commonWidth  = qMax(okPixmap.size().width(),  cancelPixmap.size().width());
    int commonHeight = qMax(okPixmap.size().height(), cancelPixmap.size().height());
    QSize commonSize(commonWidth, commonHeight);

    QString buttonStyle =
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }";

    m_okButton = new QPushButton(this);
    m_okButton->setIcon(okIcon);
    m_okButton->setIconSize(okPixmap.size());
    m_okButton->setFixedSize(commonSize);
    m_okButton->setStyleSheet(buttonStyle);
    m_okButton->setFlat(true);

    m_cancelButton = new QPushButton(this);
    m_cancelButton->setIcon(cancelIcon);
    m_cancelButton->setIconSize(cancelPixmap.size());
    m_cancelButton->setFixedSize(commonSize);
    m_cancelButton->setStyleSheet(buttonStyle);
    m_cancelButton->setFlat(true);

    buttonLayout->addStretch();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(buttonLayout);

    connect(m_typeCombo,    &QComboBox::currentTextChanged, this, &SetSymbolDialog::onTypeChanged);
    connect(m_okButton,     &QPushButton::clicked,          this, &SetSymbolDialog::onOkClicked);
    connect(m_cancelButton, &QPushButton::clicked,          this, &SetSymbolDialog::onCancelClicked);

    // Initialiseer helptekst
    onTypeChanged(m_typeCombo->currentText());
    m_addrEdit->setFocus();
}

void SetSymbolDialog::onTypeChanged(const QString &type)
{
    updateHelpText(type);
}

void SetSymbolDialog::updateHelpText(const QString &type)
{
    QString msg;

    if (type == "EXEC" || type == "CALL" || type == "JUMP") {
        msg = tr("Defines an executable entry point, function, or jump target.\n"
                 "The address points to the Z80 instruction.");
    } else if (type == "DATA") {
        msg = tr("Defines the start address of a data array or structure in memory.");
    } else if (type == "TEXT") {
        msg = tr("Defines the start address of an ASCII text string.");
    } else if (type == "PORT") {
        msg = tr("Defines an I/O port address for easy reference.");
    } else {
        msg = tr("Select a symbol type to see help.");
    }

    if (m_helpEdit)
        m_helpEdit->setPlainText(msg);
}

void SetSymbolDialog::onOkClicked()
{
    m_resultString = buildOutputString();

    // Formatteren en valideren
    m_resultString = formatSymbolString(m_resultString);

    if (m_resultString.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"), tr("Symbol format must be TYPE:ADRES:LABEL and address must be a valid 4-digit hex value."));
        return; // Sta niet toe te sluiten
    }

    QDialog::accept();
}

void SetSymbolDialog::onCancelClicked()
{
    QDialog::reject();
}

QString SetSymbolDialog::buildOutputString() const
{
    QString type = m_typeCombo->currentText();
    QString addr = m_addrEdit->text().toUpper().trimmed();
    QString label = m_labelEdit->text().trimmed();

    return QString("%1:%2:%3").arg(type, addr, label);
}

void SetSymbolDialog::parseInputString(const QString &input)
{
    if (input.isEmpty()) return;

    QString s = input.toUpper().trimmed();
    QStringList parts = s.split(':');

    if (parts.size() < 3) return;

    QString type = parts[0];
    QString address = parts[1];
    QString label = parts.mid(2).join(':'); // Label kan ':' bevatten

    int typeIdx = m_typeCombo->findText(type);
    if (typeIdx != -1) {
        m_typeCombo->setCurrentIndex(typeIdx);
    }

    m_addrEdit->setText(address);
    m_labelEdit->setText(label);

    onTypeChanged(m_typeCombo->currentText());
}

QString SetSymbolDialog::getSymbolString(QWidget *parent, const QString &currentValue)
{
    SetSymbolDialog dlg(parent);
    dlg.parseInputString(currentValue);

    if (dlg.exec() == QDialog::Accepted) {
        return dlg.m_resultString;
    }

    return QString();
}
