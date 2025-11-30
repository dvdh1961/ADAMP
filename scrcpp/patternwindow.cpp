#include "patternwindow.h"

#include <QApplication>
#include <QLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QCheckBox>
#include <QRadioButton>
#include <QScrollBar>
#include <QPushButton>
#include <QMenuBar>
#include <QMenu>
#include <QFileDialog>
#include <QClipboard>
#include <QMouseEvent>
#include <QPainter>
#include <QImage>

#include <QPainter>
#include <QImage>

#include "coleco.h"

extern "C" {
#include "emu.h"
#include "tms9928a.h"
}

extern int coleco_updatetms;

#define ROWCNT 256
#define LINECNT 256

PatternWindow::PatternWindow(QWidget *parent)
    : QMainWindow(parent),
    m_vramPixmap(512, 512),
    m_tilePixmap(8, 8),
    m_baseVram(0),
    m_vramTile(0)
{
    setWindowTitle("Pattern Table Viewer");
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint);
    setFixedSize(800, 620);

    m_vramPixmap.fill(Qt::black);
    m_tilePixmap.fill(Qt::black);

    setupUI();
    setupMenus();

    m_autoRefreshAction->setChecked(true);
}

PatternWindow::~PatternWindow()
{
}

void PatternWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setSpacing(10);

    // --- Linkerpaneel (VRAM Grid + Scrollbar) ---
    QVBoxLayout *leftLayout = new QVBoxLayout();

    QGroupBox *vramBox = new QGroupBox("VRAM Pattern View");
    vramBox->setStyleSheet("QGroupBox { color: white; }");
    QHBoxLayout *vramLayout = new QHBoxLayout(vramBox);

    m_vramLabel = new QLabel;
    m_vramLabel->setFixedSize(512, 512);
    m_vramLabel->setPixmap(m_vramPixmap);
    m_vramLabel->setMouseTracking(true);
    m_vramLabel->installEventFilter(this);
    m_vramLabel->setStyleSheet("background-color: black; border: 1px solid #808080;");
    vramLayout->addWidget(m_vramLabel);

    m_vramScroll = new QScrollBar(Qt::Vertical);
    m_vramScroll->setRange(0, 0x3F80); // 0x4000 - 0x80 (één rij)
    m_vramScroll->setSingleStep(0x80); // Eén rij van 8 pixels
    m_vramScroll->setPageStep(0x800);  // Eén pagina (32 rijen)
    vramLayout->addWidget(m_vramScroll);

    leftLayout->addWidget(vramBox);

    // Offset label onder de box
    QHBoxLayout* ofsLayout = new QHBoxLayout();
    QLabel* vramOfsTitle = new QLabel("VRAM Offset:");
    vramOfsTitle->setStyleSheet("color: white;");
    m_vramOfsLabel = new QLabel("$0000");
    m_vramOfsLabel->setStyleSheet("color: white; font-weight: bold;");
    m_vramTxtLabel = new QLabel("BG Tile");
    m_vramTxtLabel->setStyleSheet("color: cyan; font-weight: bold;");
    m_vramTxtLabel->setMinimumWidth(80);

    ofsLayout->addWidget(vramOfsTitle);
    ofsLayout->addWidget(m_vramOfsLabel);
    ofsLayout->addStretch();
    ofsLayout->addWidget(m_vramTxtLabel);
    leftLayout->addLayout(ofsLayout);


    // --- Rechterpaneel (Info, Tile, Opties) ---
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(15);

    // Tile Info Box
    m_tileInfoBox = new QGroupBox("Tile Info");
    m_tileInfoBox->setStyleSheet("QGroupBox { color: white; } QLabel { color: white; }");
    QFormLayout *infoLayout = new QFormLayout(m_tileInfoBox);
    infoLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);

    m_tileIndexLabel = new QLabel("$000");
    m_tileAddrLabel = new QLabel("$0000");
    infoLayout->addRow("Tile Index:", m_tileIndexLabel);
    infoLayout->addRow("Tile Address:", m_tileAddrLabel);

    rightLayout->addWidget(m_tileInfoBox);

    // Tile View Box
    m_tileViewBox = new QGroupBox("Tile 8x8 pixels");
    m_tileViewBox->setStyleSheet("QGroupBox { color: white; } QLabel { color: white; }");
    QHBoxLayout *tileLayout = new QHBoxLayout(m_tileViewBox);

    m_tileLabel = new QLabel;
    m_tileLabel->setFixedSize(128, 128);
    m_tileLabel->setPixmap(m_tilePixmap.scaled(128, 128));
    m_tileLabel->setStyleSheet("background-color: black; border: 1px solid #808080;");
    tileLayout->addWidget(m_tileLabel);

    // Hex waarden
    QVBoxLayout *hexLayout = new QVBoxLayout();
    QFont monospaceFont("Monospace");
    monospaceFont.setStyleHint(QFont::TypeWriter);
    hexLayout->setSpacing(0);
    for (int i = 0; i < 8; ++i) {
        m_tileValueLabels[i] = new QLabel("$00");
        m_tileValueLabels[i]->setFont(monospaceFont);
        m_tileValueLabels[i]->setFixedSize(40, 16);
        m_tileValueLabels[i]->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        hexLayout->addWidget(m_tileValueLabels[i]);
    }
    tileLayout->addLayout(hexLayout);
    rightLayout->addWidget(m_tileViewBox);

    // Options Box
    QGroupBox *optionsBox = new QGroupBox("Options");
    optionsBox->setStyleSheet("QGroupBox { color: white; } QCheckBox { color: white; } QRadioButton { color: white; }");
    QVBoxLayout *optionsLayout = new QVBoxLayout(optionsBox);

    m_colorRadio = new QRadioButton("Color");
    m_bwRadio = new QRadioButton("B&&W");
    m_gridCheck = new QCheckBox("Show Grid");

    m_colorRadio->setChecked(true);
    optionsLayout->addWidget(m_colorRadio);
    optionsLayout->addWidget(m_bwRadio);
    optionsLayout->addWidget(m_gridCheck);

    rightLayout->addWidget(optionsBox);
    rightLayout->addStretch();

    mainLayout->addLayout(leftLayout);
    mainLayout->addLayout(rightLayout);
    mainLayout->setStretch(0, 1);
    setCentralWidget(centralWidget);

    connect(m_gridCheck, &QCheckBox::toggled, this, &PatternWindow::onOptionToggled);
    connect(m_colorRadio, &QRadioButton::toggled, this, &PatternWindow::onOptionToggled);
    connect(m_bwRadio, &QRadioButton::toggled, this, &PatternWindow::onOptionToggled);
    connect(m_vramScroll, &QScrollBar::valueChanged, this, &PatternWindow::onScrollChanged);
}

