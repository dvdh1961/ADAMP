// logwindow.h
#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QWidget>
#include <QMutex>
#include <QTableWidget>
#include <QFont>
#include <QColor>

class LogWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LogWidget(QWidget *parent = nullptr);
    static void bindTo(LogWidget *inst);
    static void log(const QString &line);
    static void installQtHandler();

public slots:
    void clear();

signals:
    void appendRequested(const QString &line);

private slots:
    void onAppendRequested(const QString &line);

private:
    static LogWidget *s_instance;
    static QMutex     s_mutex;

    QTableWidget *m_table = nullptr;
    QFont         m_monoFont;

    QColor   m_rowLight = QColor(0x1D,0X1D,0X1D);
    QColor   m_rowDark  = QColor(0X2D,0X2D,0X2D);
    QColor   m_textColor = QColor(0XA5,0XA3,0XAE);
};

#endif
