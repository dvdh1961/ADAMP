#include "cartridgeinfowindow.h"
#include "cv.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTextEdit>
#include <QScrollArea>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QFontDatabase>
#include <cstring>
#include <cstdio>

CartridgeInfoDialog::CartridgeInfoDialog(QWidget *parent)
    : QDialog(parent), m_valEmpty(0xFF)
{
    setWindowTitle(tr("Cartridge Info"));
    setMinimumSize(770, 700);
    setupUi();

    connect(m_emptyFF, &QRadioButton::toggled, this, &CartridgeInfoDialog::updateEmptyValueAndRefresh);
}

CartridgeInfoDialog::~CartridgeInfoDialog()
{
}

void CartridgeInfoDialog::setupUi()
{
    // --- Linkerpaneel ---
    m_memoEdit = new QTextEdit(this);
    m_memoEdit->setReadOnly(true);
    //m_memoEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // 1. Laad het lettertype uit je resources of lokale map
    // Pas het pad aan naar waar jouw .ttf staat (bijv. ":/fonts/mijnfont.ttf")
    int fontId = QFontDatabase::addApplicationFont(":/fonts/fonts/luculent.ttf");

    QString family;

    if (fontId != -1) {
        family = QFontDatabase::applicationFontFamilies(fontId).at(0);
        qDebug() << "[UI] Custom font loaded:" << family;
    } else {
        qDebug() << "[UI] Could not load custom font, fallback to  Roboto";
        family = "Roboto";
    }

    QFont monoFont(family, 10);
    monoFont.setBold(false);

    m_memoEdit->setFont(monoFont);

    m_memoEdit->setMinimumWidth(380);

    // --- Rechterpaneel (Memory Footprint) ---
    QVBoxLayout *rightLayout = new QVBoxLayout();
    QLabel *footprintLabel = new QLabel(tr("Memory Footprint"), this);
    footprintLabel->setStyleSheet("font-weight: bold;");

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_bankContainer = new QWidget(m_scrollArea);
    m_scrollArea->setWidget(m_bankContainer);

    m_bankContainer->setLayout(new QGridLayout());

    rightLayout->addWidget(footprintLabel);
    rightLayout->addWidget(m_scrollArea, 1);


    QIcon refreshIcon(":/images/images/REFRESH.png");
    QIcon closeIcon(":/images/images/CLOSE.png");
    QPixmap refreshPixmap(":/images/images/REFRESH.png");
    QPixmap closePixmap(":/images/images/CLOSE.png");

    if (refreshIcon.isNull()) { qWarning() << "CartridgeInfoDialog: Could not load REFRESH.png ."; }
    if (closeIcon.isNull()) { qWarning() << "CartridgeInfoDialog: Could not load CLOSE.png ."; }

    QSize refreshSize = refreshPixmap.size();
    QSize closeSize = closePixmap.size();

    QPushButton *refreshButton = new QPushButton(this);
    refreshButton->setIcon(refreshIcon);
    refreshButton->setIconSize(refreshSize);
    refreshButton->setFixedSize(refreshSize);
    refreshButton->setText("");
    refreshButton->setFlat(true);
    refreshButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    QPushButton *closeButton = new QPushButton(this);
    closeButton->setIcon(closeIcon);
    closeButton->setIconSize(closeSize);
    closeButton->setFixedSize(closeSize);
    closeButton->setText("");
    closeButton->setFlat(true);
    closeButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    refreshButton->setCursor(Qt::PointingHandCursor);
    closeButton->setCursor(Qt::PointingHandCursor);

    connect(refreshButton, &QPushButton::clicked, this, &CartridgeInfoDialog::refreshData);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    QHBoxLayout *radioAndButtonLayout = new QHBoxLayout();
    radioAndButtonLayout->addWidget(m_emptyFF);
    radioAndButtonLayout->addWidget(m_empty00);

    radioAndButtonLayout->addStretch();

    radioAndButtonLayout->addWidget(refreshButton);
    radioAndButtonLayout->addWidget(closeButton);

    rightLayout->addLayout(radioAndButtonLayout);

    // --- Hoofd layout ---
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(m_memoEdit, 1);

    QWidget *rightWidget = new QWidget(this);
    rightWidget->setLayout(rightLayout);
    mainLayout->addWidget(rightWidget, 1);

    // --- Globale layout ---
    QVBoxLayout *globalLayout = new QVBoxLayout(this);
    globalLayout->addLayout(mainLayout, 1);
}

void CartridgeInfoDialog::refreshData()
{
    showProfile();
    showBanks();
}

void CartridgeInfoDialog::updateEmptyValueAndRefresh()
{
    m_valEmpty = m_emptyFF->isChecked() ? 0xFF : 0x00;
    refreshData();
}