void PatternWindow::setupMenus()
{
    m_menuBar = new QMenuBar(this);

    // File-menu
    m_fileMenu = m_menuBar->addMenu("&File");
    m_copyAction = new QAction("Copy to clipboard", this);
    connect(m_copyAction, &QAction::triggered, this, &PatternWindow::onCopyToClipboard);
    m_fileMenu->addAction(m_copyAction);

    m_saveAction = new QAction("Save Pattern Table...", this);
    connect(m_saveAction, &QAction::triggered, this, &PatternWindow::onSaveAs);
    m_fileMenu->addAction(m_saveAction);

    m_fileMenu->addSeparator();
    m_exitAction = new QAction("Exit", this);
    connect(m_exitAction, &QAction::triggered, this, &PatternWindow::close);
    m_fileMenu->addAction(m_exitAction);

    // View-menu
    m_viewMenu = m_menuBar->addMenu("&View");
    m_autoRefreshAction = new QAction("Auto Refresh", this);
    m_autoRefreshAction->setCheckable(true);
    connect(m_autoRefreshAction, &QAction::toggled, this, &PatternWindow::onAutoRefreshToggled);
    m_viewMenu->addAction(m_autoRefreshAction);

    m_refreshAction = new QAction("Refresh", this);
    m_refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_refreshAction, &QAction::triggered, this, &PatternWindow::onRefresh);
    m_viewMenu->addAction(m_refreshAction);

    setMenuBar(m_menuBar);
}

