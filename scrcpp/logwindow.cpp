#include "logwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <QScrollBar>
#include <QTableWidgetItem>
#include <QGuiApplication>
#include <QClipboard>
#include <QMenu>
#include <QAction>
#include <QToolButton>
#include <QPushButton>
#include <QEvent>
#include <QKeyEvent>
#include <QMutexLocker>
#include <QtGlobal>
#include <QFontDatabase>

//extern char g_an_ring[8192][96];
//extern volatile unsigned g_an_w;
//extern void AN_RING(const char* s);

// ------------------------------------------------------------
// Static
// ------------------------------------------------------------
LogWidget* LogWidget::s_instance = nullptr;
QMutex     LogWidget::s_mutex;

static QString g_prefixFromQtMsgType(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DBG";
    case QtInfoMsg:     return "INF";
    case QtWarningMsg:  return "WRN";
    case QtCriticalMsg: return "ERR";
    case QtFatalMsg:    return "FTL";
    }
    return "LOG";
}

// ------------------------------------------------------------
// LogWidget
// ------------------------------------------------------------
LogWidget::LogWidget(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("ADAM+ Debug logger");

    // Tag colors (must match the line coloring logic below)
    m_tagColors.insert("RESET",   QColor("#ff4400"));
    m_tagColors.insert("SOUND",   QColor("#00ff44"));
    m_tagColors.insert("CTRL",    QColor("#0099ff"));
    m_tagColors.insert("BIOS",    QColor("#ffcc00"));
    m_tagColors.insert("CPM",     QColor("#00ff99"));
    m_tagColors.insert("PORT60",  QColor("#00cccc"));
    m_tagColors.insert("DSK",     QColor("#aaaa00"));
    m_tagColors.insert("ADAMNET", QColor("#00aa99"));
    m_tagColors.insert("BP",      QColor("#ffaaaa"));
    m_tagColors.insert("UI",      QColor("#00aa00"));

    m_table = new QTableWidget(this);

    // 1. Laad het lettertype uit je resources of lokale map
    // Pas het pad aan naar waar jouw .ttf staat (bijv. ":/fonts/mijnfont.ttf")
    int fontId = QFontDatabase::addApplicationFont(":/fonts/fonts/luculent.ttf");

    QString family;
    if (fontId != -1) {
        family = QFontDatabase::applicationFontFamilies(fontId).at(0);
       // qDebug() << "[UI] Custom font geladen:" << family;
    } else {
        qDebug() << "[UI] Kon custom font niet laden, fallback naar Roboto";
        family = "Roboto";
    }

    QFont menuFont(family, 10);
    menuFont.setBold(false);

    // Pas het toe op de menubalk zelf
    m_table->setFont(menuFont);

    //QFont tableFont("Roboto", 10);     // of 9, 10, 11… wat jij mooi vindt
   // m_table->setFont(tableFont);
    m_table->setColumnCount(1);
    m_table->horizontalHeader()->setVisible(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(false);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->installEventFilter(this);
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested,
            this, &LogWidget::onTableContextMenu);

    // Layout
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    buildTopBar();
    // buildTopBar adds widgets to the layout via parent layout lookup
    // (we re-find it here to keep code straightforward)
    // NOTE: buildTopBar creates a child widget which is already parented to this,
    // so we can just add it as the first item.
    QWidget *topBar = findChild<QWidget*>("logTopBar");
    if (topBar) root->addWidget(topBar);

    root->addWidget(m_table);

    // Thread-safe append via signal/slot
    connect(this, &LogWidget::appendRequested,
            this, &LogWidget::onAppendRequested,
            Qt::QueuedConnection);
}

// void LogWidget::dumpAdamNetTrace()
// {
//     LogWidget::log("===== ADAMNET TRACE DUMP =====");

//     unsigned end = g_an_w;
//     unsigned start = (end > 50) ? (end - 50) : 0;

//     for (unsigned i = start; i < end; ++i)
//     {
//         const char* s = g_an_ring[i & 8191];
//         if (s && *s)
//             LogWidget::log(QString("[ANR] %1").arg(s));
//     }

//     LogWidget::log("===== END ADAMNET TRACE =====");
// }

