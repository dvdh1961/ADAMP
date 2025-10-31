#include "ntablewindow.h"

// Qt includes
#include <QApplication>
#include <QLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QMenuBar>
#include <QMenu>
#include <QFileDialog>
#include <QClipboard>
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QImage>

// Emulator core includes
// Zorg ervoor dat deze headers in je .pro-bestand (INCLUDEPATH) staan
#include "coleco.h"
#include "f18a.h"

extern "C" {
#include "emu.h"
#include "tms9928a.h"
}

// Nodig voor cv_pal32
extern int cv_pal32[16*4];

NTableWindow::NTableWindow(QWidget *parent)
    : QMainWindow(parent),
    m_nameTablePixmap(512, 384),
    m_tilePixmap(8, 8),
    m_vramTile(-1),
    m_namTabVal(0),
    m_lastScreenY(0)
{
    setWindowTitle("Name Table Viewer");
    setWindowFlags(Qt::Window ); // Vergelijkbaar met bsToolWindow

    setWindowFlags(windowFlags()
                   & ~Qt::WindowMaximizeButtonHint
                   & ~Qt::WindowMinimizeButtonHint);

    setFixedSize(950,620);

    // Vul de pixmaps met een standaardkleur
    m_nameTablePixmap.fill(Qt::black);
    m_tilePixmap.fill(Qt::black);

    setupUI();
    setupMenus();

    // Laad instellingen (oorspronkelijk uit TIniFile)
    // We kunnen QSettings gebruiken als je dit wilt opslaan,
    // maar voor nu gebruiken we de standaardwaarden.
    m_autoRefreshAction->setChecked(true);
}

NTableWindow::~NTableWindow()
{
    // Geen handmatige 'delete' nodig voor UI-componenten
    // die 'this' als parent hebben.
}

// In ntablewindow.cpp

void NTableWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);

    // --- Linkerpaneel (Name Table) ---
    QVBoxLayout *leftLayout = new QVBoxLayout();

    // --- 1. NameTable GroupBox ---
    QGroupBox *nameTableBox = new QGroupBox("NameTable");
    nameTableBox->setStyleSheet("QGroupBox { color: white; }");

    // Layout voor BINNEN de NameTable GroupBox
    QVBoxLayout *nameTableLayout = new QVBoxLayout();
    nameTableLayout->setContentsMargins(6, 6, 6, 6); // Voegt wat padding toe

    m_nameTableLabel = new QLabel;
    m_nameTableLabel->setFixedSize(512, 384);
    m_nameTableLabel->setPixmap(m_nameTablePixmap);
    m_nameTableLabel->setMouseTracking(true);
    m_nameTableLabel->installEventFilter(this);

    nameTableLayout->addWidget(m_nameTableLabel); // Stop label in de groupbox-layout
    nameTableBox->setLayout(nameTableLayout);     // Stel layout in voor groupbox

    leftLayout->addWidget(nameTableBox); // Voeg groupbox toe aan linkerpaneel

    // --- 2. Settings GroupBox ---
    QGroupBox *settingsBox = new QGroupBox("Settings");
    // Maak zowel de groupbox-titel als de checkboxes wit
    settingsBox->setStyleSheet("QGroupBox { color: white; } QCheckBox { color: white; }");

    QHBoxLayout *checkLayout = new QHBoxLayout();
    m_gridCheck = new QCheckBox("Show Grid");
    m_tilesCheck = new QCheckBox("Show Tile Number");
    m_bwCheck = new QCheckBox("B&&W");
    checkLayout->addWidget(m_gridCheck);
    checkLayout->addWidget(m_tilesCheck);
    checkLayout->addWidget(m_bwCheck);
    checkLayout->addStretch();

    settingsBox->setLayout(checkLayout); // Stop de checkbox-layout in de groupbox

    leftLayout->addWidget(settingsBox); // Voeg groupbox toe aan linkerpaneel
    // --- 3. Palette GroupBox ---
    m_paletteBox = new QGroupBox("Color palette");
    m_paletteBox->setStyleSheet("QGroupBox { color: white; }");

    QGridLayout *paletteLayout = new QGridLayout(m_paletteBox);
    paletteLayout->setHorizontalSpacing(0); // <-- AANGEPAST: Geen horizontale ruimte
    paletteLayout->setVerticalSpacing(2);   // Wel een beetje verticale ruimte
    paletteLayout->setContentsMargins(6, 6, 6, 6);

    // De grootte van één "pixel" in de 128x128 tile viewer (128 / 8 = 16)
    const int swatchSize = 16;

    for (int i = 0; i < 16; ++i) {
        // Rij 0: Kleurstaal (een QLabel met achtergrondkleur)
        m_paletteLabels[i] = new QLabel();
        m_paletteLabels[i]->setFixedSize(swatchSize, swatchSize);
        // Standaardstijl (wordt later overschreven door updatePalette)
        m_paletteLabels[i]->setStyleSheet("background-color: black; border: 1px solid #808080;");
        m_paletteLabels[i]->setToolTip(QString("Palette Index %1").arg(i));
        paletteLayout->addWidget(m_paletteLabels[i], 0, i); // <-- AANGEPAST: AlignHCenter weg

        // Rij 1: Nummerlabel (0-15)
        QLabel *numLabel = new QLabel(QString::number(i));
        numLabel->setStyleSheet("color: white;");
        numLabel->setAlignment(Qt::AlignHCenter); // Centreren is prima
        paletteLayout->addWidget(numLabel, 1, i, Qt::AlignHCenter);
    }

    // AANGEPAST: Voeg een 'stretch' kolom toe aan het einde (op positie 16)
    // Dit dwingt alle 16 blokjes (0-15) om links te beginnen.
    paletteLayout->setColumnStretch(16, 1);

    leftLayout->addWidget(m_paletteBox); // Voeg groupbox toe aan linkerpaneel
    // --- Einde Linkerpaneel ---

    // --- Rechterpaneel (Tile Info) ---
    QVBoxLayout *rightLayout = new QVBoxLayout();

    // --- 1. VDP Register Box  ---
    m_vdpRegBox = new QGroupBox("VDP Registers");
    m_vdpRegBox->setStyleSheet("QGroupBox { color: white; }");
    QGridLayout *regLayout = new QGridLayout(m_vdpRegBox);

    const char* registerNames[8] = {
        "R0 (Mode Ctrl 0)",
        "R1 (Mode Ctrl 1)",
        "R2 (Name Table)",
        "R3 (Color Table)",
        "R4 (Pattern Table)",
        "R5 (Sprite Attr)",
        "R6 (Sprite Ptrn)",
        "R7 (FG/BG Colors)"
    };

    for (int i = 0; i < 8; ++i) {
        // Kolom 0: Titel
        QLabel *titleLabel = new QLabel(registerNames[i]);
        titleLabel->setStyleSheet("color: white;");

        // Kolom 1: Dubbele punt
        QLabel *colonLabel = new QLabel(":");
        colonLabel->setStyleSheet("color: white;");

        // Kolom 2: Waarde
        m_vdpRegLabel[i] = new QLabel("$00");
        m_vdpRegLabel[i]->setStyleSheet("color: white;");

        // Kolom 3: Beschrijving
        m_vdpRegDesc[i] = new QLabel(""); // Standaard leeg
        m_vdpRegDesc[i]->setStyleSheet("color: #AAAAAA;"); // Grijs voor beschrijving

        // Voeg toe aan de grid
        regLayout->addWidget(titleLabel, i, 0, Qt::AlignLeft);
        regLayout->addWidget(colonLabel, i, 1, Qt::AlignLeft);
        regLayout->addWidget(m_vdpRegLabel[i], i, 2, Qt::AlignLeft);
        regLayout->addWidget(m_vdpRegDesc[i], i, 3, Qt::AlignLeft); // Nieuwe kolom
    }

    // Zorg dat de beschrijving-kolom (3) uitrekt
    regLayout->setColumnStretch(0, 0); // Titels
    regLayout->setColumnStretch(1, 0); // :
    regLayout->setColumnStretch(2, 0); // Waarden (vaste breedte)
    regLayout->setColumnStretch(3, 1); // Beschrijving (flexibel)

    rightLayout->addWidget(m_vdpRegBox);


    // --- 2. Tile Info Box  ---
    m_tileInfoBox = new QGroupBox("Tile Info");

    // Deze stylesheet geldt nu ALLEEN voor de groupbox-titel zelf
    m_tileInfoBox->setStyleSheet("QGroupBox { color: white; }");

    QGridLayout *gridLayout = new QGridLayout(m_tileInfoBox);

    // 1. Maak de titels (Kolom 1)
    QLabel *locTitle = new QLabel("Location");
    QLabel *tileIndexTitle = new QLabel("Tile Index");
    QLabel *namTabAddrTitle = new QLabel("NamTab Addr");
    QLabel *patGenAddrTitle = new QLabel("PatGen Addr");
    QLabel *colTabAddrTitle = new QLabel("ColTab Addr");

    locTitle->setStyleSheet("color: white;");
    tileIndexTitle->setStyleSheet("color: white;");
    namTabAddrTitle->setStyleSheet("color: white;");
    patGenAddrTitle->setStyleSheet("color: white;");
    colTabAddrTitle->setStyleSheet("color: white;");

    // 2. Maak de waarde-labels (Kolom 3)
    m_locLabel = new QLabel("$00,$00    -  000,000");
    m_tileIndexLabel = new QLabel("$00");
    m_namTabAddrLabel = new QLabel("$0000");
    m_patGenAddrLabel = new QLabel("$0000");
    m_colTabAddrLabel = new QLabel("$0000");

    m_locLabel->setStyleSheet("color: white;");
    m_tileIndexLabel->setStyleSheet("color: white;");
    m_namTabAddrLabel->setStyleSheet("color: cyan;");
    m_patGenAddrLabel->setStyleSheet("color: white;");
    m_colTabAddrLabel->setStyleSheet("color: white;");

    // 4. Voeg alle 3 de kolommen toe aan de grid

    // Rij 0: Location
    gridLayout->addWidget(locTitle, 0, 0);
    QLabel* colon0 = new QLabel("      :"); colon0->setStyleSheet("color: white;");
    gridLayout->addWidget(colon0, 0, 1); // Kolom 2
    gridLayout->addWidget(m_locLabel, 0, 2);

    // Rij 1: Tile Index
    gridLayout->addWidget(tileIndexTitle, 1, 0);
    QLabel* colon1 = new QLabel("      :"); colon1->setStyleSheet("color: white;");
    gridLayout->addWidget(colon1, 1, 1);
    gridLayout->addWidget(m_tileIndexLabel, 1, 2);

    // Rij 2: NamTab Addr
    gridLayout->addWidget(namTabAddrTitle, 2, 0);
    QLabel* colon2 = new QLabel("      :"); colon2->setStyleSheet("color: white;");
    gridLayout->addWidget(colon2, 2, 1);
    gridLayout->addWidget(m_namTabAddrLabel, 2, 2);

    // Rij 3: PatGen Addr
    gridLayout->addWidget(patGenAddrTitle, 3, 0);
    QLabel* colon3 = new QLabel("      :"); colon3->setStyleSheet("color: white;");
    gridLayout->addWidget(colon3, 3, 1);
    gridLayout->addWidget(m_patGenAddrLabel, 3, 2);

    // Rij 4: ColTab Addr
    gridLayout->addWidget(colTabAddrTitle, 4, 0);
    QLabel* colon4 = new QLabel("      :"); colon4->setStyleSheet("color: white;");
    gridLayout->addWidget(colon4, 4, 1);
    gridLayout->addWidget(m_colTabAddrLabel, 4, 2);

    // 5. Zorg dat de waarden-kolom (2) uitrekt, en de rest niet
    gridLayout->setColumnStretch(0, 0); // Kolom 1 (Titels)
    gridLayout->setColumnStretch(1, 0); // Kolom 2 (:)
    gridLayout->setColumnStretch(2, 1); // Kolom 3 (Waarden)

    rightLayout->addWidget(m_tileInfoBox);


    // --- Tile GroupBox  ---
    // --- Layout voor Tile Viewer (links) + Hex Waarden (rechts) ---

    // Maak een HBox voor de tile en de tekst
    QHBoxLayout *tileLayout = new QHBoxLayout();
    tileLayout->setSpacing(10); // Ruimte tussen tile en tekst-kolom

    // 1. Tile viewer (links)
    m_tileLabel = new QLabel;
    m_tileLabel->setFixedSize(128, 128);
    m_tileLabel->setPixmap(m_tilePixmap.scaled(128, 128));
    m_tileLabel->setStyleSheet("background-color: black; border: 1px solid #808080;");
    tileLayout->addWidget(m_tileLabel);

    // 2. Maak een VBox voor de 8 hex-labels
    QVBoxLayout *hexLayout = new QVBoxLayout();
    hexLayout->setSpacing(0); // Geen ruimte tussen de labels
    hexLayout->setContentsMargins(0, 0, 0, 0); // Geen marges

    QFont monospaceFont("Monospace");
    monospaceFont.setStyleHint(QFont::TypeWriter);
    monospaceFont.setPointSize(10); // Iets groter lettertype

    // 128 pixels / 8 rijen = 16 pixels per rij
    const int rowHeight = 16;
    const int rowWidth = 40; // Breedte voor "$XX"

    for (int i = 0; i < 8; ++i) {
        m_tileValueLabels[i] = new QLabel("$00"); // Standaard tekst
        m_tileValueLabels[i]->setStyleSheet("color: white;");
        m_tileValueLabels[i]->setFont(monospaceFont);
        // Stel een vaste grootte in: 16px hoog
        m_tileValueLabels[i]->setFixedSize(rowWidth, rowHeight);
        // Centreer de tekst VERTICAAL binnen die 16px
        m_tileValueLabels[i]->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        hexLayout->addWidget(m_tileValueLabels[i]);
    }

    // Voeg de VBox met labels toe aan de HBox
    tileLayout->addLayout(hexLayout);
    tileLayout->addStretch(); // Zorgt dat de tekst naast de tile blijft

    // Maak een container widget voor de HBox
    QWidget *tileContainer = new QWidget();
    tileContainer->setLayout(tileLayout);

    // 1. Maak de GroupBox met de titel "Tile"
    QGroupBox *tileGroupBox = new QGroupBox("Tile 8x8 pixels");
    tileGroupBox->setStyleSheet("QGroupBox { color: white; }"); // Stijl net als de andere boxen

    // 2. Maak een layout voor BINNEN de GroupBox
    //    We gebruiken een QHBoxLayout om de tileContainer te centreren.
    QHBoxLayout *groupBoxLayout = new QHBoxLayout();
    groupBoxLayout->addWidget(tileContainer, 0, Qt::AlignHCenter); // Centreer de container

    // 3. Stel de layout in voor de GroupBox
    tileGroupBox->setLayout(groupBoxLayout);

    // 4. Voeg de NIEUWE GroupBox (i.p.v. de container) toe aan de rechter VBox
    rightLayout->addWidget(tileGroupBox);


    // Layout samenstellen
    mainLayout->addLayout(leftLayout);
    mainLayout->addLayout(rightLayout);
    setCentralWidget(centralWidget);

    // Slots verbinden
    connect(m_gridCheck, &QCheckBox::toggled, this, &NTableWindow::onOptionToggled);
    connect(m_tilesCheck, &QCheckBox::toggled, this, &NTableWindow::onOptionToggled);
    connect(m_bwCheck, &QCheckBox::toggled, this, &NTableWindow::onOptionToggled);
}



