#include "logwindow.h"
#include <QVBoxLayout>
#include <QScrollBar>
#include <QDateTime>
#include <QtGlobal>

// static members
LogWidget* LogWidget::s_instance = nullptr;
QMutex     LogWidget::s_mutex;

// ---------- ctor ----------
LogWidget::LogWidget(QWidget *parent)
    : QWidget(parent),
    m_editor(new QPlainTextEdit(this))
{
    // basis UI voor log output
    m_editor->setReadOnly(true);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    setWindowTitle("ADAM+ Debug logger");

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4,4,4,4);
    lay->addWidget(m_editor);
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
    // timestamp zoals EmulTwo vibe
    const QString stamp =
        QDateTime::currentDateTime().toString("[HH:mm:ss.zzz] ");

    m_editor->appendPlainText(stamp + line);

    // auto-scroll naar onder
    auto *bar = m_editor->verticalScrollBar();
    bar->setValue(bar->maximum());
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
