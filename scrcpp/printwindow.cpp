#include "printwindow.h"
#include "adnet.h"

#include <QTableWidget>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFileDialog>
#include <QPrinter>
#include <QTextDocument>
#include <QPainter>
#include <QDateTime>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QLabel>
#include <QTextStream>
#include <QMetaObject>
#include <QMutexLocker>
#include <QScrollBar>
#include <QResizeEvent>
#include <QFrame>
#include <QStyledItemDelegate>
#include <QClipboard>
#include <QGuiApplication>
#include <QContextMenuEvent>
#include <algorithm>

extern "C" {
#include "tms9928a.h"
extern tTMS9981A tms;
}
extern BYTE VDP_Memory[0x10000];

// ===== Delegates (Standaard weergave) =====
class TextZebraDelegate : public QStyledItemDelegate
{
public:
    TextZebraDelegate(const QColor& light, const QColor& dark, QObject* parent=nullptr)
        : QStyledItemDelegate(parent), m_light(light), m_dark(dark) {}

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        const bool odd = (idx.row() % 2) == 1;
        p->fillRect(opt.rect, odd ? m_dark : m_light);

        p->save();
        QPen pen(QColor(120,120,120));
        pen.setStyle(Qt::DotLine);
        p->setPen(pen);
        p->drawLine(opt.rect.topLeft(),  opt.rect.bottomLeft());
        p->drawLine(opt.rect.topRight(), opt.rect.bottomRight());
        p->restore();

        QStyleOptionViewItem o(opt);
        o.backgroundBrush = Qt::NoBrush;
        o.palette.setColor(QPalette::Text, Qt::black);
        o.palette.setColor(QPalette::WindowText, Qt::black);
        o.textElideMode = Qt::ElideRight;

        QStyledItemDelegate::paint(p, o, idx);
    }

private:
    QColor m_light, m_dark;
};

class HoleDelegate : public QStyledItemDelegate
{
public:
    HoleDelegate(const QColor& light, const QColor& dark, int diameter, int padding, QObject* parent=nullptr)
        : QStyledItemDelegate(parent), m_light(light), m_dark(dark),
        m_diameter(diameter), m_padding(padding) {}

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        const bool odd = (idx.row() % 2) == 1;
        p->fillRect(opt.rect, odd ? m_dark : m_light);

        p->save();
        QPen pen(QColor(120,120,120));
        pen.setStyle(Qt::DotLine);
        p->setPen(pen);
        p->drawLine(opt.rect.topLeft(),  opt.rect.bottomLeft());
        p->drawLine(opt.rect.topRight(), opt.rect.bottomRight());

        const QRect r = opt.rect;
        const int avail = qMin(r.width(), r.height()) - 2*m_padding;
        const int dia   = qMax(2, qMin(m_diameter, avail));
        const int rad   = dia / 2;
        const QPoint c(r.center().x(), r.center().y());

        p->setPen(QPen(QColor(190,190,190), 1));
        p->setBrush(Qt::white);
        p->drawEllipse(QRect(c.x()-rad, c.y()-rad, dia, dia));
        p->restore();
    }

private:
    QColor m_light, m_dark;
    int    m_diameter;
    int    m_padding;
};

PrintWindow* PrintWindow::s_instance = nullptr;

PrintWindow* PrintWindow::instance()
{
    return s_instance ? s_instance : (s_instance = new PrintWindow());
}

void PrintWindow::setAsSink(PrintWindow* w)
{
    s_instance = w;
}

// ====== C-bridge: AdamNet → UI ======
extern "C" void adam_printer_chunk(const uint8_t* data, int len)
{
    if (!data || len <= 0) return;

    PrintWindow* w = PrintWindow::instance();
    if (!w) return;

    QMetaObject::invokeMethod(
        w,
        "appendPrinterBytes",
        Qt::QueuedConnection,
        Q_ARG(QByteArray, QByteArray(reinterpret_cast<const char*>(data), len))
        );
}