void NTableWindow::setupMenus()
{
    m_menuBar = new QMenuBar(this);

    // File-menu
    m_fileMenu = m_menuBar->addMenu("&File");
    m_copyAction = new QAction("Copy to clipboard", this);
    connect(m_copyAction, &QAction::triggered, this, &NTableWindow::onCopyToClipboard);
    m_fileMenu->addAction(m_copyAction);

    m_saveAction = new QAction("Save Name Table...", this);
    connect(m_saveAction, &QAction::triggered, this, &NTableWindow::onSaveAs);
    m_fileMenu->addAction(m_saveAction);

    m_fileMenu->addSeparator();
    m_exitAction = new QAction("Exit", this);
    connect(m_exitAction, &QAction::triggered, this, &NTableWindow::close);
    m_fileMenu->addAction(m_exitAction);

    // View-menu
    m_viewMenu = m_menuBar->addMenu("&View");
    m_autoRefreshAction = new QAction("Auto Refresh", this);
    m_autoRefreshAction->setCheckable(true);
    connect(m_autoRefreshAction, &QAction::toggled, this, &NTableWindow::onAutoRefreshToggled);
    m_viewMenu->addAction(m_autoRefreshAction);

    m_refreshAction = new QAction("Refresh", this);
    m_refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_refreshAction, &QAction::triggered, this, &NTableWindow::onRefresh);
    m_viewMenu->addAction(m_refreshAction);

    setMenuBar(m_menuBar);
}

