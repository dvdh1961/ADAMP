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
    static const QRegularExpression reWhitespace(QStringLiteral("\\s+"));
    s.replace(reWhitespace, " ");
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
    else if (type == "EXE" && parts.size() == 4 && parts[2] == "FLAG") {
        // Form: EXE <addr> FLAG Z=0/1
        QString flagExpr = parts[3];      // bv. "Z=1"
        int eqPos = flagExpr.indexOf('=');
        if (eqPos <= 0 || eqPos == flagExpr.size() - 1) {
            qWarning() << "[BP] Invalid EXE+FLAG expression:" << input;
            return QString();
        }

        QString flagName = flagExpr.left(eqPos);       // "Z"
        QString flagVal  = flagExpr.mid(eqPos + 1);    // "1"

        if (!flags.contains(flagName)) {
            qWarning() << "[BP] Unknown flag in EXE+FLAG:" << flagName;
            return QString();
        }
        if (flagVal != "0" && flagVal != "1") {
            qWarning() << "[BP] Invalid flag value in EXE+FLAG:" << flagVal;
            return QString();
        }

        // Alles ok → vorm is geldig
        return s;
    }
    else if (type == "FLAG" && parts.size() == 4 && parts[2] == "=") {
        // Zuivere flag-breakpoint: FLAG Z = 1
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
    if (type == "EXE")  return "01.  Execute on memory address";
    if (type == "RD")   return "02.  Break when it reads from memory";
    if (type == "WR")   return "03.  Break when it writes to memory";
    if (type == "OUT")  return "04.  OUT exact port (OUT (n),A / OUT (C),A)";
    if (type == "OUTH") return "05.  OUT match high byte (port & FF00)";
    if (type == "OUTL") return "06.  OUT match low byte (port & 00FF)";
    if (type == "IN")   return "07.  IN exact port (IN A,(n) / IN A,(C))";
    if (type == "INH")  return "08.  IN match high byte (port & FF00)";
    if (type == "INL")  return "09.  IN match low byte (port & 00FF)";
    if (type == "CLK")  return "10.  Clock (T-states - profiling)";
    if (type == "MEM")  return "11.  Break when into selected memory";
    if (type == "REG")  return "12.  Break on any selected register";
    if (type == "FLAG") return "13.  Break on any selected flag";

    bool ok = false;
    type.toUInt(&ok, 16);
    if (ok)
        return "01.  Execute on memory address";

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
    setFixedSize(480,440);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGridLayout *gridLayout = new QGridLayout();
    m_typeLabel       = new QLabel(tr("Type:"), this);
    m_addrCondLabel   = new QLabel(tr("Condition:"), this);
    m_addr1Label      = new QLabel(tr("Addr/Port:"), this);
    m_valueCondLabel  = new QLabel(tr("Condition:"), this);
    m_valueLabel      = new QLabel(tr("Value:"), this);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({
        "01.  Execute on memory address",
        "02.  Break when it reads from memory",
        "03.  Break when it writes to memory",
        "04.  OUT exact port (OUT (n),A / OUT (C),A)",
        "05.  OUT match high byte (port & FF00)",
        "06.  OUT match low byte (port & 00FF)",
        "07.  IN exact port (IN A,(n) / IN A,(C))",
        "08.  IN match high byte (port & FF00)",
        "09.  IN match low byte (port & 00FF)",
        "10.  Clock (T-states - profiling)",
        "11.  Break when into selected memory",
        "12.  Break on any selected register",
        "13.  Break on any selected flag"
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

    gridLayout->addWidget(m_addr1Edit,      1, 3, 1, 1);
    gridLayout->addWidget(m_registerCombo,  1, 3, 1, 2);
    gridLayout->addWidget(m_flagCombo,      1, 4, 1, 1);

    gridLayout->addWidget(m_valueCondLabel, 2, 0);
    gridLayout->addWidget(m_valueCondCombo, 2, 1);
    gridLayout->addWidget(m_valueLabel,     2, 2);
    gridLayout->addWidget(m_addr2Edit,      2, 3, 1, 2);

    mainLayout->addLayout(gridLayout);

    m_helpEdit = new QTextEdit(this);
    m_helpEdit->setReadOnly(true);
    m_helpEdit->setFixedHeight(250);
    m_helpEdit->setWordWrapMode(QTextOption::WordWrap);
    mainLayout->addWidget(m_helpEdit);
    mainLayout->addStretch(1);

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

    // Make I/O breakpoints less confusing: use "Port" instead of "Addr"
    const bool isIoType = (type.startsWith("04") || type.startsWith("05") || type.startsWith("06") ||
                           type.startsWith("07") || type.startsWith("08") || type.startsWith("09"));

    m_addr1Edit->setVisible(true);
    m_registerCombo->setVisible(false);
    m_flagCombo->setVisible(false);

    m_addrCondLabel->setText(tr("Condition:"));
    m_addr1Label->setText(isIoType ? tr("Port:") : tr("Addr Start:"));
    m_valueCondLabel->setText(tr("Condition:"));
    m_valueLabel->setText(tr("Value:"));
    m_addrCondCombo->clear();
    m_valueCondCombo->clear();
    m_addrCondCombo->addItems(m_addrCondList);
    m_valueCondCombo->addItems(m_valueCondList);


    m_addr1Edit->setPlaceholderText(isIoType ? tr("Hex port, e.g. 98 or F198") : tr("Hex address, e.g. 8000"));
    m_addr2Edit->setPlaceholderText(isIoType ? tr("Hex value or end port") : tr("Hex value or end address"));
    m_addr1Edit->setToolTip(isIoType
        ? tr("I/O port in hex. For OUT (98),A enter 98. For OUT (C),A with BC=F198 enter F198.")
        : tr("Address in hex."));

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
        // Toon PC-adres én vlag
        m_addr1Edit->setVisible(true);   // <-- adresveld tonen
        m_flagCombo->setVisible(true);

        m_addrCondLabel->setText(tr("Address:"));
        m_addr1Label->setText(tr("PC Addr:"));
        m_valueLabel->setText(tr("Flag (0/1):"));

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
    QString typeText = m_typeCombo->currentText();
    int dotPos = typeText.indexOf('.');
    QString label = (dotPos != -1) ? typeText.mid(dotPos + 1) : typeText;
    if (label.startsWith("Register") || label.startsWith("Flag") || label.startsWith("Memory") || label.startsWith("Clock")) {
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
        if (!label.startsWith("Execute")) {
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
    QString type    = mapUiTypeToCore(m_typeCombo->currentText());
    QString addrCond = m_addrCondCombo->currentText().trimmed();
    QString valCond  = m_valueCondCombo->currentText().trimmed();

    QString addr1 = m_addr1Edit->text().trimmed().toUpper();
    QString addr2 = m_addr2Edit->text().trimmed().toUpper();

    // Allow "0x" prefix in GUI fields
    if (addr1.startsWith("0X")) addr1 = addr1.mid(2);
    if (addr2.startsWith("0X")) addr2 = addr2.mid(2);

    // REG: "REG <reg> <cond> <value>"
    if (type == "REG") {
        const QString reg = m_registerCombo->currentText().trimmed().toUpper();
        if (reg.isEmpty() || addrCond.isEmpty() || addr2.isEmpty())
            return QString();
        return QString("REG %1 %2 %3").arg(reg, addrCond, addr2);
    }

    // FLAG: "FLAG Z = 0/1"
    if (type == "FLAG") {
        const QString flag = m_flagCombo->currentText().trimmed().toUpper();
        if (flag.isEmpty() || addrCond.isEmpty() || addr2.isEmpty())
            return QString();
        return QString("FLAG %1 %2 %3").arg(flag, addrCond, addr2);
    }

    // CLK: keep existing special format used by your parser: "CLK = <a> <> <b>"
    if (type == "CLK") {
        if (addr1.isEmpty() || addr2.isEmpty())
            return QString();
        return QString("CLK = %1 <> %2").arg(addr1, addr2);
    }

    // Types that require an address/port
    const bool needsAddr =
        (type == "EXE" || type == "RD" || type == "WR" || type == "MEM" ||
         type == "IN"  || type == "OUT" ||
         type == "INH" || type == "INL" || type == "OUTH" || type == "OUTL");

    if (needsAddr && addr1.isEmpty())
        return QString();

    // Range: "<TYPE> <start> ... <end>"
    if (addrCond == "->") {
        if (addr2.isEmpty())
            return QString();
        return QString("%1 %2 ... %3").arg(type, addr1, addr2);
    }

    // Auto-fix Z80 reality for immediate IO ports (OUT (n),A / IN A,(n)):
    // The Z80 uses A as the *high* byte for these forms, so matching an 8-bit port
    // is almost always done via low-byte matching (OUTL/INL).
    // If the user entered only 1–2 hex digits (e.g. "98"), treat it as low-byte match.
    auto isShortHex = [](const QString& s) -> bool {
        if (s.isEmpty() || s.size() > 2) return false;
        bool ok = false;
        s.toUInt(&ok, 16);
        return ok;
    };

    QString effectiveType = type;
    if (type == "OUT" && isShortHex(addr1)) effectiveType = "OUTL";
    if (type == "IN"  && isShortHex(addr1)) effectiveType = "INL";

    // Value compare: "<TYPE> <addr> <cond> <value>"
    // Only emit if the value field is visible (UI intent) and value is provided.
    if (m_valueCondCombo->isVisible() && m_addr2Edit->isVisible() && !addr2.isEmpty()) {
        return QString("%1 %2 %3 %4").arg(effectiveType, addr1, valCond, addr2);
    }

    // Simple: "<TYPE> <addr>"
    if (effectiveType == "EXE") {
        return QString("EXE %1").arg(addr1);
    }
    return QString("%1 %2").arg(effectiveType, addr1);
}

void SetBreakpointDialog::parseInputString(const QString &input)
{
    if (input.isEmpty()) {
        // Bij nieuw breakpoint: gebruik huidige selectie en sync helptext
        onTypeChanged(m_typeCombo->currentText());
        return;
    }

    QString s = input.toUpper();
    static const QRegularExpression reWhitespace(QStringLiteral("\\s+"));
    s.replace(reWhitespace, " ");
    QStringList parts = s.split(' ');
    QString coreType = parts[0];

    bool isSimpleAddr = false;
    bool ok = false;
    coreType.toUInt(&ok, 16);
    if (ok && parts.size() == 1) {
        coreType = "EXE";
        isSimpleAddr = true;
    }

    // Speciaal geval: EXE <addr> FLAG Z=0/1 -> behandelen als "Flag"-breakpoint
    if (coreType == "EXE" && parts.size() == 4 && parts[2] == "FLAG") {
        // Schakel UI naar type 13.Flag
        int idxFlag = m_typeCombo->findText("13.Flag");
        if (idxFlag != -1) {
            m_typeCombo->setCurrentIndex(idxFlag);
        }
        // Zorg dat de juiste widgets zichtbaar zijn (addr + flag + value)
        onTypeChanged(m_typeCombo->currentText());

        // parts[1] = adres
        m_addr1Edit->setText(parts.at(1));

        // parts[3] = bv. "Z=1"
        QString flagExpr = parts.at(3);
        int eqPos = flagExpr.indexOf('=');
        if (eqPos > 0 && eqPos < flagExpr.size() - 1) {
            QString flagName = flagExpr.left(eqPos);      // "Z"
            QString flagVal  = flagExpr.mid(eqPos + 1);   // "1"

            m_flagCombo->setCurrentText(flagName);
            m_addr2Edit->setText(flagVal);
        }

        return; // Klaar, niet verder parsen als gewone EXE
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
        msg = tr("Break on OUT to an exact I/O port.\n"
                 "Covers both Z80 forms:\n"
                 "  OUT (n),A   (8-bit port, e.g. OUT (98),A)\n"
                 "  OUT (C),A   (16-bit port via BC, e.g. BC=F198 -> OUT (C),A)\n"
                 "\nDialog fields:\n"
                 "  Port: enter the port in hex (e.g. 98 or F198)\n"
                 "  Value: optional (match the byte written)\n"
                 "\nExamples:\n"
                 "  OUT 98\n"
                 "  OUT 98 = 80\n"
                 "  OUT F198");
    } else if (t == "05") {
        msg = tr("Break on OUT when the HIGH byte of the 16-bit port matches.\n"
                 "Match rule: (port & FF00) == (xx << 8)\n"
                 "So OUTH F1 matches ports F100..F1FF.\n"
                 "\nDialog fields:\n"
                 "  Port: enter the HIGH byte (00..FF)\n"
                 "  Value: optional\n"
                 "\nExamples:\n"
                 "  OUTH F1\n"
                 "  OUTH F1 = 80");
    } else if (t == "06") {
        msg = tr("Break on OUT when the LOW byte of the 16-bit port matches.\n"
                 "Match rule: (port & 00FF) == nn\n"
                 "So OUTL 98 matches 0098, 0198, F198, ...\n"
                 "\nDialog fields:\n"
                 "  Port: enter the LOW byte (00..FF)\n"
                 "  Value: optional\n"
                 "\nExamples:\n"
                 "  OUTL 98\n"
                 "  OUTL 98 <> 00");
    } else if (t == "07") {
        msg = tr("Break on IN from an exact I/O port.\n"
                 "Covers both Z80 forms:\n"
                 "  IN A,(n)    (8-bit port, e.g. IN A,(99))\n"
                 "  IN A,(C)    (16-bit port via BC, e.g. BC=F198 -> IN A,(C))\n"
                 "\nDialog fields:\n"
                 "  Port: enter the port in hex (e.g. 99 or F198)\n"
                 "  Value: optional (match the byte read)\n"
                 "\nExamples:\n"
                 "  IN 99\n"
                 "  IN 99 = 80\n"
                 "  IN F198");
    } else if (t == "08") {
        msg = tr("Break on IN when the HIGH byte of the 16-bit port matches.\n"
                 "Match rule: (port & FF00) == (xx << 8)\n"
                 "So INH F1 matches ports F100..F1FF.\n"
                 "\nDialog fields:\n"
                 "  Port: enter the HIGH byte (00..FF)\n"
                 "  Value: not used for INH\n"
                 "\nExample:\n"
                 "  INH F1");
    } else if (t == "09") {
        msg = tr("Break on IN when the LOW byte of the 16-bit port matches.\n"
                 "Match rule: (port & 00FF) == nn\n"
                 "So INL 99 matches 0099, 0199, F199, ...\n"
                 "\nDialog fields:\n"
                 "  Port: enter the LOW byte (00..FF)\n"
                 "  Value: optional\n"
                 "\nExamples:\n"
                 "  INL 99\n"
                 "  INL 99 = 80");
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
        msg = tr("Break when a flag in the F register meets condition.\n"
                 "Must be combined with an address breakpoint.\n"
                 "\nFormat:\n"
                 "  EXE <addr> FLAG <flag>=<0/1>\n"
                 "Example:\n"
                 "  EXE 8000 FLAG Z=1\n"
                 "\nNote: FLAG alone is no longer valid.");
    } else {
        msg = tr("Select a breakpoint type to see help.");
    }

    if (m_helpEdit)
        m_helpEdit->setPlainText(msg);
}
