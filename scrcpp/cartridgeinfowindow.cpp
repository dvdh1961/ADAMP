#include "cartridgeinfowindow.h"
#include "coleco.h"

// Qt Includes
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
#include <QPainter> // Nodig voor QImage vullen
#include <QFontDatabase>
// Standaard C/C++
#include <cstring>
#include <cstdio> // voor sprintf

CartridgeInfoDialog::CartridgeInfoDialog(QWidget *parent)
    : QDialog(parent), m_valEmpty(0xFF)
{
    setWindowTitle(tr("Cartridge Info"));
    setMinimumSize(770, 700);
    setupUi();

    // Connecteer de radiobuttons aan hun slot
    connect(m_emptyFF, &QRadioButton::toggled, this, &CartridgeInfoDialog::updateEmptyValueAndRefresh);
    // (m_empty00 wordt via de buttongroup afgehandeld)
}

CartridgeInfoDialog::~CartridgeInfoDialog()
{
    // Standaard destructor. UI-elementen worden automatisch opgeruimd.
}

void CartridgeInfoDialog::setupUi()
{
    // --- Linkerpaneel (Tekstinfo) ---
    m_memoEdit = new QTextEdit(this);
    m_memoEdit->setReadOnly(true);
    m_memoEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
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

    // --- Knoppen ---
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), this);
    QPushButton *closeButton = new QPushButton(tr("OK"), this); // Dit is de 'OK' knop
    connect(refreshButton, &QPushButton::clicked, this, &CartridgeInfoDialog::refreshData);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    // --- Radiobuttons & Knoppen Layout ---
    //m_emptyFF = new QRadioButton(tr("Empty=Green"), this);
    //m_empty00 = new QRadioButton(tr("Empty=Black"), this);
    //m_emptyFF->setChecked(true);
    //m_emptyGroup = new QButtonGroup(this);
    //m_emptyGroup->addButton(m_emptyFF);
    //m_emptyGroup->addButton(m_empty00);

    QHBoxLayout *radioAndButtonLayout = new QHBoxLayout();
    radioAndButtonLayout->addWidget(m_emptyFF);
    radioAndButtonLayout->addWidget(m_empty00);
    // Voeg de knoppen hier direct toe
    radioAndButtonLayout->addWidget(refreshButton);
    radioAndButtonLayout->addWidget(closeButton);
    // --- AANGEPAST ---
    // Verplaats de stretch naar het einde
    //radioAndButtonLayout->addStretch(); // <--- STRETCH (duwt alles naar links)

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
    // Roep de twee helpers aan die de UI vullen
    showProfile();
    showBanks();
}

void CartridgeInfoDialog::updateEmptyValueAndRefresh()
{
    // Update de waarde en roep refreshData aan
    m_valEmpty = m_emptyFF->isChecked() ? 0xFF : 0x00;
    refreshData();
}