void PatternWindow::closeEvent(QCloseEvent *event)
{
    emit windowClosed();
    event->accept();
}

void PatternWindow::showEvent(QShowEvent *event)
{
    if (!emulator) return;
    onRefresh();
    event->accept();
}

bool PatternWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_vramLabel && event->type() == QEvent::MouseMove)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        updateTileInfo(mouseEvent->pos().x(), mouseEvent->pos().y());
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void PatternWindow::doRefresh()
{
    if (m_autoRefreshAction->isChecked() && coleco_updatetms) {
        updateChanges();
        smallUpdateChanges();
        coleco_updatetms = 0;
    }
}

void PatternWindow::onSaveAs()
{
    QString fileName = QFileDialog::getSaveFileName(this, "Save Image", "", "PNG Image (*.png);;Bitmap Image (*.bmp)");
    if (!fileName.isEmpty()) {
        m_vramPixmap.save(fileName);
    }
}

void PatternWindow::onCopyToClipboard()
{
    QApplication::clipboard()->setPixmap(m_vramPixmap);
}

void PatternWindow::onRefresh()
{
    if (!emulator) return;
    updateOfsText();
    updateChanges();
    smallUpdateChanges();
}

void PatternWindow::onAutoRefreshToggled(bool checked)
{
    if (checked) {
        onRefresh();
    }
}

void PatternWindow::onOptionToggled()
{
    updateChanges();
    smallUpdateChanges();
}

void PatternWindow::onScrollChanged(int value)
{
    // Align op 128-byte boundary (halve rij tiles)
    int alignedValue = value - (value % 0x80);
    if (alignedValue != m_baseVram) {
        m_baseVram = alignedValue;

        m_vramScroll->blockSignals(true);
        m_vramScroll->setValue(m_baseVram);
        m_vramScroll->blockSignals(false);

        m_vramOfsLabel->setText("$" + intToHex(m_baseVram, 4));
        updateOfsText();
        updateChanges();
    }
}

QString PatternWindow::intToHex(int value, int width)
{
    return QString("%1").arg(value, width, 16, QChar('0')).toUpper();
}

void PatternWindow::updateOfsText(void)
{
    unsigned short bgmap = coleco_gettmsaddr(CHRMAP, 0, 0);
    unsigned short bgtil = coleco_gettmsaddr(CHRGEN, 0, 0);
    unsigned short bgcol = coleco_gettmsaddr(CHRCOL, 0, 0);
    unsigned short sprtil = coleco_gettmsaddr(SPRGEN, 0, 0);
    unsigned short spratr = coleco_gettmsaddr(SPRATTR, 0, 0);

    unsigned short bgmapsize = 0x300;
    unsigned short bgtilsize = 0x1800;
    unsigned short bgcolsize = (tms.Mode == 2) ? 0x1800 : 0x0800;
    unsigned short sprtilsize = 0x0800;

    int currentTile = m_vramTile;

    if ((currentTile >= bgmap) && (currentTile < bgmap + bgmapsize))
        m_vramTxtLabel->setText("BG Map");
    else if ((currentTile >= bgtil) && (currentTile < bgtil + bgtilsize))
        m_vramTxtLabel->setText("BG Tile");
    else if ((currentTile >= bgcol) && (currentTile < bgcol + bgcolsize))
        m_vramTxtLabel->setText("BG Col");
    else if ((currentTile >= sprtil) && (currentTile < sprtil + sprtilsize))
        m_vramTxtLabel->setText("SPR Tile");
    else if ((currentTile >= spratr) && (currentTile < spratr + 0x0080))
        m_vramTxtLabel->setText("SPR Attr");
    else
        m_vramTxtLabel->setText("VRAM");
}


