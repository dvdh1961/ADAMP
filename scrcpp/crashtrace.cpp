#include "crashtrace.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QThread>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {
QMutex g_mutex;
QString g_tracePath;
QByteArray g_tracePathLocal8Bit;
bool g_installed = false;
bool g_previousRunCrashed = false;

QString nowString()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
}

QString buildTracePath()
{
    QString base = QCoreApplication::applicationDirPath();
    if (base.isEmpty())
        base = QDir::currentPath();
    QDir dir(base);
    if (!dir.exists("logs"))
        dir.mkpath("logs");
    return dir.filePath("logs/MSXP_EMU_crash_trace.txt");
}

void appendRawLine(const char* line)
{
    if (g_tracePathLocal8Bit.isEmpty())
        return;
    FILE* f = std::fopen(g_tracePathLocal8Bit.constData(), "ab");
    if (!f)
        return;
    std::fputs(line, f);
    std::fputs("\n", f);
    std::fflush(f);
    std::fclose(f);
}

void signalHandler(int sig)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "FATAL SIGNAL %d caught by Step62 crash trace", sig);
    appendRawLine(buf);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void terminateHandler()
{
    appendRawLine("std::terminate caught by Step62 crash trace");
    std::abort();
}

#ifdef Q_OS_WIN
LONG WINAPI windowsExceptionHandler(EXCEPTION_POINTERS* info)
{
    char buf[512];
    const unsigned long code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    const void* addr = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
    std::snprintf(buf, sizeof(buf), "WINDOWS SEH EXCEPTION code=0x%08lX address=%p caught by Step62 crash trace", code, addr);
    appendRawLine(buf);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace

namespace CrashTrace {

QString traceFilePath()
{
    if (g_tracePath.isEmpty())
        g_tracePath = buildTracePath();
    return g_tracePath;
}

static bool computePreviousRunCrashedFromExistingFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QByteArray all = f.readAll();
    const int lastBegin = all.lastIndexOf("SESSION BEGIN");
    if (lastBegin < 0)
        return false;
    const int lastClean = all.lastIndexOf("SESSION CLEAN EXIT");
    return lastClean < lastBegin;
}

bool previousRunCrashed()
{
    return g_previousRunCrashed;
}

void mark(const QString& message)
{
    QMutexLocker locker(&g_mutex);
    const QString path = traceFilePath();
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;

    QTextStream ts(&f);
    ts << nowString()
       << " [tid=" << reinterpret_cast<quintptr>(QThread::currentThreadId()) << "] "
       << message << '\n';
    ts.flush();
}

void cleanExit()
{
    mark("SESSION CLEAN EXIT");
}

void install()
{
    if (g_installed)
        return;
    g_installed = true;

    g_tracePath = buildTracePath();
    g_previousRunCrashed = computePreviousRunCrashedFromExistingFile(g_tracePath);
    g_tracePathLocal8Bit = QFile::encodeName(g_tracePath);

    mark("============================================================");
    mark("SESSION BEGIN - Step62 crash trace installed");
    mark(QString("Trace file: %1").arg(g_tracePath));
    mark(QString("Previous run crashed: %1").arg(g_previousRunCrashed ? "YES" : "NO"));

    std::set_terminate(terminateHandler);
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGFPE, signalHandler);
    std::signal(SIGILL, signalHandler);
#ifdef SIGTERM
    std::signal(SIGTERM, signalHandler);
#endif
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(windowsExceptionHandler);
#endif
}

} // namespace CrashTrace
