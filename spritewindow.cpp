#include "spritewindow.h"

// Qt includes
#include <QApplication>
#include <QLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QMenuBar>
#include <QMenu>
#include <QFileDialog>
#include <QClipboard>
#include <QPainter>
#include <QImage>
#include <QCloseEvent>

// Emulator core includes
#include "coleco.h"

extern "C" {
#include "emu.h"
#include "tms9928a.h"
}

// Globale variabelen
extern int coleco_updatetms;
extern int cv_pal32[16*4];

// Constanten uit spriteviewer_.cpp
#define SPXCOR 0
#define SPYCOR 1
#define SPPATR 2
#define SPATTR 3
#define SPEARL 4

SpriteWindow::SpriteWindow(QWidget *parent)
    : QMainWindow(parent),
    m_spriteTablePixmap(64, 128), // 4 sprites breed (4*16=64), 8 sprites hoog (8*16=128)
    m_spriteViewPixmap(16, 16),
    m_selectedSprite(-1),
    m_is8x8(true),
    m_isZoomed(false),
    m_spritesDisabled(false)
{
    setWindowTitle("Sprites Viewer");
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint);
    setFixedSize(640, 700);

    m_spriteTablePixmap.fill(Qt::black);
    m_spriteViewPixmap.fill(Qt::black);

    setupUI();
    setupMenus();

    // Initialiseer de tabel
    for (int i = 0; i < 32; ++i) {
        m_spriteListWidget->insertRow(i);

        QTableWidgetItem *itemNum = new QTableWidgetItem(QString::number(i));
        itemNum->setForeground(Qt::white);
        m_spriteListWidget->setItem(i, 0, itemNum);

        for (int j = 1; j < 6; ++j) {
            QTableWidgetItem *item = new QTableWidgetItem("");
            item->setForeground(Qt::white);
            m_spriteListWidget->setItem(i, j, item);
        }
    }

    // Laad instellingen (oorspronkelijk uit TIniFile)
    m_autoRefreshAction->setChecked(true);
}

SpriteWindow::~SpriteWindow()
{
}

void SpriteWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setSpacing(10);

    // --- Linkerpaneel (Tabel + Info) ---
    QVBoxLayout *leftLayout = new QVBoxLayout();

    // 1. Sprite List (QTableWidget)
    m_spriteListWidget = new QTableWidget(0, 5, this); // 32 rijen, 5 kolommen
    m_spriteListWidget->setHorizontalHeaderLabels(QStringList() << "#" << "X" << "Y" << "Pattern" << "Attr/Clr");
    m_spriteListWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_spriteListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_spriteListWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_spriteListWidget->setMinimumWidth(320);

    // Stel kolombreedtes in
    m_spriteListWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_spriteListWidget->verticalHeader()->setVisible(false);
    m_spriteListWidget->setColumnWidth(0, 40); // #
    m_spriteListWidget->setColumnWidth(1, 60); // X
    m_spriteListWidget->setColumnWidth(2, 60); // Y
    m_spriteListWidget->setColumnWidth(3, 70); // Pattern
    m_spriteListWidget->setColumnWidth(4, 70); // Attr/Clr
    m_spriteListWidget->horizontalHeader()->setStretchLastSection(true);

    leftLayout->addWidget(m_spriteListWidget);

    // --- Rechterpaneel (Viewers + Info) ---
    QVBoxLayout *rightLayout = new QVBoxLayout();

    // 1. Sprite Table (4x8 grid)
    m_spriteTableBox = new QGroupBox("Sprite Table (0-31)");
    QHBoxLayout *tableLayout = new QHBoxLayout(m_spriteTableBox);
    m_spriteTableLabel = new QLabel;
    m_spriteTableLabel->setFixedSize(64, 128); // 4*16, 8*16
    m_spriteTableLabel->setPixmap(m_spriteTablePixmap);
    m_spriteTableLabel->setStyleSheet("background-color: black; border: 1px solid #808080;");
    tableLayout->addWidget(m_spriteTableLabel, 0, Qt::AlignCenter);
    rightLayout->addWidget(m_spriteTableBox);


    // 2. Sprite Info
    m_spriteInfoBox = new QGroupBox("Sprite Info");
    QGridLayout *infoLayout = new QGridLayout(m_spriteInfoBox); // <-- Gebruik QGridLayout

    // Maak de labels
    m_attrAddrLabel = new QLabel("$0000");
    m_tileAddrLabel = new QLabel("$0000");
    m_spriteSizeLabel = new QLabel("8x8 not zoomed");
    m_tileenableLabel = new QLabel("Green");
    m_tiledisableLabel = new QLabel("Red");

    QLabel *attrTitle = new QLabel("Attr Address");
    QLabel *tileTitle = new QLabel("Tile Address");
    QLabel *sizeTitle = new QLabel("Size");
    QLabel *enabledTitle = new QLabel("Sprite Enabled");
    QLabel *disabledTitle = new QLabel("Sprite Disabled");

    // Voeg toe aan de grid in 3 kolommen
    // Rij 0: Attr Address
    infoLayout->addWidget(attrTitle, 0, 0);
    infoLayout->addWidget(new QLabel(":"), 0, 1);
    infoLayout->addWidget(m_attrAddrLabel, 0, 2);

    // Rij 1: Tile Address
    infoLayout->addWidget(tileTitle, 1, 0);
    infoLayout->addWidget(new QLabel(":"), 1, 1);
    infoLayout->addWidget(m_tileAddrLabel, 1, 2);

    // Rij 2: Size
    infoLayout->addWidget(sizeTitle, 2, 0);
    infoLayout->addWidget(new QLabel(":"), 2, 1);
    infoLayout->addWidget(m_spriteSizeLabel, 2, 2);

    // Rij 3: Sprite enabled
    infoLayout->addWidget(enabledTitle, 3, 0);
    infoLayout->addWidget(new QLabel(":"), 3, 1);
    infoLayout->addWidget(m_tileenableLabel, 3, 2);

    // Rij 4: Sprite disabled
    infoLayout->addWidget(disabledTitle, 4, 0);
    infoLayout->addWidget(new QLabel(":"), 4, 1);
    infoLayout->addWidget(m_tiledisableLabel, 4, 2);

    // Zorg dat de waarde-kolom (2) uitrekt, en de rest niet
    infoLayout->setColumnStretch(0, 0); // Kolom 1 (Titels)
    infoLayout->setColumnStretch(1, 0); // Kolom 2 (:)
    infoLayout->setColumnStretch(2, 1); // Kolom 3 (Waarden)

    rightLayout->addWidget(m_spriteInfoBox);

    // 3. Sprite View (Zoomed)
    m_spriteViewBox = new QGroupBox("Sprite View");
    QHBoxLayout *viewLayout = new QHBoxLayout(m_spriteViewBox);
    m_spriteViewLabel = new QLabel;
    m_spriteViewLabel->setFixedSize(256, 256);
    m_spriteViewLabel->setPixmap(m_spriteViewPixmap.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::FastTransformation));
    m_spriteViewLabel->setStyleSheet("background-color: black; border: 1px solid #808080;");
    viewLayout->addWidget(m_spriteViewLabel, 0, Qt::AlignCenter);
    rightLayout->addWidget(m_spriteViewBox);

    rightLayout->addStretch();

    // Layout samenstellen
    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addLayout(rightLayout, 0);
    setCentralWidget(centralWidget);

    // Slots verbinden
    connect(m_spriteListWidget, &QTableWidget::itemPressed, this, [this](QTableWidgetItem *item){
        if (item) onSpriteSelected(item, nullptr);
    });
}


