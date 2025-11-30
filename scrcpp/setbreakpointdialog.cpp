#include "setbreakpointdialog.h"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QRegularExpression>
#include <QDebug>
#include <QIcon>
#include <QPixmap>
#include <QTextEdit>
#include <QFrame>

static QString formatBreakpointString(const QString& input)
{
    // --- DEBUG ---
    qDebug() << "[SetBreakpointDialog] formatBreakpointString validating:" << input;

    QString s = input.toUpper().trimmed();
    s.replace(QRegularExpression("\\s+"), " ");
    QStringList parts = s.split(' ');
    if (parts.isEmpty()) return QString();

    QString type = parts[0];

    const QSet<QString> valueConditions = {"=", "<>", "<=", "=>"};
    const QSet<QString> registers = {
        "PC", "SP", "AF", "BC", "DE", "HL", "IX", "IY",
        "AF'", "BC'", "DE'", "HL'", "A", "B", "C", "D", "E", "H", "L",
        "A'", "B'", "C'", "D'", "E'", "H'", "L'", "I", "R"
    };
    const QSet<QString> flags = {"S", "Z", "H", "P/V", "N", "C"};

    if (type == "REG" && parts.size() == 4 && valueConditions.contains(parts[2])) {
        if (registers.contains(parts[1])) {
            return s;
        }
    }
    else if (type == "FLAG" && parts.size() == 4 && parts[2] == "=") {
        if (flags.contains(parts[1])) {
            return s;
        }
    }
    else if (type == "MEM" && parts.size() == 4 && valueConditions.contains(parts[2])) {
        return s;
    }
    else if (QStringList{"RD", "WR", "EXE", "IN", "OUT", "INH", "INL", "OUTH", "OUTL"}.contains(type)) {
        if (parts.size() == 2) {
            return s;
        }
        if (parts.size() == 4 && (valueConditions.contains(parts[2]) || parts[2] == "..." || parts[2] == "->")) {
            return s;
        }
    }
    else if (type == "CLK" && parts.size() == 5 && parts[1] == "=" && parts[3] == "<>") {
        return s;
    }
    else if (parts.size() == 1) {
        bool ok;
        parts[0].toUInt(&ok, 16);
        if (ok) {
            return "EXE " + parts[0].rightJustified(4, '0');
        }
    }

    // --- DEBUG ---
    qWarning() << "[SetBreakpointDialog] Invalid breakpoint format:" << input;
    return QString();
}

static QString mapUiTypeToCore(QString type) {
    QString t = type.left(2);

    if (t == "01") return "EXE";
    if (t == "02") return "RD";
    if (t == "03") return "WR";
    if (t == "04") return "OUT";
    if (t == "05") return "OUTH";
    if (t == "06") return "OUTL";
    if (t == "07") return "IN";
    if (t == "08") return "INH";
    if (t == "09") return "INL";
    if (t == "10") return "CLK";
    if (t == "11") return "MEM";
    if (t == "12") return "REG";
    if (t == "13") return "FLAG";
    return t;
}

static QString mapCoreTypeToUi(QString type) {
    // Map *core* type naar de exacte UI-string in de combobox
    if (type == "EXE")  return "01.Execute address";
    if (type == "RD")   return "02.Read memory";
    if (type == "WR")   return "03.Write memory";
    if (type == "OUT")  return "04.Out 16B value";
    if (type == "OUTH") return "05.Out High significant Byte 8B value";
    if (type == "OUTL") return "06.Out Low significant Byte 8B value";
    if (type == "IN")   return "07.IN 16B value";
    if (type == "INH")  return "08.IN High significant Byte 8B";
    if (type == "INL")  return "09.IN Low significant Byte 8B value";
    if (type == "CLK")  return "10.Clock";
    if (type == "MEM")  return "11.Memory";
    if (type == "REG")  return "12.Register";
    if (type == "FLAG") return "13.Flag";

    bool ok = false;
    type.toUInt(&ok, 16);
    if (ok)
        return "01.Execute address";

    return type;
}