void CartridgeInfoDialog::showProfile()
{
    QString line; // <--- THIS LINE WAS MISSING

        char text[384];
        unsigned char bval;
        QString html;

        const QString monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();

        // Stijl voor de titels (rode achtergrond, witte tekst, vet)
        const QString titleStyle = "style='background-color:#444444; color:#AAAA00; font-weight:bold; padding:2px; margin: 5px 0 0 0;'";

        // Stijl voor de gewone tekst.
        // font-family: zorgt voor monospace.
        // white-space: pre; zorgt ervoor dat alle spaties behouden blijven.
        // margin: 0; padding: 0; verwijdert extra witruimte tussen regels.
        const QString bodyStyle = QString("style='font-family: \"%1\"; white-space: pre; margin: 0; padding: 0;'").arg(monoFont);

        m_memoEdit->clear(); // Maak de inhoud leeg

        // Begin de HTML
        html = "<html><body>";

        // --- Titel 1: Cartridge Identifier ---
        html += QString("<div %1>Cartridge Identifier</div>").arg(titleStyle);

        // Check of er een ROM geladen is
        if (emul2 == nullptr || emul2->romCartridgeType == ROMCARTRIDGENONE) {
            html += QString("<p %1>No ROM loaded.</p>").arg(bodyStyle);
            m_memoEdit->setHtml(html + "</body>"); // Sluit de body-tag af en zet de HTML
            return;
        }

        // Info lijnen (platte tekst)
        // We gebruiken .toHtmlEscaped() om ervoor te zorgen dat eventuele speciale tekens in de naam correct worden weergegeven.
        html += QString("<p %1>%2</p>").arg(bodyStyle).arg(QString("Name: %1").arg(emul2->currentrom).toHtmlEscaped());

        BYTE b0 = coleco_getbyte(0x8000);
        BYTE b1 = coleco_getbyte(0x8001);

        // Bepaal het header-type
        QString headerType;
        if (b0 == 0xAA && b1 == 0x55)
            headerType = "Header Type: Game (0xAA55)";
        else if (b0 == 0x55 && b1 == 0xAA)
            headerType = "Header Type: Test (0x55AA)";
        else if (emul2->romCartridgeType == ROMCARTRIDGEMEGA)
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
        if (emul2->typebackup == EEP24C08) backupType = "256-byte EEPROM";
        else if (emul2->typebackup == EEP24C256) backupType = "32kB EEPROM";
        else if (emul2->typebackup == EEPSRAM) backupType = "2kB SRAM";
        html += QString("<p %1>%2</p>").arg(bodyStyle).arg(backupType);


        // --- Titel 2: Pointers ---
        html += QString("<div %1>Pointers</div>").arg(titleStyle);
        html += QString("<p %1>Sprite Table    : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x8002) + (coleco_getbyte(0x8003) * 256), 4, 16, QChar('0')).toUpper();
        html += QString("<p %1>Sprite Order    : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x8004) + (coleco_getbyte(0x8005) * 256), 4, 16, QChar('0')).toUpper();
        html += QString("<p %1>Work Buffer     : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x8006) + (coleco_getbyte(0x8007) * 256), 4, 16, QChar('0')).toUpper();
        html += QString("<p %1>Controller Map  : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x8008) + (coleco_getbyte(0x8009) * 256), 4, 16, QChar('0')).toUpper();
        html += QString("<p %1>Game Start      : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x800A) + (coleco_getbyte(0x800B) * 256), 4, 16, QChar('0')).toUpper();

        // --- Titel 3: Restart Vectors ---
        html += QString("<div %1>Restart Vectors</div>").arg(titleStyle);
        for (int iy = 0x800C; iy < 0x801D; iy += 3) {
            line = QString("Rst%1H          : $%2")
            .arg((iy - 0x800C) / 3 * 8 + 8, 2, 16, QChar('0')).toUpper()
                .arg(coleco_getbyte(iy) + (coleco_getbyte(iy + 1) * 256), 4, 16, QChar('0')).toUpper();
            html += QString("<p %1>%2</p>").arg(bodyStyle).arg(line);
        }

        // --- Titel 4: Interrupt Vectors ---
        html += QString("<div %1>Interrupt Vectors</div>").arg(titleStyle);
        html += QString("<p %1>IRQ Vector      : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x801E) + (coleco_getbyte(0x801F) * 256), 4, 16, QChar('0')).toUpper();
        html += QString("<p %1>NMI Vector      : $%2</p>").arg(bodyStyle).arg(coleco_getbyte(0x8021) + (coleco_getbyte(0x8022) * 256), 4, 16, QChar('0')).toUpper();

        // --- Titel 5: Header Dump ---
        html += QString("<div %1>Header Dump</div>").arg(titleStyle);

        // Voor de dump gebruiken we nu ook <p> met de bodyStyle, omdat die al 'white-space: pre' bevat
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
            }
            html += QString("<p %1>%2</p>").arg(bodyStyle).arg(lineHtml + charsHtml);
        }

        // Sluit de body tag
        html += "</body></html>";

        // Zet de volledige HTML-string in één keer
        m_memoEdit->setHtml(html);
    }
// ... (includes, constructor, setupUi, showProfile, etc. blijven hetzelfde) ...

void CartridgeInfoDialog::showBanks()
{
    // --- AANGEPAST: Robuustere layout-afhandeling ---
    if (!m_bankContainer) {
        // Dit mag nooit gebeuren als setupUi() is aangeroepen
        qWarning("CartridgeInfoDialog: m_bankContainer is null!");
        return;
    }

    // 1. Haal de layout op en cast direct
    QGridLayout *layout = qobject_cast<QGridLayout*>(m_bankContainer->layout());

    // 2. Als layout niet bestaat of geen grid is, maak een nieuwe
    if (!layout) {
        // Verwijder de oude layout als die bestond maar van verkeerd type was
        delete m_bankContainer->layout();

        layout = new QGridLayout();
        m_bankContainer->setLayout(layout);
    }

    // 3. Maak de grid leeg (veilig, want we weten dat 'layout' valid is)
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }
    // --- EINDE AANPASSING ---

    if (emul2 == nullptr || emul2->romCartridgeType == ROMCARTRIDGENONE) {
        layout->addWidget(new QLabel(tr("Geen ROM geladen."), this), 0, 0);
        layout->setRowStretch(1, 1); // Zorg dat de melding bovenaan blijft
        layout->setColumnStretch(1, 1); // Zorg dat de melding links blijft
        return;
    }

    // Haal ROM data op
    const BYTE *romentry = ROM_Memory;
    int bknum = coleco_megasize; // Dit is het aantal 16K banken

    for (int i = 0; i < bknum; i++)
    {
        int row = i / 2; // 0, 0, 1, 1, 2, 2, etc.
        int col = i % 2; // 0, 1, 0, 1, 0, 1, etc.

        const BYTE *bankData = romentry + (i * 16384); // 16K per bank
        QWidget *bankWidget = createBankWidget(i, bankData);

        layout->addWidget(bankWidget, row, col, Qt::AlignTop | Qt::AlignLeft); // Lijn widgets uit
    }

    // Zorg dat de widgets bovenaan beginnen (voeg stretch toe aan de *volgende* rij)
    if (bknum > 0) {
        int last_row_index = (bknum - 1) / 2;
        layout->setRowStretch(last_row_index + 1, 1);
    } else {
        layout->setRowStretch(0, 1); // Stretch op eerste rij als leeg
    }

    // Zorg dat de kolommen niet uitrekken en links lijnen
    layout->setColumnStretch(0, 0); // Kolom 0: geen stretch
    layout->setColumnStretch(1, 0); // Kolom 1: geen stretch
    layout->setColumnStretch(2, 1); // Voeg een lege, rekkende 3e kolom toe
}

QWidget* CartridgeInfoDialog::createBankWidget(int bankIndex, const BYTE* data)
{
    // --- Container voor deze bank ---
    QWidget *container = new QWidget(this);
    // De breedte iets groter maken zodat de linkse tekst past
    container->setFixedSize(110, 180);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    // --- Titel (BANK $01) ---
    QLabel *title = new QLabel(QString("BANK $%1").arg(bankIndex +1 , 2, 16, QChar('0')).toUpper(), this);
    // --- AANGEPAST ---
    title->setAlignment(Qt::AlignCenter); // Was Qt::AlignCenter

    // --- Afbeelding (64x128) ---
    QImage bankImage(64, 128, QImage::Format_Indexed8);
    bankImage.setColor(0, qRgb(180, 180, 0)); // Index 0: donker (Gebruikt)
    bankImage.setColor(1, qRgb(40, 40, 40));   // Index 1: licht (Leeg)

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
    // De interne alignment van het label (niet nodig, want fixed size)
    // imageLabel->setAlignment(Qt::AlignCenter);

    // --- Vrije ruimte berekenen ---
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
    // --- AANGEPAST ---
    info->setAlignment(Qt::AlignCenter); // Was Qt::AlignCenter

    // --- Voeg alles toe aan de layout ---
    layout->addWidget(title);
    // --- AANGEPAST: Centreer de afbeelding in de VBox ---
    layout->addWidget(imageLabel, 0, Qt::AlignCenter);
    layout->addWidget(info);
    layout->addStretch(); // Zorgt dat alles bovenaan blijft

    return container;
}