void SpriteWindow::setupMenus()
{
    m_menuBar = new QMenuBar(this);

    // File-menu
    m_fileMenu = m_menuBar->addMenu("&File");
    m_copyAction = new QAction("Copy Sprite Table to clipboard", this);
    connect(m_copyAction, &QAction::triggered, this, &SpriteWindow::onCopyToClipboard);
    m_fileMenu->addAction(m_copyAction);

    m_saveAction = new QAction("Save Sprite Table...", this);
    connect(m_saveAction, &QAction::triggered, this, &SpriteWindow::onSaveAs);
    m_fileMenu->addAction(m_saveAction);

    m_fileMenu->addSeparator();
    m_exitAction = new QAction("Exit", this);
    connect(m_exitAction, &QAction::triggered, this, &SpriteWindow::close);
    m_fileMenu->addAction(m_exitAction);

    // View-menu
    m_viewMenu = m_menuBar->addMenu("&View");
    m_autoRefreshAction = new QAction("Auto Refresh", this);
    m_autoRefreshAction->setCheckable(true);
    connect(m_autoRefreshAction, &QAction::toggled, this, &SpriteWindow::onAutoRefreshToggled);
    m_viewMenu->addAction(m_autoRefreshAction);

    m_refreshAction = new QAction("Refresh", this);
    m_refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_refreshAction, &QAction::triggered, this, &SpriteWindow::onRefresh);
    m_viewMenu->addAction(m_refreshAction);

    setMenuBar(m_menuBar);
}

// --- Event Overrides ---

void SpriteWindow::closeEvent(QCloseEvent *event)
{
    emit windowClosed();
    event->accept();
}

void SpriteWindow::showEvent(QShowEvent *event)
{
    if (!emul2) return;
    onRefresh();

    // Selecteer de eerste rij en geef de tabel de focus
    m_spriteListWidget->setFocus();
    m_spriteListWidget->selectRow(0);

    // De selectRow(0) roept automatisch onSpriteSelected aan,
    // wat de sprite view en tabel-selectie bijwerkt.

    event->accept();
}

// --- Slots ---

void SpriteWindow::doRefresh()
{
    if (m_autoRefreshAction->isChecked() && coleco_updatetms) {
        updateChanges();
        coleco_updatetms = 0;
    }
}

void SpriteWindow::onSaveAs()
{
    QString fileName = QFileDialog::getSaveFileName(this, "Save Sprite Table Image", "", "PNG Image (*.png);;Bitmap Image (*.bmp)");
    if (!fileName.isEmpty()) {
        // Maak een 8x4 sprite-tabel (128x64)
        QPixmap fullTable(128, 64);
        QPainter painter(&fullTable);
        QPixmap tempSprite(16, 16);
        for (int i = 0; i < 32; ++i) {
            refreshSprite(i, tempSprite);
            painter.drawPixmap((i % 8) * 16, (i / 8) * 16, tempSprite);
        }
        painter.end();
        fullTable.save(fileName);
    }
}

void SpriteWindow::onCopyToClipboard()
{
    // Maak een 8x4 sprite-tabel (128x64)
    QPixmap fullTable(128, 64);
    QPainter painter(&fullTable);
    QPixmap tempSprite(16, 16);
    for (int i = 0; i < 32; ++i) {
        refreshSprite(i, tempSprite);
        painter.drawPixmap((i % 8) * 16, (i / 8) * 16, tempSprite);
    }
    painter.end();
    QApplication::clipboard()->setPixmap(fullTable);
}

void SpriteWindow::onRefresh()
{
    if (!emul2) return;
    updateChanges();
}

void SpriteWindow::onAutoRefreshToggled(bool checked)
{
    if (checked) {
        onRefresh();
    }
}

void SpriteWindow::onSpriteSelected(QTableWidgetItem *current, QTableWidgetItem *previous)
{
    if (!current) {
        m_selectedSprite = -1;
    } else {
        m_selectedSprite = current->row();
    }
    // Update alleen de rode selectie en de zoom-view
    updateSpriteTable();
    updateSpriteView();
}