PrintWindow::PrintWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("ADAM Printer Output");
    setFixedWidth(550);
    setMinimumHeight(700);

    // ===== statusbar =====
    m_status   = new QStatusBar(this);
    m_status->setFixedHeight(21);
    m_statLeft = new QLabel(this);
    m_statMid  = new QLabel(this);
    m_statRight= new QLabel(this);

    m_statLeft ->setText("Lines: 0");
    m_statMid  ->setText("Bytes: 0");
    m_statRight->setText("");

    QFont statusFont("Roboto", 9);
    statusBar()->setFont(statusFont);
    m_statLeft ->setFont(statusFont);
    m_statMid  ->setFont(statusFont);
    m_statRight->setFont(statusFont);

    m_status->addWidget(m_statLeft, 0);
    m_status->addWidget(m_statMid,  0);
    m_status->addPermanentWidget(m_statRight, 1);
    setStatusBar(m_status);

    // ===== BOVEN: QLabel container voor QTableWidget =====
    m_paperLabel = new QLabel(this);
    m_paperLabel->setObjectName("paperLabel");
    m_paperLabel->setFrameShape(QFrame::NoFrame);
    m_paperLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // QTableWidget (3 kolommen)
    m_table = new QTableWidget(m_paperLabel);
    m_table->setColumnCount(3);
    m_table->setStyleSheet(
        "QTableView{border:0; background:#1e1e1e;}"
        "QTableCornerButton::section{background:#1e1e1e; border:0;}"
        "QHeaderView::section{background:#1e1e1e; border:1;}"
        );
    m_table->horizontalHeader()->setVisible(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);

    // Selectie
    m_table->setSelectionMode(QAbstractItemView::ContiguousSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // UI Settings
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->setColumnWidth(0, m_holeColWidth);
    m_table->setColumnWidth(2, m_holeColWidth);

    // Delegates
    m_table->setItemDelegateForColumn(1, new TextZebraDelegate(m_rowLight, m_rowDark, m_table));
    m_table->setItemDelegateForColumn(0, new HoleDelegate(m_rowLight, m_rowDark, m_holeDiameter, m_holePadding, m_table));
    m_table->setItemDelegateForColumn(2, new HoleDelegate(m_rowLight, m_rowDark, m_holeDiameter, m_holePadding, m_table));

    // Font
    m_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    applyTextFont(m_monoFont);

    // Layout
    {
        auto *paperLay = new QVBoxLayout(m_paperLabel);
        paperLay->setContentsMargins(0,0,0,0);
        paperLay->setSpacing(0);
        paperLay->addWidget(m_table);
    }

    // ===== ONDER: bitmap =====
    m_bitmapLabel = new QLabel(this);
    m_bitmapLabel->setFixedSize(600,168);
    m_bitmapLabel->setContentsMargins(0,0,0,0);
    m_bitmapLabel->setStyleSheet(
        "QTableView{border:0; background:#1e1e1e;}"
        "QTableCornerButton::section{background:#1e1e1e; border:0;}"
        "QHeaderView::section{background:#1e1e1e; border:0;}"
        );

    {
        QPixmap logo(":/images/images/Adam_PrinterBG_TM");
        if (!logo.isNull())
            m_bitmapLabel->setPixmap(logo);
    }

    // ===== Menu =====
    QMenu* menuFont   = menuBar()->addMenu(tr("&Font"));
    QMenu* menuView   = menuBar()->addMenu(tr("&Options"));

    QAction* actChooseFont = menuFont->addAction(tr("Choose workspace Font..."));
    connect(actChooseFont, &QAction::triggered, this, &PrintWindow::chooseFont);

    QAction* actClear = menuView->addAction(tr("Clear workspace"));
    connect(actClear, &QAction::triggered, this, &PrintWindow::clearText);

    menuView->addSeparator();
    QAction* actRemoveFirstChar = menuView->addAction(tr("Remove ']' first char"));
    actRemoveFirstChar->setCheckable(true);
    connect(actRemoveFirstChar, &QAction::toggled, this, &PrintWindow::toggleRemoveFirstChar);

    menuView->addSeparator();

    QAction* actExportPdf = menuView->addAction(tr("Workspace to PDF"));
    connect(actExportPdf, &QAction::triggered, this, &PrintWindow::saveAsPdf);

    QAction* actExportTxt = menuView->addAction(tr("Workspace to TXT"));
    connect(actExportTxt, &QAction::triggered, this, &PrintWindow::saveAsTxt);

    QAction* actCopyAll = menuView->addAction(tr("Copy workspace to clipboard"));
    connect(actCopyAll, &QAction::triggered, this, &PrintWindow::copyAllToClipboard);

    // ===== Centrale lay-out =====
    QWidget* c = new QWidget(this);
    auto *mainLay = new QVBoxLayout(c);
    mainLay->setContentsMargins(0,0,0,0);
    mainLay->setSpacing(0);
    m_paperLabel->setFixedWidth(525);
    m_paperLabel->setStyleSheet("background:#1e1e1e;");
    mainLay->addWidget(m_paperLabel, /*stretch*/1, Qt::AlignHCenter);
    mainLay->addWidget(m_bitmapLabel, /*stretch*/0);
    setCentralWidget(c);

    updateTopContainerMaxHeight();
    updateStatus();
}

PrintWindow::~PrintWindow()
{
    if (s_instance == this) s_instance = nullptr;
}