void LogWidget::buildTopBar()
{
    QFont btnFont("Roboto", 10);

    QWidget *bar = new QWidget(this);
    bar->setObjectName("logTopBar");

    QHBoxLayout *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    // ---------------------------------------
    // DEFAULT filter (voor unknown tags)
    // ---------------------------------------
    QString defaultTag   = "DEFAULT";
    QString defaultColor = "#dddddd";   // neutraal Fusion-kleur

    {
        QToolButton *btn = new QToolButton(this);
        btn->setText(defaultTag);
        btn->setCheckable(true);
        btn->setFont(btnFont);
        btn->setProperty("tag", defaultTag);
        btn->setProperty("origColor", defaultColor);

        // originele Fusion-style achtergrond
        btn->setStyleSheet(
            QString("background-color:%1; color:black;")
                .arg(defaultColor)
            );

        layout->addWidget(btn);

        m_filterButtons.insert(defaultTag, btn);

        connect(btn, &QToolButton::toggled, this, [this, btn](bool checked)
                {
                    QString tag  = btn->property("tag").toString();
                    QString orig = btn->property("origColor").toString();

                    if (checked)
                    {
                        // zwart/wit tone
                        btn->setStyleSheet(
                            "background-color:#c0c0c0;"
                            "color:#606060;"
                            );
                        m_filteredTags.insert(tag);
                    }
                    else
                    {
                        btn->setStyleSheet(
                            QString("background-color:%1; color:black;")
                                .arg(orig)
                            );
                        m_filteredTags.remove(tag);
                    }

                    rebuildFilterButtonTexts();
                    applyFiltersToAllRows();
                });
    }

    // ---------------------------------------
    // Overige tags (in volgorde)
    // ---------------------------------------
    struct FilterDef { QString tag; QString color; };

    QVector<FilterDef> defs = {
        {"UI",      "#00aa00"},
        {"CTRL",    "#0099ff"},
        {"ADAMNET", "#00aa99"},
        {"DSK",     "#aaaa00"},
        {"CPM",     "#00ff99"},
        {"BIOS",    "#ffcc00"},
        {"PORT60",  "#00cccc"},
        {"SOUND",   "#00ff44"},
        {"RESET",   "#ff4400"},
        {"BP",      "#ffaaaa"}
    };

    for (const auto &f : defs)
    {
        QToolButton *btn = new QToolButton(this);
        btn->setText(f.tag);
        btn->setCheckable(true);
        btn->setFont(btnFont);
        btn->setProperty("tag", f.tag);
        btn->setProperty("origColor", f.color);

        btn->setStyleSheet(
            QString("background-color:%1; color:black;")
                .arg(f.color)
            );

        layout->addWidget(btn);
        m_filterButtons.insert(f.tag, btn);

        connect(btn, &QToolButton::toggled, this, [this, btn](bool checked)
                {
                    QString tag  = btn->property("tag").toString();
                    QString orig = btn->property("origColor").toString();

                    if (checked)
                    {
                        btn->setStyleSheet(
                            "background-color:#c0c0c0;"
                            "color:#606060;"
                            );
                        m_filteredTags.insert(tag);
                    }
                    else
                    {
                        btn->setStyleSheet(
                            QString("background-color:%1; color:black;")
                                .arg(orig)
                            );
                        m_filteredTags.remove(tag);
                    }

                    rebuildFilterButtonTexts();
                    applyFiltersToAllRows();
                });
    }

    // ---------------------------------------
    // CLEAR button (Fusion-style)
    // ---------------------------------------
    m_clearBtn = new QPushButton("Clear", this);
    m_clearBtn->setFont(btnFont);
    m_clearBtn->setStyleSheet(
        "background-color:#ff4444; color:black;"
        );

    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        clear();
    });

    // // ---------------------------------------
    // // TRACE button (Fusion-style)
    // // ---------------------------------------
    // m_traceBtn = new QPushButton("Trace", this);
    // m_traceBtn->setFont(btnFont);
    // m_traceBtn->setStyleSheet(
    //     "background-color:#ffffff; color:black;"
    //     );

    // connect(m_traceBtn, &QPushButton::clicked, this, [this]() {
    //     dumpAdamNetTrace();
    // });


    // ---------------------------------------
    // ALL OFF (alles tonen, filters uit)
    // ---------------------------------------
    QPushButton *btnAllOff = new QPushButton("ON", this);
    btnAllOff->setFont(btnFont);
    btnAllOff->setFixedWidth(50);
    btnAllOff->setStyleSheet("background-color:#ffffff; color:black;");

    connect(btnAllOff, &QPushButton::clicked, this, [this]() {

        m_filteredTags.clear();

        for (auto it = m_filterButtons.begin(); it != m_filterButtons.end(); ++it)
        {
            QToolButton *btn = it.value();
            if (!btn) continue;

            QString orig = btn->property("origColor").toString();
            btn->blockSignals(true);
            btn->setChecked(false);
            btn->setStyleSheet(
                QString("background-color:%1; color:black;").arg(orig)
                );
            btn->blockSignals(false);
        }

        rebuildFilterButtonTexts();
        applyFiltersToAllRows();
    });

    layout->addWidget(btnAllOff);

    // ---------------------------------------
    // ALL ON (alles verbergen, filters AAN)
    // ---------------------------------------
    QPushButton *btnAllOn = new QPushButton("OFF", this);
    btnAllOn->setFont(btnFont);
    btnAllOn->setFixedWidth(50);
    btnAllOn->setStyleSheet("background-color:#ffffff; color:black;");

    connect(btnAllOn, &QPushButton::clicked, this, [this]() {

        m_filteredTags.clear();

        for (auto it = m_filterButtons.begin(); it != m_filterButtons.end(); ++it)
        {
            QString tag = it.key();
            QToolButton *btn = it.value();
            if (!btn) continue;

            m_filteredTags.insert(tag);

            btn->blockSignals(true);
            btn->setChecked(true);
            btn->setStyleSheet(
                "background-color:#c0c0c0;"
                "color:#606060;"
                );
            btn->blockSignals(false);
        }

        rebuildFilterButtonTexts();
        applyFiltersToAllRows();
    });

    layout->addWidget(btnAllOn);


    layout->addWidget(m_clearBtn);
    //layout->addWidget(m_traceBtn);

    // ---------------------------------------
    // Add bar to layout
    // ---------------------------------------
    if (auto *mainLayout = qobject_cast<QVBoxLayout*>(this->layout()))
        mainLayout->insertWidget(0, bar);
}

