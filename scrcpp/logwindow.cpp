#include "logwindow.h"
#include <QVBoxLayout>
#include <QScrollBar>
#include <QDateTime>
#include <QtGlobal>
#include <QFontDatabase>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QPainter>         // Nodig voor QPainter
#include <QStyledItemDelegate> // Nodig voor QStyledItemDelegate

// ===== Delegate voor 1-koloms log look (gebaseerd op PrintWindow's TextZebraDelegate) =====
class LogZebraDelegate : public QStyledItemDelegate
{
public:
    // **WIJZIGING 1:** Voeg de tekstkleur 'text' toe aan de constructor
    LogZebraDelegate(const QColor& light, const QColor& dark, const QColor& text, QObject* parent=nullptr)
        : QStyledItemDelegate(parent), m_light(light), m_dark(dark), m_text(text) {}

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        const bool odd = (idx.row() % 2) == 1;
        p->fillRect(opt.rect, odd ? m_dark : m_light);

        // Teken de tekst bovenop de achtergrond
        QStyleOptionViewItem o(opt);
        o.backgroundBrush = Qt::NoBrush; // Zorgt ervoor dat de delegate de achtergrond beheert
        // **WIJZIGING 2:** Gebruik de opgeslagen tekstkleur m_text
        o.palette.setColor(QPalette::Text, m_text);
        o.palette.setColor(QPalette::WindowText, m_text);
        QStyledItemDelegate::paint(p, o, idx);
    }

private:
    QColor m_light, m_dark, m_text; // <-- Nieuwe member
};

// static members
LogWidget* LogWidget::s_instance = nullptr;
QMutex     LogWidget::s_mutex;

// ---------- ctor ----------
LogWidget::LogWidget(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("ADAM+ Debug logger");

    // Maak de QTableWidget aan (vervangt QPlainTextEdit)
    m_table = new QTableWidget(this);
    m_table->setColumnCount(1);
    m_table->horizontalHeader()->setVisible(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(false);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Monospace font instellen (zoals in PrintWindow)
    m_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_table->setFont(m_monoFont);

    // Bepaal de rijhoogte op basis van het font
    const int rowH = QFontMetrics(m_monoFont).lineSpacing() + 2;
    m_table->verticalHeader()->setDefaultSectionSize(rowH);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    // **WIJZIGING 3:** Geef m_textColor mee aan de LogZebraDelegate constructor
    m_table->setItemDelegateForColumn(0, new LogZebraDelegate(m_rowLight, m_rowDark, m_textColor, m_table));
    // Stel de achtergrond van de tabel zelf in (bijvoorbeeld op de lichte kleur)
    m_table->setStyleSheet(QString(
                               "QTableView{border:0px solid #C0C0C0; background: rgb(%1,%2,%3);}"
                               "QTableCornerButton::section{background:#1e1e1e; border:0;}"
                               "QHeaderView::section{background:#1e1e1e; border:1;}"
                               ).arg(m_rowLight.red()).arg(m_rowLight.green()).arg(m_rowLight.blue()));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4,4,4,4);
    lay->addWidget(m_table);
    setLayout(lay);

    // als iemand vanuit een andere thread wil loggen, komt dat hier terecht
    connect(this, &LogWidget::appendRequested,
            this, &LogWidget::onAppendRequested);
}

// ---------- singleton binding ----------
void LogWidget::bindTo(LogWidget *inst)
{
    QMutexLocker locker(&s_mutex);
    s_instance = inst;
}

// ---------- manueel loggen ----------
void LogWidget::log(const QString &line)
{
    QMutexLocker locker(&s_mutex);
    if (!s_instance)
        return;

    // dispatch naar GUI-thread
    emit s_instance->appendRequested(line);
}

// ---------- GUI-thread append ----------
void LogWidget::onAppendRequested(const QString &line)
{
    if (!m_table) return;

    // timestamp zoals EmulTwo vibe
    const QString stamp =
        QDateTime::currentDateTime().toString("[HH:mm:ss.zzz] ");

    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* item = new QTableWidgetItem(stamp + line);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    item->setFlags(Qt::ItemIsEnabled); // Alleen lezen
    m_table->setItem(row, 0, item);

    // auto-scroll naar onder
    if (auto* it = m_table->item(row, 0)) {
        m_table->scrollToItem(it);
    }
}

// Nieuwe slot: Cleart de QTableWidget
void LogWidget::clear()
{
    if (m_table) {
        m_table->setRowCount(0);
        // Optioneel: scroll naar de top
        m_table->scrollToTop();
    }
}

// ---------- Qt global message handler ----------
static void qtLogForwarder(QtMsgType type,
                           const QMessageLogContext &ctx,
                           const QString &msg)
{
    Q_UNUSED(ctx);

    QString prefix;
    switch (type) {
    case QtDebugMsg:    prefix = "DBG"; break;
    case QtInfoMsg:     prefix = "INF"; break;
    case QtWarningMsg:  prefix = "WRN"; break;
    case QtCriticalMsg: prefix = "CRT"; break;
    case QtFatalMsg:    prefix = "FTL"; break;
    }

    // stuur door naar LogWidget::log(...)
    LogWidget::log(QString("%1: %2").arg(prefix, msg));

    if (type == QtFatalMsg) {
        abort();
    }
}

void LogWidget::installQtHandler()
{
    qInstallMessageHandler(qtLogForwarder);
}