void PrintWindow::closeEvent(QCloseEvent* ev)
{
    QMainWindow::closeEvent(ev);
}

void PrintWindow::resizeEvent(QResizeEvent* ev)
{
    QMainWindow::resizeEvent(ev);

    updateTopContainerMaxHeight();

    if (m_bitmapLabel && !m_bitmapLabel->pixmap(Qt::ReturnByValue).isNull()) {
        QPixmap px = m_bitmapLabel->pixmap(Qt::ReturnByValue);
        m_bitmapLabel->setPixmap(px.scaled(m_bitmapLabel->size(),
                                           Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
    }
}

void PrintWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);

    // Lees direct de status uit adamnet
    // Zet de juiste afbeelding
    updatePrinterMode(g_prn_in_wp);
}

void PrintWindow::updatePrinterMode(bool isTypemachine)
{
    if (!m_bitmapLabel) return;

    QString res = isTypemachine ? ":/images/images/Adam_PrinterBG_WP.png"
                                : ":/images/images/Adam_PrinterBG_TM.png";

    QPixmap px(res);
    if (!px.isNull()) {
        m_bitmapLabel->setPixmap(px.scaled(m_bitmapLabel->size(),
                                           Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
        m_bitmapLabel->update(); // Dwing de UI om opnieuw te tekenen
    } else {
        qDebug() << "[PRN UI] Fout: Kon afbeelding niet laden:" << res;
    }
}

void PrintWindow::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_table) {
        QMainWindow::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    QAction* copyAction = menu.addAction(tr("Copy Selection to clipboard"));
    connect(copyAction, &QAction::triggered, this, &PrintWindow::copySelection);
    copyAction->setEnabled(!m_table->selectedItems().isEmpty());
    menu.exec(event->globalPos());
}

void PrintWindow::updateTopContainerMaxHeight()
{
    if (!m_paperLabel || !m_bitmapLabel) return;
    const int chrome = (menuBar() ? menuBar()->height() : 0) + (statusBar() ? statusBar()->height() : 0);
    int topMax = height() - chrome - m_bitmapLabel->height();
    if (topMax < 0) topMax = 0;
    m_paperLabel->setMaximumHeight(topMax);
}

void PrintWindow::toggleRemoveFirstChar(bool checked)
{
    m_removeFirstChar = checked;
}

void PrintWindow::applyTextFont(const QFont& f)
{
    m_monoFont = f;
    if (!m_table) return;
    m_table->setFont(m_monoFont);

    const int rowH = QFontMetrics(m_monoFont).lineSpacing() + 2;
    m_table->verticalHeader()->setDefaultSectionSize(rowH);

    m_table->setColumnWidth(0, m_holeColWidth);
    m_table->setColumnWidth(2, m_holeColWidth);
    m_table->setMinimumWidth(m_holeColWidth + m_textColMin + m_holeColWidth + 2);
}

void PrintWindow::updateStatus()
{
    if (!m_status) return;
    if (m_statLeft)  m_statLeft ->setText(QStringLiteral("Lines: %1").arg(m_totalLines));
    if (m_statMid)   m_statMid  ->setText(QStringLiteral("Bytes: %1").arg(m_totalBytes));
}

void PrintWindow::ensureLastRowVisible()
{
    if (!m_table || m_table->rowCount() == 0) return;
    if (auto* it = m_table->item(m_table->rowCount()-1, 1))
        m_table->scrollToItem(it);
}

void PrintWindow::flushCurrentLine(bool forceEmpty)
{
    if (!m_table) return;
    if (m_currentLine.isEmpty() && !forceEmpty)
        return;

    QString lineToDisplay = m_currentLine;

    // 1. ']' verwijderen
    if (m_removeFirstChar && !lineToDisplay.isEmpty() && lineToDisplay.at(0) == QLatin1Char(']')) {
        lineToDisplay.remove(0, 1);
    }

    // 2. Clean data
    lineToDisplay.replace('\n', ' ');
    lineToDisplay.replace('\r', ' ');

    const int row = m_table->rowCount();
    m_table->insertRow(row);

    // left hole
    auto* l = new QTableWidgetItem(QString());
    l->setFlags(Qt::ItemIsEnabled);
    m_table->setItem(row, 0, l);

    // center text
    auto* t = new QTableWidgetItem(lineToDisplay);
    t->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    t->setToolTip(lineToDisplay);

    m_table->setItem(row, 1, t);

    // right hole
    auto* r = new QTableWidgetItem(QString());
    r->setFlags(Qt::ItemIsEnabled);
    m_table->setItem(row, 2, r);

    ensureLastRowVisible();

    m_currentLine.clear();
    ++m_totalLines;
    updateStatus();
}

void PrintWindow::copySelection()
{
    if (!m_table || m_table->selectedItems().isEmpty()) return;

    QModelIndexList selection = m_table->selectionModel()->selectedIndexes();

    if (selection.isEmpty()) return;

    std::sort(selection.begin(), selection.end(), [](const QModelIndex& a, const QModelIndex& b) {
        if (a.row() != b.row()) return a.row() < b.row();
        return a.column() < b.column();
    });

    QString text;
    int currentRow = -1;

    for (const QModelIndex& index : selection) {
        if (index.row() != currentRow) {
            if (currentRow != -1) text += QLatin1Char('\n');
            currentRow = index.row();
        } else {
            text += QLatin1Char('\t');
        }
        text += index.data().toString();
    }

    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setText(text);
}

void PrintWindow::appendPrinterBytes(const QByteArray& bytes)
{
    QMutexLocker lk(&m_appendMutex);

    m_totalBytes += std::uint64_t(bytes.size());
    updateStatus();

    for (unsigned char raw_c : bytes) {
        if (raw_c == 0x8D) continue; // hardware wrap negeren

        unsigned char b = raw_c & 0x7F;

        qDebug() << "[PRN CHAR]:" << b << "[" << (char)b << "]" ;

        if (b == 0x0D || b == 0x0A) {
            int len = m_currentLine.length();

            int remainder = len % 80;
            bool isWrapPoint = (remainder == 0 || remainder == 1 || remainder == 79);

            if (len >= 79 && isWrapPoint) {
                continue;
            }

            flushCurrentLine();
            m_atLineStart = true;
            continue;
        }

        if (b == 0x0C) { // FF
            flushCurrentLine(true);
            m_currentLine = QStringLiteral("-----[Page Break]-----");
            flushCurrentLine();
            flushCurrentLine(true);
            m_atLineStart = true;
            continue;
        }

        if (b == 0x09) { // TAB
            m_currentLine.append(QString(4, QLatin1Char(' ')));
            m_atLineStart = false;
            continue;
        }

        if (b >= 0x20 && b <= 0x7E) {
            m_currentLine.append(QChar::fromLatin1(char(b)));
            m_atLineStart = false;
        }
    }
}

void PrintWindow::saveAsPdf()
{
    const QString def = QString("adam_print_%1.pdf")
    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Opslaan als PDF"), def, "PDF (*.pdf)");
    if (path.isEmpty()) return;

    QString all;
    all.reserve(m_table->rowCount() * 32);
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (auto* it = m_table->item(r, 1)) {
            all += it->text();
        }
        if (r + 1 < m_table->rowCount()) all += QLatin1Char('\n');
    }

    QTextDocument doc;
    doc.setDefaultFont(m_monoFont);
    doc.setPlainText(all);

    QPrinter pr(QPrinter::HighResolution);
    pr.setOutputFormat(QPrinter::PdfFormat);
    pr.setOutputFileName(path);
    doc.print(&pr);
}

