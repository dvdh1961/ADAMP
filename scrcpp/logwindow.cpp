#include "logwindow.h"
#include <QVBoxLayout>
#include <QScrollBar>
#include <QDateTime>
#include <QtGlobal>
#include <QFontDatabase>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QPainter>
#include <QStyledItemDelegate>

class LogZebraDelegate : public QStyledItemDelegate
{
public:
    LogZebraDelegate(const QColor& light, const QColor& dark, const QColor& text, QObject* parent=nullptr)
        : QStyledItemDelegate(parent), m_light(light), m_dark(dark), m_text(text) {}

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        const bool odd = (idx.row() % 2) == 1;
        p->fillRect(opt.rect, odd ? m_dark : m_light);

        QStyleOptionViewItem o(opt);
        o.backgroundBrush = Qt::NoBrush;
        o.palette.setColor(QPalette::Text, m_text);
        o.palette.setColor(QPalette::WindowText, m_text);
        QStyledItemDelegate::paint(p, o, idx);
    }

private:
    QColor m_light, m_dark, m_text; // <-- Nieuwe member
};

LogWidget* LogWidget::s_instance = nullptr;
QMutex     LogWidget::s_mutex;

LogWidget::LogWidget(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("ADAM+ Debug logger");

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

    m_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_table->setFont(m_monoFont);

    const int rowH = QFontMetrics(m_monoFont).lineSpacing() + 2;
    m_table->verticalHeader()->setDefaultSectionSize(rowH);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    m_table->setItemDelegateForColumn(0, new LogZebraDelegate(m_rowLight, m_rowDark, m_textColor, m_table));
    m_table->setStyleSheet(QString(
                               "QTableView{border:0px solid #C0C0C0; background: rgb(%1,%2,%3);}"
                               "QTableCornerButton::section{background:#1e1e1e; border:0;}"
                               "QHeaderView::section{background:#1e1e1e; border:1;}"
                               ).arg(m_rowLight.red()).arg(m_rowLight.green()).arg(m_rowLight.blue()));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4,4,4,4);
    lay->addWidget(m_table);
    setLayout(lay);

    connect(this, &LogWidget::appendRequested,
            this, &LogWidget::onAppendRequested);
}

void LogWidget::bindTo(LogWidget *inst)
{
    QMutexLocker locker(&s_mutex);
    s_instance = inst;
}

void LogWidget::log(const QString &line)
{
    QMutexLocker locker(&s_mutex);
    if (!s_instance)
        return;

    emit s_instance->appendRequested(line);
}

void LogWidget::onAppendRequested(const QString &line)
{
    if (!m_table) return;

    const QString stamp =
        QDateTime::currentDateTime().toString("[HH:mm:ss.zzz] ");

    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* item = new QTableWidgetItem(stamp + line);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    item->setFlags(Qt::ItemIsEnabled);
    m_table->setItem(row, 0, item);

    if (auto* it = m_table->item(row, 0)) {
        m_table->scrollToItem(it);
    }
}

void LogWidget::clear()
{
    if (m_table) {
        m_table->setRowCount(0);
        m_table->scrollToTop();
    }
}

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

    LogWidget::log(QString("%1: %2").arg(prefix, msg));

    if (type == QtFatalMsg) {
        abort();
    }
}

void LogWidget::installQtHandler()
{
    qInstallMessageHandler(qtLogForwarder);
}