void CartridgeInfoDialog::showProfile()
{
        QString line;
        char text[384];
        unsigned char bval;
        QString html;

        //const QString monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();

        // 1. Laad het lettertype uit je resources of lokale map
        // Pas het pad aan naar waar jouw .ttf staat (bijv. ":/fonts/mijnfont.ttf")
        int fontId = QFontDatabase::addApplicationFont(":/fonts/fonts/luculent.ttf");

        QString family;

        if (fontId != -1) {
            family = QFontDatabase::applicationFontFamilies(fontId).at(0);
            qDebug() << "[UI] Custom font loaded:" << family;
        } else {
            qDebug() << "[UI] Could not load custom font, fallback to Roboto";
            family = "Roboto";
        }

        QFont monoFont(family, 12);
        monoFont.setBold(false);

        const QString titleStyle = "style='background-color:#444444; color:#AAAA00; font-weight:bold; padding:2px; margin: 5px 0 0 0;'";

        const QString bodyStyle = QString("style='font-family: \"%1\"; white-space: pre; margin: 0; padding: 0;'").arg(monoFont.family());

        m_memoEdit->clear();

        html = "<html><body>";

        html += QString("<div %1>Cartridge Identifier</div>").arg(titleStyle);

        if (emulator == nullptr || emulator->romCartridgeType == ROMCARTRIDGENONE) {
            html += QString("<p %1>No ROM loaded.</p>").arg(bodyStyle);
            m_memoEdit->setHtml(html + "</body>");
            return;
        }

        html += QString("<p %1>%2</p>").arg(bodyStyle).arg(QString("Name: %1").arg(emulator->currentrom).toHtmlEscaped());

        BYTE b0 = coleco_getbyte(0x8000);
        BYTE b1 = coleco_getbyte(0x8001);

        QString headerType;
        if (b0 == 0xAA && b1 == 0x55)
            headerType = "Header Type: Game (0xAA55)";
        else if (b0 == 0x55 && b1 == 0xAA)
            headerType = "Header Type: Test (0x55AA)";
        else if (emulator->romCartridgeType == ROMCARTRIDGEMEGA)
            headerType = QString("Header Type: MegaCart (0x%1)").arg(b0 + b1 * 256, 4, 16, QChar('0')).toUpper();
        else
            headerType = "Header Type: Invalid (InsertCartridge message)";
        html += QString("<p %1>%2</p>").arg(bodyStyle).arg(headerType);

        // Game Name
        strcpy(text, "Game Name  : ....................................");
        for (int ix = 0; ix < 36; ix++)
        {
            bval = coleco_getbyte(0x8024 + ix);
            if (bval >= 32 && bval <= 127)
                text[ix + 13] = (char)bval;
        }
        html += QString("<p %1>%2</p>").arg(bodyStyle).arg(QString(text).toHtmlEscaped());

        // Backup Type
        QString backupType = "No Backup SRAM/EEPROM";
        if (emulator->typebackup == EEP24C08) backupType = "256-byte EEPROM";
        else if (emulator->typebackup == EEP24C256) backupType = "32kB EEPROM";
        else if (emulator->typebackup == EEPSRAM) backupType = "2kB SRAM";
        html += QString("<p %1>%2</p>").arg(bodyStyle).arg(backupType);


        // --- Pointers ---
        html += QString("<div %1>Pointers</div>").arg(titleStyle);
        html += QString("<p %1>Sprite Table    : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x8002) + (coleco_getbyte(0x8003) * 256), 4, 16, QChar('0')).toUpper();
        html += QString("<p %1>Sprite Order    : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x8004) + (coleco_getbyte(0x8005) * 256), 4, 16, QChar('0')).toUpper();
        html += QString("<p %1>Work Buffer     : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x8006) + (coleco_getbyte(0x8007) * 256), 4, 16, QChar('0')).toUpper();
        html += QString("<p %1>Controller Map  : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x8008) + (coleco_getbyte(0x8009) * 256), 4, 16, QChar('0')).toUpper();
        html += QString("<p %1>Game Start      : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x800A) + (coleco_getbyte(0x800B) * 256), 4, 16, QChar('0')).toUpper();

        // --- Restart Vectors ---
        html += QString("<div %1>Restart Vectors</div>").arg(titleStyle);
        for (int iy = 0x800C; iy < 0x801D; iy += 3) {
            line = QString("Rst%1H          : $%2")
            .arg((iy - 0x800C) / 3 * 8 + 8, 2, 16, QChar('0')).toUpper()
                .arg(coleco_getbyte(iy) + (coleco_getbyte(iy + 1) * 256), 4, 16, QChar('0')).toUpper();
            html += QString("<p %1>%2</p>").arg(bodyStyle).arg(line);
        }

        // --- Interrupt Vectors ---
        html += QString("<div %1>Interrupt Vectors</div>").arg(titleStyle);
        html += QString("<p %1>IRQ Vector      : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x801E) + (coleco_getbyte(0x801F) * 256), 4, 16, QChar('0')).toUpper();
        html += QString("<p %1>NMI Vector      : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x8021) + (coleco_getbyte(0x8022) * 256), 4, 16, QChar('0')).toUpper();

        // --- Header Dump ---
        html += QString("<div %1>Header Dump</div>").arg(titleStyle);

        for (int iy = 0x8000; iy < 0x8040; iy += 8)
        {
            QString lineHtml = QString("%1: ").arg(iy, 4, 16, QChar('0')).toUpper();
            QString charsHtml = " ";
            for (int ix = 0; ix < 8; ix++)
            {
                bval = coleco_getbyte(iy + ix);
                lineHtml += QString("%1 ").arg(bval, 2, 16, QChar('0')).toUpper();

                QChar c = QChar(bval);
                charsHtml += (c.isPrint()) ? QString(c).toHtmlEscaped() : QChar('.');
                charsHtml += " ";
            }
            html += QString("<p %1>%2 </p>").arg(bodyStyle).arg(lineHtml + charsHtml);
        }

        html += "</body></html>";

        m_memoEdit->setHtml(html);
    }

void CartridgeInfoDialog::showBanks()
{
    if (!m_bankContainer) {
        qWarning("CartridgeInfoDialog: m_bankContainer is null!");
        return;
    }

    QGridLayout *layout = qobject_cast<QGridLayout*>(m_bankContainer->layout());

    if (!layout) {
        delete m_bankContainer->layout();

        layout = new QGridLayout();
        m_bankContainer->setLayout(layout);
    }

    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    if (emulator == nullptr || emulator->romCartridgeType == ROMCARTRIDGENONE) {
        layout->addWidget(new QLabel(tr("No ROM loaded."), this), 0, 0);
        layout->setRowStretch(1, 1);
        layout->setColumnStretch(1, 1);
        return;
    }

    const BYTE *romentry = ROM_Memory;
    int bknum = coleco_megasize;

    for (int i = 0; i < bknum; i++)
    {
        int row = i / 2; // 0, 0, 1, 1, 2, 2, etc.
        int col = i % 2; // 0, 1, 0, 1, 0, 1, etc.

        const BYTE *bankData = romentry + (i * 16384);
        QWidget *bankWidget = createBankWidget(i, bankData);

        layout->addWidget(bankWidget, row, col, Qt::AlignTop | Qt::AlignLeft);
    }

    if (bknum > 0) {
        int last_row_index = (bknum - 1) / 2;
        layout->setRowStretch(last_row_index + 1, 1);
    } else {
        layout->setRowStretch(0, 1);
    }

    layout->setColumnStretch(0, 0);
    layout->setColumnStretch(1, 0);
    layout->setColumnStretch(2, 1);
}

QWidget* CartridgeInfoDialog::createBankWidget(int bankIndex, const BYTE* data)
{
    // --- Container ---
    QWidget *container = new QWidget(this);
    container->setFixedSize(110, 180);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    // --- Titel (BANK $01) ---
    QLabel *title = new QLabel(QString("BANK $%1").arg(bankIndex +1 , 2, 16, QChar('0')).toUpper(), this);
    title->setAlignment(Qt::AlignCenter);

    // --- Afbeelding (64x128) ---
    QImage bankImage(64, 128, QImage::Format_Indexed8);
    bankImage.setColor(0, qRgb(180, 180, 0));
    bankImage.setColor(1, qRgb(40, 40, 40));

    const BYTE *s = data;
    for (int y = 0; y < 128; y++) {
        uchar *line = bankImage.scanLine(y);
        for (int x = 0; x < 64; x++) {
            line[x] = (*s == m_valEmpty) ? 1 : 0;
            s += 2;
        }
    }

    QLabel *imageLabel = new QLabel(this);
    imageLabel->setPixmap(QPixmap::fromImage(bankImage));
    imageLabel->setFixedSize(64, 128);

    int bf = 0;
    const BYTE *src = data;
    for (int i = 0; i < 1024; ++i)
    {
        int n = 16;
        for (int j = 0; j < 16; j++)
        {
            if (*src++ != m_valEmpty) n = 0;
        }
        bf += n;
    }

    float percent = (bf > 0) ? (100.0f / 16384.0f) * (float)bf : 0.0f;
    QLabel *info = new QLabel(QString("~%1K free (%2%)").arg(bf / 1024).arg(percent, 0, 'f', 0), this);
    info->setAlignment(Qt::AlignCenter); // Was Qt::AlignCenter

    layout->addWidget(title);
    layout->addWidget(imageLabel, 0, Qt::AlignCenter);
    layout->addWidget(info);
    layout->addStretch();

    return container;
}
