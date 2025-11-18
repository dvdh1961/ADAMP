#include "gotoaddressdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QIcon>
#include <QPixmap>
#include <QDebug>

GotoAddressDialog::GotoAddressDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

void GotoAddressDialog::setupUi()
{
    setWindowTitle(tr("Goto address"));
    resize(260, 130);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Label + hex invoer
    QHBoxLayout *row = new QHBoxLayout();
    QLabel *lbl = new QLabel(tr("Address (hex):"), this);
    m_addrEdit = new QLineEdit(this);
    m_addrEdit->setMaxLength(4);
    m_addrEdit->setAlignment(Qt::AlignLeft);

    // Enkel 1..4 hex chars
    auto *validator = new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Fa-f]{1,4}"),
        m_addrEdit
        );
    m_addrEdit->setValidator(validator);

    row->addWidget(lbl);
    row->addWidget(m_addrEdit);
    mainLayout->addLayout(row);

    mainLayout->addStretch();

    // Image knoppen (zelfde stijl als SetBreakpointDialog)
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    QIcon okIcon(":/images/images/OK.png");
    QIcon cancelIcon(":/images/images/CANCEL.png");
    QPixmap okPixmap(":/images/images/OK.png");
    QPixmap cancelPixmap(":/images/images/CANCEL.png");

    if (okIcon.isNull()) {
        qWarning() << "GotoAddressDialog: Kon OK.png niet laden.";
    }
    if (cancelIcon.isNull()) {
        qWarning() << "GotoAddressDialog: Kon CANCEL.png niet laden.";
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

    connect(m_okButton, &QPushButton::clicked,
            this, &GotoAddressDialog::onOkClicked);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &GotoAddressDialog::onCancelClicked);

    m_addrEdit->setFocus();
}

bool GotoAddressDialog::getAddress(QWidget *parent,
                                   uint16_t currentPC,
                                   uint16_t &outAddr)
{
    GotoAddressDialog dlg(parent);

    // Voorvullen met huidige PC
    dlg.m_addrEdit->setText(
        QString("%1").arg(currentPC, 4, 16, QChar('0')).toUpper()
        );

    if (dlg.exec() == QDialog::Accepted) {
        outAddr = dlg.m_resultAddr;
        return true;
    }
    return false;
}

void GotoAddressDialog::onOkClicked()
{
    bool ok = false;
    QString text = m_addrEdit->text().trimmed();

    if (text.isEmpty()) {
        // Niets ingevuld -> negeren, dialoog blijft open
        return;
    }

    uint16_t val = static_cast<uint16_t>(text.toUShort(&ok, 16));
    if (!ok) {
        // Ongeldige hex -> negeren, dialoog blijft open
        return;
    }

    m_resultAddr = val;
    accept();
}

void GotoAddressDialog::onCancelClicked()
{
    reject();
}
