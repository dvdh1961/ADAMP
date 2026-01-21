// logwindow.h
#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QWidget>
#include <QMutex>
#include <QTableWidget>
#include <QFont>
#include <QColor>
#include <QHash>
#include <QSet>

class QToolButton;
class QPushButton;

class LogWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LogWidget(QWidget *parent = nullptr);
    ~LogWidget() override = default;

    // Singleton-style convenience (used from anywhere in the app)
    static void bindTo(LogWidget *inst);
    static void log(const QString &line);

    void clear();
    static void installQtHandler();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void appendRequested(const QString &line);

private slots:
    void onAppendRequested(const QString &line);
    void onTableContextMenu(const QPoint &pos);
    void onFilterToggled(bool checked);
    //void dumpAdamNetTrace();

private:
    void buildTopBar();
    void rebuildFilterButtonTexts();
    void applyFiltersToAllRows();
    QString detectTag(const QString &line, QColor *outColor = nullptr) const;
    void applyFilterToRow(int row);

private:
    static LogWidget *s_instance;
    static QMutex     s_mutex;

    QTableWidget *m_table = nullptr;

    // Tag -> color (same colors as log line coloring)
    QHash<QString, QColor> m_tagColors;

    // Tag -> filter button
    QHash<QString, QToolButton*> m_filterButtons;

    // Tags currently filtered out (hidden)
    QSet<QString> m_filteredTags;

    QPushButton *m_clearBtn = nullptr;
    //QPushButton *m_traceBtn = nullptr;

    QColor   m_rowLight  = QColor(0x1D,0x1D,0x1D);
    QColor   m_rowDark   = QColor(0x2D,0x2D,0x2D);
    QColor   m_textColor = QColor(0xA5,0xA3,0xAE);
};

#endif // LOGWINDOW_H