// --- Event Overrides ---

void NTableWindow::closeEvent(QCloseEvent *event)
{
    emit windowClosed();
    event->accept();
}

void NTableWindow::showEvent(QShowEvent *event)
{
    if (!emul2) { /* ... */ return; }

    m_namTabVal = coleco_gettmsaddr(CHRMAP, 0, 0);
    m_namTabAddrLabel->setText(QString("$%1").arg(intToHex(m_namTabVal, 4)));

    updateChanges();
    updatePalette();
    smallUpdateChanges(0);
    updateVdpRegisters();

    event->accept();
}

bool NTableWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_nameTableLabel && event->type() == QEvent::MouseMove)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        int x = mouseEvent->pos().x();
        int y = mouseEvent->pos().y();

        int ix = x / (m_nameTableLabel->width() / 32);
        int iy = y / (m_nameTableLabel->height() / 24);

        if (ix < 0) ix = 0; if (ix > 31) ix = 31;
        if (iy < 0) iy = 0; if (iy > 23) iy = 23;

        updateTileInfo(ix, iy);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void NTableWindow::updatePalette()
{
    // Veiligheidscheck
    if (!emul2) return;

    for (int i = 0; i < 16; ++i) {
        // We halen de kleur op dezelfde manier op als in createMap/createTile
        // tms.IdxPal[i] geeft de correcte index in de master cv_pal32 tabel.
        int paletteIndex = tms.IdxPal[i];

        // Bouw de QRgb kleurwaarde (32-bit ARGB, A=FF)
        QRgb colorValue = 0xFF000000 | cv_pal32[paletteIndex];

        // Stel de achtergrondkleur van het label in met een stylesheet
        // We formatteren de kleur als #RRGGBB
        m_paletteLabels[i]->setStyleSheet(
            QString("background-color: #%1; border: 1px solid #808080;")
                .arg(colorValue & 0x00FFFFFF, 6, 16, QChar('0'))
            );
    }
}