void LogWidget::bindTo(LogWidget *inst)
{
    QMutexLocker lk(&s_mutex);
    s_instance = inst;
}

void LogWidget::log(const QString &line)
{
    QMutexLocker lk(&s_mutex);
    if (!s_instance) return;
    emit s_instance->appendRequested(line);
}

void LogWidget::onAppendRequested(const QString &line)
{
    static int counter = 0;

    const QString scnt  = QString("[%1]").arg(counter, 4, 10, QChar('0'));
    counter++;

    const QString stamp =
        QDateTime::currentDateTime().toString("[HH:mm:ss.zzz]");

    QColor col = m_textColor;
    const QString tag = detectTag(line, &col);

    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto *item = new QTableWidgetItem(scnt + " - " + stamp + " - " + line);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    // Store tag for filtering
    item->setData(Qt::UserRole, tag);

    // Foreground
    item->setForeground(QBrush(col));

    // Alternating row background (dark theme)
    const QColor bg = (row % 2 == 0) ? m_rowDark : m_rowLight;
    item->setBackground(QBrush(bg));

    m_table->setItem(row, 0, item);

    applyFilterToRow(row);

    // Autoscroll to bottom if already at bottom-ish
    if (auto *sb = m_table->verticalScrollBar()) {
        const bool nearBottom = (sb->value() >= sb->maximum() - 4);
        if (nearBottom) sb->setValue(sb->maximum());
    }
}