void PrintWindow::saveAsTxt()
{
    const QString def = QString("adam_print_%1.txt")
    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Opslaan als TXT..."), def, tr("Tekstbestanden (*.txt)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;

    QTextStream ts(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    ts.setEncoding(QStringConverter::Utf8);
#endif

    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (auto* it = m_table->item(r, 1)) ts << it->text();
        if (r + 1 < m_table->rowCount())     ts << QLatin1Char('\n');
    }
    ts.flush();
}

void PrintWindow::copyAllToClipboard()
{
    if (!m_table) return;

    QString allText;
    allText.reserve(m_table->rowCount() * 40);

    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (auto* it = m_table->item(r, 1)) {
            allText += it->text();
        }
        if (r + 1 < m_table->rowCount()) {
            allText += QLatin1Char('\n');
        }
    }

    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setText(allText);
}

void PrintWindow::chooseFont()
{
    bool ok=false;
    QFont f = QFontDialog::getFont(&ok, m_monoFont, this, tr("Choose Font"));
    if (ok) applyTextFont(f);
}

// ---------------- Bitmap ----------------
void PrintWindow::setBitmap(const QImage& img)
{
    if (!m_bitmapLabel) return;

    if (img.isNull()) { clearBitmap(); return; }

    QPixmap px = QPixmap::fromImage(img);
    m_bitmapLabel->setPixmap(px.scaled(m_bitmapLabel->size(),
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
}

void PrintWindow::clearBitmap()
{
    if (m_bitmapLabel) m_bitmapLabel->clear();
}

void PrintWindow::clearText()
{
    QMutexLocker lk(&m_appendMutex);

    if (m_table) m_table->setRowCount(0);
    m_currentLine.clear();
    m_atLineStart = true;

    m_totalBytes = 0;
    m_totalLines = 0;
    updateStatus();
}