void PatternWindow::updateTileInfo(int x, int y)
{
    int ix = x / (m_vramLabel->width() / 32);  // 32 tiles breed
    int iy = y / (m_vramLabel->height() / 32); // 32 tiles hoog

    if (ix < 0) ix = 0; if (ix > 31) ix = 31;
    if (iy < 0) iy = 0; if (iy > 31) iy = 31;

    // 'it' is het VRAM-adres van het patroon
    int it = (ix * 8) + (256 * iy) + m_baseVram;

    // 'tile index' is de index in de grid
    int tileIndex = ix + (32 * iy) + (m_baseVram / 8);

    m_tileIndexLabel->setText("$" + intToHex(tileIndex, 3));
    m_tileAddrLabel->setText("$" + intToHex(it, 4));

    if (it != m_vramTile) {
        m_vramTile = it;
        updateOfsText();
        smallUpdateChanges();
    }
}

void PatternWindow::createTilePixmap()
{
    int iy, value, vcol, ofsvram;
    QRgb fgcol, bgcol;
    QImage tileImage(8, 8, QImage::Format_RGB32);

    if (!emulator) {
        m_tilePixmap = QPixmap::fromImage(tileImage);
        return;
    }

    for (iy = 0; iy < 8; iy++) {
        QRgb *linePtr = (QRgb *)tileImage.scanLine(iy);

        BYTE fake_y = 0x00;
        ofsvram = m_vramTile - coleco_gettmsaddr(CHRGEN, 0, 0);

        if (ofsvram >= 0x1000) fake_y = 0x80;
        else if (ofsvram >= 0x800) fake_y = 0x40;

        int pattern_byte_addr = (m_vramTile & 0x7FF) + iy;

        if ((m_vramTile >= coleco_gettmsaddr(CHRGEN, 0, 0)) && (m_vramTile < coleco_gettmsaddr(CHRGEN, 0, 0) + 0x1800)) {

            if (tms.Mode == 1) {
                // Mode 1: kleur is per 8 bytes (hele tile)
                vcol = coleco_gettmsval(CHRCOL, m_vramTile >> 3, tms.Mode, 0);
            } else {
                // Mode 2: kleur is per byte
                vcol = coleco_gettmsval(CHRCOL, pattern_byte_addr, tms.Mode, fake_y);
            }

            // Haal patroon data op
            value = coleco_gettmsval(CHRGEN, pattern_byte_addr, tms.Mode, fake_y);

            if (m_bwRadio->isChecked()) {
                fgcol = 0xFFFFFFFF;
                bgcol = 0xFF000000 | cv_pal32[0];
            } else {
                fgcol = 0xFF000000 | cv_pal32[tms.IdxPal[vcol >> 4]];
                bgcol = 0xFF000000 | cv_pal32[tms.IdxPal[vcol & 0xf]];
            }
        } else {
            // Niet in CHRGEN (bv. BG Map, SPR Attr), toon als ruwe data
            fgcol = 0xFFFFFFFF;
            bgcol = 0xFF000000 | cv_pal32[0];
            value = coleco_gettmsval(VRAM, m_vramTile + iy, 0x00, 0x00);
        }

        // Teken bitmap
        *(linePtr++) = (value & 0x80) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x40) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x20) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x10) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x08) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x04) ? fgcol : bgcol;
        *(linePtr++) = (value & 0x02) ? fgcol : bgcol;
        *(linePtr)   = (value & 0x01) ? fgcol : bgcol;

        m_tileValueLabels[iy]->setText("$" + intToHex(value, 2));
    }

    m_tilePixmap = QPixmap::fromImage(tileImage);
}