QString LogWidget::detectTag(const QString &line, QColor *outColor) const
{
    // Keep this in sync with the old behavior (color selection)
    auto set = [&](const QString &tagKey, const QString &needle, const QColor &color) -> bool {
        if (line.contains(needle)) {
            if (outColor) *outColor = color;
            return true;
        }
        return false;
    };

    // Exact matches from previous logic
    if (set("RESET",   "[RESET]",   QColor("#ff4400"))) return "RESET";
    if (set("SOUND",   "[SOUND]",   QColor("#00ff44"))) return "SOUND";
    if (set("CTRL",    "[CTRL]",    QColor("#0099ff"))) return "CTRL";
    if (set("BIOS",    "[BIOS]",    QColor("#ffcc00"))) return "BIOS";
    if (set("CPM",     "[CPM]",     QColor("#00ff99"))) return "CPM";
    if (set("PORT60",  "[PORT60]",  QColor("#00cccc"))) return "PORT60";
    if (set("DSK",     "[DSK]",     QColor("#aaaa00"))) return "DSK";
    if (set("ADAMNET", "[ADAMNET]", QColor("#00aa99"))) return "ADAMNET";
    if (set("BP",      "[BP]",      QColor("#ffaaaa"))) return "BP";

    // Separator / headline-ish
    if (line.contains("---")) {
        if (outColor) *outColor = QColor("#ffaa00");
        return "SEP";
    }

    // UI (but avoid the --- case already handled)
    if (line.contains("[UI]")) {
        if (outColor) *outColor = QColor("#00aa00");
        return "UI";
    }

    if (outColor) *outColor = m_textColor;
    return "OTHER";
}

void LogWidget::applyFilterToRow(int row)
{
    auto *it = m_table->item(row, 0);
    if (!it) return;

    const QString tag = it->data(Qt::UserRole).toString();

    bool hide = false;

    // filter bekende tags
    if (m_filteredTags.contains(tag))
        hide = true;

    // filter DEFAULT (unknown tags)
    if (m_filteredTags.contains("DEFAULT") && tag == "OTHER")
        hide = true;

    m_table->setRowHidden(row, hide);
}

void LogWidget::applyFiltersToAllRows()
{
    const int rows = m_table->rowCount();
    for (int r = 0; r < rows; ++r) {
        applyFilterToRow(r);
    }
}

void LogWidget::rebuildFilterButtonTexts()
{
    for (auto it = m_filterButtons.begin(); it != m_filterButtons.end(); ++it)
    {
        const QString tag = it.key();
        QToolButton *btn = it.value();
        if (!btn) continue;

        // tekst blijft ALTIJD gewoon tag
        btn->setText(tag);
    }
}

void LogWidget::onFilterToggled(bool checked)
{
    auto *btn = qobject_cast<QToolButton*>(sender());
    if (!btn) return;

    const QString tag = btn->property("tag").toString();
    if (tag.isEmpty()) return;

    if (checked) m_filteredTags.insert(tag);
    else         m_filteredTags.remove(tag);

    rebuildFilterButtonTexts();
    applyFiltersToAllRows();
}

void LogWidget::clear()
{
    m_table->setRowCount(0);
}

void LogWidget::installQtHandler()
{
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
        Q_UNUSED(ctx);
        const QString prefix = g_prefixFromQtMsgType(type);
        LogWidget::log(QString("%1: %2").arg(prefix, msg));
        if (type == QtFatalMsg) abort();
    });
}

bool LogWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_table && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent*>(event);
        if (key->matches(QKeySequence::Copy)) {
            QList<QTableWidgetItem*> items = m_table->selectedItems();
            QString text;
            for (auto *it : items) {
                if (!it) continue;
                text += it->text();
                text += "\n";
            }
            if (!text.isEmpty())
                QGuiApplication::clipboard()->setText(text.trimmed());
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void LogWidget::onTableContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *actCopy  = menu.addAction("Copy selection");
    QAction *actCopyAll = menu.addAction("Copy ALL");
    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    // ------------------------------------------------------
    // COPY SELECTED
    // ------------------------------------------------------
    if (chosen == actCopy) {
        QList<QTableWidgetItem*> items = m_table->selectedItems();
        QString text;
        for (auto *it : items) {
            if (!it) continue;
            text += it->text();
            text += "\n";
        }
        if (!text.isEmpty()) QGuiApplication::clipboard()->setText(text.trimmed());
        return;
        }
    // ------------------------------------------------------
    // COPY ALL
    // ------------------------------------------------------
    if (chosen == actCopyAll)
        {
        QString text;

        const int rows = m_table->rowCount();
        for (int r = 0; r < rows; ++r)
        {
            if (auto *item = m_table->item(r, 0))
                text += item->text() + "\n";
        }

        if (!text.isEmpty())
            QGuiApplication::clipboard()->setText(text.trimmed());

        return;
        }

}