// --- Slots ---

void NTableWindow::doRefresh()
{
    if (m_autoRefreshAction->isChecked()) {
        updateChanges();
        smallUpdateChanges(m_lastScreenY);
    }
}

void NTableWindow::onSaveAs()
{
    QString fileName = QFileDialog::getSaveFileName(this, "Save Image", "", "PNG Image (*.png);;Bitmap Image (*.bmp)");
    if (!fileName.isEmpty()) {
        m_nameTablePixmap.save(fileName);
    }
}

void NTableWindow::onCopyToClipboard()
{
    QApplication::clipboard()->setPixmap(m_nameTablePixmap);
}

void NTableWindow::onRefresh()
{
    if (!emul2) return;
    updateChanges();
    smallUpdateChanges(0);
    updateVdpRegisters();
    updatePalette();
}

void NTableWindow::onAutoRefreshToggled(bool checked)
{
    if (checked) {
        onRefresh();
    }
}

void NTableWindow::onOptionToggled()
{
    updateChanges();
    smallUpdateChanges(0);
}

void NTableWindow::onEditNamTabAddr()
{
    bool ok;
    QString text = QInputDialog::getText(this, "Edit Name Table Address",
                                         "New Address (hex):", QLineEdit::Normal,
                                         intToHex(m_namTabVal, 4), &ok);

    if (ok && !text.isEmpty()) {
        bool conversionOk;
        int n = text.toInt(&conversionOk, 16);
        if (conversionOk) {
            m_namTabVal = n & 0x3FFF; // Mask naar 14 bits (VRAM-grootte)
            m_namTabAddrLabel->setText(QString("$%1").arg(intToHex(n, 4)));
            updateChanges();
        }
    }
}

// --- Core Ported Logic ---

QString NTableWindow::intToHex(int value, int width)
{
    return QString("%1").arg(value, width, 16, QChar('0')).toUpper();
}

void NTableWindow::updateTileInfo(int ix, int iy)
{
    // Deze logica is een directe port van VRamMouseMove
    int it, bgti;

    it = coleco_gettmsval(VRAM, m_namTabVal + ix + 32 * iy, 0, 0);
    m_locLabel->setText(QString("$%1,$%2  -  %3,%4").arg(intToHex(ix, 2)).arg(intToHex(iy, 2)).arg(ix, 3).arg(iy, 3));
    m_tileIndexLabel->setText("$" + intToHex(it, 2));
    m_namTabAddrLabel->setText(QString("$%1").arg(intToHex(m_namTabVal + ix + 32 * iy, 4)));

    bgti = it * 8 + coleco_gettmsaddr(CHRGEN, tms.Mode, iy * 8);
    m_patGenAddrLabel->setText("$" + intToHex(bgti, 4));

    if (tms.Mode == 1)
        m_colTabAddrLabel->setText("$" + intToHex((it >> 3) + coleco_gettmsaddr(CHRCOL, 0, 0), 4));
    else
        m_colTabAddrLabel->setText("$" + intToHex((it << 3) + coleco_gettmsaddr(CHRCOL, tms.Mode, iy * 8), 4));

    // Update de tile-weergave als de muis over een nieuwe tile beweegt
    if (bgti != m_vramTile) {
        m_vramTile = bgti;
        m_lastScreenY = iy * 8; // Sla de Y-positie op
        smallUpdateChanges(m_lastScreenY); // Gebruik het
        //smallUpdateChanges(iy * 8);
    }
}