void PatternWindow::smallUpdateChanges()
{
    createTilePixmap();

    // Schaal de 8x8 tile naar 128x128
    QPixmap scaledTile = m_tilePixmap.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    // Teken het grid (net als in ntablewindow, altijd)
    QPainter painter(&scaledTile);
    painter.setPen(QColor(Qt::darkGray));
    int step = 128 / 8;
    for (int x = 0; x <= 128; x += step) {
        painter.drawLine(x, 0, x, 128);
    }
    for (int y = 0; y <= 128; y += step) {
        painter.drawLine(0, y, 128, y);
    }

    m_tileLabel->setPixmap(scaledTile);
}

void PatternWindow::createVramPixmap(QPaintDevice *device, int w, int h)
{
    // Port van Tpatternviewer::CreateBitmap
    int ix, iy, it, ofsit, vcol, value;
    QRgb fgcol, bgcol;
    BYTE fake_y;

    // Maak de 256x256 basis-image
    QImage image(ROWCNT, LINECNT, QImage::Format_RGB32);
    image.fill(Qt::black);

    if (!emulator) {
        QPainter painter(device);
        painter.drawPixmap(0, 0, w, h, QPixmap::fromImage(image));
        return;
    }

    for (iy = 0; iy < LINECNT; iy++) {
        QRgb *scanLine = (QRgb *)image.scanLine(iy);
        int line_offset = (iy & 7); // 0-7

        for (ix = 0; ix < (ROWCNT / 8); ix++) { // 32 tiles breed

            // Adres van de tile-basis
            it = (ix * 8) + LINECNT * (iy >> 3) + m_baseVram;

            // Fake de Y-coördinaat voor bank switching
            fake_y = 0x00;
            ofsit = it - coleco_gettmsaddr(CHRGEN, 0, 0);
            if (ofsit >= 0x1000) fake_y = 0x80;
            else if (ofsit >= 0x800) fake_y = 0x40;

            // Adres voor deze specifieke byte
            int pattern_byte_addr = (it & 0x7FF) + line_offset;

            if ((it >= coleco_gettmsaddr(CHRGEN, 0, 0)) && (it < coleco_gettmsaddr(CHRGEN, 0, 0) + 0x1800)) {

                if (tms.Mode == 1) {
                    vcol = coleco_gettmsval(CHRCOL, it >> 3, tms.Mode, 0);
                } else {
                    vcol = coleco_gettmsval(CHRCOL, pattern_byte_addr, tms.Mode, fake_y);
                }

                value = coleco_gettmsval(CHRGEN, pattern_byte_addr, tms.Mode, fake_y);

                if (m_bwRadio->isChecked()) {
                    fgcol = 0xFFFFFFFF;
                    bgcol = 0xFF000000 | cv_pal32[0];
                } else {
                    fgcol = 0xFF000000 | cv_pal32[tms.IdxPal[vcol >> 4]];
                    bgcol = 0xFF000000 | cv_pal32[tms.IdxPal[vcol & 0xf]];
                }
            } else {
                // Buiten CHRGEN
                fgcol = 0xFFFFFFFF;
                bgcol = 0xFF000000 | cv_pal32[0];
                value = coleco_gettmsval(VRAM, it + line_offset, 0x00, 0x00);
            }

            // Teken de 8 pixels
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

    // Converteer naar QPixmap en schaal naar het doel-device
    QPixmap basePixmap = QPixmap::fromImage(image);
    QPainter painter(device);
    // Schaal 256x256 naar 512x512
    painter.drawPixmap(0, 0, w, h, basePixmap);

    // Teken het grid (indien nodig) over de geschaalde pixmap
    if (m_gridCheck->isChecked()) {
        int step = w / 32; // 512 / 32 = 16
        QColor defaultColor(Qt::darkGray);

        // Haal de boundaries *eenmaal* op
        if (!emulator) { // Veiligheidscheck als de emulator nog niet draait
            painter.setPen(defaultColor);
            for(int i = 0; i <= 32; ++i) {
                int x = (i * step); int y = (i * step);
                if (x >= w) x = w - 1; if (y >= h) y = h - 1;
                painter.drawLine(x, 0, x, h - 1);
                painter.drawLine(0, y, w - 1, y);
            }
            return;
        }

        int bgmap = coleco_gettmsaddr(CHRMAP, 0, 0);
        int bgtil = coleco_gettmsaddr(CHRGEN, 0, 0);
        int bgcol = coleco_gettmsaddr(CHRCOL, 0, 0);
        int sprtil = coleco_gettmsaddr(SPRGEN, 0, 0);
        int spratr = coleco_gettmsaddr(SPRATTR, 0, 0);
        int bgmapsize = 0x300;
        int bgtilsize = 0x1800;
        int bgcolsize = (tms.Mode == 2) ? 0x1800 : 0x0800;
        int sprtilsize = 0x0800;
        int spratrsize = 0x0080;

        auto getRegionId = [&](int vramAddr) {
            if ((vramAddr >= bgmap) && (vramAddr < bgmap + bgmapsize)) return 1; // BG Map
            if ((vramAddr >= bgtil) && (vramAddr < bgtil + bgtilsize)) return 2; // BG Tile
            if ((vramAddr >= bgcol) && (vramAddr < bgcol + bgcolsize)) return 3; // BG Col
            if ((vramAddr >= sprtil) && (vramAddr < sprtil + sprtilsize)) return 4; // SPR Tile
            if ((vramAddr >= spratr) && (vramAddr < spratr + spratrsize)) return 5; // SPR Attr
            return 0; // Default VRAM
        };

        // Maak een 2D-array van de regio-ID's voor de zichtbare grid
        int gridRegions[32][32]; // [iy][ix]
        for (int iy = 0; iy < 32; iy++) {
            for (int ix = 0; ix < 32; ix++) {
                // Adres van de tegel op (ix, iy) in de grid
                int tileAddr = m_baseVram + (iy * 256) + (ix * 8);
                gridRegions[iy][ix] = getRegionId(tileAddr);
            }
        }

        for (int i = 0; i <= 32; i++) {
            int x = (i * step);
            int y = (i * step);
            if (x >= w) x = w - 1;
            if (y >= h) y = h - 1;

            if (i > 0 && i < 32) {
                bool boundaryFound = false;
                for(int row = 0; row < 32; row++) {
                    if (gridRegions[row][i-1] != gridRegions[row][i]) {
                        boundaryFound = true;
                        break;
                    }
                }

                if (boundaryFound) {
                    // Als EEN van de grenzen BGMap (ID=1) is, maak de lijn rood
                    if (gridRegions[0][i-1] == 1 || gridRegions[0][i] == 1) {
                        painter.setPen(QColor(Qt::red)); // De "rode lijn"
                    } else {
                        painter.setPen(QColor(Qt::white));
                    }
                } else {
                    painter.setPen(defaultColor);
                }
            } else {
                painter.setPen(defaultColor); // Randlijnen
            }
            painter.drawLine(x, 0, x, h - 1);

            // --- Horizontale Lijn (op positie y) ---
            // (Lijn 'i' scheidt rij 'i-1' en 'i')
            if (i > 0 && i < 32) {
                bool boundaryFound = false;
                for(int col = 0; col < 32; col++) {
                    if (gridRegions[i-1][col] != gridRegions[i][col]) {
                        boundaryFound = true;
                        break;
                    }
                }

                if (boundaryFound) {
                    // Als EEN van de grenzen BGMap (ID=1) is, maak de lijn rood
                    if (gridRegions[i-1][0] == 1 || gridRegions[i][0] == 1) {
                        painter.setPen(QColor(Qt::red)); // De "rode lijn"
                    } else {
                        painter.setPen(QColor(Qt::white));
                    }
                } else {
                    painter.setPen(defaultColor);
                }
            } else {
                painter.setPen(defaultColor); // Randlijnen
            }
            painter.drawLine(0, y, w - 1, y);
        }
    }
}

void PatternWindow::updateChanges()
{
    // device, w, h
    createVramPixmap(&m_vramPixmap, m_vramPixmap.width(), m_vramPixmap.height());
    m_vramLabel->setPixmap(m_vramPixmap);
}