SetBreakpointDialog::SetBreakpointDialog(QWidget *parent)
    : QDialog(parent)
{
    m_registerList = {
        "PC", "SP", "AF", "BC", "DE", "HL", "IX", "IY",
        "AF'", "BC'", "DE'", "HL'", "A", "B", "C", "D", "E", "H", "L",
        "A'", "B'", "C'", "D'", "E'", "H'", "L'", "I", "R"
    };
    m_flagList = {"S", "Z", "H", "P/V", "N", "C"};

    m_registerList.sort();
    m_flagList.sort();
    setupUi();
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

void SetBreakpointDialog::setupUi()
{
    setWindowTitle(tr("Set Breakpoint"));
    resize(480, 260);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGridLayout *gridLayout = new QGridLayout();
    m_typeLabel       = new QLabel(tr("Type:"), this);
    m_addrCondLabel   = new QLabel(tr("Condition:"), this);
    m_addr1Label      = new QLabel(tr("Addr Start:"), this);
    m_valueCondLabel  = new QLabel(tr("Condition:"), this);
    m_valueLabel      = new QLabel(tr("Value:"), this);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({
        "01.Execute address", "02.Read memory", "03.Write memory",
        "04.Out 16B value", "05.Out High significant Byte 8B value", "06.Out Low significant Byte 8B value",
        "07.IN 16B value", "08.IN High significant Byte 8B", "09.IN Low significant Byte 8B value",
        "10.Clock", "11.Memory", "12.Register", "13.Flag"
    });

    m_addrCondCombo  = new QComboBox(this);
    m_addrCondCombo->addItems(m_addrCondList);
    m_valueCondCombo = new QComboBox(this);
    m_valueCondCombo->addItems(m_valueCondList);

    m_addr1Edit = new QLineEdit(this);

    m_registerCombo = new QComboBox(this);
    m_registerCombo->addItems(m_registerList);

    m_flagCombo = new QComboBox(this);
    m_flagCombo->addItems(m_flagList);

    m_addr2Edit = new QLineEdit(this);

    gridLayout->addWidget(m_typeLabel,      0, 0);
    gridLayout->addWidget(m_typeCombo,      0, 1, 1, 4);
    gridLayout->addWidget(m_addrCondLabel,  1, 0);
    gridLayout->addWidget(m_addrCondCombo,  1, 1);
    gridLayout->addWidget(m_addr1Label,     1, 2);

    gridLayout->addWidget(m_addr1Edit,      1, 3, 1, 2);
    gridLayout->addWidget(m_registerCombo,  1, 3, 1, 2);
    gridLayout->addWidget(m_flagCombo,      1, 3, 1, 2);

    gridLayout->addWidget(m_valueCondLabel, 2, 0);
    gridLayout->addWidget(m_valueCondCombo, 2, 1);
    gridLayout->addWidget(m_valueLabel,     2, 2);
    gridLayout->addWidget(m_addr2Edit,      2, 3, 1, 2);

    mainLayout->addLayout(gridLayout);

    m_helpEdit = new QTextEdit(this);
    m_helpEdit->setReadOnly(true);
    m_helpEdit->setMinimumHeight(60);
    m_helpEdit->setWordWrapMode(QTextOption::WordWrap);
    mainLayout->addWidget(m_helpEdit);

    mainLayout->addStretch();

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QIcon okIcon(":/images/images/OK.png");
    QIcon cancelIcon(":/images/images/CANCEL.png");
    QPixmap okPixmap(":/images/images/OK.png");
    QPixmap cancelPixmap(":/images/images/CANCEL.png");

    if (okIcon.isNull()) {
        qWarning() << "SetBreakpointDialog: Kon OK.png niet laden.";
    }
    if (cancelIcon.isNull()) {
        qWarning() << "SetBreakpointDialog: Kon CANCEL.png niet laden.";
    }

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

    connect(m_typeCombo,      &QComboBox::currentTextChanged, this, &SetBreakpointDialog::onTypeChanged);
    connect(m_addrCondCombo,  &QComboBox::currentTextChanged, this, &SetBreakpointDialog::onAddrCondChanged);
    connect(m_okButton,       &QPushButton::clicked,          this, &SetBreakpointDialog::onOkClicked);
    connect(m_cancelButton,   &QPushButton::clicked,          this, &SetBreakpointDialog::onCancelClicked);

    onTypeChanged(m_typeCombo->currentText());
    m_addr1Edit->setFocus();
}

void SetBreakpointDialog::setAllControlsVisible(bool visible)
{
    m_addrCondLabel->setVisible(visible);
    m_addrCondCombo->setVisible(visible);
    m_addr1Label->setVisible(visible);
    m_addr1Edit->setVisible(visible);
    m_valueCondLabel->setVisible(visible);
    m_valueCondCombo->setVisible(visible);
    m_valueLabel->setVisible(visible);
    m_addr2Edit->setVisible(visible);

    if (visible == false) {
        m_registerCombo->setVisible(false);
        m_flagCombo->setVisible(false);
    }
}

void SetBreakpointDialog::onTypeChanged(const QString &type)
{
    QString label = type;
    int dotPos = label.indexOf('.');
    if (dotPos != -1) {
        label = label.mid(dotPos + 1);  // "Execute address", "Memory", "Register", ...
    }

    setAllControlsVisible(true);

    m_addr1Edit->setVisible(true);
    m_registerCombo->setVisible(false);
    m_flagCombo->setVisible(false);

    m_addrCondLabel->setText(tr("Condition:"));
    m_addr1Label->setText(tr("Addr Start:"));
    m_valueCondLabel->setText(tr("Condition:"));
    m_valueLabel->setText(tr("Value:"));
    m_addrCondCombo->clear();
    m_valueCondCombo->clear();
    m_addrCondCombo->addItems(m_addrCondList);
    m_valueCondCombo->addItems(m_valueCondList);

    if (label.startsWith("Register")) {
        m_addr1Edit->setVisible(false);
        m_registerCombo->setVisible(true);
        m_addrCondLabel->setText(tr("Register:"));
        m_addr1Label->setText(tr("Condition:"));
        m_valueLabel->setText(tr("Value:"));
        m_addrCondCombo->clear();
        m_addrCondCombo->addItems(m_valueCondList);
        m_valueCondLabel->setVisible(false);
        m_valueCondCombo->setVisible(false);
    }
    else if (label.startsWith("Flag")) {
        m_addr1Edit->setVisible(false);
        m_flagCombo->setVisible(true);
        m_addrCondLabel->setText(tr("Flag:"));
        m_addr1Label->setText(tr("="));
        m_valueLabel->setText(tr("Value:"));
        m_addrCondCombo->setVisible(false);
        m_valueCondLabel->setVisible(false);
        m_valueCondCombo->setVisible(false);
    }
    else if (label.startsWith("Memory")) {
        m_addrCondLabel->setText(tr("Address:"));
        m_addr1Label->setText(tr("="));
        m_valueLabel->setText(tr("Value:"));
        m_addrCondCombo->setVisible(false);
        m_valueCondLabel->setVisible(true);
        m_valueCondCombo->setVisible(true);
    }
    else if (label.startsWith("Clock")) {
        m_addrCondLabel->setText(tr("Addr Start:"));
        m_addr1Label->setText(tr("T-States:"));
        m_valueLabel->setText(tr("T-States:"));
        m_addrCondCombo->clear();
        m_addrCondCombo->addItems({"="});
        m_valueCondLabel->setText(tr("<>"));
        m_valueCondCombo->setVisible(false);
    }
    else if (label.startsWith("Execute")) {
        m_addr1Label->setText(tr("Address:"));
        m_addrCondCombo->setVisible(false);
        m_addrCondLabel->setVisible(false);
        m_valueCondLabel->setVisible(false);
        m_valueCondCombo->setVisible(false);
        m_valueLabel->setVisible(false);
        m_addr2Edit->setVisible(false);
    }
    else {
        // Execute, Read, Write, IN/OUT, ...
        onAddrCondChanged(m_addrCondCombo->currentText());
    }

    updateHelpText(type);
}


void SetBreakpointDialog::onAddrCondChanged(const QString &addrCond)
{
    QString type = m_typeCombo->currentText();
    if (QStringList{"Register", "Flag", "Memory", "Clock"}.contains(type)) {
        return;
    }
    if (addrCond == "->") {
        m_valueLabel->setText(tr("Addr End:"));
        m_valueLabel->setVisible(true);
        m_addr2Edit->setVisible(true);
        m_valueCondLabel->setVisible(false);
        m_valueCondCombo->setVisible(false);
    }
    else {
        m_valueLabel->setVisible(false);
        m_addr2Edit->setVisible(false);
        if (type != "Execute") {
            m_valueCondLabel->setText(tr("Condition:"));
            m_valueLabel->setText(tr("Value:"));
            m_valueCondLabel->setVisible(true);
            m_valueCondCombo->setVisible(true);
            m_valueLabel->setVisible(true);
            m_addr2Edit->setVisible(true);
        } else {
            m_valueCondLabel->setVisible(false);
            m_valueCondCombo->setVisible(false);
            m_valueLabel->setVisible(false);
            m_addr2Edit->setVisible(false);
        }
    }
}

void SetBreakpointDialog::onOkClicked()
{
    // --- DEBUG 8 ---
    qDebug() << "[SetBreakpointDialog] onOkClicked() called.";

    m_resultString = buildOutputString();
    // --- DEBUG 9 ---
    qDebug() << "[SetBreakpointDialog] buildOutputString created:" << m_resultString;

    m_resultString = formatBreakpointString(m_resultString); // Valideer
    // --- DEBUG 10 ---
    qDebug() << "[SetBreakpointDialog] formatBreakpointString returned:" << m_resultString;

    if (m_resultString.isEmpty()) {
        // --- DEBUG 11 ---
        qWarning() << "[SetBreakpointDialog] Validation failed. Not accepting.";
        return; // Sta niet toe te sluiten
    }

    // --- DEBUG 12 ---
    qDebug() << "[SetBreakpointDialog] Validation OK. Accepting dialog.";
    QDialog::accept();
}

void SetBreakpointDialog::onCancelClicked()
{
    QDialog::reject();
}

QString SetBreakpointDialog::buildOutputString() const
{
    QString type = mapUiTypeToCore(m_typeCombo->currentText());
    QString addrCond = m_addrCondCombo->currentText();
    QString valCond = m_valueCondCombo->currentText();
    QString addr2 = m_addr2Edit->text().toUpper();

    if (type == "REG") {
        QString reg = m_registerCombo->currentText();
        return QString("REG %1 %2 %3").arg(reg, addrCond, addr2);
    }
    if (type == "FLAG") {
        QString flag = m_flagCombo->currentText();
        return QString("FLAG %1 = %2").arg(flag, addr2);
    }

    QString addr1 = m_addr1Edit->text().toUpper();

    if (type == "MEM") {
        return QString("MEM %1 %2 %3").arg(addr1, valCond, addr2);
    }
    if (type == "CLK") {
        return QString("CLK = %1 <> %2").arg(addr1, addr2);
    }
    if (addrCond == "->") { // Range
        return QString("%1 %2 ... %3").arg(type, addr1, addr2);
    }
    if (type == "EXE") { // Enkel adres
        return QString("EXE %1").arg(addr1);
    }
    return QString("%1 %2 %3 %4").arg(type, addr1, valCond, addr2);
}

void SetBreakpointDialog::parseInputString(const QString &input)
{
    if (input.isEmpty()) {
        // Bij nieuw breakpoint: gebruik huidige selectie en sync helptext
        onTypeChanged(m_typeCombo->currentText());
        return;
    }

    QString s = input.toUpper();
    s.replace(QRegularExpression("\\s+"), " ");
    QStringList parts = s.split(' ');
    QString coreType = parts[0];

    bool isSimpleAddr = false;
    bool ok = false;
    coreType.toUInt(&ok, 16);
    if (ok && parts.size() == 1) {
        coreType = "EXE";
        isSimpleAddr = true;
    }

    QString uiType = mapCoreTypeToUi(coreType);

    int idx = m_typeCombo->findText(uiType);
    if (idx != -1) {
        m_typeCombo->setCurrentIndex(idx);
    } else {
        // fallback: laat de default staan maar sync UI
        onTypeChanged(m_typeCombo->currentText());
        return;
    }

    // Dit zorgt meteen ook voor de juiste helptekst
    onTypeChanged(m_typeCombo->currentText());

    try {
        if (isSimpleAddr) {
            m_addrCondCombo->setCurrentText("=");
            m_addr1Edit->setText(parts.at(0));
            return;
        }
        QString currentLabel = m_typeCombo->currentText();
        int dotPos = currentLabel.indexOf('.');
        QString label = (dotPos != -1) ? currentLabel.mid(dotPos + 1) : currentLabel;

        if (label.startsWith("Register")) {
            m_registerCombo->setCurrentText(parts.at(1));
            m_addrCondCombo->setCurrentText(parts.at(2));
            m_addr2Edit->setText(parts.at(3));
        }
        else if (label.startsWith("Flag")) {
            m_flagCombo->setCurrentText(parts.at(1));
            m_addr2Edit->setText(parts.at(3));
        }
        else if (label.startsWith("Memory")) {
            m_addr1Edit->setText(parts.at(1));
            m_valueCondCombo->setCurrentText(parts.at(2));
            m_addr2Edit->setText(parts.at(3));
        }
        else if (label.startsWith("Clock")) {
            m_addr1Edit->setText(parts.at(2));
            m_addr2Edit->setText(parts.at(4));
        }
        else if (parts.size() == 4 && (parts.at(2) == "..." || parts.at(2) == "->")) {
            m_addrCondCombo->setCurrentText("->");
            m_addr1Edit->setText(parts.at(1));
            m_addr2Edit->setText(parts.at(3));
        }
        else if (parts.size() == 4) {
            m_addrCondCombo->setCurrentText("=");
            m_valueCondCombo->setCurrentText(parts.at(2));
            m_addr1Edit->setText(parts.at(1));
            m_addr2Edit->setText(parts.at(3));
        }
        else if (parts.size() == 2) {
            m_addrCondCombo->setCurrentText("=");
            m_addr1Edit->setText(parts.at(1));
        }
    } catch (...) {
        qWarning() << "Kan breakpoint string niet parsen:" << input;
    }
}

QString SetBreakpointDialog::getBreakpointString(QWidget *parent, const QString &currentValue)
{
    SetBreakpointDialog dlg(parent);
    dlg.parseInputString(currentValue);

    if (dlg.exec() == QDialog::Accepted) {
        return dlg.m_resultString;
    }

    return QString();
}

void SetBreakpointDialog::updateHelpText(const QString &type)
{
    QString t = type.left(2);

    QString msg;
    if        (t == "01") {
        msg = tr("Break when address has been executed.\n"
                 "Format examples:\n"
                 "  EXE BBA3\n"
                 "  BBA3  (short for EXE BBA3)");
    } else if (t == "02") {
        msg = tr("Break when memory is READ at the given address or range.\n"
                 "Examples:\n"
                 "  RD B000 -> B0FF\n"
                 "  RD B000 = FF");
    } else if (t == "03") {
        msg = tr("Break when memory is WRITTEN at the given address or range.\n"
                 "Examples:\n"
                 "  WR 6000 = 80\n"
                 "  WR 6000 -> 67FF");
    } else if (t == "04") {
        msg = tr("Break on OUT to full 16-bit I/O port address (OUT PORT-B0..B15).");
    } else if (t == "05") {
        msg = tr("Break on OUT to (HSB) high-byte of I/O port (OUT PORT-B8..B15).\n"
                 "Low 8 bits are 'xx'.");
    } else if (t == "06") {
        msg = tr("Break on OUT to (LSB) low-byte of I/O port (OUT PORT-B0..B7).");
    } else if (t == "07") {
        msg = tr("Break on IN from full 16-bit I/O port address (IN PORT-B0..B15).");
    } else if (t == "08") {
        msg = tr("Break on IN from high-byte of I/O port (IN PORT-B8..B15).");
    } else if (t == "09") {
        msg = tr("Break on IN from low-byte of I/O port (IN PORT-0..B7).");
    } else if (t == "10") {
        msg = tr("Break on Z80 T-states (CPU clock cycles).\n"
                 "Format: CLK = <addr> <> <tstates>\n"
                 "Used for profiling / timing.");
    } else if (t == "11") {
        msg = tr("Break when a memory location satisfies a value condition.\n"
                 "Format: MEM <addr> <cond> <value>\n"
                 "Example: MEM 6000 <> 00");
    } else if (t == "12") {
        msg = tr("Break when a CPU register meets a condition.\n"
                 "Format: REG <reg> <cond> <value>\n"
                 "Example: REG PC = BBA3");
    } else if (t == "13") {
        msg = tr("Break when a flag in F register is set/cleared.\n"
                 "Format: FLAG <flag> = <0/1>\n"
                 "Example: FLAG Z = 1");
    } else {
        msg = tr("Select a breakpoint type to see help.");
    }

    if (m_helpEdit)
        m_helpEdit->setPlainText(msg);
}