void NTableWindow::createTile(int screen_y)
{
    // Port van Tnametabviewer::CreateTile
    int iy, value, vcol, ofsvram;
    QRgb fgcol, bgcol;
    QImage tileImage(8, 8, QImage::Format_RGB32);
    QStringList values;

    for (iy = 0; iy < 8; iy++) {
        QRgb *linePtr = (QRgb *)tileImage.scanLine(iy);

        if ((m_vramTile >= coleco_gettmsaddr(CHRGEN, 0, 0)) && (m_vramTile < coleco_gettmsaddr(CHRGEN, 0, 0) + 0x1800)) {


            // 1. Bereken de 'tile index' (0-255)
            // ofsvram = byte-offset van de tile t.o.v. de start van CHRGEN
            ofsvram = m_vramTile - coleco_gettmsaddr(CHRGEN, 0, 0);
            int pattern_index = ofsvram / 8; // De tile index

            // 2. Bereken het adres voor de KLEUR-data
            // Dit is (tile_index * 8) + de huidige lijn (iy)
            int color_address = (pattern_index * 8) + iy;

            // 3. Haal de kleur-byte op met het juiste adres en de juiste Y-coordinaat (voor bank switching)
            if (tms.Mode == 1)
            {
                // Mode 1 gebruikt iy>>3 voor de hele tegel
                vcol = coleco_gettmsval(CHRCOL, color_address >> 3, tms.Mode, iy >> 3);
            }
            else if (f18a.Mode == F18A_MODE_GRAPHICS){
                vcol = coleco_gettmsval(CHRCOL, color_address, tms.Mode, iy >> 3);
            }
            else
            {
                // Mode 2 gebruikt de absolute Y-positie van de lijn
                vcol = coleco_gettmsval(CHRCOL, color_address, tms.Mode, screen_y + iy);
            }


            if (m_bwCheck->isChecked()) {
                fgcol = 0xFFFFFFFF;
                bgcol = 0xFF000000 | cv_pal32[0];
            } else {
                fgcol = 0xFF000000 | cv_pal32[tms.IdxPal[vcol >> 4]];
                bgcol = 0xFF000000 | cv_pal32[tms.IdxPal[vcol & 0xf]];
            }

            // De PATTERN-data ophalen (deze logica was al correct)
            if (ofsvram >= 0x1000)
                value = coleco_gettmsval(CHRGEN, (m_vramTile & 0x7ff) + (iy & 7), tms.Mode, screen_y + iy);
            else if (ofsvram >= 0x800)
                value = coleco_gettmsval(CHRGEN, (m_vramTile & 0x7ff) + (iy & 7), tms.Mode, screen_y + iy);
            else
                value = coleco_gettmsval(CHRGEN, (m_vramTile & 0x7ff) + (iy & 7), tms.Mode, screen_y + iy);

        } else {
            // ... (logica voor niet-vram blijft hetzelfde) ...
            if (m_bwCheck->isChecked()) {
                fgcol = 0xFFFFFFFF;
                bgcol = 0xFF000000 | cv_pal32[0];
            } else {
                fgcol = 0xFF000000 | cv_pal32[15];
                bgcol = 0xFF000000 | cv_pal32[0];
            }
            value = coleco_gettmsval(VRAM, m_vramTile + (iy & 7), 0x00, 0x00);
        }

        // Teken bitmap
        *(linePtr++) = (value & 0x80) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x40) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x20) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x10) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x08) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x04) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x02) ? fgcol : bgcol;
        *(linePtr) = (value & 0x01) ? fgcol : bgcol;

        values.append("$" + intToHex(value, 2));
    }

    m_tilePixmap = QPixmap::fromImage(tileImage);

    // Vul de 8 aparte labels met de waarden
    for (int i = 0; i < 8; ++i) {
        if (i < values.size()) {
            m_tileValueLabels[i]->setText(values[i]);
        } else {
            m_tileValueLabels[i]->setText(""); // Maak leeg (zou niet mogen gebeuren)
        }
    }
}

