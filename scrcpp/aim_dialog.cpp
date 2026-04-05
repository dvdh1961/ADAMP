#include "aim_dialog.h"
#include "customfiledialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QFontDatabase>
#include <QStyle>
#include <QEvent>
#include <QBrush>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QStatusBar>
#include <QFile>
#include <QMessageBox>
#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QGridLayout>
#include <QMessageBox>
#include <QDate>
#include <QInputDialog>
#include <QRadioButton>
#include <QComboBox>
#include <QFrame>
#include <QPlainTextEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTableWidgetItem>

/* ---------------------------------------------------------------------------------------------------------------------*/
AimDialog::AimDialog(QWidget *parent) : QDialog(parent) {
    InitFDI(&m_fdiL); InitFDI(&m_fdiR);
    setupUi();
    setupConnections();
    setWindowTitle(tr("ADAM EOS Media Manager"));
    setFixedSize(1150, 630);

    onModePc();
    updateTableFocus(SideL);
}
/* ---------------------------------------------------------------------------------------------------------------------*/
AimDialog::~AimDialog() { EjectFDI(&m_fdiL); EjectFDI(&m_fdiR); }
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::configureTableStyle(QTableWidget *table) {
    table->setFixedSize(475, 480);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({"START", "LEN", "A", "DESCRIPTION", "DATE"});
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(20);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // Dit is de belangrijke lijn
    table->setAlternatingRowColors(true);

    table->setStyleSheet(
        "QTableWidget {"
        "  background-color: #3C3C3C;"
        "  alternate-background-color: #282828;"
        "  color: white;"
        "  border: 2px solid #222222;"
        "  gridline-color: #1E1E1E;"
        "  selection-background-color: #3399FF;"
        "  selection-color: black;"
        "}"
        "QTableWidget::item {"
        "  padding-left: 4px;"
        "  padding-right: 4px;"
        "}"
        "QTableWidget#tableL[active=\"true\"] {"
        "  border: 2px solid #4B0082;"
        "}"
        "QTableWidget#tableR[active=\"true\"] {"
        "  border: 2px solid #008200;"
        "}"
        "QHeaderView::section {"
        "  background-color: #2B2B2B;"
        "  color: white;"
        "  border: 1px solid #1E1E1E;"
        "  padding: 4px;"
        "}"
        );

    table->setColumnWidth(0, 50);
    table->setColumnWidth(1, 50);
    table->setColumnWidth(2, 45);
    table->setColumnWidth(3, 200);
    //table->setColumnWidth(4, 75);

    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::updateTableFocus(Side side) {
    m_tableL->setProperty("active", side == SideL);
    m_tableR->setProperty("active", side == SideR);
    m_tableL->style()->unpolish(m_tableL); m_tableL->style()->polish(m_tableL);
    m_tableR->style()->unpolish(m_tableR); m_tableR->style()->polish(m_tableR);
}
/* ---------------------------------------------------------------------------------------------------------------------*/
bool AimDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);

        // ---------------- LEFT TABLE ----------------
        if (obj == m_tableL->viewport()) {
            updateTableFocus(SideL);

            QTableWidgetItem *item = m_tableL->itemAt(me->pos());
            if (item) {
                const int row = item->row();
                m_tableL->setCurrentCell(row, 0);
                m_tableL->selectRow(row);
            }

            if (me->button() == Qt::RightButton) {
                onTableContextMenuL(me->pos());
                return true;   // event afgehandeld
            }
        }

        // ---------------- RIGHT TABLE ----------------
        if (obj == m_tableR->viewport()) {
            updateTableFocus(SideR);

            QTableWidgetItem *item = m_tableR->itemAt(me->pos());
            if (item) {
                const int row = item->row();
                m_tableR->setCurrentCell(row, 0);
                m_tableR->selectRow(row);
            }

            if (me->button() == Qt::RightButton) {
                onTableContextMenuR(me->pos());
                return true;   // event afgehandeld
            }
        }
    }

    return QDialog::eventFilter(obj, event);
}
/* ---------------------------------------------------------------------------------------------------------------------*/
QHBoxLayout* AimDialog::createStatusGroup(Side side, QLineEdit*& maxOut, QLineEdit*& usedOut, QLineEdit*& freeOut, QLineEdit*& typeOut) {
    QHBoxLayout *layout = new QHBoxLayout();
    layout->setSpacing(0); layout->setContentsMargins(0, 0, 0, 0);

    QString editStyle = "color: white; background-color: transparent; border: none; padding-top: 0px; padding-bottom: 0px; margin: 0px; font-family: monospace;";
    QString labelStyle = "color: #BBBBBB; padding: 0px; font-family: monospace;";

    auto setupEdit = [&](QLineEdit*& edit, int width, const QString &defaultText) {
        edit = new QLineEdit(defaultText, this);
        edit->setReadOnly(true); edit->setFixedWidth(width);
        edit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        edit->setStyleSheet(editStyle);
    };

    setupEdit(maxOut, 25, "000");
    setupEdit(usedOut, 25, "000");
    setupEdit(freeOut, 25, "000");
    setupEdit(typeOut, 35, "---");

    auto addLbl = [&](const QString &txt) {
        QLabel *l = new QLabel(txt, this);
        l->setStyleSheet(labelStyle);
        layout->addWidget(l);
    };

    addLbl(side == SideL ? "L.V:" : "R.V:");
    layout->addWidget(maxOut);
    addLbl(" U:"); layout->addWidget(usedOut);
    addLbl(" F:"); layout->addWidget(freeOut);
    addLbl(" MEDIA:"); layout->addWidget(typeOut);
    layout->addStretch();

    return layout;
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::setupUi() {
    QVBoxLayout *mainVerticalLayout = new QVBoxLayout(this);
    mainVerticalLayout->setContentsMargins(10, 10, 10, 5);
    // Spacing verlaagd om statusbalk 5 pixels omhoog te schuiven
    mainVerticalLayout->setSpacing(1);

    QHBoxLayout *columnsLayout = new QHBoxLayout();
    this->setStyleSheet("QPushButton { color: white; } QPushButton:disabled { color: #888888; }");

    // --- BLOK 1: LINKS ---
    QWidget *leftContainer = new QWidget(this);
    leftContainer->setFixedWidth(475);
    QVBoxLayout *leftColumn = new QVBoxLayout(leftContainer);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(5);

    QHBoxLayout *volLayoutL = new QHBoxLayout();
    volLayoutL->setContentsMargins(0, 0, 0, 0);
    volLayoutL->addWidget(new QLabel("L-Vol:"));
    m_volEditL = new QLineEdit(this);
    m_volEditL->setReadOnly(true);
    m_volEditL->setContextMenuPolicy(Qt::CustomContextMenu);
    volLayoutL->addWidget(m_volEditL);

    m_dirSizeLabelL = new QLabel("Dir Size:");
    volLayoutL->addWidget(m_dirSizeLabelL);
    m_dirSizeEditL = new QLineEdit("0", this);
    m_dirSizeEditL->setFixedWidth(25);
    m_dirSizeEditL->setReadOnly(true);
    m_dirSizeEditL->setAlignment(Qt::AlignCenter);
    volLayoutL->addWidget(m_dirSizeEditL);

    leftColumn->addLayout(volLayoutL);

    m_tableL = new QTableWidget(this);
    m_tableL->setObjectName("tableL");
    configureTableStyle(m_tableL);
    m_tableL->viewport()->installEventFilter(this);
    leftColumn->addWidget(m_tableL);

    leftColumn->addLayout(createStatusGroup(SideL, m_maxL, m_usedL, m_freeL, m_typeLabelL));
    columnsLayout->addWidget(leftContainer, 0, Qt::AlignCenter);

    // --- BLOK 2: MIDDEN ---
    QVBoxLayout *centerColumn = new QVBoxLayout();
    centerColumn->setSpacing(2);
    centerColumn->addSpacing(40);

    m_btnLabelAdam = new QPushButton("ADAM", this);
    m_btnLabelAdam->setStyleSheet("background-color: #4B0082; color: white; font-weight: bold;");
    m_btnOpenDskL = new QPushButton("< LOAD DSK", this);
    m_btnOpenDdpL = new QPushButton("< LOAD DDP", this);
    m_btnClearL = new QPushButton("< CLEAR", this);
    m_btnCopyL = new QPushButton("< COPY", this);
    m_btnCopyR = new QPushButton("> COPY", this);

    centerColumn->addWidget(m_btnLabelAdam);
    centerColumn->addWidget(m_btnOpenDskL);
    centerColumn->addWidget(m_btnOpenDdpL);
    centerColumn->addWidget(m_btnClearL);

    centerColumn->addSpacing(10);

    centerColumn->addWidget(m_btnCopyL);
    centerColumn->addWidget(m_btnCopyR);
    centerColumn->addSpacing(20);

    m_btnModePc = new QPushButton("PC", this);
    m_btnModeAdam = new QPushButton("ADAM", this);
    m_btnBrowsPc = new QPushButton("> BROWSE PC", this);
    m_btnOpenDskR = new QPushButton("> LOAD DSK", this);
    m_btnOpenDdpR = new QPushButton("> LOAD DDP", this);
    m_btnClearR = new QPushButton("> CLEAR", this);
    m_btnNewImgR = new QPushButton("MAKE IMAGE", this);
    m_btnBlockCopy = new QPushButton("BLOCK COPY", this);

    centerColumn->addWidget(m_btnModePc);
    centerColumn->addWidget(m_btnModeAdam);
    centerColumn->addSpacing(10);
    centerColumn->addWidget(m_btnBrowsPc);
    centerColumn->addWidget(m_btnOpenDskR);
    centerColumn->addWidget(m_btnOpenDdpR);
    centerColumn->addWidget(m_btnClearR);

    centerColumn->addSpacing(10);

    centerColumn->addWidget(m_btnNewImgR);
    centerColumn->addWidget(m_btnBlockCopy);

    centerColumn->addSpacing(10);

    m_btnBlockCopy->setEnabled(false);

    m_btnDel = new QPushButton("DELETE", this);
    m_btnAttrib = new QPushButton("ATTRIB", this);
    m_btnRename = new QPushButton("RENAME", this);
    m_btnCrunch = new QPushButton("CRUNCH", this);

    centerColumn->addWidget(m_btnDel);
    centerColumn->addWidget(m_btnAttrib);
    centerColumn->addWidget(m_btnRename);
    centerColumn->addWidget(m_btnCrunch);
    centerColumn->addSpacing(20);

    m_btnQuit = new QPushButton("QUIT", this);
    centerColumn->addWidget(m_btnQuit);

    for(int i = 0; i < centerColumn->count(); ++i) {
        QLayoutItem *item = centerColumn->itemAt(i);
        if (item->widget()) item->widget()->setFixedWidth(110);
    }
    columnsLayout->addLayout(centerColumn);

    // --- BLOK 3: RECHTS ---
    QWidget *rightContainer = new QWidget(this);
    rightContainer->setFixedWidth(475);
    QVBoxLayout *rightColumn = new QVBoxLayout(rightContainer);
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(5);

    QHBoxLayout *volLayoutR = new QHBoxLayout();
    volLayoutR->setContentsMargins(0, 0, 0, 0);
    volLayoutR->addWidget(new QLabel("R-Vol:"));
    m_volEditR = new QLineEdit(this);
    m_volEditR->setReadOnly(true);
    m_volEditR->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_volEditR->setContextMenuPolicy(Qt::CustomContextMenu);
    volLayoutR->addWidget(m_volEditR);

    m_dirSizeLabelR = new QLabel("Dir Size:");
    volLayoutR->addWidget(m_dirSizeLabelR);
    m_dirSizeEditR = new QLineEdit("0", this);
    m_dirSizeEditR->setFixedWidth(25);
    m_dirSizeEditR->setReadOnly(true);
    m_dirSizeEditR->setAlignment(Qt::AlignCenter);
    volLayoutR->addWidget(m_dirSizeEditR);

    rightColumn->addLayout(volLayoutR);

    m_tableR = new QTableWidget(this);
    m_tableR->setObjectName("tableR");
    configureTableStyle(m_tableR);
    m_tableR->viewport()->installEventFilter(this);
    rightColumn->addWidget(m_tableR);

    m_statusWidgetR = new QWidget(this);
    m_statusWidgetR->setLayout(createStatusGroup(SideR, m_maxR, m_usedR, m_freeR, m_typeLabelR));
    rightColumn->addWidget(m_statusWidgetR);

    m_pcStatusWidgetR = new QWidget(this);
    QHBoxLayout *pcStatusLayout = new QHBoxLayout(m_pcStatusWidgetR);
    pcStatusLayout->setContentsMargins(0, 0, 0, 0);
    pcStatusLayout->setSpacing(0);

    QLabel *filesLbl = new QLabel("FILES:", this);
    filesLbl->setStyleSheet("color: #BBBBBB; font-family: monospace;");
    m_pcFileCountEditR = new QLineEdit("000", this);
    m_pcFileCountEditR->setReadOnly(true);
    m_pcFileCountEditR->setFixedWidth(30);
    m_pcFileCountEditR->setStyleSheet("color: white; background-color: transparent; border: none; font-family: monospace;");

    pcStatusLayout->addWidget(filesLbl);
    pcStatusLayout->addWidget(m_pcFileCountEditR);
    pcStatusLayout->addStretch();

    rightColumn->addWidget(m_pcStatusWidgetR);
    m_pcStatusWidgetR->hide();

    columnsLayout->addWidget(rightContainer, 0, Qt::AlignCenter);
    mainVerticalLayout->addLayout(columnsLayout);

    // Statusbalk onderaan
    m_statusBar = new QStatusBar(this);
    m_statusBar->setSizeGripEnabled(false);
    m_statusBar->setStyleSheet("QStatusBar { color: white; background-color: #333333; border-top: 1px solid black; font-family: monospace; }");

    int lineHeight = m_statusBar->fontMetrics().lineSpacing();
    m_statusBar->setFixedHeight(lineHeight * 1 + 10);

    m_statusBar->showMessage(tr("Ready"));
    mainVerticalLayout->addWidget(m_statusBar);

    for (QPushButton *btn : findChildren<QPushButton*>()) {
        btn->setCursor(Qt::PointingHandCursor);
    }

    m_volEditL->setCursor(Qt::PointingHandCursor);
    m_volEditR->setCursor(Qt::PointingHandCursor);

    m_volEditL->setStyleSheet("QLineEdit { color: white; } QLineEdit:hover { background-color: #444444; }");
    m_volEditR->setStyleSheet("QLineEdit { color: white; } QLineEdit:hover { background-color: #444444; }");

    m_tableL->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableR->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::setupConnections() {
    connect(m_btnModePc, &QPushButton::clicked, this, &AimDialog::onModePc);
    connect(m_btnModeAdam, &QPushButton::clicked, this, &AimDialog::onModeAdam);
    connect(m_btnBrowsPc, &QPushButton::clicked, this, &AimDialog::onPC);
    connect(m_btnOpenDskL, &QPushButton::clicked, this, &AimDialog::onOpenDskL);
    connect(m_btnOpenDdpL, &QPushButton::clicked, this, &AimDialog::onOpenDdpL);
    connect(m_btnOpenDskR, &QPushButton::clicked, this, &AimDialog::onOpenDskR);
    connect(m_btnOpenDdpR, &QPushButton::clicked, this, &AimDialog::onOpenDdpR);
    connect(m_btnNewImgR,  &QPushButton::clicked, this, &AimDialog::onNewImgR);
    connect(m_btnCopyL,    &QPushButton::clicked, this, &AimDialog::onCopyL);
    connect(m_btnCopyR,    &QPushButton::clicked, this, &AimDialog::onCopyR);
    connect(m_btnDel,      &QPushButton::clicked, this, &AimDialog::onDelete);
    connect(m_btnAttrib,   &QPushButton::clicked, this, &AimDialog::onAttrib);
    connect(m_btnRename,   &QPushButton::clicked, this, &AimDialog::onRename);
    connect(m_btnCrunch,   &QPushButton::clicked, this, &AimDialog::onCrunch);
    connect(m_btnQuit,     &QPushButton::clicked, this, &AimDialog::onQuit);
    connect(m_btnClearL, &QPushButton::clicked, this, &AimDialog::onClearL);
    connect(m_btnClearR, &QPushButton::clicked, this, &AimDialog::onClearR);
    connect(m_btnBlockCopy, &QPushButton::clicked, this, &AimDialog::onBlockCopy);
    connect(m_volEditL, &QLineEdit::customContextMenuRequested,
            this, &AimDialog::onEditVolumeNameL);

    connect(m_volEditR, &QLineEdit::customContextMenuRequested,
            this, &AimDialog::onEditVolumeNameR);
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onModePc() {
    m_volEditL->setCursor(Qt::ArrowCursor);
    m_volEditR->setCursor(Qt::ArrowCursor);
    m_tableR->setRowCount(0);
    m_tableR->setColumnCount(4);
    m_tableR->setHorizontalHeaderLabels({"LEN", "DESCRIPTION", "TIME", "DATE"});
    m_tableR->setColumnWidth(0, 80);
    m_tableR->setColumnWidth(1, 200);
    m_tableR->setColumnWidth(2, 85);
    m_tableR->setColumnWidth(3, 85);

    m_btnModePc->setStyleSheet("background-color: darkgreen; color: white; font-weight: bold;");
    m_btnModeAdam->setStyleSheet("color: white;");

    m_btnBrowsPc->setEnabled(true);
    m_btnNewImgR->setEnabled(true);
    m_btnOpenDskR->setEnabled(false);
    m_btnOpenDdpR->setEnabled(false);
    m_btnBlockCopy->setEnabled(false);

    if (m_dirSizeLabelR) m_dirSizeLabelR->hide();
    if (m_dirSizeEditR) m_dirSizeEditR->hide();
    if (m_statusWidgetR) m_statusWidgetR->hide();
    if (m_pcStatusWidgetR) m_pcStatusWidgetR->show();

    if (m_statusBar) m_statusBar->showMessage(tr("PC Mode Active"));
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onModeAdam()
{
    m_btnModeAdam->setStyleSheet("background-color: darkgreen; color: white; font-weight: bold;");
    m_btnModePc->setStyleSheet("color: white;");

    m_btnBrowsPc->setEnabled(false);
    m_btnNewImgR->setEnabled(false);
    m_btnOpenDskR->setEnabled(true);
    m_btnOpenDdpR->setEnabled(true);
    m_btnBlockCopy->setEnabled(true);

    m_tableR->setColumnCount(5);
    m_tableR->setHorizontalHeaderLabels({"START", "LEN", "A", "DESCRIPTION", "DATE"});

    // ADAM kolombreedtes opnieuw zetten
    m_tableR->setColumnWidth(0, 50);
    m_tableR->setColumnWidth(1, 50);
    m_tableR->setColumnWidth(2, 45);   // attribute wat breder
    m_tableR->setColumnWidth(3, 200);
    m_tableR->horizontalHeader()->setStretchLastSection(true);
    m_tableR->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    if (m_dirSizeLabelR) m_dirSizeLabelR->show();
    if (m_dirSizeEditR)  m_dirSizeEditR->show();
    if (m_statusWidgetR) m_statusWidgetR->show();
    if (m_pcStatusWidgetR) m_pcStatusWidgetR->hide();

    if (m_typeR != ImageNone)
        repopulateTable(SideR);
    else {
        m_tableR->setRowCount(0);
        m_tableR->clearContents();
    }

    if (m_statusBar) m_statusBar->showMessage(tr("ADAM Mode Active"));
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onPC() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Open PC Directory"), m_diskRootPath, QFileDialog::ShowDirsOnly);
    if (!dir.isEmpty()) {
        m_volEditR->setText(dir);
        QDir qdir(dir);
        QFileInfoList list = qdir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        m_pcFileCountEditR->setText(QString("%1").arg(list.size(), 3, 10, QChar('0')));

        m_tableR->setRowCount(0);
        for (const QFileInfo &fi : list) {
            int row = m_tableR->rowCount();
            m_tableR->insertRow(row);
            auto *iLen = new QTableWidgetItem(QString::number(fi.size()));
            iLen->setTextAlignment(Qt::AlignCenter);
            auto *iDesc = new QTableWidgetItem(fi.fileName());
            auto *iTime = new QTableWidgetItem(fi.lastModified().toString("HH:mm:ss"));
            iTime->setTextAlignment(Qt::AlignCenter);
            auto *iDate = new QTableWidgetItem(fi.lastModified().toString("dd/MM/yyyy"));
            iDate->setTextAlignment(Qt::AlignCenter);
            m_tableR->setItem(row, 0, iLen);
            m_tableR->setItem(row, 1, iDesc);
            m_tableR->setItem(row, 2, iTime);
            m_tableR->setItem(row, 3, iDate);
        }
        if (m_statusBar) m_statusBar->showMessage(tr("Loaded: %1").arg(dir));
    }
}
/* ---------------------------------------------------------------------------------------------------------------------*/
int AimDialog::getMaxBlocks(Side side) const {
    ImageType t = (side == SideL) ? m_typeL : m_typeR;
    return (t == ImageDsk) ? 160 : (t == ImageDdp ? 256 : 0);
}
/* ---------------------------------------------------------------------------------------------------------------------*/
QByteArray AimDialog::readAdamBlock(int b, Side s) {
    static const byte IT[8]= {0,5,2,7,4,1,6,3}; FDIDisk *fdi = (s == SideL) ? &m_fdiL : &m_fdiR;
    QByteArray bd; for(int i=0, sec=b<<1 ; i<2 ; ++sec, ++i) {
        int p = (sec & ~7) | IT[sec & 7]; byte* d = LinearFDI(fdi, p);
        if(d) bd.append((const char*)d, 512);
    } return bd;
}
/* ---------------------------------------------------------------------------------------------------------------------*/
bool AimDialog::openImageFile(const QString &p, ImageType t, Side s) {
    FDIDisk *fdi = (s == SideL) ? &m_fdiL : &m_fdiR; EjectFDI(fdi);
    int fmt = (t == ImageDsk) ? FMT_ADMDSK : FMT_DDP;
    if (!LoadFDI(fdi, p.toUtf8().constData(), fmt)) return false;
    if(s == SideL) { m_typeL = t; m_pathL = p; }
    else { m_typeR = t; m_pathR = p; }
    if (m_statusBar) m_statusBar->showMessage(tr("Opened: %1").arg(p));
    return parseDirectory(s);
}
/* ---------------------------------------------------------------------------------------------------------------------*/
bool AimDialog::parseDirectory(Side side)
{
    QVector<EosEntry> &entries = (side == SideL) ? m_entriesL : m_entriesR;
    entries.clear();

    bool sf = false;
    int blocksRead = 0;
    int realDirBlocks = 0;

    for (int b = 1; b <= 3 && !sf; ++b) {
        QByteArray data = readAdamBlock(b, side);
        if (data.size() < 1024)
            continue;

        blocksRead = b;
        const uchar *rb = reinterpret_cast<const uchar*>(data.constData());

        for (int i = 0; i < 39; ++i) {
            const uchar *raw = rb + (i * 26);
            if (raw[0] == 0x00 || raw[0] == 0xFF)
                continue;

            const bool isVolume = (raw[13] == 0x55 && raw[14] == 0xAA);
            EosEntry e = entryFromBytes(raw, isVolume, side);

            if (!isVolume && !e.isSentinel() && e.sblock >= (quint16)getMaxBlocks(side))
                continue;

            entries.append(e);

            // DIR SIZE zit in volume attribute:
            // 0x81 = 1, 0x82 = 2, 0x83 = 3
            if (isVolume) {
                int dirBlocksFromVolAttr = raw[12] & 0x03;
                if (dirBlocksFromVolAttr >= 1 && dirBlocksFromVolAttr <= 3)
                    realDirBlocks = dirBlocksFromVolAttr;
            }

            if (e.isSentinel()) {
                sf = true;
                break;
            }
        }
    }

    if (realDirBlocks <= 0)
        realDirBlocks = qMax(1, blocksRead);

    if (side == SideL)
        m_dirBlocksL = realDirBlocks;
    else
        m_dirBlocksR = realDirBlocks;

    repopulateTable(side);
    return true;
}
/* ---------------------------------------------------------------------------------------------------------------------*/
AimDialog::EosEntry AimDialog::entryFromBytes(const uchar *raw, bool vol, Side side) const {
    EosEntry e; e.attribute = raw[12];
    if (vol) { e.name = formatVolumeName(raw); e.typeChar = " "; }
    else { e.name = formatFileName(raw, e.attribute, e.typeChar); }
    e.used   = raw[19] | (raw[20] << 8);
    e.lcount = raw[21] | (raw[22] << 8);
    e.sblock = raw[13] | (raw[14] << 8); e.allocd = raw[17] | (raw[18] << 8);
    e.day = raw[23]; e.month = raw[24]; e.year = raw[25];
    if (e.isVolume()) { if(side == SideL) m_volEditL->setText(e.name); else m_volEditR->setText(e.name); }
    return e;
}
/* ---------------------------------------------------------------------------------------------------------------------*/
QString AimDialog::formatFileName(const uchar *raw, quint8 attr, QString &to) const {
    int etx = -1; for (int i = 0; i < 12; ++i) { if (raw[i] == 0x03) { etx = i; break; } }
    int limit = (etx != -1) ? etx : 12; QString f;
    for (int i = 0; i < limit; ++i) { if (raw[i] >= 32) f.append(QChar(raw[i])); } f = f.trimmed();
    if ((attr & 0x08) || (attr & 0x01) || f == "BOOT" || f == "DIRECTORY") { to = " "; return f; }
    else { if (f.length() > 1) { to = f.right(1); return f.left(f.length() - 1).trimmed(); } to = " "; return f; }
}
/* ---------------------------------------------------------------------------------------------------------------------*/
QString AimDialog::formatVolumeName(const uchar *raw) const {
    QString n; for (int i = 0; i < 12; ++i) { if (raw[i] == 0x03 || raw[i] == 0x00) break; if (raw[i] >= 32) n.append(QChar(raw[i])); }
    return n.trimmed();
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::repopulateTable(Side s) {
    QTableWidget *t = (s == SideL) ? m_tableL : m_tableR;
    QVector<EosEntry> *en = (s == SideL) ? &m_entriesL : &m_entriesR;
    QLineEdit *mx = (s == SideL) ? m_maxL : m_maxR, *us = (s == SideL) ? m_usedL : m_usedR, *fr = (s == SideL) ? m_freeL : m_freeR;
    QLineEdit *tp = (s == SideL) ? m_typeLabelL : m_typeLabelR;
    QLineEdit *dsEdit = (s == SideL) ? m_dirSizeEditL : m_dirSizeEditR;
    int dirBlocks = (s == SideL) ? m_dirBlocksL : m_dirBlocksR;
    ImageType type = (s == SideL) ? m_typeL : m_typeR;

    t->setRowCount(0); EosEntry last; bool has = false;
    for (const auto &e : *en) {
        if (e.isVolume() || e.isSentinel()) continue;
        last = e; has = true; int row = t->rowCount(); t->insertRow(row);
        QString dt = QString("%1/%2/%3").arg(e.day,2,16,QChar('0')).toUpper().arg(e.month,2,16,QChar('0')).toUpper().arg(e.year,2,16,QChar('0')).toUpper();
        auto *iS = new QTableWidgetItem(QString("%1").arg(e.sblock, 5, 10, QChar('0')));
        auto *iL = new QTableWidgetItem(QString("%1").arg(e.allocd, 5, 10, QChar('0')));

        // Attribute kolom: exact 2 chars breed
        /*
            A = normaal bestand
            A* = normaal + permanently protected
            S = system
            S* = system + protected
            D = deleted
            D* = deleted + protected
        */

        QString attrText = "  ";

        // positie 0 = type/status
        if (e.isDeleted())
            attrText[0] = 'D';
        else if (e.isSystem())
            attrText[0] = 'S';
        else if (e.attribute & 0x10)
            attrText[0] = 'A';
        else if (!e.typeChar.trimmed().isEmpty())
            attrText[0] = e.typeChar.at(0);   // fallback op bestaand typechar

        // positie 1 = flags
        if (e.attribute & 0x80)   // permanently protect
            attrText[1] = '*';

        auto *iA = new QTableWidgetItem(attrText);

        auto *iD = new QTableWidgetItem(e.name);
        auto *iT = new QTableWidgetItem(dt);

        for (auto *i : {iS, iL, iA, iT})
            i->setTextAlignment(Qt::AlignCenter);

        // PRIORITEIT:
        // 1. boot (cyan)
        // 2. deleted (rood)
        // 3. permanent protect (oranje)
        // 4. system (geel)

        if (e.name.trimmed().compare("BOOT", Qt::CaseInsensitive) == 0) {
            QBrush brush(Qt::cyan);
            for (auto *i : {iS, iL, iA, iD, iT})
                i->setForeground(brush);
        }
        else if (e.isDeleted()) {
            QBrush brush(Qt::red);
            for (auto *i : {iS, iL, iA, iD, iT})
                i->setForeground(brush);
        }
        else if (e.attribute & 0x80 && !e.isSystem()) {
            QBrush brush(QColor("#FF8C00"));
            for (auto *i : {iS, iL, iA, iD, iT})
                i->setForeground(brush);
        }
        else if (e.isSystem()) {
            QBrush brush(QColor("#FFD700"));
            for (auto *i : {iS, iL, iA, iD, iT})
                i->setForeground(brush);
        }

        t->setItem(row, 0, iS);
        t->setItem(row, 1, iL);
        t->setItem(row, 2, iA);
        t->setItem(row, 3, iD);
        t->setItem(row, 4, iT);
    }

    dsEdit->setText(QString::number(dirBlocks));
    if (has) {
        int m = getMaxBlocks(s), f = m - (last.sblock + last.allocd);
        mx->setText(QString("%1").arg(m, 3, 10, QChar('0')));
        us->setText(QString("%1").arg(m - f, 3, 10, QChar('0')));
        fr->setText(QString("%1").arg(qMax(0, f), 3, 10, QChar('0')));
        tp->setText(type == ImageDsk ? "DSK" : "DDP");
    } else {
        mx->setText("000"); us->setText("000"); fr->setText("000"); tp->setText("---");
    }
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onOpenDskL() { QString f = CustomFileDialog::getOpenFileName(this, "Open L", m_diskRootPath, "Disk (*.dsk)", nullptr, CustomFileDialog::PathDisk); if (!f.isEmpty()) openImageFile(f, ImageDsk, SideL); }
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onOpenDdpL() { QString f = CustomFileDialog::getOpenFileName(this, "Open L", m_tapeRootPath, "Tape (*.ddp)", nullptr, CustomFileDialog::PathTape); if (!f.isEmpty()) openImageFile(f, ImageDdp, SideL); }
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onOpenDskR() { QString f = CustomFileDialog::getOpenFileName(this, "Open R", m_diskRootPath, "Disk (*.dsk)", nullptr, CustomFileDialog::PathDisk); if (!f.isEmpty()) openImageFile(f, ImageDsk, SideR); }
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onOpenDdpR() { QString f = CustomFileDialog::getOpenFileName(this, "Open R", m_tapeRootPath, "Tape (*.ddp)", nullptr, CustomFileDialog::PathTape); if (!f.isEmpty()) openImageFile(f, ImageDdp, SideR); }
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onNewImgR()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Create New Image"));
    dlg.setModal(true);
    dlg.setFixedSize(340, 250);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    QLabel *lblType = new QLabel(tr("Image Type"), &dlg);
    QFont boldFont = lblType->font();
    boldFont.setBold(true);
    lblType->setFont(boldFont);
    mainLayout->addWidget(lblType);

    QRadioButton *rbDsk160 = new QRadioButton(tr("DSK (160K)"), &dlg);
    QRadioButton *rbDdp256 = new QRadioButton(tr("DDP (256K)"), &dlg);
    rbDsk160->setChecked(true);

    mainLayout->addWidget(rbDsk160);
    mainLayout->addWidget(rbDdp256);

    QLabel *lblDir = new QLabel(tr("Dir Size"), &dlg);
    lblDir->setFont(boldFont);
    mainLayout->addWidget(lblDir);

    QComboBox *cbDirSize = new QComboBox(&dlg);
    cbDirSize->addItem("1");
    cbDirSize->addItem("2");
    cbDirSize->addItem("3");
    cbDirSize->setCurrentIndex(0);
    mainLayout->addWidget(cbDirSize);

    mainLayout->addSpacing(6);

    QLabel *lblVol = new QLabel(tr("Volume Name"), &dlg);
    lblVol->setFont(boldFont);
    mainLayout->addWidget(lblVol);

    QLineEdit *editVolName = new QLineEdit(&dlg);
    editVolName->setMaxLength(11);
    mainLayout->addWidget(editVolName);

    QLabel *lblFile = new QLabel(tr("Filename (no extension)"), &dlg);
    lblFile->setFont(boldFont);
    mainLayout->addWidget(lblFile);

    QLineEdit *editFileName = new QLineEdit(&dlg);
    editFileName->setMaxLength(64);
    mainLayout->addWidget(editFileName);

    mainLayout->addStretch();

    QFrame *line = new QFrame(&dlg);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnOk = new QPushButton(tr("OK"), &dlg);
    QPushButton *btnClose = new QPushButton(tr("CLOSE"), &dlg);
    btnLayout->addStretch();
    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnClose);
    mainLayout->addLayout(btnLayout);

    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted)
        return;

    QString baseName = editFileName->text().trimmed();
    if (baseName.isEmpty()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Please enter a filename"));
        return;
    }

    QString volumeName = editVolName->text().trimmed().toUpper();
    if (volumeName.isEmpty())
        volumeName = "NEWDISK";

    const int dirSize = cbDirSize->currentText().toInt();

    const bool makeDsk = rbDsk160->isChecked();
    const ImageType imgType = makeDsk ? ImageDsk : ImageDdp;
    const QString extension = makeDsk ? ".dsk" : ".ddp";

    const QString outDir = makeDsk
                               ? (m_diskRootPath.isEmpty() ? QDir::homePath() : m_diskRootPath)
                               : (m_tapeRootPath.isEmpty() ? QDir::homePath() : m_tapeRootPath);

    QString templatePath;

    if (makeDsk) {
        switch (dirSize) {
        case 1: templatePath = QString(":/template/templates/DSK160_D1.DSK"); break;
        case 2: templatePath = QString(":/template/templates/DSK160_D2.DSK"); break;
        case 3: templatePath = QString(":/template/templates/DSK160_D3.DSK"); break;
        default: templatePath = QString(":/template/templates/DSK160_D1.DSK"); break;
        }
    } else {
        switch (dirSize) {
        case 1: templatePath = QString(":/template/templates/DDP256_D1.DDP"); break;
        case 2: templatePath = QString(":/template/templates/DDP256_D2.DDP"); break;
        case 3: templatePath = QString(":/template/templates/DDP256_D3.DDP"); break;
        default: templatePath = QString(":/template/templates/DDP256_D1.DDP"); break;
        }
    }
    const QString outPath = QDir(outDir).filePath(baseName + extension);

    if (!QFileInfo::exists(templatePath)) {
        if (m_statusBar)
            m_statusBar->showMessage(tr("Template not found: %1").arg(templatePath));
        return;
    }

    if (QFileInfo::exists(outPath)) {
        if (QMessageBox::question(
                this,
                tr("Overwrite file"),
                tr("File already exists.\nOverwrite '%1'?").arg(QFileInfo(outPath).fileName()),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
            return;
        }

        const QString cleanOutPath = QDir::cleanPath(outPath);

        if (QDir::cleanPath(m_pathL) == cleanOutPath) {
            EjectFDI(&m_fdiL);
            m_typeL = ImageNone;
            m_pathL.clear();
            m_entriesL.clear();
            m_tableL->setRowCount(0);
            m_volEditL->clear();
            m_dirSizeEditL->setText("0");
            m_maxL->setText("000");
            m_usedL->setText("000");
            m_freeL->setText("000");
            m_typeLabelL->setText("---");
        }

        if (QDir::cleanPath(m_pathR) == cleanOutPath) {
            EjectFDI(&m_fdiR);
            m_typeR = ImageNone;
            m_pathR.clear();
            m_entriesR.clear();
            m_tableR->setRowCount(0);
            m_volEditR->clear();
            m_dirSizeEditR->setText("0");
            m_maxR->setText("000");
            m_usedR->setText("000");
            m_freeR->setText("000");
            m_typeLabelR->setText("---");
        }

        QFile existingFile(outPath);

        // probeer eerst write-permissies te zetten
        QFileDevice::Permissions perms = existingFile.permissions();
        existingFile.setPermissions(perms | QFileDevice::WriteOwner | QFileDevice::WriteUser |
                                    QFileDevice::WriteGroup | QFileDevice::WriteOther);

        // nog eens proberen verwijderen
        if (!existingFile.remove()) {
            // fallback: eerst hernoemen en dan verwijderen
            const QString tempOldPath = outPath + ".old_delete_tmp";

            QFile::remove(tempOldPath); // oude tmp opruimen indien nodig

            if (!QFile::rename(outPath, tempOldPath)) {
                if (m_statusBar) {
                    m_statusBar->showMessage(
                        tr("Failed to remove existing image: %1 (%2)")
                            .arg(QFileInfo(outPath).fileName())
                            .arg(existingFile.errorString())
                        );
                }
                return;
            }

            QFile tempFile(tempOldPath);
            tempFile.setPermissions(tempFile.permissions() | QFileDevice::WriteOwner | QFileDevice::WriteUser |
                                    QFileDevice::WriteGroup | QFileDevice::WriteOther);
            tempFile.remove();
        }
    }

    QFile templateFile(templatePath);
    if (!templateFile.open(QIODevice::ReadOnly)) {
        if (m_statusBar)
            m_statusBar->showMessage(tr("Failed to open template resource"));
        return;
    }

    QByteArray data = templateFile.readAll();
    templateFile.close();

    if (data.isEmpty()) {
        if (m_statusBar)
            m_statusBar->showMessage(tr("Template resource is empty"));
        return;
    }

    QByteArray oldName("FIRST DIR");
    int pos = data.indexOf(oldName);
    if (pos < 0) {
        if (m_statusBar)
            m_statusBar->showMessage(tr("Could not find 'FIRST DIR' in template"));
        return;
    }

    QByteArray newName(12, '\0');
    QByteArray latin = volumeName.left(11).toUpper().toLatin1();
    const int n = qMin(11, latin.size());
    for (int i = 0; i < n; ++i)
        newName[i] = latin[i];
    newName[n] = 0x03;

    memcpy(data.data() + pos, newName.constData(), 12);

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (m_statusBar)
            m_statusBar->showMessage(tr("Failed to create new image file"));
        return;
    }

    if (outFile.write(data) != data.size()) {
        outFile.close();
        if (m_statusBar)
            m_statusBar->showMessage(tr("Failed writing new image file"));
        return;
    }

    outFile.close();

    onModeAdam();

    if (!openImageFile(outPath, imgType, SideR)) {
        if (m_statusBar)
            m_statusBar->showMessage(tr("Image created, but opening failed"));
        return;
    }

    m_volEditR->setText(volumeName);

    if (m_statusBar) {
        m_statusBar->showMessage(
            tr("Created new %1 image (DIR=%2): %3")
                .arg(makeDsk ? "DSK 160K" : "DDP 256K")
                .arg(dirSize)
                .arg(QFileInfo(outPath).fileName())
            );
    }
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onCopyL()
{
    auto isPcModeRight = [&]() -> bool {
        return m_pcStatusWidgetR && m_pcStatusWidgetR->isVisible();
    };

    auto writeAdamBlock = [&](Side side, int blockNum, const QByteArray &blockData) -> bool {
        static const byte IT[8] = {0,5,2,7,4,1,6,3};
        FDIDisk *fdi = (side == SideL) ? &m_fdiL : &m_fdiR;

        QByteArray padded = blockData;
        if (padded.size() < 1024)
            padded.append(QByteArray(1024 - padded.size(), '\0'));
        if (padded.size() != 1024)
            return false;

        for (int i = 0, sec = blockNum << 1; i < 2; ++i, ++sec) {
            int p = (sec & ~7) | IT[sec & 7];
            byte *dst = LinearFDI(fdi, p);
            if (!dst) return false;
            memcpy(dst, padded.constData() + (i * 512), 512);
        }
        return true;
    };

    auto readRawDirEntry = [&](Side side, int entryIndex, uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(raw, dirBlock.constData() + offset, 26);
        return true;
    };

    auto writeRawDirEntry = [&](Side side, int entryIndex, const uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(dirBlock.data() + offset, raw, 26);

        return writeAdamBlock(side, blockNo, dirBlock);
    };

    auto getRawName = [&](const uchar raw[26]) -> QString {
        QString s;
        for (int i = 0; i < 12; ++i) {
            if (raw[i] == 0x03 || raw[i] == 0x00)
                break;
            if (raw[i] >= 32)
                s.append(QChar(raw[i]));
        }
        return s.trimmed();
    };

    auto makeAdamNameField = [&](const QString &srcName, uchar outName[12], QString &visibleName, QString &typeChar) {
        memset(outName, 0, 12);

        QString base = srcName.trimmed().toUpper();
        QString ext;

        int dotPos = base.lastIndexOf('.');
        if (dotPos > 0) {
            ext = base.mid(dotPos + 1).trimmed();
            base = base.left(dotPos).trimmed();
        }

        base.remove(' ');
        base.remove('.');
        base.remove('\t');

        typeChar.clear();
        if (!ext.isEmpty())
            typeChar = ext.left(1);

        QString rawName;
        if (!typeChar.isEmpty())
            rawName = base.left(11) + typeChar;
        else
            rawName = base.left(11);

        QByteArray latin = rawName.toLatin1();
        const int n = qMin(11, latin.size());
        for (int i = 0; i < n; ++i)
            outName[i] = static_cast<uchar>(latin[i]);

        outName[n] = 0x03;

        visibleName = rawName;
        if (!typeChar.isEmpty() && !visibleName.isEmpty())
            visibleName = visibleName.left(visibleName.size() - 1).trimmed();
    };

    auto buildVisibleEntries = [&](Side side) -> QVector<const EosEntry*> {
        QVector<const EosEntry*> visible;
        const QVector<EosEntry> &entries = (side == SideL) ? m_entriesL : m_entriesR;
        visible.reserve(entries.size());

        for (const auto &e : entries) {
            if (e.isVolume() || e.isSentinel())
                continue;
            visible.append(&e);
        }
        return visible;
    };

    auto readFileData = [&](Side side, const EosEntry &entry) -> QByteArray {
        QByteArray fileData;
        const int maxBlocks = getMaxBlocks(side);

        for (int i = 0; i < entry.used; ++i) {
            const int blockNum = entry.sblock + i;
            if (blockNum < 0 || blockNum >= maxBlocks)
                return QByteArray();

            QByteArray blockData = readAdamBlock(blockNum, side);
            if (blockData.size() != 1024)
                return QByteArray();

            const bool isLastBlock = (i == entry.used - 1);
            if (isLastBlock) {
                int lastSize = entry.lcount;
                if (lastSize <= 0 || lastSize > 1024)
                    lastSize = 1024;
                fileData.append(blockData.constData(), lastSize);
            } else {
                fileData.append(blockData);
            }
        }
        return fileData;
    };

         auto copyToAdamSide = [&](const QByteArray &srcData,
                              const QString &sourceName,
                              quint8 sourceAttr,
                              Side destSide) -> bool {
        if (((destSide == SideL) ? m_typeL : m_typeR) == ImageNone) {
            if (m_statusBar) m_statusBar->showMessage(tr("No destination ADAM image loaded"));
            return false;
        }

        const quint16 blocksNeeded = static_cast<quint16>((srcData.size() + 1023) / 1024);
        const quint16 usedBlocks   = blocksNeeded;
        const quint16 lastCount    = static_cast<quint16>((srcData.size() % 1024) == 0 ? 1024 : (srcData.size() % 1024));

        uchar newNameRaw[12];
        QString visibleName;
        QString newTypeChar;
        makeAdamNameField(sourceName, newNameRaw, visibleName, newTypeChar);

        const QVector<EosEntry> &destEntries = (destSide == SideL) ? m_entriesL : m_entriesR;
        for (const auto &e : destEntries) {
            if (e.isVolume() || e.isSentinel() || e.isDeleted())
                continue;

            if (e.name.trimmed().compare(visibleName, Qt::CaseInsensitive) == 0 &&
                e.typeChar.trimmed().compare(newTypeChar, Qt::CaseInsensitive) == 0) {
                if (m_statusBar) m_statusBar->showMessage(tr("A file with that name already exists on destination ADAM media"));
                return false;
            }
        }

        const int totalDirEntries = 39 * 3;
        int blocksLeftIndex = -1;
        uchar blocksLeftRaw[26] = {0};
        int firstEmptyIndex = -1;
        bool foundLastActive = false;
        EosEntry lastActive;

        for (int idx = 0; idx < totalDirEntries; ++idx) {
            uchar raw[26] = {0};
            if (!readRawDirEntry(destSide, idx, raw))
                break;

            if ((raw[0] == 0x00 || raw[0] == 0xFF)) {
                if (firstEmptyIndex < 0)
                    firstEmptyIndex = idx;
                continue;
            }

            const QString rawName = getRawName(raw);
            if (rawName.compare("BLOCKS LEFT", Qt::CaseInsensitive) == 0) {
                blocksLeftIndex = idx;
                memcpy(blocksLeftRaw, raw, 26);
            }

            bool isVolume = (raw[13] == 0x55 && raw[14] == 0xAA);
            EosEntry e = entryFromBytes(raw, isVolume, destSide);

            if (e.isVolume() || e.isSentinel() || e.isDeleted())
                continue;

            if (!foundLastActive || e.sblock > lastActive.sblock) {
                lastActive = e;
                foundLastActive = true;
            }
        }

        quint16 startBlock = 0;
        quint16 freeAlloc  = 0;
        int targetEntryIndex = -1;
        int newBlocksLeftIndex = -1;

        if (blocksLeftIndex >= 0) {
            if ((blocksLeftIndex + 1) >= totalDirEntries) {
                if (m_statusBar) m_statusBar->showMessage(tr("No directory room for new BLOCKS LEFT entry"));
                return false;
            }

            uchar nextRaw[26] = {0};
            if (!readRawDirEntry(destSide, blocksLeftIndex + 1, nextRaw)) {
                if (m_statusBar) m_statusBar->showMessage(tr("Cannot read next directory entry"));
                return false;
            }

            if (!(nextRaw[0] == 0x00 || nextRaw[0] == 0xFF)) {
                if (m_statusBar) m_statusBar->showMessage(tr("Next directory entry after BLOCKS LEFT is not empty"));
                return false;
            }

            startBlock = static_cast<quint16>(blocksLeftRaw[13] | (blocksLeftRaw[14] << 8));
            freeAlloc  = static_cast<quint16>(blocksLeftRaw[17] | (blocksLeftRaw[18] << 8));

            targetEntryIndex = blocksLeftIndex;
            newBlocksLeftIndex = blocksLeftIndex + 1;
        } else {
            if (firstEmptyIndex < 0 || (firstEmptyIndex + 1) >= totalDirEntries) {
                if (m_statusBar) m_statusBar->showMessage(tr("No room left in directory"));
                return false;
            }

            targetEntryIndex = firstEmptyIndex;
            newBlocksLeftIndex = firstEmptyIndex + 1;

            if (foundLastActive)
                startBlock = static_cast<quint16>(lastActive.sblock + lastActive.allocd);
            else
                startBlock = 4;

            freeAlloc = static_cast<quint16>(getMaxBlocks(destSide) - startBlock);
        }

        if (freeAlloc < blocksNeeded) {
            if (m_statusBar) m_statusBar->showMessage(tr("Not enough free space on destination ADAM media"));
            return false;
        }

        const int maxBlocks = getMaxBlocks(destSide);
        if ((startBlock + blocksNeeded) > maxBlocks) {
            if (m_statusBar) m_statusBar->showMessage(tr("Target blocks exceed media size"));
            return false;
        }

        for (quint16 i = 0; i < usedBlocks; ++i) {
            const int offset = i * 1024;
            QByteArray chunk = srcData.mid(offset, 1024);

            if (!writeAdamBlock(destSide, startBlock + i, chunk)) {
                if (m_statusBar) m_statusBar->showMessage(tr("Failed writing ADAM block %1").arg(startBlock + i));
                return false;
            }
        }

        uchar newEntry[26] = {0};
        memcpy(newEntry, newNameRaw, 12);

        quint8 newAttr = sourceAttr;

        // deleted-bit nooit meenemen
        newAttr &= ~0x04;

        // als je sentinel/special bit niet wil meenemen:
        if (sourceName.compare("BLOCKS LEFT", Qt::CaseInsensitive) != 0)
            newAttr &= ~0x01;

        // als alles wegvalt, maak er minstens een gewone user file van
        if (newAttr == 0x00)
            newAttr = 0x10;

        newEntry[12] = newAttr;
        newEntry[13] = static_cast<uchar>(startBlock & 0xFF);
        newEntry[14] = static_cast<uchar>((startBlock >> 8) & 0xFF);
        newEntry[15] = 0x00;
        newEntry[16] = 0x00;
        newEntry[17] = static_cast<uchar>(blocksNeeded & 0xFF);
        newEntry[18] = static_cast<uchar>((blocksNeeded >> 8) & 0xFF);
        newEntry[19] = static_cast<uchar>(usedBlocks & 0xFF);
        newEntry[20] = static_cast<uchar>((usedBlocks >> 8) & 0xFF);
        newEntry[21] = static_cast<uchar>(lastCount & 0xFF);
        newEntry[22] = static_cast<uchar>((lastCount >> 8) & 0xFF);

        const QDate now = QDate::currentDate();
        newEntry[23] = static_cast<uchar>(now.day());
        newEntry[24] = static_cast<uchar>(now.month());
        newEntry[25] = static_cast<uchar>(now.year() % 100);

        if (!writeRawDirEntry(destSide, targetEntryIndex, newEntry)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed writing new file entry"));
            return false;
        }

        uchar newBlocksLeft[26] = {0};
        const QByteArray blName = QByteArray("BLOCKS LEFT");
        memcpy(newBlocksLeft, blName.constData(), qMin(11, blName.size()));
        newBlocksLeft[qMin(11, blName.size())] = 0x03;
        newBlocksLeft[12] = 0x01;

        const quint16 newStart = static_cast<quint16>(startBlock + blocksNeeded);
        const quint16 newAlloc = static_cast<quint16>(freeAlloc - blocksNeeded);

        newBlocksLeft[13] = static_cast<uchar>(newStart & 0xFF);
        newBlocksLeft[14] = static_cast<uchar>((newStart >> 8) & 0xFF);
        newBlocksLeft[15] = 0x00;
        newBlocksLeft[16] = 0x00;
        newBlocksLeft[17] = static_cast<uchar>(newAlloc & 0xFF);
        newBlocksLeft[18] = static_cast<uchar>((newAlloc >> 8) & 0xFF);
        newBlocksLeft[19] = 0;
        newBlocksLeft[20] = 0;
        newBlocksLeft[21] = 0;
        newBlocksLeft[22] = 0;
        newBlocksLeft[23] = static_cast<uchar>(now.day());
        newBlocksLeft[24] = static_cast<uchar>(now.month());
        newBlocksLeft[25] = static_cast<uchar>(now.year() % 100);

        if (!writeRawDirEntry(destSide, newBlocksLeftIndex, newBlocksLeft)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed writing BLOCKS LEFT entry"));
            return false;
        }

        // Zorg dat alle mirrored / alternate BLOCKS LEFT entries identiek zijn
        if (!updateAllBlocksLeftEntries(destSide, newStart, newAlloc)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed updating mirrored BLOCKS LEFT entries"));
            return false;
        }

        parseDirectory(destSide);
        if (!saveEOSfile(destSide)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Copied in memory, but saving image failed"));
            return false;
        }

        return true;
    };

    // --------------------------------------------------
    // PC -> LEFT ADAM
    // --------------------------------------------------
    if (isPcModeRight()) {
        if (m_typeL == ImageNone) {
            if (m_statusBar) m_statusBar->showMessage(tr("No ADAM image loaded on left side"));
            return;
        }

        if (!m_tableR || !m_volEditR) {
            if (m_statusBar) m_statusBar->showMessage(tr("PC side is not available"));
            return;
        }

        const int selectedRow = m_tableR->currentRow();
        if (selectedRow < 0) {
            if (m_statusBar) m_statusBar->showMessage(tr("No PC file selected on right side"));
            return;
        }

        QString sourceDir = m_volEditR->text().trimmed();
        if (sourceDir.isEmpty()) {
            if (m_statusBar) m_statusBar->showMessage(tr("No PC directory selected"));
            return;
        }

        QTableWidgetItem *descItem = m_tableR->item(selectedRow, 1);
        if (!descItem) {
            if (m_statusBar) m_statusBar->showMessage(tr("Invalid PC file selection"));
            return;
        }

        const QString pcFileName = descItem->text().trimmed();
        if (pcFileName.isEmpty()) {
            if (m_statusBar) m_statusBar->showMessage(tr("Invalid PC file name"));
            return;
        }

        const QString sourcePath = QDir(sourceDir).filePath(pcFileName);
        QFile inFile(sourcePath);
        if (!inFile.open(QIODevice::ReadOnly)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Cannot open PC file: %1").arg(pcFileName));
            return;
        }

        const QByteArray srcData = inFile.readAll();
        inFile.close();

        if (srcData.isEmpty()) {
            if (m_statusBar) m_statusBar->showMessage(tr("PC file is empty"));
            return;
        }

       if (copyToAdamSide(srcData, pcFileName, 0x10, SideL) && m_statusBar) {
            m_statusBar->showMessage(
                tr("Copied to ADAM: %1 (%2 bytes)")
                    .arg(pcFileName)
                    .arg(srcData.size())
                );
        }
        return;
    }

    // --------------------------------------------------
    // RIGHT ADAM -> LEFT ADAM
    // --------------------------------------------------
    if (m_typeR == ImageNone) {
        if (m_statusBar) m_statusBar->showMessage(tr("No ADAM image loaded on right side"));
        return;
    }

    if (!m_tableR) {
        if (m_statusBar) m_statusBar->showMessage(tr("Right table not available"));
        return;
    }

    const int selectedRow = m_tableR->currentRow();
    if (selectedRow < 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("No file selected on right side"));
        return;
    }

    QVector<const EosEntry*> visibleEntries = buildVisibleEntries(SideR);
    if (selectedRow >= visibleEntries.size()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Selected row is out of range"));
        return;
    }

    const EosEntry &entry = *visibleEntries[selectedRow];
    if (entry.isDeleted()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Deleted entry cannot be copied"));
        return;
    }

    QByteArray srcData = readFileData(SideR, entry);
    if (srcData.isEmpty() && entry.used > 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed reading source ADAM file"));
        return;
    }

    QString sourceName = entry.name.trimmed();
    if (!entry.typeChar.trimmed().isEmpty())
        sourceName += "." + entry.typeChar.trimmed().toLower();

    if (copyToAdamSide(srcData, sourceName, entry.attribute, SideL) && m_statusBar) {
        m_statusBar->showMessage(
            tr("Copied ADAM->ADAM: %1 (%2 bytes)")
                .arg(sourceName)
                .arg(srcData.size())
            );
    }
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onCopyR()
{
    auto isPcModeRight = [&]() -> bool {
        return m_pcStatusWidgetR && m_pcStatusWidgetR->isVisible();
    };

    auto writeAdamBlock = [&](Side side, int blockNum, const QByteArray &blockData) -> bool {
        static const byte IT[8] = {0,5,2,7,4,1,6,3};
        FDIDisk *fdi = (side == SideL) ? &m_fdiL : &m_fdiR;

        QByteArray padded = blockData;
        if (padded.size() < 1024)
            padded.append(QByteArray(1024 - padded.size(), '\0'));
        if (padded.size() != 1024)
            return false;

        for (int i = 0, sec = blockNum << 1; i < 2; ++i, ++sec) {
            int p = (sec & ~7) | IT[sec & 7];
            byte *dst = LinearFDI(fdi, p);
            if (!dst) return false;
            memcpy(dst, padded.constData() + (i * 512), 512);
        }
        return true;
    };

    auto readRawDirEntry = [&](Side side, int entryIndex, uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(raw, dirBlock.constData() + offset, 26);
        return true;
    };

    auto writeRawDirEntry = [&](Side side, int entryIndex, const uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(dirBlock.data() + offset, raw, 26);

        return writeAdamBlock(side, blockNo, dirBlock);
    };

    auto getRawName = [&](const uchar raw[26]) -> QString {
        QString s;
        for (int i = 0; i < 12; ++i) {
            if (raw[i] == 0x03 || raw[i] == 0x00)
                break;
            if (raw[i] >= 32)
                s.append(QChar(raw[i]));
        }
        return s.trimmed();
    };

    auto makeAdamNameField = [&](const QString &srcName, uchar outName[12], QString &visibleName, QString &typeChar) {
        memset(outName, 0, 12);

        QString base = srcName.trimmed().toUpper();
        QString ext;

        int dotPos = base.lastIndexOf('.');
        if (dotPos > 0) {
            ext = base.mid(dotPos + 1).trimmed();
            base = base.left(dotPos).trimmed();
        }

        base.remove(' ');
        base.remove('.');
        base.remove('\t');

        typeChar.clear();
        if (!ext.isEmpty())
            typeChar = ext.left(1);

        QString rawName;
        if (!typeChar.isEmpty())
            rawName = base.left(11) + typeChar;
        else
            rawName = base.left(11);

        QByteArray latin = rawName.toLatin1();
        const int n = qMin(11, latin.size());
        for (int i = 0; i < n; ++i)
            outName[i] = static_cast<uchar>(latin[i]);

        outName[n] = 0x03;

        visibleName = rawName;
        if (!typeChar.isEmpty() && !visibleName.isEmpty())
            visibleName = visibleName.left(visibleName.size() - 1).trimmed();
    };

    auto buildVisibleEntries = [&](Side side) -> QVector<const EosEntry*> {
        QVector<const EosEntry*> visible;
        const QVector<EosEntry> &entries = (side == SideL) ? m_entriesL : m_entriesR;
        visible.reserve(entries.size());

        for (const auto &e : entries) {
            if (e.isVolume() || e.isSentinel())
                continue;
            visible.append(&e);
        }
        return visible;
    };

    auto readFileData = [&](Side side, const EosEntry &entry) -> QByteArray {
        QByteArray fileData;
        const int maxBlocks = getMaxBlocks(side);

        for (int i = 0; i < entry.used; ++i) {
            const int blockNum = entry.sblock + i;
            if (blockNum < 0 || blockNum >= maxBlocks)
                return QByteArray();

            QByteArray blockData = readAdamBlock(blockNum, side);
            if (blockData.size() != 1024)
                return QByteArray();

            const bool isLastBlock = (i == entry.used - 1);
            if (isLastBlock) {
                int lastSize = entry.lcount;
                if (lastSize <= 0 || lastSize > 1024)
                    lastSize = 1024;
                fileData.append(blockData.constData(), lastSize);
            } else {
                fileData.append(blockData);
            }
        }
        return fileData;
    };

        auto copyToAdamSide = [&](const QByteArray &srcData,
                              const QString &sourceName,
                              quint8 sourceAttr,
                              Side destSide) -> bool {
        if (((destSide == SideL) ? m_typeL : m_typeR) == ImageNone) {
            if (m_statusBar) m_statusBar->showMessage(tr("No destination ADAM image loaded"));
            return false;
        }

        const quint16 blocksNeeded = static_cast<quint16>((srcData.size() + 1023) / 1024);
        const quint16 usedBlocks   = blocksNeeded;
        const quint16 lastCount    = static_cast<quint16>((srcData.size() % 1024) == 0 ? 1024 : (srcData.size() % 1024));

        uchar newNameRaw[12];
        QString visibleName;
        QString newTypeChar;
        makeAdamNameField(sourceName, newNameRaw, visibleName, newTypeChar);

        const QVector<EosEntry> &destEntries = (destSide == SideL) ? m_entriesL : m_entriesR;
        for (const auto &e : destEntries) {
            if (e.isVolume() || e.isSentinel() || e.isDeleted())
                continue;

            if (e.name.trimmed().compare(visibleName, Qt::CaseInsensitive) == 0 &&
                e.typeChar.trimmed().compare(newTypeChar, Qt::CaseInsensitive) == 0) {
                if (m_statusBar) m_statusBar->showMessage(tr("A file with that name already exists on destination ADAM media"));
                return false;
            }
        }

        const int totalDirEntries = 39 * 3;
        int blocksLeftIndex = -1;
        uchar blocksLeftRaw[26] = {0};
        int firstEmptyIndex = -1;
        bool foundLastActive = false;
        EosEntry lastActive;

        for (int idx = 0; idx < totalDirEntries; ++idx) {
            uchar raw[26] = {0};
            if (!readRawDirEntry(destSide, idx, raw))
                break;

            if ((raw[0] == 0x00 || raw[0] == 0xFF)) {
                if (firstEmptyIndex < 0)
                    firstEmptyIndex = idx;
                continue;
            }

            const QString rawName = getRawName(raw);
            if (rawName.compare("BLOCKS LEFT", Qt::CaseInsensitive) == 0) {
                blocksLeftIndex = idx;
                memcpy(blocksLeftRaw, raw, 26);
            }

            bool isVolume = (raw[13] == 0x55 && raw[14] == 0xAA);
            EosEntry e = entryFromBytes(raw, isVolume, destSide);

            if (e.isVolume() || e.isSentinel() || e.isDeleted())
                continue;

            if (!foundLastActive || e.sblock > lastActive.sblock) {
                lastActive = e;
                foundLastActive = true;
            }
        }

        quint16 startBlock = 0;
        quint16 freeAlloc  = 0;
        int targetEntryIndex = -1;
        int newBlocksLeftIndex = -1;

        if (blocksLeftIndex >= 0) {
            if ((blocksLeftIndex + 1) >= totalDirEntries) {
                if (m_statusBar) m_statusBar->showMessage(tr("No directory room for new BLOCKS LEFT entry"));
                return false;
            }

            uchar nextRaw[26] = {0};
            if (!readRawDirEntry(destSide, blocksLeftIndex + 1, nextRaw)) {
                if (m_statusBar) m_statusBar->showMessage(tr("Cannot read next directory entry"));
                return false;
            }

            if (!(nextRaw[0] == 0x00 || nextRaw[0] == 0xFF)) {
                if (m_statusBar) m_statusBar->showMessage(tr("Next directory entry after BLOCKS LEFT is not empty"));
                return false;
            }

            startBlock = static_cast<quint16>(blocksLeftRaw[13] | (blocksLeftRaw[14] << 8));
            freeAlloc  = static_cast<quint16>(blocksLeftRaw[17] | (blocksLeftRaw[18] << 8));

            targetEntryIndex = blocksLeftIndex;
            newBlocksLeftIndex = blocksLeftIndex + 1;
        } else {
            if (firstEmptyIndex < 0 || (firstEmptyIndex + 1) >= totalDirEntries) {
                if (m_statusBar) m_statusBar->showMessage(tr("No room left in directory"));
                return false;
            }

            targetEntryIndex = firstEmptyIndex;
            newBlocksLeftIndex = firstEmptyIndex + 1;

            if (foundLastActive)
                startBlock = static_cast<quint16>(lastActive.sblock + lastActive.allocd);
            else
                startBlock = 4;

            freeAlloc = static_cast<quint16>(getMaxBlocks(destSide) - startBlock);
        }

        if (freeAlloc < blocksNeeded) {
            if (m_statusBar) m_statusBar->showMessage(tr("Not enough free space on destination ADAM media"));
            return false;
        }

        const int maxBlocks = getMaxBlocks(destSide);
        if ((startBlock + blocksNeeded) > maxBlocks) {
            if (m_statusBar) m_statusBar->showMessage(tr("Target blocks exceed media size"));
            return false;
        }

        for (quint16 i = 0; i < usedBlocks; ++i) {
            const int offset = i * 1024;
            QByteArray chunk = srcData.mid(offset, 1024);

            if (!writeAdamBlock(destSide, startBlock + i, chunk)) {
                if (m_statusBar) m_statusBar->showMessage(tr("Failed writing ADAM block %1").arg(startBlock + i));
                return false;
            }
        }

        uchar newEntry[26] = {0};
        memcpy(newEntry, newNameRaw, 12);

        quint8 newAttr = sourceAttr;

        // deleted-bit nooit meenemen
        newAttr &= ~0x04;

        // special/sentinel bit niet als gewone file meenemen
        if (sourceName.compare("BLOCKS LEFT", Qt::CaseInsensitive) != 0)
            newAttr &= ~0x01;

        // fallback = gewone user file
        if (newAttr == 0x00)
            newAttr = 0x10;

        newEntry[12] = newAttr;
        newEntry[13] = static_cast<uchar>(startBlock & 0xFF);
        newEntry[14] = static_cast<uchar>((startBlock >> 8) & 0xFF);
        newEntry[15] = 0x00;
        newEntry[16] = 0x00;
        newEntry[17] = static_cast<uchar>(blocksNeeded & 0xFF);
        newEntry[18] = static_cast<uchar>((blocksNeeded >> 8) & 0xFF);
        newEntry[19] = static_cast<uchar>(usedBlocks & 0xFF);
        newEntry[20] = static_cast<uchar>((usedBlocks >> 8) & 0xFF);
        newEntry[21] = static_cast<uchar>(lastCount & 0xFF);
        newEntry[22] = static_cast<uchar>((lastCount >> 8) & 0xFF);

        const QDate now = QDate::currentDate();
        newEntry[23] = static_cast<uchar>(now.day());
        newEntry[24] = static_cast<uchar>(now.month());
        newEntry[25] = static_cast<uchar>(now.year() % 100);

        if (!writeRawDirEntry(destSide, targetEntryIndex, newEntry)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed writing new file entry"));
            return false;
        }

        uchar newBlocksLeft[26] = {0};
        const QByteArray blName = QByteArray("BLOCKS LEFT");
        memcpy(newBlocksLeft, blName.constData(), qMin(11, blName.size()));
        newBlocksLeft[qMin(11, blName.size())] = 0x03;
        newBlocksLeft[12] = 0x01;

        const quint16 newStart = static_cast<quint16>(startBlock + blocksNeeded);
        const quint16 newAlloc = static_cast<quint16>(freeAlloc - blocksNeeded);

        newBlocksLeft[13] = static_cast<uchar>(newStart & 0xFF);
        newBlocksLeft[14] = static_cast<uchar>((newStart >> 8) & 0xFF);
        newBlocksLeft[15] = 0x00;
        newBlocksLeft[16] = 0x00;
        newBlocksLeft[17] = static_cast<uchar>(newAlloc & 0xFF);
        newBlocksLeft[18] = static_cast<uchar>((newAlloc >> 8) & 0xFF);
        newBlocksLeft[19] = 0;
        newBlocksLeft[20] = 0;
        newBlocksLeft[21] = 0;
        newBlocksLeft[22] = 0;
        newBlocksLeft[23] = static_cast<uchar>(now.day());
        newBlocksLeft[24] = static_cast<uchar>(now.month());
        newBlocksLeft[25] = static_cast<uchar>(now.year() % 100);

        if (!writeRawDirEntry(destSide, newBlocksLeftIndex, newBlocksLeft)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed writing BLOCKS LEFT entry"));
            return false;
        }

        // Zorg dat alle mirrored / alternate BLOCKS LEFT entries identiek zijn
        if (!updateAllBlocksLeftEntries(destSide, newStart, newAlloc)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed updating mirrored BLOCKS LEFT entries"));
            return false;
        }

        parseDirectory(destSide);
        if (!saveEOSfile(destSide)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Copied in memory, but saving image failed"));
            return false;
        }

        return true;
    };

    if (m_typeL == ImageNone) {
        if (m_statusBar) m_statusBar->showMessage(tr("No ADAM image loaded on left side"));
        return;
    }

    if (!m_tableL) {
        if (m_statusBar) m_statusBar->showMessage(tr("Left table not available"));
        return;
    }

    const int selectedRow = m_tableL->currentRow();
    if (selectedRow < 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("No file selected on left side"));
        return;
    }

    QVector<const EosEntry*> visibleEntries = buildVisibleEntries(SideL);
    if (selectedRow >= visibleEntries.size()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Selected row is out of range"));
        return;
    }

    const EosEntry &entry = *visibleEntries[selectedRow];

    if (entry.isDeleted()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Deleted entry cannot be copied"));
        return;
    }

    QByteArray srcData = readFileData(SideL, entry);
    if (srcData.isEmpty() && entry.used > 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed reading source ADAM file"));
        return;
    }

    QString sourceName = entry.name.trimmed();
    if (!entry.typeChar.trimmed().isEmpty())
        sourceName += "." + entry.typeChar.trimmed().toLower();

    // LEFT ADAM -> RIGHT PC
    if (isPcModeRight()) {
        QString targetDir = m_volEditR ? m_volEditR->text().trimmed() : QString();
        if (targetDir.isEmpty())
            targetDir = m_diskRootPath;

        if (targetDir.isEmpty()) {
            if (m_statusBar) m_statusBar->showMessage(tr("No PC target directory selected"));
            return;
        }

        QDir dir(targetDir);
        if (!dir.exists()) {
            if (m_statusBar) m_statusBar->showMessage(tr("Target directory does not exist"));
            return;
        }

        QString outName = sourceName;
        outName.replace('\\', '_');
        outName.replace('/',  '_');
        outName.replace(':',  '_');
        outName.replace('*',  '_');
        outName.replace('?',  '_');
        outName.replace('"',  '_');
        outName.replace('<',  '_');
        outName.replace('>',  '_');
        outName.replace('|',  '_');

        QString outPath = dir.filePath(outName);

        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly)) {
            if (m_statusBar)
                m_statusBar->showMessage(tr("Cannot create file: %1").arg(QFileInfo(outPath).fileName()));
            return;
        }

        const qint64 written = outFile.write(srcData);
        outFile.close();

        if (written != srcData.size()) {
            if (m_statusBar)
                m_statusBar->showMessage(tr("Write error for file: %1").arg(QFileInfo(outPath).fileName()));
            return;
        }

        if (m_volEditR && QDir::cleanPath(m_volEditR->text().trimmed()) == QDir::cleanPath(targetDir)) {
            QDir qdir(targetDir);
            QFileInfoList list = qdir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

            if (m_pcFileCountEditR)
                m_pcFileCountEditR->setText(QString("%1").arg(list.size(), 3, 10, QChar('0')));

            if (m_tableR) {
                m_tableR->setRowCount(0);
                for (const QFileInfo &fi : list) {
                    const int r = m_tableR->rowCount();
                    m_tableR->insertRow(r);

                    auto *iLen  = new QTableWidgetItem(QString::number(fi.size()));
                    auto *iDesc = new QTableWidgetItem(fi.fileName());
                    auto *iTime = new QTableWidgetItem(fi.lastModified().toString("HH:mm:ss"));
                    auto *iDate = new QTableWidgetItem(fi.lastModified().toString("dd/MM/yyyy"));

                    iLen->setTextAlignment(Qt::AlignCenter);
                    iTime->setTextAlignment(Qt::AlignCenter);
                    iDate->setTextAlignment(Qt::AlignCenter);

                    m_tableR->setItem(r, 0, iLen);
                    m_tableR->setItem(r, 1, iDesc);
                    m_tableR->setItem(r, 2, iTime);
                    m_tableR->setItem(r, 3, iDate);
                }
            }
        }

        if (m_statusBar) {
            m_statusBar->showMessage(
                tr("Copied to PC: %1 (%2 bytes)")
                    .arg(QFileInfo(outPath).fileName())
                    .arg(srcData.size())
                );
        }
        return;
    }

    // LEFT ADAM -> RIGHT ADAM
    if (m_typeR == ImageNone) {
        if (m_statusBar) m_statusBar->showMessage(tr("No ADAM image loaded on right side"));
        return;
    }

   if (copyToAdamSide(srcData, sourceName, entry.attribute, SideR) && m_statusBar) {
        m_statusBar->showMessage(
            tr("Copied ADAM->ADAM: %1 (%2 bytes)")
                .arg(sourceName)
                .arg(srcData.size())
            );
    }
}
/* ---------------------------------------------------------------------------------------------------------------------*/
bool AimDialog::saveEOSfile(Side side)
{
    FDIDisk *fdi = (side == SideL) ? &m_fdiL : &m_fdiR;
    const QString outPath = (side == SideL) ? m_pathL : m_pathR;
    const ImageType type  = (side == SideL) ? m_typeL : m_typeR;

    if (type == ImageNone) {
        if (m_statusBar) m_statusBar->showMessage(tr("No image loaded to save"));
        return false;
    }

    if (outPath.isEmpty()) {
        if (m_statusBar) m_statusBar->showMessage(tr("No target image path available"));
        return false;
    }

    const int totalBlocks  = getMaxBlocks(side);
    const int totalSectors = totalBlocks * 2;   // 2 x 512 per 1024-byte block

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (m_statusBar) {
            m_statusBar->showMessage(tr("Cannot save image: %1").arg(QFileInfo(outPath).fileName()));
        }
        return false;
    }

    for (int sector = 0; sector < totalSectors; ++sector) {
        byte *src = LinearFDI(fdi, sector);
        if (!src) {
            outFile.close();
            if (m_statusBar) {
                m_statusBar->showMessage(tr("Save failed: invalid sector %1").arg(sector));
            }
            return false;
        }

        const qint64 written = outFile.write(reinterpret_cast<const char*>(src), 512);
        if (written != 512) {
            outFile.close();
            if (m_statusBar) {
                m_statusBar->showMessage(tr("Save failed while writing sector %1").arg(sector));
            }
            return false;
        }
    }

    outFile.flush();
    outFile.close();

    if (m_statusBar) {
        m_statusBar->showMessage(tr("Image saved: %1").arg(QFileInfo(outPath).fileName()));
    }

    return true;
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onDelete()
{
    auto isPcModeRight = [&]() -> bool {
        return m_pcStatusWidgetR && m_pcStatusWidgetR->isVisible();
    };

    auto getActiveSide = [&]() -> Side {
        if (m_tableR && m_tableR->property("active").toBool())
            return SideR;
        return SideL;
    };

    auto buildVisibleEntries = [&](Side side) -> QVector<const EosEntry*> {
        QVector<const EosEntry*> visible;
        const QVector<EosEntry> &entries = (side == SideL) ? m_entriesL : m_entriesR;
        visible.reserve(entries.size());

        for (const auto &e : entries) {
            if (e.isVolume() || e.isSentinel())
                continue;
            visible.append(&e);
        }
        return visible;
    };

    auto writeAdamBlock = [&](Side side, int blockNum, const QByteArray &blockData) -> bool {
        static const byte IT[8] = {0,5,2,7,4,1,6,3};
        FDIDisk *fdi = (side == SideL) ? &m_fdiL : &m_fdiR;

        QByteArray padded = blockData;
        if (padded.size() < 1024)
            padded.append(QByteArray(1024 - padded.size(), '\0'));
        if (padded.size() != 1024)
            return false;

        for (int i = 0, sec = blockNum << 1; i < 2; ++i, ++sec) {
            int p = (sec & ~7) | IT[sec & 7];
            byte *dst = LinearFDI(fdi, p);
            if (!dst) return false;
            memcpy(dst, padded.constData() + (i * 512), 512);
        }
        return true;
    };

    auto readRawDirEntry = [&](Side side, int entryIndex, uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(raw, dirBlock.constData() + offset, 26);
        return true;
    };

    auto writeRawDirEntry = [&](Side side, int entryIndex, const uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(dirBlock.data() + offset, raw, 26);

        return writeAdamBlock(side, blockNo, dirBlock);
    };

    auto saveSide = [&](Side side) -> bool {
        return saveEOSfile(side);
    };

    auto refreshSide = [&](Side side) {
        parseDirectory(side);
    };

    const Side activeSide = getActiveSide();

    // --------------------------------------------------
    // DELETE ON PC SIDE
    // --------------------------------------------------
    if (activeSide == SideR && isPcModeRight()) {
        if (!m_tableR || !m_volEditR) {
            if (m_statusBar) m_statusBar->showMessage(tr("PC side is not available"));
            return;
        }

        const int row = m_tableR->currentRow();
        if (row < 0) {
            if (m_statusBar) m_statusBar->showMessage(tr("No PC file selected"));
            return;
        }

        QTableWidgetItem *descItem = m_tableR->item(row, 1);
        if (!descItem) {
            if (m_statusBar) m_statusBar->showMessage(tr("Invalid PC file selection"));
            return;
        }

        const QString fileName = descItem->text().trimmed();
        const QString dirPath = m_volEditR->text().trimmed();
        const QString filePath = QDir(dirPath).filePath(fileName);

        if (fileName.isEmpty() || dirPath.isEmpty()) {
            if (m_statusBar) m_statusBar->showMessage(tr("Invalid PC file path"));
            return;
        }

        if (QMessageBox::question(
                this,
                tr("Delete file"),
                tr("Delete PC file '%1'?").arg(fileName),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
            return;
        }

        if (!QFile::remove(filePath)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed to delete PC file: %1").arg(fileName));
            return;
        }

        // refresh PC listing
        QDir qdir(dirPath);
        QFileInfoList list = qdir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

        if (m_pcFileCountEditR)
            m_pcFileCountEditR->setText(QString("%1").arg(list.size(), 3, 10, QChar('0')));

        m_tableR->setRowCount(0);
        for (const QFileInfo &fi : list) {
            int r = m_tableR->rowCount();
            m_tableR->insertRow(r);

            auto *iLen  = new QTableWidgetItem(QString::number(fi.size()));
            auto *iDesc = new QTableWidgetItem(fi.fileName());
            auto *iTime = new QTableWidgetItem(fi.lastModified().toString("HH:mm:ss"));
            auto *iDate = new QTableWidgetItem(fi.lastModified().toString("dd/MM/yyyy"));

            iLen->setTextAlignment(Qt::AlignCenter);
            iTime->setTextAlignment(Qt::AlignCenter);
            iDate->setTextAlignment(Qt::AlignCenter);

            m_tableR->setItem(r, 0, iLen);
            m_tableR->setItem(r, 1, iDesc);
            m_tableR->setItem(r, 2, iTime);
            m_tableR->setItem(r, 3, iDate);
        }

        if (m_statusBar) m_statusBar->showMessage(tr("Deleted PC file: %1").arg(fileName));
        return;
    }

    // --------------------------------------------------
    // DELETE ON ADAM SIDE (LEFT OR RIGHT)
    // --------------------------------------------------
    QTableWidget *table = (activeSide == SideL) ? m_tableL : m_tableR;
    if (!table) {
        if (m_statusBar) m_statusBar->showMessage(tr("Active table not available"));
        return;
    }

    const int selectedRow = table->currentRow();
    if (selectedRow < 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("No ADAM file selected"));
        return;
    }

    QVector<const EosEntry*> visibleEntries = buildVisibleEntries(activeSide);
    if (selectedRow >= visibleEntries.size()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Selected row is out of range"));
        return;
    }

    const EosEntry &entry = *visibleEntries[selectedRow];

    if (entry.isDeleted()) {
        if (m_statusBar) m_statusBar->showMessage(tr("File is already marked as deleted"));
        return;
    }

    const QString displayName =
        entry.typeChar.trimmed().isEmpty()
            ? entry.name.trimmed()
            : QString("%1.%2").arg(entry.name.trimmed(), entry.typeChar.trimmed().toLower());

    if (QMessageBox::question(
            this,
            tr("Delete file"),
            tr("Delete ADAM file '%1'?").arg(displayName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    // Zoek overeenkomstige directory entry opnieuw op ruwe index
    const QVector<EosEntry> &entries = (activeSide == SideL) ? m_entriesL : m_entriesR;
    int visibleIndex = -1;
    int targetRawIndex = -1;

    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        if (e.isVolume() || e.isSentinel())
            continue;

        ++visibleIndex;
        if (visibleIndex == selectedRow) {
            targetRawIndex = i;
            break;
        }
    }

    if (targetRawIndex < 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("Could not resolve directory entry"));
        return;
    }

    uchar raw[26] = {0};
    if (!readRawDirEntry(activeSide, targetRawIndex, raw)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed reading ADAM directory entry"));
        return;
    }

    // EOS deleted bit zetten
    raw[12] = static_cast<uchar>(raw[12] | 0x04);

    if (!writeRawDirEntry(activeSide, targetRawIndex, raw)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed writing ADAM directory entry"));
        return;
    }

    refreshSide(activeSide);

    if (!saveSide(activeSide)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Deleted in memory, but saving image failed"));
        return;
    }

    if (m_statusBar) m_statusBar->showMessage(tr("Deleted ADAM file: %1").arg(displayName));
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onAttrib()
{
    auto getActiveSide = [&]() -> Side {
        if (m_tableR && m_tableR->property("active").toBool())
            return SideR;
        return SideL;
    };

    auto isPcModeRight = [&]() -> bool {
        return m_pcStatusWidgetR && m_pcStatusWidgetR->isVisible();
    };

    auto buildVisibleEntries = [&](Side side) -> QVector<const EosEntry*> {
        QVector<const EosEntry*> visible;
        const QVector<EosEntry> &entries = (side == SideL) ? m_entriesL : m_entriesR;
        visible.reserve(entries.size());

        for (const auto &e : entries) {
            if (e.isVolume() || e.isSentinel())
                continue;
            visible.append(&e);
        }
        return visible;
    };

    auto writeAdamBlock = [&](Side side, int blockNum, const QByteArray &blockData) -> bool {
        static const byte IT[8] = {0,5,2,7,4,1,6,3};
        FDIDisk *fdi = (side == SideL) ? &m_fdiL : &m_fdiR;

        QByteArray padded = blockData;
        if (padded.size() < 1024)
            padded.append(QByteArray(1024 - padded.size(), '\0'));
        if (padded.size() != 1024)
            return false;

        for (int i = 0, sec = blockNum << 1; i < 2; ++i, ++sec) {
            int p = (sec & ~7) | IT[sec & 7];
            byte *dst = LinearFDI(fdi, p);
            if (!dst) return false;
            memcpy(dst, padded.constData() + (i * 512), 512);
        }
        return true;
    };

    auto readRawDirEntry = [&](Side side, int entryIndex, uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(raw, dirBlock.constData() + offset, 26);
        return true;
    };

    auto writeRawDirEntry = [&](Side side, int entryIndex, const uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(dirBlock.data() + offset, raw, 26);

        return writeAdamBlock(side, blockNo, dirBlock);
    };

    const Side activeSide = getActiveSide();

    if (activeSide == SideR && isPcModeRight()) {
        if (m_statusBar) m_statusBar->showMessage(tr("ATTRIB is only for ADAM media"));
        return;
    }

    QTableWidget *table = (activeSide == SideL) ? m_tableL : m_tableR;
    if (!table) {
        if (m_statusBar) m_statusBar->showMessage(tr("No active ADAM table"));
        return;
    }

    const int selectedRow = table->currentRow();
    if (selectedRow < 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("No ADAM file selected"));
        return;
    }

    QVector<const EosEntry*> visibleEntries = buildVisibleEntries(activeSide);
    if (selectedRow >= visibleEntries.size()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Selected row out of range"));
        return;
    }

    const EosEntry &entry = *visibleEntries[selectedRow];

    const QVector<EosEntry> &entries = (activeSide == SideL) ? m_entriesL : m_entriesR;
    int visibleIndex = -1;
    int targetRawIndex = -1;

    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        if (e.isVolume() || e.isSentinel())
            continue;

        ++visibleIndex;
        if (visibleIndex == selectedRow) {
            targetRawIndex = i;
            break;
        }
    }

    if (targetRawIndex < 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("Could not resolve raw directory entry"));
        return;
    }

    uchar raw[26] = {0};
    if (!readRawDirEntry(activeSide, targetRawIndex, raw)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed reading directory entry"));
        return;
    }

    constexpr quint8 ATTR_NOT_A_FILE        = 0x01;
    constexpr quint8 ATTR_WRITE_PROTECTED   = 0x40;
    constexpr quint8 ATTR_FILE_DELETED      = 0x04;
    constexpr quint8 ATTR_SYSTEM_FILE       = 0x08;
    constexpr quint8 ATTR_USER_FILE         = 0x10;
    constexpr quint8 ATTR_READ_PROTECTED    = 0x20;
    constexpr quint8 ATTR_EXECUTE_PROTECTED = 0x02;
    constexpr quint8 ATTR_PERMANENT_PROTECT = 0x80;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Attributes for: %1").arg(entry.name));
    dlg.setModal(true);
    dlg.setFixedWidth(520);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(0);

    QLabel *title = new QLabel(tr("Attribute for: %1").arg(entry.name), &dlg);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    mainLayout->addWidget(title);

    mainLayout->addSpacing(10);

    QFrame *line0 = new QFrame(&dlg);
    line0->setFrameShape(QFrame::HLine);
    line0->setFrameShadow(QFrame::Sunken);
    line0->setStyleSheet("color: #555;");
    mainLayout->addWidget(line0);

    mainLayout->addSpacing(10);

    QGridLayout *grid = new QGridLayout();

    QCheckBox *cbNotFile   = new QCheckBox("Not a File", &dlg);
    QCheckBox *cbDeleted   = new QCheckBox("File Deleted", &dlg);
    QCheckBox *cbUser      = new QCheckBox("User File", &dlg);
    QCheckBox *cbWriteProt = new QCheckBox("Write Protected", &dlg);

    QCheckBox *cbExecProt  = new QCheckBox("Execute Protect", &dlg);
    QCheckBox *cbSystem    = new QCheckBox("System File", &dlg);
    QCheckBox *cbReadProt  = new QCheckBox("Read Protected", &dlg);
    QCheckBox *cbPermProt  = new QCheckBox("Permanently Protect", &dlg);

    quint8 attr = raw[12];

    cbNotFile->setChecked(attr & ATTR_NOT_A_FILE);
    cbDeleted->setChecked(attr & ATTR_FILE_DELETED);
    cbUser->setChecked(attr & ATTR_USER_FILE);
    cbWriteProt->setChecked(attr & ATTR_WRITE_PROTECTED);

    cbExecProt->setChecked(attr & ATTR_EXECUTE_PROTECTED);
    cbSystem->setChecked(attr & ATTR_SYSTEM_FILE);
    cbReadProt->setChecked(attr & ATTR_READ_PROTECTED);
    cbPermProt->setChecked(attr & ATTR_PERMANENT_PROTECT);

    grid->addWidget(cbNotFile,   0, 0);
    grid->addWidget(cbExecProt,  0, 1);
    grid->addWidget(cbDeleted,   1, 0);
    grid->addWidget(cbSystem,    1, 1);
    grid->addWidget(cbUser,      2, 0);
    grid->addWidget(cbReadProt,  2, 1);
    grid->addWidget(cbWriteProt, 3, 0);
    grid->addWidget(cbPermProt,  3, 1);

    mainLayout->addLayout(grid);

    mainLayout->addSpacing(10);

    QFrame *line1 = new QFrame(&dlg);
    line1->setFrameShape(QFrame::HLine);
    line1->setFrameShadow(QFrame::Sunken);
    line1->setStyleSheet("color: #555;");
    mainLayout->addWidget(line1);

    mainLayout->addSpacing(10);

    QGridLayout *info = new QGridLayout();
    info->addWidget(new QLabel("Start"), 0, 0);
    info->addWidget(new QLabel(QString::number(entry.sblock)), 0, 1);
    info->addWidget(new QLabel("Length"), 0, 2);
    info->addWidget(new QLabel(QString::number(entry.allocd)), 0, 3);

    info->addWidget(new QLabel("Used"), 1, 0);
    info->addWidget(new QLabel(QString::number(entry.used)), 1, 1);
    info->addWidget(new QLabel("In Last"), 1, 2);
    info->addWidget(new QLabel(QString::number(entry.lcount)), 1, 3);

    mainLayout->addLayout(info);

    mainLayout->addSpacing(10);

    QFrame *line2 = new QFrame(&dlg);
    line2->setFrameShape(QFrame::HLine);
    line2->setFrameShadow(QFrame::Sunken);
    line2->setStyleSheet("color: #555;");
    mainLayout->addWidget(line2);

    mainLayout->addSpacing(10);

    QHBoxLayout *dateLayout = new QHBoxLayout();
    QSpinBox *spDay = new QSpinBox(&dlg);
    QSpinBox *spMonth = new QSpinBox(&dlg);
    QSpinBox *spYear = new QSpinBox(&dlg);

    spDay->setRange(1, 31);
    spMonth->setRange(1, 12);
    spYear->setRange(0, 99);

    spDay->setValue(entry.day == 0 ? 1 : entry.day);
    spMonth->setValue(entry.month == 0 ? 1 : entry.month);
    spYear->setValue(entry.year);

    QPushButton *btnToday = new QPushButton("SET TO TODAY", &dlg);
    QObject::connect(btnToday, &QPushButton::clicked, [&]() {
        const QDate now = QDate::currentDate();
        spDay->setValue(now.day());
        spMonth->setValue(now.month());
        spYear->setValue(now.year() % 100);
    });

    dateLayout->addWidget(new QLabel("Day"));
    dateLayout->addWidget(spDay);
    dateLayout->addWidget(new QLabel("Month"));
    dateLayout->addWidget(spMonth);
    dateLayout->addWidget(new QLabel("Year"));
    dateLayout->addWidget(spYear);
    dateLayout->addSpacing(10);
    dateLayout->addWidget(btnToday);

    mainLayout->addLayout(dateLayout);

    mainLayout->addSpacing(10);

    QFrame *line3 = new QFrame(&dlg);
    line3->setFrameShape(QFrame::HLine);
    line3->setFrameShadow(QFrame::Sunken);
    line3->setStyleSheet("color: #555;");
    mainLayout->addWidget(line3);

    mainLayout->addSpacing(10);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnOk = new QPushButton("OK", &dlg);
    QPushButton *btnCancel = new QPushButton("CANCEL", &dlg);
    btnLayout->addStretch();
    btnLayout->addWidget(btnCancel);
    btnLayout->addSpacing(10);
    btnLayout->addWidget(btnOk);
    mainLayout->addLayout(btnLayout);

    QObject::connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted)
        return;

    quint8 newAttr = 0x00;
    if (cbNotFile->isChecked())   newAttr |= ATTR_NOT_A_FILE;
    if (cbDeleted->isChecked())   newAttr |= ATTR_FILE_DELETED;
    if (cbUser->isChecked())      newAttr |= ATTR_USER_FILE;
    if (cbWriteProt->isChecked()) newAttr |= ATTR_WRITE_PROTECTED;
    if (cbExecProt->isChecked())  newAttr |= ATTR_EXECUTE_PROTECTED;
    if (cbSystem->isChecked())    newAttr |= ATTR_SYSTEM_FILE;
    if (cbReadProt->isChecked())  newAttr |= ATTR_READ_PROTECTED;
    if (cbPermProt->isChecked())  newAttr |= ATTR_PERMANENT_PROTECT;

    raw[12] = newAttr;
    raw[23] = static_cast<uchar>(spDay->value());
    raw[24] = static_cast<uchar>(spMonth->value());
    raw[25] = static_cast<uchar>(spYear->value());

    if (!writeRawDirEntry(activeSide, targetRawIndex, raw)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed writing attributes"));
        return;
    }

    parseDirectory(activeSide);

    if (!saveEOSfile(activeSide)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Attributes changed in memory, but saving failed"));
        return;
    }

    if (m_statusBar) m_statusBar->showMessage(tr("Attributes updated for: %1").arg(entry.name));
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onRename()
{
    auto isPcModeRight = [&]() -> bool {
        return m_pcStatusWidgetR && m_pcStatusWidgetR->isVisible();
    };

    auto getActiveSide = [&]() -> Side {
        if (m_tableR && m_tableR->property("active").toBool())
            return SideR;
        return SideL;
    };

    auto buildVisibleEntries = [&](Side side) -> QVector<const EosEntry*> {
        QVector<const EosEntry*> visible;
        const QVector<EosEntry> &entries = (side == SideL) ? m_entriesL : m_entriesR;
        visible.reserve(entries.size());

        for (const auto &e : entries) {
            if (e.isVolume() || e.isSentinel())
                continue;
            visible.append(&e);
        }
        return visible;
    };

    auto readRawDirEntry = [&](Side side, int entryIndex, uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(raw, dirBlock.constData() + offset, 26);
        return true;
    };

    auto writeAdamBlock = [&](Side side, int blockNum, const QByteArray &blockData) -> bool {
        static const byte IT[8] = {0,5,2,7,4,1,6,3};
        FDIDisk *fdi = (side == SideL) ? &m_fdiL : &m_fdiR;

        QByteArray padded = blockData;
        if (padded.size() < 1024)
            padded.append(QByteArray(1024 - padded.size(), '\0'));
        if (padded.size() != 1024)
            return false;

        for (int i = 0, sec = blockNum << 1; i < 2; ++i, ++sec) {
            int p = (sec & ~7) | IT[sec & 7];
            byte *dst = LinearFDI(fdi, p);
            if (!dst) return false;
            memcpy(dst, padded.constData() + (i * 512), 512);
        }
        return true;
    };

    auto writeRawDirEntry = [&](Side side, int entryIndex, const uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(dirBlock.data() + offset, raw, 26);

        return writeAdamBlock(side, blockNo, dirBlock);
    };

    auto sanitizePcName = [&](QString name) -> QString {
        name = name.trimmed();
        name.replace('\\', '_');
        name.replace('/',  '_');
        name.replace(':',  '_');
        name.replace('*',  '_');
        name.replace('?',  '_');
        name.replace('"',  '_');
        name.replace('<',  '_');
        name.replace('>',  '_');
        name.replace('|',  '_');
        return name;
    };

    auto buildAdamRawName = [&](const QString &typedName, uchar outName[12], QString &visibleName, QString &typeChar) -> bool {
        memset(outName, 0, 12);

        QString input = typedName.trimmed().toUpper();
        if (input.isEmpty())
            return false;

        QString base = input;
        QString ext;

        const int dotPos = input.lastIndexOf('.');
        if (dotPos > 0) {
            base = input.left(dotPos).trimmed();
            ext  = input.mid(dotPos + 1).trimmed();
        }

        base.remove(' ');
        base.remove('.');
        base.remove('\t');

        typeChar.clear();
        if (!ext.isEmpty())
            typeChar = ext.left(1);

        QString rawName;
        if (!typeChar.isEmpty())
            rawName = base.left(11) + typeChar;
        else
            rawName = base.left(11);

        if (rawName.isEmpty())
            return false;

        QByteArray latin = rawName.toLatin1();
        const int n = qMin(11, latin.size());
        for (int i = 0; i < n; ++i)
            outName[i] = static_cast<uchar>(latin[i]);

        outName[n] = 0x03;

        visibleName = rawName;
        if (!typeChar.isEmpty() && !visibleName.isEmpty())
            visibleName = visibleName.left(visibleName.size() - 1).trimmed();

        return true;
    };

    const Side activeSide = getActiveSide();

    // ----------------------------------------------------------------
    // RENAME PC FILE
    // ----------------------------------------------------------------
    if (activeSide == SideR && isPcModeRight()) {
        if (!m_tableR || !m_volEditR) {
            if (m_statusBar) m_statusBar->showMessage(tr("PC side is not available"));
            return;
        }

        const int row = m_tableR->currentRow();
        if (row < 0) {
            if (m_statusBar) m_statusBar->showMessage(tr("No PC file selected"));
            return;
        }

        QTableWidgetItem *descItem = m_tableR->item(row, 1);
        if (!descItem) {
            if (m_statusBar) m_statusBar->showMessage(tr("Invalid PC file selection"));
            return;
        }

        const QString oldName = descItem->text().trimmed();
        const QString dirPath = m_volEditR->text().trimmed();

        if (oldName.isEmpty() || dirPath.isEmpty()) {
            if (m_statusBar) m_statusBar->showMessage(tr("Invalid PC file path"));
            return;
        }

        bool ok = false;
        QString newName = QInputDialog::getText(
            this,
            tr("Rename PC file"),
            tr("New file name:"),
            QLineEdit::Normal,
            oldName,
            &ok
            );

        if (!ok)
            return;

        newName = sanitizePcName(newName);
        if (newName.isEmpty() || newName == oldName)
            return;

        const QString oldPath = QDir(dirPath).filePath(oldName);
        const QString newPath = QDir(dirPath).filePath(newName);

        if (QFileInfo::exists(newPath)) {
            if (m_statusBar) m_statusBar->showMessage(tr("A PC file with that name already exists"));
            return;
        }

        if (!QFile::rename(oldPath, newPath)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed to rename PC file"));
            return;
        }

        // refresh PC list
        QDir qdir(dirPath);
        QFileInfoList list = qdir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

        if (m_pcFileCountEditR)
            m_pcFileCountEditR->setText(QString("%1").arg(list.size(), 3, 10, QChar('0')));

        m_tableR->setRowCount(0);
        for (const QFileInfo &fi : list) {
            int r = m_tableR->rowCount();
            m_tableR->insertRow(r);

            auto *iLen  = new QTableWidgetItem(QString::number(fi.size()));
            auto *iDesc = new QTableWidgetItem(fi.fileName());
            auto *iTime = new QTableWidgetItem(fi.lastModified().toString("HH:mm:ss"));
            auto *iDate = new QTableWidgetItem(fi.lastModified().toString("dd/MM/yyyy"));

            iLen->setTextAlignment(Qt::AlignCenter);
            iTime->setTextAlignment(Qt::AlignCenter);
            iDate->setTextAlignment(Qt::AlignCenter);

            m_tableR->setItem(r, 0, iLen);
            m_tableR->setItem(r, 1, iDesc);
            m_tableR->setItem(r, 2, iTime);
            m_tableR->setItem(r, 3, iDate);
        }

        if (m_statusBar) m_statusBar->showMessage(tr("Renamed PC file to: %1").arg(newName));
        return;
    }

    // ----------------------------------------------------------------
    // RENAME ADAM FILE
    // ----------------------------------------------------------------
    QTableWidget *table = (activeSide == SideL) ? m_tableL : m_tableR;
    if (!table) {
        if (m_statusBar) m_statusBar->showMessage(tr("No active ADAM table"));
        return;
    }

    const int selectedRow = table->currentRow();
    if (selectedRow < 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("No ADAM file selected"));
        return;
    }

    QVector<const EosEntry*> visibleEntries = buildVisibleEntries(activeSide);
    if (selectedRow >= visibleEntries.size()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Selected row out of range"));
        return;
    }

    const EosEntry &entry = *visibleEntries[selectedRow];

    if (entry.isDeleted()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Deleted ADAM entry cannot be renamed"));
        return;
    }

    QString oldDisplayName = entry.name.trimmed();
    if (!entry.typeChar.trimmed().isEmpty())
        oldDisplayName += "." + entry.typeChar.trimmed().toLower();

    bool ok = false;
    QString typedName = QInputDialog::getText(
        this,
        tr("Rename ADAM file"),
        tr("New ADAM file name:"),
        QLineEdit::Normal,
        oldDisplayName,
        &ok
        );

    if (!ok)
        return;

    uchar newNameRaw[12];
    QString newVisibleName;
    QString newTypeChar;

    if (!buildAdamRawName(typedName, newNameRaw, newVisibleName, newTypeChar)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Invalid ADAM file name"));
        return;
    }

    if (newVisibleName.compare(entry.name.trimmed(), Qt::CaseInsensitive) == 0 &&
        newTypeChar.compare(entry.typeChar.trimmed(), Qt::CaseInsensitive) == 0) {
        return;
    }

    // dubbel naamcheck
    const QVector<EosEntry> &entries = (activeSide == SideL) ? m_entriesL : m_entriesR;
    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        if (e.isVolume() || e.isSentinel() || e.isDeleted())
            continue;

        if (&e == visibleEntries[selectedRow])
            continue;

        if (e.name.trimmed().compare(newVisibleName, Qt::CaseInsensitive) == 0 &&
            e.typeChar.trimmed().compare(newTypeChar, Qt::CaseInsensitive) == 0) {
            if (m_statusBar) m_statusBar->showMessage(tr("An ADAM file with that name already exists"));
            return;
        }
    }

    int visibleIndex = -1;
    int targetRawIndex = -1;

    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        if (e.isVolume() || e.isSentinel())
            continue;

        ++visibleIndex;
        if (visibleIndex == selectedRow) {
            targetRawIndex = i;
            break;
        }
    }

    if (targetRawIndex < 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("Could not resolve ADAM directory entry"));
        return;
    }

    uchar raw[26] = {0};
    if (!readRawDirEntry(activeSide, targetRawIndex, raw)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed reading ADAM directory entry"));
        return;
    }

    memcpy(raw, newNameRaw, 12);

    if (!writeRawDirEntry(activeSide, targetRawIndex, raw)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed writing ADAM directory entry"));
        return;
    }

    parseDirectory(activeSide);

    if (!saveEOSfile(activeSide)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Renamed in memory, but saving image failed"));
        return;
    }

    QString newDisplayName = newVisibleName;
    if (!newTypeChar.trimmed().isEmpty())
        newDisplayName += "." + newTypeChar.trimmed().toLower();

    if (m_statusBar) m_statusBar->showMessage(tr("Renamed ADAM file to: %1").arg(newDisplayName));
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onCrunch()
{
    auto isPcModeRight = [&]() -> bool {
        return m_pcStatusWidgetR && m_pcStatusWidgetR->isVisible();
    };

    auto getActiveSide = [&]() -> Side {
        if (m_tableR && m_tableR->property("active").toBool())
            return SideR;
        return SideL;
    };

    auto readRawDirEntry = [&](Side side, int entryIndex, uchar raw[26]) -> bool {
        const int entriesPerBlock = 39;
        const int blockNo = 1 + (entryIndex / entriesPerBlock);
        const int indexInBlock = entryIndex % entriesPerBlock;

        QByteArray dirBlock = readAdamBlock(blockNo, side);
        if (dirBlock.size() < 1024)
            return false;

        const int offset = indexInBlock * 26;
        memcpy(raw, dirBlock.constData() + offset, 26);
        return true;
    };

    auto writeAdamBlock = [&](Side side, int blockNum, const QByteArray &blockData) -> bool {
        static const byte IT[8] = {0,5,2,7,4,1,6,3};
        FDIDisk *fdi = (side == SideL) ? &m_fdiL : &m_fdiR;

        QByteArray padded = blockData;
        if (padded.size() < 1024)
            padded.append(QByteArray(1024 - padded.size(), '\0'));
        if (padded.size() != 1024)
            return false;

        for (int i = 0, sec = blockNum << 1; i < 2; ++i, ++sec) {
            int p = (sec & ~7) | IT[sec & 7];
            byte *dst = LinearFDI(fdi, p);
            if (!dst) return false;
            memcpy(dst, padded.constData() + (i * 512), 512);
        }
        return true;
    };

    auto getRawName = [&](const uchar raw[26]) -> QString {
        QString s;
        for (int i = 0; i < 12; ++i) {
            if (raw[i] == 0x03 || raw[i] == 0x00)
                break;
            if (raw[i] >= 32)
                s.append(QChar(raw[i]));
        }
        return s.trimmed();
    };

    auto readFileData = [&](Side side, const EosEntry &entry) -> QByteArray {
        QByteArray data;
        const int maxBlocks = getMaxBlocks(side);

        for (int i = 0; i < entry.used; ++i) {
            const int blockNum = entry.sblock + i;
            if (blockNum < 0 || blockNum >= maxBlocks)
                return QByteArray();

            QByteArray blockData = readAdamBlock(blockNum, side);
            if (blockData.size() != 1024)
                return QByteArray();

            const bool isLastBlock = (i == entry.used - 1);
            if (isLastBlock) {
                int lastSize = entry.lcount;
                if (lastSize <= 0 || lastSize > 1024)
                    lastSize = 1024;
                data.append(blockData.constData(), lastSize);
            } else {
                data.append(blockData);
            }
        }

        return data;
    };

    struct CrunchEntry {
        uchar raw[26];
        EosEntry entry;
        QByteArray fileData;
        bool isVolume = false;
    };

    const Side activeSide = getActiveSide();

    if (activeSide == SideR && isPcModeRight()) {
        if (m_statusBar) m_statusBar->showMessage(tr("CRUNCH is only for ADAM media"));
        return;
    }

    ImageType type = (activeSide == SideL) ? m_typeL : m_typeR;
    if (type == ImageNone) {
        if (m_statusBar) m_statusBar->showMessage(tr("No ADAM image loaded on active side"));
        return;
    }

    if (QMessageBox::question(
            this,
            tr("Crunch ADAM media"),
            tr("Crunch the active ADAM media?\n\nDeleted files will be removed from the directory."),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    const int totalDirEntries = 39 * 3;
    QVector<CrunchEntry> keptEntries;
    keptEntries.reserve(totalDirEntries);

    uchar oldBlocksLeftRaw[26] = {0};
    bool hadBlocksLeft = false;

    // 1) Lees huidige directory en verzamel actieve entries
    for (int idx = 0; idx < totalDirEntries; ++idx) {
        uchar raw[26] = {0};
        if (!readRawDirEntry(activeSide, idx, raw))
            break;

        if (raw[0] == 0x00 || raw[0] == 0xFF)
            continue;

        const bool isVolume = (raw[13] == 0x55 && raw[14] == 0xAA);
        const QString rawName = getRawName(raw);
        EosEntry e = entryFromBytes(raw, isVolume, activeSide);

        if (isVolume) {
            CrunchEntry ce{};
            memcpy(ce.raw, raw, 26);
            ce.entry = e;
            ce.isVolume = true;
            keptEntries.append(ce);
            continue;
        }

        if (rawName.compare("BLOCKS LEFT", Qt::CaseInsensitive) == 0) {
            memcpy(oldBlocksLeftRaw, raw, 26);
            hadBlocksLeft = true;
            continue;
        }

        if (e.isSentinel())
            continue;

        if (e.isDeleted())
            continue;

        CrunchEntry ce{};
        memcpy(ce.raw, raw, 26);
        ce.entry = e;
        ce.isVolume = false;
        ce.fileData = readFileData(activeSide, e);

        if (ce.fileData.isEmpty() && e.used > 0) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed reading file data during crunch: %1").arg(e.name));
            return;
        }

        keptEntries.append(ce);
    }

    if (keptEntries.isEmpty()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Nothing to crunch"));
        return;
    }

    // 2) Compacteer datablocks
    quint16 nextBlock = 4; // directory zit in 1..3

    for (int i = 0; i < keptEntries.size(); ++i) {
        CrunchEntry &ce = keptEntries[i];
        if (ce.isVolume)
            continue;

        const quint16 oldAllocd = ce.entry.allocd;
        const quint16 usedBlocks = ce.entry.used;
        const quint16 lcount = ce.entry.lcount;

        ce.entry.sblock = nextBlock;

        // raw entry updaten
        ce.raw[13] = static_cast<uchar>(nextBlock & 0xFF);
        ce.raw[14] = static_cast<uchar>((nextBlock >> 8) & 0xFF);

        ce.raw[17] = static_cast<uchar>(oldAllocd & 0xFF);
        ce.raw[18] = static_cast<uchar>((oldAllocd >> 8) & 0xFF);

        ce.raw[19] = static_cast<uchar>(usedBlocks & 0xFF);
        ce.raw[20] = static_cast<uchar>((usedBlocks >> 8) & 0xFF);

        ce.raw[21] = static_cast<uchar>(lcount & 0xFF);
        ce.raw[22] = static_cast<uchar>((lcount >> 8) & 0xFF);

        // enkel effectieve data schrijven
        for (quint16 b = 0; b < usedBlocks; ++b) {
            QByteArray chunk = ce.fileData.mid(b * 1024, 1024);
            if (!writeAdamBlock(activeSide, nextBlock + b, chunk)) {
                if (m_statusBar) m_statusBar->showMessage(tr("Failed writing compacted block %1").arg(nextBlock + b));
                return;
            }
        }

        // allocd behouden zodat structuur veilig blijft
        nextBlock = static_cast<quint16>(nextBlock + oldAllocd);
    }

    const int maxBlocks = getMaxBlocks(activeSide);
    if (nextBlock > maxBlocks) {
        if (m_statusBar) m_statusBar->showMessage(tr("Crunch failed: compacted data exceeds media size"));
        return;
    }

    // 3) Nieuwe BLOCKS LEFT entry maken
    uchar newBlocksLeft[26] = {0};

    if (hadBlocksLeft) {
        memcpy(newBlocksLeft, oldBlocksLeftRaw, 26);
    } else {
        const QByteArray blName("BLOCKS LEFT");
        memcpy(newBlocksLeft, blName.constData(), qMin(11, blName.size()));
        newBlocksLeft[qMin(11, blName.size())] = 0x03;
        newBlocksLeft[12] = 0x01;
    }

    const quint16 freeAlloc = static_cast<quint16>(maxBlocks - nextBlock);

    newBlocksLeft[13] = static_cast<uchar>(nextBlock & 0xFF);
    newBlocksLeft[14] = static_cast<uchar>((nextBlock >> 8) & 0xFF);
    newBlocksLeft[15] = 0x00;
    newBlocksLeft[16] = 0x00;
    newBlocksLeft[17] = static_cast<uchar>(freeAlloc & 0xFF);
    newBlocksLeft[18] = static_cast<uchar>((freeAlloc >> 8) & 0xFF);
    newBlocksLeft[19] = 0;
    newBlocksLeft[20] = 0;
    newBlocksLeft[21] = 0;
    newBlocksLeft[22] = 0;

    const QDate now = QDate::currentDate();
    newBlocksLeft[23] = static_cast<uchar>(now.day());
    newBlocksLeft[24] = static_cast<uchar>(now.month());
    newBlocksLeft[25] = static_cast<uchar>(now.year() % 100);

    // 4) Volledige directory opnieuw opbouwen in geheugen
    QByteArray dirBytes(39 * 3 * 26, '\0');
    int outIndex = 0;

    for (const CrunchEntry &ce : keptEntries) {
        if (outIndex >= 39 * 3) {
            if (m_statusBar) m_statusBar->showMessage(tr("Crunch failed: directory overflow"));
            return;
        }
        memcpy(dirBytes.data() + (outIndex * 26), ce.raw, 26);
        ++outIndex;
    }

    if (outIndex >= 39 * 3) {
        if (m_statusBar) m_statusBar->showMessage(tr("Crunch failed: no room for BLOCKS LEFT"));
        return;
    }

    memcpy(dirBytes.data() + (outIndex * 26), newBlocksLeft, 26);
    ++outIndex;

    // de rest blijft 0x00

    // 5) Directory blocks 1..3 terugschrijven
    for (int blockNo = 1; blockNo <= 3; ++blockNo) {
        QByteArray blockData(1024, '\0');
        const int offset = (blockNo - 1) * 39 * 26;
        memcpy(blockData.data(), dirBytes.constData() + offset, qMin(1024, dirBytes.size() - offset));

        if (!writeAdamBlock(activeSide, blockNo, blockData)) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed writing directory block %1").arg(blockNo));
            return;
        }
    }

    // 6) Refresh + save
    parseDirectory(activeSide);

    if (!saveEOSfile(activeSide)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Crunch completed in memory, but saving failed"));
        return;
    }

    if (m_statusBar) {
        m_statusBar->showMessage(tr("CRUNCH completed successfully"));
    }
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onQuit() { accept(); }
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::setDiskRootPath(const QString &p) { m_diskRootPath = p; }
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::setTapeRootPath(const QString &p) { m_tapeRootPath = p; }
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onClearL()
{
    EjectFDI(&m_fdiL);

    m_entriesL.clear();
    m_typeL = ImageNone;
    m_pathL.clear();

    m_tableL->setRowCount(0);
    m_tableL->clearContents();

    m_volEditL->clear();
    m_dirSizeEditL->setText("0");

    m_maxL->setText("000");
    m_usedL->setText("000");
    m_freeL->setText("000");
    m_typeLabelL->setText("---");

    if (m_statusBar) m_statusBar->showMessage("Left side cleared");
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onClearR()
{
    EjectFDI(&m_fdiR);

    m_entriesR.clear();
    m_typeR = ImageNone;
    m_pathR.clear();

    // tabel leeg
    m_tableR->setRowCount(0);
    m_tableR->clearContents();

    // PC FILES teller resetten 🔥
    if (m_pcFileCountEditR)
        m_pcFileCountEditR->setText("000");

    // UI reset
    m_volEditR->clear();
    m_dirSizeEditR->setText("0");

    m_maxR->setText("000");
    m_usedR->setText("000");
    m_freeR->setText("000");
    m_typeLabelR->setText("---");

    if (m_statusBar)
        m_statusBar->showMessage("Right side cleared");
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onBlockCopy()
{
    auto writeAdamBlock = [&](Side side, int blockNum, const QByteArray &blockData) -> bool {
        static const byte IT[8] = {0,5,2,7,4,1,6,3};
        FDIDisk *fdi = (side == SideL) ? &m_fdiL : &m_fdiR;

        QByteArray padded = blockData;
        if (padded.size() < 1024)
            padded.append(QByteArray(1024 - padded.size(), '\0'));
        if (padded.size() != 1024)
            return false;

        for (int i = 0, sec = blockNum << 1; i < 2; ++i, ++sec) {
            int p = (sec & ~7) | IT[sec & 7];
            byte *dst = LinearFDI(fdi, p);
            if (!dst) return false;
            memcpy(dst, padded.constData() + (i * 512), 512);
        }
        return true;
    };

    if (!(m_statusWidgetR && m_statusWidgetR->isVisible())) {
        if (m_statusBar) m_statusBar->showMessage(tr("BLOCK COPY is only available when right side is ADAM"));
        return;
    }

    if (m_typeL == ImageNone) {
        if (m_statusBar) m_statusBar->showMessage(tr("No source ADAM image loaded on left side"));
        return;
    }

    if (m_typeR == ImageNone) {
        if (m_statusBar) m_statusBar->showMessage(tr("No target ADAM image loaded on right side"));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Block Copy"));
    dlg.setModal(true);
    dlg.setFixedSize(350, 180);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    QGridLayout *grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);

    QLabel *lblSource = new QLabel(tr("SOURCE:  ADAM DSK"), &dlg);
    QLabel *lblTarget = new QLabel(tr("TARGET:  ADAM DSK"), &dlg);

    QFont bold = lblSource->font();
    bold.setBold(true);
    lblSource->setFont(bold);
    lblTarget->setFont(bold);

    QLabel *lblSrcBlock = new QLabel(tr("Start Block:"), &dlg);
    QLabel *lblDstBlock = new QLabel(tr("Start Block:"), &dlg);
    QLabel *lblCount    = new QLabel(tr("Number of Blocks:"), &dlg);

    QLineEdit *editSrcBlock = new QLineEdit(&dlg);
    QLineEdit *editDstBlock = new QLineEdit(&dlg);
    QLineEdit *editCount    = new QLineEdit(&dlg);

    editSrcBlock->setMaxLength(5);
    editDstBlock->setMaxLength(5);
    editCount->setMaxLength(5);

    editSrcBlock->setFixedWidth(60);
    editDstBlock->setFixedWidth(60);
    editCount->setFixedWidth(60);

    grid->addWidget(lblSource,   0, 0, 1, 2);
    grid->addWidget(lblTarget,   0, 3, 1, 2);
    grid->addWidget(lblSrcBlock, 1, 0);
    grid->addWidget(editSrcBlock,1, 1);
    grid->addWidget(lblDstBlock, 1, 3);
    grid->addWidget(editDstBlock,1, 4);
    grid->addWidget(lblCount,    2, 0);
    grid->addWidget(editCount,   2, 1);

    mainLayout->addLayout(grid);
    mainLayout->addStretch();

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnCancel = new QPushButton(tr("CANCEL"), &dlg);
    QPushButton *btnOk     = new QPushButton(tr("OK"), &dlg);

    btnLayout->addStretch();
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnOk);

    mainLayout->addLayout(btnLayout);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted)
        return;

    bool ok1 = false, ok2 = false, ok3 = false;
    const int srcBlock = editSrcBlock->text().trimmed().toInt(&ok1);
    const int dstBlock = editDstBlock->text().trimmed().toInt(&ok2);
    const int numBlocks = editCount->text().trimmed().toInt(&ok3);

    if (!ok1 || !ok2 || !ok3) {
        if (m_statusBar) m_statusBar->showMessage(tr("Invalid block copy values"));
        return;
    }

    if (srcBlock < 0 || dstBlock < 0 || numBlocks <= 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("Blocks must be >= 0 and count > 0"));
        return;
    }

    const int maxSrc = getMaxBlocks(SideL);
    const int maxDst = getMaxBlocks(SideR);

    if ((srcBlock + numBlocks) > maxSrc) {
        if (m_statusBar) m_statusBar->showMessage(tr("Source range exceeds left image size"));
        return;
    }

    if ((dstBlock + numBlocks) > maxDst) {
        if (m_statusBar) m_statusBar->showMessage(tr("Target range exceeds right image size"));
        return;
    }

    QVector<QByteArray> cache;
    cache.reserve(numBlocks);

    for (int i = 0; i < numBlocks; ++i) {
        QByteArray blockData = readAdamBlock(srcBlock + i, SideL);
        if (blockData.size() != 1024) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed reading source block %1").arg(srcBlock + i));
            return;
        }
        cache.append(blockData);
    }

    for (int i = 0; i < numBlocks; ++i) {
        if (!writeAdamBlock(SideR, dstBlock + i, cache[i])) {
            if (m_statusBar) m_statusBar->showMessage(tr("Failed writing target block %1").arg(dstBlock + i));
            return;
        }
    }

    if (!saveEOSfile(SideR)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Blocks copied in memory, but saving target image failed"));
        return;
    }

    parseDirectory(SideR);

    if (m_statusBar) {
        m_statusBar->showMessage(
            tr("BLOCK COPY done: %1 block(s) from L:%2 to R:%3")
                .arg(numBlocks)
                .arg(srcBlock)
                .arg(dstBlock)
            );
    }
}
/* ---------------------------------------------------------------------------------------------------------------------*/
bool AimDialog::updateVolumeName(Side side, const QString &newName)
{
    auto writeAdamBlock = [&](Side s, int blockNum, const QByteArray &blockData) -> bool {
        static const byte IT[8] = {0,5,2,7,4,1,6,3};
        FDIDisk *fdi = (s == SideL) ? &m_fdiL : &m_fdiR;

        QByteArray padded = blockData;
        if (padded.size() < 1024)
            padded.append(QByteArray(1024 - padded.size(), '\0'));
        if (padded.size() != 1024)
            return false;

        for (int i = 0, sec = blockNum << 1; i < 2; ++i, ++sec) {
            int p = (sec & ~7) | IT[sec & 7];
            byte *dst = LinearFDI(fdi, p);
            if (!dst) return false;
            memcpy(dst, padded.constData() + (i * 512), 512);
        }
        return true;
    };

    QString finalName = newName.trimmed().toUpper();
    if (finalName.isEmpty())
        return false;

    finalName = finalName.left(11);

    bool foundAny = false;

    // minstens tot blok 5 scannen
    for (int b = 1; b <= 5; ++b) {
        QByteArray data = readAdamBlock(b, side);
        if (data.size() < 1024)
            continue;

        bool changedBlock = false;
        uchar *rb = reinterpret_cast<uchar*>(data.data());

        for (int i = 0; i < 39; ++i) {
            uchar *raw = rb + (i * 26);

            if (raw[0] == 0x00 || raw[0] == 0xFF)
                continue;

            const bool isVolume = (raw[13] == 0x55 && raw[14] == 0xAA);
            if (!isVolume)
                continue;

            memset(raw, 0x00, 12);

            QByteArray latin = finalName.toLatin1();
            const int n = qMin(11, latin.size());
            for (int j = 0; j < n; ++j)
                raw[j] = static_cast<uchar>(latin[j]);

            raw[n] = 0x03;   // EOS terminator

            changedBlock = true;
            foundAny = true;
        }

        if (changedBlock) {
            if (!writeAdamBlock(side, b, data))
                return false;
        }
    }

    return foundAny;
}
/* ---------------------------------------------------------------------------------------------------------------------*/
bool AimDialog::updateAllBlocksLeftEntries(Side side, quint16 startBlock, quint16 freeBlocks)
{
    auto writeAdamBlock = [&](Side s, int blockNum, const QByteArray &blockData) -> bool {
        static const byte IT[8] = {0,5,2,7,4,1,6,3};
        FDIDisk *fdi = (s == SideL) ? &m_fdiL : &m_fdiR;

        QByteArray padded = blockData;
        if (padded.size() < 1024)
            padded.append(QByteArray(1024 - padded.size(), '\0'));
        if (padded.size() != 1024)
            return false;

        for (int i = 0, sec = blockNum << 1; i < 2; ++i, ++sec) {
            int p = (sec & ~7) | IT[sec & 7];
            byte *dst = LinearFDI(fdi, p);
            if (!dst)
                return false;
            memcpy(dst, padded.constData() + (i * 512), 512);
        }
        return true;
    };

    auto getRawName = [&](const uchar *raw) -> QString {
        QString s;
        for (int i = 0; i < 12; ++i) {
            if (raw[i] == 0x03 || raw[i] == 0x00)
                break;
            if (raw[i] >= 32)
                s.append(QChar(raw[i]));
        }
        return s.trimmed().toUpper();
    };

    bool foundAny = false;

    for (int b = 0; b < getMaxBlocks(side); ++b) {
        QByteArray data = readAdamBlock(b, side);
        if (data.size() < 1024)
            continue;

        bool changedBlock = false;
        uchar *rb = reinterpret_cast<uchar*>(data.data());

        for (int i = 0; i < 39; ++i) {
            uchar *raw = rb + (i * 26);

            if (raw[0] == 0x00 || raw[0] == 0xFF)
                continue;

            const QString rawName = getRawName(raw);
            if (rawName != "BLOCKS LEFT")
                continue;

            raw[12] = 0x01;
            raw[13] = static_cast<uchar>(startBlock & 0xFF);
            raw[14] = static_cast<uchar>((startBlock >> 8) & 0xFF);
            raw[15] = 0x00;
            raw[16] = 0x00;
            raw[17] = static_cast<uchar>(freeBlocks & 0xFF);
            raw[18] = static_cast<uchar>((freeBlocks >> 8) & 0xFF);
            raw[19] = 0x00;
            raw[20] = 0x00;
            raw[21] = 0x00;
            raw[22] = 0x00;

            changedBlock = true;
            foundAny = true;
        }

        if (changedBlock) {
            if (!writeAdamBlock(side, b, data))
                return false;
        }
    }

    return foundAny;
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onEditVolumeNameL()
{
    if (m_typeL == ImageNone) {
        if (m_statusBar) m_statusBar->showMessage(tr("No ADAM image loaded on left side"));
        return;
    }

    bool ok = false;
    QString currentName = m_volEditL->text().trimmed();

    QString newName = QInputDialog::getText(
                          this,
                          tr("Edit Volume Name"),
                          tr("New volume name:"),
                          QLineEdit::Normal,
                          currentName,
                          &ok
                          ).trimmed().toUpper();

    if (!ok)
        return;

    if (newName.isEmpty()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Volume name cannot be empty"));
        return;
    }

    if (!updateVolumeName(SideL, newName)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed to update left volume name"));
        return;
    }

    parseDirectory(SideL);

    if (!saveEOSfile(SideL)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Left volume name changed in memory, but saving failed"));
        return;
    }

    if (m_statusBar)
        m_statusBar->showMessage(tr("Left volume name changed to: %1").arg(newName.left(11)));
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onEditVolumeNameR()
{
    if (!(m_statusWidgetR && m_statusWidgetR->isVisible())) {
        if (m_statusBar) m_statusBar->showMessage(tr("Right volume rename is only available in ADAM mode"));
        return;
    }

    if (m_typeR == ImageNone) {
        if (m_statusBar) m_statusBar->showMessage(tr("No ADAM image loaded on right side"));
        return;
    }

    bool ok = false;
    QString currentName = m_volEditR->text().trimmed();

    QString newName = QInputDialog::getText(
                          this,
                          tr("Edit Volume Name"),
                          tr("New volume name:"),
                          QLineEdit::Normal,
                          currentName,
                          &ok
                          ).trimmed().toUpper();

    if (!ok)
        return;

    if (newName.isEmpty()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Volume name cannot be empty"));
        return;
    }

    if (!updateVolumeName(SideR, newName)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed to update right volume name"));
        return;
    }

    parseDirectory(SideR);

    if (!saveEOSfile(SideR)) {
        if (m_statusBar) m_statusBar->showMessage(tr("Right volume name changed in memory, but saving failed"));
        return;
    }

    if (m_statusBar)
        m_statusBar->showMessage(tr("Right volume name changed to: %1").arg(newName.left(11)));
}
/* ---------------------------------------------------------------------------------------------------------------------*/
bool AimDialog::writeAdamBlock(int blockNum, Side side, const QByteArray &blockData)
{
    static const byte IT[8] = {0,5,2,7,4,1,6,3};
    FDIDisk *fdi = (side == SideL) ? &m_fdiL : &m_fdiR;

    QByteArray padded = blockData;
    if (padded.size() < 1024)
        padded.append(QByteArray(1024 - padded.size(), '\0'));
    if (padded.size() != 1024)
        return false;

    for (int i = 0, sec = blockNum << 1; i < 2; ++i, ++sec) {
        int p = (sec & ~7) | IT[sec & 7];
        byte *dst = LinearFDI(fdi, p);
        if (!dst)
            return false;
        memcpy(dst, padded.constData() + (i * 512), 512);
    }
    return true;
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onTableContextMenuL(const QPoint &pos)
{
    if (!m_tableL)
        return;

    QTableWidgetItem *item = m_tableL->itemAt(pos);
    if (!item)
        return;

    const int row = item->row();
    if (row < 0)
        return;

    m_tableL->setCurrentCell(row, 0);
    m_tableL->selectRow(row);
    updateTableFocus(SideL);

    QMenu menu(this);
    menu.setCursor(Qt::PointingHandCursor);

    QAction *actHexEdit = menu.addAction(tr("Hex Edit File"));
    menu.addSeparator();
    QAction *actDelete = menu.addAction(tr("Delete"));
    QAction *actRename = menu.addAction(tr("Rename"));
    QAction *actAttrib = menu.addAction(tr("Attributes"));

    QAction *chosen = menu.exec(m_tableL->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == actHexEdit) {
        if (!editFileAtRow(SideL, row) && m_statusBar)
            m_statusBar->showMessage(tr("Failed opening file editor"));
        return;
    }

    if (chosen == actDelete) { onDelete(); return; }
    if (chosen == actRename) { onRename(); return; }
    if (chosen == actAttrib) { onAttrib(); return; }
}
/* ---------------------------------------------------------------------------------------------------------------------*/
void AimDialog::onTableContextMenuR(const QPoint &pos)
{
    if (!m_tableR)
        return;

    if (!(m_statusWidgetR && m_statusWidgetR->isVisible()))
        return;

    QTableWidgetItem *item = m_tableR->itemAt(pos);
    if (!item)
        return;

    const int row = item->row();
    if (row < 0)
        return;

    m_tableR->setCurrentCell(row, 0);
    m_tableR->selectRow(row);
    updateTableFocus(SideR);

    QMenu menu(this);
    menu.setCursor(Qt::PointingHandCursor);

    QAction *actHexEdit = menu.addAction(tr("Hex Edit File"));
    menu.addSeparator();
    QAction *actDelete = menu.addAction(tr("Delete"));
    QAction *actRename = menu.addAction(tr("Rename"));
    QAction *actAttrib = menu.addAction(tr("Attributes"));

    QAction *chosen = menu.exec(m_tableR->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == actHexEdit) {
        if (!editFileAtRow(SideR, row) && m_statusBar)
            m_statusBar->showMessage(tr("Failed opening file editor"));
        return;
    }

    if (chosen == actDelete) { onDelete(); return; }
    if (chosen == actRename) { onRename(); return; }
    if (chosen == actAttrib) { onAttrib(); return; }
}
/* ---------------------------------------------------------------------------------------------------------------------*/
bool AimDialog::editFileAtRow(Side side, int row)
{
    QTableWidget *table = (side == SideL) ? m_tableL : m_tableR;
    QVector<EosEntry> &entries = (side == SideL) ? m_entriesL : m_entriesR;

    if (!table)
        return false;

    if (row < 0 || row >= table->rowCount())
        return false;

    QVector<int> visibleToEntry;
    visibleToEntry.reserve(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        if (e.isVolume() || e.isSentinel())
            continue;
        visibleToEntry.append(i);
    }

    if (row < 0 || row >= visibleToEntry.size())
        return false;

    EosEntry &entry = entries[visibleToEntry[row]];

    if (entry.isDeleted()) {
        if (m_statusBar) m_statusBar->showMessage(tr("Cannot edit deleted file"));
        return false;
    }

    if (entry.used == 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("Selected file has no data blocks"));
        return false;
    }

    auto readFileDataLocal = [&](Side s, const EosEntry &e) -> QByteArray {
        QByteArray fileData;
        const int maxBlocks = getMaxBlocks(s);

        for (int i = 0; i < e.used; ++i) {
            const int blockNum = e.sblock + i;
            if (blockNum < 0 || blockNum >= maxBlocks)
                return QByteArray();

            QByteArray blockData = readAdamBlock(blockNum, s);
            if (blockData.size() != 1024)
                return QByteArray();

            const bool isLastBlock = (i == e.used - 1);
            if (isLastBlock) {
                int lastSize = e.lcount;
                if (lastSize <= 0 || lastSize > 1024)
                    lastSize = 1024;
                fileData.append(blockData.constData(), lastSize);
            } else {
                fileData.append(blockData);
            }
        }
        return fileData;
    };

    QByteArray fileData = readFileDataLocal(side, entry);
    if (fileData.isEmpty() && entry.used > 0) {
        if (m_statusBar) m_statusBar->showMessage(tr("Failed reading file data"));
        return false;
    }

    const int bytesPerRow = 16;
    const int rowCount = (fileData.size() + bytesPerRow - 1) / bytesPerRow;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Hex Edit File - %1").arg(entry.name));
    dlg.setModal(true);
    dlg.resize(1024, 500);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);

    QLabel *info = new QLabel(
        tr("File: %1    Start block: %2    Blocks: %3    Size: %4 bytes")
            .arg(entry.name)
            .arg(entry.sblock)
            .arg(entry.used)
            .arg(fileData.size()),
        &dlg
        );
    mainLayout->addWidget(info);

    QHBoxLayout *searchLayout = new QHBoxLayout();
    QLabel *searchLbl = new QLabel(tr("Search:"), &dlg);
    QLineEdit *searchEdit = new QLineEdit(&dlg);
    QRadioButton *rbSearchAscii = new QRadioButton(tr("ASCII"), &dlg);
    QRadioButton *rbSearchHex = new QRadioButton(tr("HEX"), &dlg);
    QPushButton *btnSearch = new QPushButton(tr("SEARCH"), &dlg);
    QPushButton *btnPrev = new QPushButton(tr("PREV"), &dlg);
    QPushButton *btnNext = new QPushButton(tr("NEXT"), &dlg);
    QPushButton *btnClearSearch = new QPushButton(tr("CLEAR"), &dlg);

    rbSearchAscii->setChecked(true);

    for (QPushButton *b : {btnSearch, btnPrev, btnNext, btnClearSearch})
        b->setCursor(Qt::PointingHandCursor);

    searchLayout->addWidget(searchLbl);
    searchLayout->addWidget(searchEdit, 1);
    searchLayout->addWidget(rbSearchAscii);
    searchLayout->addWidget(rbSearchHex);
    searchLayout->addWidget(btnSearch);
    searchLayout->addWidget(btnPrev);
    searchLayout->addWidget(btnNext);
    searchLayout->addWidget(btnClearSearch);

    QTableWidget *hexTable = new QTableWidget(&dlg);
    hexTable->setRowCount(rowCount);
    hexTable->setColumnCount(34); // 1 offset + 16 hex + 1 sep + 16 ascii
    hexTable->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    hexTable->verticalHeader()->setVisible(false);
    hexTable->setSelectionMode(QAbstractItemView::SingleSelection);
    hexTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    hexTable->setEditTriggers(QAbstractItemView::DoubleClicked |
                              QAbstractItemView::EditKeyPressed |
                              QAbstractItemView::SelectedClicked);

    QStringList headers;
    headers << "OFFSET";
    headers << "0" << "1" << "2" << "3"
            << "4" << "5" << "6" << "7"
            << "8" << "9" << "A" << "B"
            << "C" << "D" << "E" << "F";
    headers << "";
    headers << "0" << "1" << "2" << "3"
            << "4" << "5" << "6" << "7"
            << "8" << "9" << "A" << "B"
            << "C" << "D" << "E" << "F";
    hexTable->setHorizontalHeaderLabels(headers);

    QHeaderView *hh = hexTable->horizontalHeader();
    hh->setSectionResizeMode(QHeaderView::Fixed);
    hh->setMinimumSectionSize(1);
    hh->setDefaultSectionSize(38);

    hexTable->setColumnWidth(0, 80);
    for (int i = 1; i <= 16; ++i)
        hexTable->setColumnWidth(i, 32);

    hh->setSectionResizeMode(17, QHeaderView::Fixed);
    hexTable->setColumnWidth(17, 2);
    hh->resizeSection(17, 2);

    for (int i = 18; i <= 33; ++i)
        hexTable->setColumnWidth(i, 24);

    hexTable->setStyleSheet(
        "QTableWidget { background-color: #202020; gridline-color: #404040; }"
        "QHeaderView::section { background-color: #2B2B2B; color: white; padding: 4px; border: 1px solid #1E1E1E; }"
        "QTableWidget::item:selected { background-color: #3399FF; color: black; }"
        );

    const QColor rowBgA("#3C3C3C");
    const QColor rowBgB("#282828");
    const QColor sepBg("#C0C0C0");
    const QColor offsetBgA("#353535");
    const QColor offsetBgB("#252525");

    auto applyRowBackground = [&](QTableWidgetItem *it, int rowIndex, int col) {
        if (!it)
            return;

        const bool oddRow = (rowIndex % 2) != 0;

        if (col == 17) {
            it->setBackground(QBrush(sepBg));
            it->setForeground(QBrush(Qt::black));
            return;
        }

        if (col == 0) {
            it->setBackground(QBrush(oddRow ? offsetBgB : offsetBgA));
            it->setForeground(QBrush(Qt::white));
            return;
        }

        it->setBackground(QBrush(oddRow ? rowBgB : rowBgA));
    };

    auto makeNonEditableItem = [&](const QString &text, int rowIndex, int col) -> QTableWidgetItem* {
        QTableWidgetItem *it = new QTableWidgetItem(text);
        it->setFlags((it->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled) & ~Qt::ItemIsEditable);
        it->setTextAlignment(Qt::AlignCenter);
        applyRowBackground(it, rowIndex, col);
        return it;
    };

    auto makeEditableHexItem = [&](unsigned char value, int rowIndex, int col) -> QTableWidgetItem* {
        QTableWidgetItem *it = new QTableWidgetItem(QString("%1").arg(value, 2, 16, QChar('0')).toUpper());
        it->setTextAlignment(Qt::AlignCenter);
        applyRowBackground(it, rowIndex, col);
        return it;
    };

    auto makeEditableAsciiItem = [&](unsigned char value, int rowIndex, int col) -> QTableWidgetItem* {
        QChar ch = (value >= 32 && value <= 126) ? QChar(value) : QChar('.');
        QTableWidgetItem *it = new QTableWidgetItem(QString(ch));
        it->setTextAlignment(Qt::AlignCenter);
        applyRowBackground(it, rowIndex, col);
        return it;
    };

    for (int r = 0; r < rowCount; ++r) {
        const int baseOffset = r * bytesPerRow;

        hexTable->setItem(
            r,
            0,
            makeNonEditableItem(QString("%1").arg(baseOffset, 6, 16, QChar('0')).toUpper(), r, 0)
            );

        for (int c = 0; c < bytesPerRow; ++c) {
            const int index = baseOffset + c;

            if (index < fileData.size()) {
                unsigned char value = static_cast<unsigned char>(fileData[index]);
                hexTable->setItem(r, 1 + c, makeEditableHexItem(value, r, 1 + c));
                hexTable->setItem(r, 18 + c, makeEditableAsciiItem(value, r, 18 + c));
            } else {
                hexTable->setItem(r, 1 + c, makeNonEditableItem("", r, 1 + c));
                hexTable->setItem(r, 18 + c, makeNonEditableItem("", r, 18 + c));
            }
        }

        QTableWidgetItem *sep = makeNonEditableItem("", r, 17);
        sep->setFlags(Qt::NoItemFlags);
        hexTable->setItem(r, 17, sep);
    }

    auto resetAllCellColors = [&]() {
        for (int r = 0; r < hexTable->rowCount(); ++r) {
            for (int c = 1; c < hexTable->columnCount(); ++c) { // overslaan offset kolom 0
                if (c == 17) continue;
                QTableWidgetItem *it = hexTable->item(r, c);
                if (it) {
                    it->setForeground(QBrush(Qt::white)); // Zet hier handmatig terug op wit
                    QFont f = it->font();
                    f.setBold(false);
                    it->setFont(f);
                }
            }
        }
        hexTable->viewport()->update();
    };

    auto highlightByteIndex = [&](int byteIndex) {
        if (byteIndex < 0 || byteIndex >= fileData.size()) return;

        const int r = byteIndex / bytesPerRow;
        const int offsetInRow = byteIndex % bytesPerRow;

        QTableWidgetItem *hexItem = hexTable->item(r, 1 + offsetInRow);
        QTableWidgetItem *asciiItem = hexTable->item(r, 18 + offsetInRow);

        if (hexItem) {
            hexItem->setForeground(QBrush(Qt::yellow)); // Nu pakt hij de kleur wel
            QFont f = hexItem->font();
            f.setBold(true);
            hexItem->setFont(f);
        }

        if (asciiItem) {
            asciiItem->setForeground(QBrush(Qt::yellow)); // Nu pakt hij de kleur wel
            QFont f = asciiItem->font();
            f.setBold(true);
            asciiItem->setFont(f); // Gecorrigeerd: stond eerst op hexItem
        }
    };

    auto jumpToByteIndex = [&](int byteIndex) {
        if (byteIndex < 0 || byteIndex >= fileData.size())
            return;

        const int r = byteIndex / bytesPerRow;
        const int c = 18 + (byteIndex % bytesPerRow);

        QTableWidgetItem *item = hexTable->item(r, c);
        if (item) {
            hexTable->clearSelection();
            hexTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            hexTable->viewport()->update();
        }
    };

    auto parseHexSearchString = [&](const QString &text, QByteArray &outBytes) -> bool {
        outBytes.clear();

        QString s = text.trimmed();
        if (s.isEmpty())
            return false;

        const QStringList parts = s.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty())
            return false;

        for (const QString &part : parts) {
            bool ok = false;
            int value = part.toInt(&ok, 16);
            if (!ok || part.size() > 2 || value < 0 || value > 0xFF)
                return false;

            outBytes.append(static_cast<char>(value & 0xFF));
        }

        return !outBytes.isEmpty();
    };

    auto updateSearchPlaceholder = [&]() {
        if (rbSearchHex->isChecked())
            searchEdit->setPlaceholderText(tr("HEX bytes, bv: 20 68 14 25"));
        else
            searchEdit->setPlaceholderText(tr("ASCII text"));
    };

    updateSearchPlaceholder();

    QList<int> searchHits;
    int currentHitIndex = -1;

    auto runSearch = [&]() {
        const QString needleText = searchEdit->text().trimmed();

        resetAllCellColors();
        searchHits.clear();
        currentHitIndex = -1;

        if (needleText.isEmpty()) {
            if (m_statusBar)
                m_statusBar->showMessage(tr("Search string is empty"));
            return;
        }

        QByteArray needleBytes;

        if (rbSearchHex->isChecked()) {
            if (!parseHexSearchString(needleText, needleBytes)) {
                if (m_statusBar)
                    m_statusBar->showMessage(tr("Invalid HEX search. Example: 20 68 14 25"));
                return;
            }
        } else {
            needleBytes = needleText.toUtf8();
            if (needleBytes.isEmpty()) {
                if (m_statusBar)
                    m_statusBar->showMessage(tr("ASCII search string is empty"));
                return;
            }
        }

        int pos = 0;
        while (true) {
            pos = fileData.indexOf(needleBytes, pos);
            if (pos < 0)
                break;
            searchHits.append(pos);
            pos += 1;
        }

        if (searchHits.isEmpty()) {
            if (m_statusBar) {
                m_statusBar->showMessage(
                    tr("Not found (%1): %2")
                        .arg(rbSearchHex->isChecked() ? "HEX" : "ASCII")
                        .arg(needleText)
                    );
            }
            return;
        }

        for (int hitStart : searchHits) {
            for (int i = 0; i < needleBytes.size(); ++i)
                highlightByteIndex(hitStart + i);
        }
        hexTable->viewport()->repaint();

        hexTable->clearSelection();
        hexTable->viewport()->update();
        qApp->processEvents();

        currentHitIndex = 0;
        jumpToByteIndex(searchHits[currentHitIndex]);

        if (m_statusBar) {
            m_statusBar->showMessage(
                tr("Found %1 occurrence(s) for %2 search")
                    .arg(searchHits.size())
                    .arg(rbSearchHex->isChecked() ? "HEX" : "ASCII")
                );
        }
    };

    auto gotoNextHit = [&]() {
        if (searchHits.isEmpty()) {
            if (m_statusBar)
                m_statusBar->showMessage(tr("No active search results"));
            return;
        }

        currentHitIndex++;
        if (currentHitIndex >= searchHits.size())
            currentHitIndex = 0;

        jumpToByteIndex(searchHits[currentHitIndex]);

        if (m_statusBar)
            m_statusBar->showMessage(
                tr("Match %1 of %2").arg(currentHitIndex + 1).arg(searchHits.size())
                );
    };

    auto gotoPrevHit = [&]() {
        if (searchHits.isEmpty()) {
            if (m_statusBar)
                m_statusBar->showMessage(tr("No active search results"));
            return;
        }

        currentHitIndex--;
        if (currentHitIndex < 0)
            currentHitIndex = searchHits.size() - 1;

        jumpToByteIndex(searchHits[currentHitIndex]);

        if (m_statusBar)
            m_statusBar->showMessage(
                tr("Match %1 of %2").arg(currentHitIndex + 1).arg(searchHits.size())
                );
    };

    mainLayout->addWidget(hexTable);
    mainLayout->addLayout(searchLayout);

    QLabel *help = new QLabel(tr(""), &dlg);
    mainLayout->addWidget(help);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnSave = new QPushButton(tr("SAVE"), &dlg);
    QPushButton *btnCancel = new QPushButton(tr("CANCEL"), &dlg);

    btnSave->setCursor(Qt::PointingHandCursor);
    btnCancel->setCursor(Qt::PointingHandCursor);

    btnLayout->addStretch();
    btnLayout->addWidget(btnSave);
    btnLayout->addWidget(btnCancel);
    mainLayout->addLayout(btnLayout);

    bool internalUpdate = false;

    connect(hexTable, &QTableWidget::itemChanged, &dlg,
            [&](QTableWidgetItem *item) {
                if (!item || internalUpdate)
                    return;

                const int r = item->row();
                const int c = item->column();

                if (c >= 1 && c <= 16) {
                    QString txt = item->text().trimmed().toUpper();

                    if (txt.isEmpty())
                        txt = "00";

                    if (txt.size() > 2)
                        txt = txt.right(2);

                    bool ok = false;
                    int val = txt.toInt(&ok, 16);
                    const int index = r * bytesPerRow + (c - 1);

                    if (!ok || val < 0 || val > 255 || index >= fileData.size()) {
                        if (index < fileData.size()) {
                            unsigned char oldVal = static_cast<unsigned char>(fileData[index]);
                            internalUpdate = true;
                            item->setText(QString("%1").arg(oldVal, 2, 16, QChar('0')).toUpper());
                            applyRowBackground(item, r, c);
                            internalUpdate = false;
                        }
                        return;
                    }

                    fileData[index] = static_cast<char>(val);

                    internalUpdate = true;
                    item->setText(QString("%1").arg(val, 2, 16, QChar('0')).toUpper());
                    applyRowBackground(item, r, c);

                    QTableWidgetItem *asciiItem = hexTable->item(r, 18 + (c - 1));
                    if (asciiItem) {
                        QChar ch = (val >= 32 && val <= 126) ? QChar(val) : QChar('.');
                        asciiItem->setText(QString(ch));
                        applyRowBackground(asciiItem, r, 18 + (c - 1));
                    }
                    internalUpdate = false;
                }
                else if (c >= 18 && c <= 33) {
                    QString txt = item->text();
                    const int index = r * bytesPerRow + (c - 18);

                    if (index >= fileData.size())
                        return;

                    if (txt.isEmpty())
                        txt = ".";

                    QChar ch = txt.at(0);
                    unsigned char val = static_cast<unsigned char>(ch.unicode() & 0xFF);
                    fileData[index] = static_cast<char>(val);

                    internalUpdate = true;
                    item->setText(QString(ch));
                    applyRowBackground(item, r, c);

                    QTableWidgetItem *hexItem = hexTable->item(r, 1 + (c - 18));
                    if (hexItem) {
                        hexItem->setText(QString("%1").arg(val, 2, 16, QChar('0')).toUpper());
                        applyRowBackground(hexItem, r, 1 + (c - 18));
                    }

                    internalUpdate = false;
                }
            });

    connect(btnSearch, &QPushButton::clicked, &dlg, runSearch);
    connect(btnNext, &QPushButton::clicked, &dlg, gotoNextHit);
    connect(btnPrev, &QPushButton::clicked, &dlg, gotoPrevHit);

    connect(rbSearchAscii, &QRadioButton::toggled, &dlg, updateSearchPlaceholder);
    connect(rbSearchHex, &QRadioButton::toggled, &dlg, updateSearchPlaceholder);

    connect(btnClearSearch, &QPushButton::clicked, &dlg, [&]() {
        searchEdit->clear();
        searchHits.clear();
        currentHitIndex = -1;
        resetAllCellColors();

        hexTable->clearSelection();
        hexTable->setCurrentCell(0, 1);

        if (hexTable->item(0, 1))
            hexTable->scrollToItem(hexTable->item(0, 1), QAbstractItemView::PositionAtTop);

        if (m_statusBar)
            m_statusBar->showMessage(tr("Search cleared"));
    });

    connect(searchEdit, &QLineEdit::returnPressed, btnSearch, &QPushButton::click);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    connect(btnSave, &QPushButton::clicked, &dlg, [&]() {
        for (int i = 0; i < entry.used; ++i) {
            const int blockNum = entry.sblock + i;
            const int offset = i * 1024;
            QByteArray chunk = fileData.mid(offset, 1024);

            if (chunk.size() < 1024)
                chunk.append(QByteArray(1024 - chunk.size(), '\0'));

            if (!writeAdamBlock(blockNum, side, chunk)) {
                QMessageBox::critical(&dlg, tr("Write failed"),
                                      tr("Failed writing block %1").arg(blockNum));
                return;
            }
        }

        if (!saveEOSfile(side)) {
            QMessageBox::critical(&dlg, tr("Save failed"),
                                  tr("File data changed in memory, but saving image failed."));
            return;
        }

        parseDirectory(side);
        dlg.accept();
    });

    if (dlg.exec() != QDialog::Accepted)
        return false;

    if (m_statusBar)
        m_statusBar->showMessage(tr("Saved file: %1").arg(entry.name));

    return true;
}
/* ---------------------------------------------------------------------------------------------------------------------*/