// --- Core Ported Logic ---

QString SpriteWindow::intToHex(int value, int width)
{
    return QString("%1").arg(value, width, 16, QChar('0')).toUpper();
}

void SpriteWindow::refreshSprite(int spriteIndex, QPixmap &pixmap)
{
    // Port van Tspriteviewer::RefreshSprite
    int iy, it, value;
    QRgb fgcol, bgcol;

    pixmap.fill(Qt::transparent); // Begin met een transparante pixmap
    if (!emul2) return;

    int address = spriteIndex * 4;
    int spriteWidth = m_is8x8 ? 8 : 16;
    int spriteHeight = m_is8x8 ? 8 : 16;

    // 1. Haal kleur op
    BYTE attr = coleco_gettmsval(SPRATTR, address + 3, 0, 0);
    fgcol = 0xFF000000 | cv_pal32[tms.IdxPal[attr & 0x0F]];
    bgcol = 0x00000000; // Transparant

    QImage image(spriteWidth, spriteHeight, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    // 2. Teken de sprite
    int patternIndex = coleco_gettmsval(SPRATTR, address + 2, 0, 0);
    int patternBase = patternIndex * 8;

    for (iy = 0; iy < spriteHeight; iy++) {
        QRgb *linePtr = (QRgb *)image.scanLine(iy);

        // 8x8 of 16x16 (linker 8 pixels)
        it = patternBase + iy;
        value = coleco_gettmsval(SPRGEN, it, 0, 0);

        linePtr[0] = (value & 0x80) ? fgcol : bgcol;
        linePtr[1] = (value & 0x40) ? fgcol : bgcol;
        linePtr[2] = (value & 0x20) ? fgcol : bgcol;
        linePtr[3] = (value & 0x10) ? fgcol : bgcol;
        linePtr[4] = (value & 0x08) ? fgcol : bgcol;
        linePtr[5] = (value & 0x04) ? fgcol : bgcol;
        linePtr[6] = (value & 0x02) ? fgcol : bgcol;
        linePtr[7] = (value & 0x01) ? fgcol : bgcol;

        if (!m_is8x8) {
            // 16x16 (rechter 8 pixels)
            it = patternBase + 16 + iy;

            value = coleco_gettmsval(SPRGEN, it, 0, 0);
            linePtr[8]  = (value & 0x80) ? fgcol : bgcol;
            linePtr[9]  = (value & 0x40) ? fgcol : bgcol;
            linePtr[10] = (value & 0x20) ? fgcol : bgcol;
            linePtr[11] = (value & 0x10) ? fgcol : bgcol;
            linePtr[12] = (value & 0x08) ? fgcol : bgcol;
            linePtr[13] = (value & 0x04) ? fgcol : bgcol;
            linePtr[14] = (value & 0x02) ? fgcol : bgcol;
            linePtr[15] = (value & 0x01) ? fgcol : bgcol;
        }
    }

    QPainter pixmapPainter(&pixmap);
    pixmapPainter.drawImage(0, 0, image);
    pixmapPainter.end();
}

void SpriteWindow::updateSpriteList()
{
    if (!emul2) return;

    // Bepaal sprite-eigenschappen
    BYTE R1 = tms.VR[1];
    m_is8x8 = (R1 & 0x02) == 0;
    m_isZoomed = (R1 & 0x01) != 0;

    QString sizeStr;
    if (m_is8x8) sizeStr = "8x8"; else sizeStr = "16x16";
    if (m_isZoomed) sizeStr += " zoomed"; else sizeStr += " not zoomed";
    m_spriteSizeLabel->setText(sizeStr);

    m_spritesDisabled = false;
    m_spriteListWidget->blockSignals(true);

    for (int i = 0; i < 32; i++) {
        int addr = i * 4;
        BYTE y = coleco_gettmsval(SPRATTR, addr + 0, 0, 0);
        BYTE x = coleco_gettmsval(SPRATTR, addr + 1, 0, 0);
        BYTE pat = coleco_gettmsval(SPRATTR, addr + 2, 0, 0);
        BYTE attr = coleco_gettmsval(SPRATTR, addr + 3, 0, 0);

        if (y == 0xD0) {
            m_spritesDisabled = true;
        }

        m_spriteListWidget->item(i, SPXCOR + 1)->setText("$" + intToHex(x, 2));
        m_spriteListWidget->item(i, SPYCOR + 1)->setText("$" + intToHex(y, 2));
        m_spriteListWidget->item(i, SPPATR + 1)->setText("$" + intToHex(pat, 2));
        m_spriteListWidget->item(i, SPATTR + 1)->setText("$" + intToHex(attr, 2));

        // Kleur de rij grijs als sprites uitgeschakeld zijn
        QColor fgColor = m_spritesDisabled ? Qt::red : Qt::green;
        for (int j = 0; j < 5; ++j) {
            m_spriteListWidget->item(i, j)->setForeground(fgColor);
        }
    }
    m_spriteListWidget->blockSignals(false);
}

void SpriteWindow::updateSpriteTable()
{
    if (!emul2) return;

    m_spriteTablePixmap.fill(Qt::black);
    QPainter painter(&m_spriteTablePixmap);
    QPixmap tempSprite(16, 16);

    for (int i = 0; i < 32; i++) {
        refreshSprite(i, tempSprite);

        // Teken 4 breed, 8 hoog
        int x = (i % 4) * 16;
        int y = (i / 4) * 16;
        painter.drawPixmap(x, y, tempSprite);

        // Teken rode rechthoek indien geselecteerd
        if (m_selectedSprite == i) {
            painter.setPen(QPen(Qt::red, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(x, y, 16, 16);
        }
    }
    painter.end();
    m_spriteTableLabel->setPixmap(m_spriteTablePixmap);
}

void SpriteWindow::updateSpriteView()
{
    if (!emul2 || m_selectedSprite == -1) {
        m_spriteViewPixmap.fill(Qt::black);
        m_attrAddrLabel->setText("$----");
        m_tileAddrLabel->setText("$----");
    } else {
        // ... (info labels bijwerken blijft hetzelfde) ...
        int attrAddr = coleco_gettmsaddr(SPRATTR, 0, 0) + 4 * m_selectedSprite;
        int patIndex = coleco_gettmsval(SPRATTR, 4 * m_selectedSprite + 2, 0, 0);
        int tileAddr = coleco_gettmsaddr(SPRGEN, 0, 0) + 8 * patIndex;

        m_attrAddrLabel->setText("$" + intToHex(attrAddr, 4));
        m_tileAddrLabel->setText("$" + intToHex(tileAddr, 4));

        // Update de 16x16 pixmap
        refreshSprite(m_selectedSprite, m_spriteViewPixmap);
    }

    // Schaal de 16x16 pixmap naar 256x256
    QPixmap finalSpritePixmap = m_spriteViewPixmap.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::FastTransformation); // <--- AANGEPAST (was 128, 128)

    // Teken het 16x16 raster
    QPainter painter(&finalSpritePixmap);
    painter.setPen(QColor(Qt::darkGray)); // cl3DDkShadow

    int steps = 16; // Altijd 16x16
    int stepSize = 256 / steps; // 256 / 16 = 16 pixels per blokje

    // Teken het raster
    for (int i = 0; i <= steps; ++i) {
        int pos = i * stepSize;

        painter.drawLine(pos, 0, pos, 256); // Verticale lijn
        painter.drawLine(0, pos, 256, pos); // Horizontale lijn
    }
    painter.end();

    m_spriteViewLabel->setPixmap(finalSpritePixmap);
}

void SpriteWindow::updateChanges()
{
    // Port van Tspriteviewer::CreateSprite
    updateSpriteList();
    updateSpriteTable();
    updateSpriteView();
}