void NTableWindow::smallUpdateChanges(int screen_y)
{
    createTile(screen_y);

    // Schaal de 8x8 tile naar 128x128
    QPixmap scaledTile = m_tilePixmap.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    // Teken het grid indien nodig
    //if (m_gridCheck->isChecked()) {
        QPainter painter(&scaledTile);
        painter.setPen(QColor(Qt::darkGray)); // cl3DDkShadow
        int step = 128 / 8;
        for (int x = 0; x <= 128; x += step) {
            painter.drawLine(x, 0, x, 128);
        }
        for (int y = 0; y <= 128; y += step) {
            painter.drawLine(0, y, 128, y);
        }
    //}

    m_tileLabel->setPixmap(scaledTile);
}

void NTableWindow::createMap(QPaintDevice *device, int w, int h)
{
    // Port van Tnametabviewer::CreateMap
    int ix, iy, it, value;
    QRgb fgcol, bgcol;
    BYTE memtile[32 * 24];
    BYTE ic;
    int Offset;

    // 1. Maak de 256x192 basis-image
    QImage image(256, 192, QImage::Format_RGB32);
    image.fill(Qt::black); // Start met een zwarte achtergrond

    for (iy = 0; iy < 192; iy++) {
        QRgb *scanLine = (QRgb *)image.scanLine(iy);
        Offset = (iy & 7);
        for (ix = 0; ix < 32; ix++) {
            it = coleco_gettmsval(VRAM, m_namTabVal + ix + ((iy & 0xF8) << 2), 0, 0);
            memtile[ix + (iy >> 3) * 32] = it;
            it = it * 8 + Offset;

            if (tms.Mode == 1) {
                ic = coleco_gettmsval(CHRCOL, (it >> 3), 0, 0);
            } else {
                ic = coleco_gettmsval(CHRCOL, it, tms.Mode, iy);
            }

            if (m_bwCheck->isChecked()) {
                fgcol = 0xFFFFFFFF;
                bgcol = 0xFF000000 | cv_pal32[0];
            } else {
                // --- AANPASSING ---
                // Gebruik tms.IdxPal, net zoals createTile() doet
                fgcol = 0xFF000000 | cv_pal32[tms.IdxPal[(ic >> 4) & 0x0f]];
                bgcol = 0xFF000000 | cv_pal32[tms.IdxPal[(ic & 0x0f)]];
                // --- EINDE AANPASSING ---
            }

            value = coleco_gettmsval(CHRGEN, it, tms.Mode, iy);
            *(scanLine++) = (value & 0x80) ? fgcol : bgcol;
            *(scanLine++) = (value & 0x40) ? fgcol : bgcol;
            *(scanLine++) = (value & 0x20) ? fgcol : bgcol;
            *(scanLine++) = (value & 0x10) ? fgcol : bgcol;
            *(scanLine++) = (value & 0x08) ? fgcol : bgcol;
            *(scanLine++) = (value & 0x04) ? fgcol : bgcol;
            *(scanLine++) = (value & 0x02) ? fgcol : bgcol;
            *(scanLine++) = (value & 0x01) ? fgcol : bgcol;
        }
    }

    // 2. Converteer naar QPixmap en schaal naar het doel-device
    QPixmap basePixmap = QPixmap::fromImage(image);
    QPainter painter(device);
    painter.drawPixmap(0, 0, w, h, basePixmap);

    // 3. Teken het grid (indien nodig) over de geschaalde pixmap
    if (m_gridCheck->isChecked()) {
        painter.setPen(QColor(Qt::darkGray)); // cl3DDkShadow
        int stepX = w / 32;
        int stepY = h / 24;
        for (ix = 0; ix <= w; ix += stepX) {
            painter.drawLine(ix, 0, ix, h);
        }
        for (iy = 0; iy <= h; iy += stepY) {
            painter.drawLine(0, iy, w, iy);
        }
    }

    // 4. Teken de tile-nummers (indien nodig)
    if (m_tilesCheck->isChecked()) {
        // --- AANGEPASTE REGEL ---
        QFont font("Roboto", 8); // Was "Courier New", 10
        // --- EINDE AANPASSING ---

        painter.setFont(font);
        painter.setPen(QColor(64, 255, 0)); // RGB(64,255,0)

        int stepX = w / 32;
        int stepY = h / 24;
        for (iy = 0; iy < 24; iy++) {
            for (ix = 0; ix < 32; ix++) {
                // Y-positie iets aangepast (was 11) voor het kleinere lettertype
                painter.drawText(stepX * ix + 1, stepY * iy + 9, intToHex(memtile[ix + iy * 32], 2));
            }
        }
    }
}

