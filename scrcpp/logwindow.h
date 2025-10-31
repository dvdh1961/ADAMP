// logwidget.h
#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QMutex>

class LogWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LogWidget(QWidget *parent = nullptr);

    // toegang tot de editor voor manueel toevoegen (optioneel)
    QPlainTextEdit* editor() const { return m_editor; }

    // globale singleton-binding zodat we overal kunnen loggen
    static void bindTo(LogWidget *inst);

    // roep deze op om een regel manueel te loggen
    static void log(const QString &line);

    // installeer globale Qt message handler (qDebug/qWarning/...),
    // aan te roepen eenmaal in main() nadat bindTo() gebeurd is
    static void installQtHandler();

signals:
    // wordt gebruikt om vanuit eender welke thread veilig in de GUI-thread te schrijven
    void appendRequested(const QString &line);

private slots:
    void onAppendRequested(const QString &line);

private:
    static LogWidget *s_instance;
    static QMutex     s_mutex;

    QPlainTextEdit *m_editor;
};

#endif // LOGWIDGET_H