void NTableWindow::updateChanges()
{
    // device, w, h
    createMap(&m_nameTablePixmap, m_nameTablePixmap.width(), m_nameTablePixmap.height());
    m_nameTableLabel->setPixmap(m_nameTablePixmap);
}

void NTableWindow::updateVdpRegisters()
{
    // Veiligheidscheck
    if (!emul2) return;

    // --- Update R0 ---
    BYTE R0 = tms.VR[0];
    m_vdpRegLabel[0]->setText("$" + intToHex(R0, 2));
    QString R0_Desc = "";

    // tms.Mode bevat de berekende mode (0-3)
    switch (tms.Mode) {
    case 0: R0_Desc = "Text (40x24)"; break;
    case 1: R0_Desc = "GFX 1 (32x24)"; break;
    case 2: R0_Desc = "GFX 2 (32x24)"; break;
    case 3: R0_Desc = "Multicolor"; break;
    }
    if (R0 & 0x01) R0_Desc += ", ExtVDP"; // REG0_EXTVDP
    m_vdpRegDesc[0]->setText(R0_Desc);

    // --- Update R1 ---
    BYTE R1 = tms.VR[1];
    m_vdpRegLabel[1]->setText("$" + intToHex(R1, 2));
    QStringList R1_Desc;
    if (R1 & 0x80) R1_Desc << "16k RAM";     // REG1_RAM16K
    if (R1 & 0x40) R1_Desc << "Screen ON";   // REG1_SCREEN
    if (R1 & 0x20) R1_Desc << "IRQ ON";      // REG1_IRQ
    if (R1 & 0x02) R1_Desc << "16x16 Sprites"; // REG1_SPR16
    if (R1 & 0x01) R1_Desc << "Mag Sprites";   // REG1_BIGSPR
    if (R1_Desc.isEmpty()) R1_Desc << "8k, Screen OFF";
    m_vdpRegDesc[1]->setText(R1_Desc.join(", "));

    // --- Update R2 (Name Table) ---
    BYTE R2 = tms.VR[2];
    m_vdpRegLabel[2]->setText("$" + intToHex(R2, 2));
    // tms.ChrTab is een pointer, VDP_Memory is de start. Het verschil is de offset.
    m_vdpRegDesc[2]->setText(QString("Addr: $%1").arg(intToHex(tms.ChrTab - VDP_Memory, 4)));

    // --- Update R3 (Color Table) ---
    BYTE R3 = tms.VR[3];
    m_vdpRegLabel[3]->setText("$" + intToHex(R3, 2));
    m_vdpRegDesc[3]->setText(QString("Addr: $%1").arg(intToHex(tms.ColTab - VDP_Memory, 4)));

    // --- Update R4 (Pattern Table) ---
    BYTE R4 = tms.VR[4];
    m_vdpRegLabel[4]->setText("$" + intToHex(R4, 2));
    m_vdpRegDesc[4]->setText(QString("Addr: $%1").arg(intToHex(tms.ChrGen - VDP_Memory, 4)));

    // --- Update R5 (Sprite Attr) ---
    BYTE R5 = tms.VR[5];
    m_vdpRegLabel[5]->setText("$" + intToHex(R5, 2));
    m_vdpRegDesc[5]->setText(QString("Addr: $%1").arg(intToHex(tms.SprTab - VDP_Memory, 4)));

    // --- Update R6 (Sprite Ptrn) ---
    BYTE R6 = tms.VR[6];
    m_vdpRegLabel[6]->setText("$" + intToHex(R6, 2));
    m_vdpRegDesc[6]->setText(QString("Addr: $%1").arg(intToHex(tms.SprGen - VDP_Memory, 4)));

    // --- Update R7 (Colors) ---
    //BYTE R7 = tms.VR[7];
    BYTE R7 = (tms.FGColor << 4) | (tms.BGColor & 0x0F);
    m_vdpRegLabel[7]->setText("$" + intToHex(R7, 2));
    m_vdpRegDesc[7]->setText(QString("FG Idx: %1, BG Idx: %2").arg(R7 >> 4).arg(R7 & 0x0F));
}


