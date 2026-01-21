#include "mainwindow.h"
#include "customfiledialog.h"
#include "colecocontroller.h"
#include "screenwidget.h"
#include "inputwidget.h"
#include "logwindow.h"
#include "debuggerwindow.h"
#include "disasm_bridge.h"
#include "cartridgeinfowindow.h"
#include "ntablewindow.h"
#include "patternwindow.h"
#include "spritewindow.h"
#include "settingswindow.h"
#include "hardwarewindow.h"
#include "coleco.h"
#include "joypadwindow.h"
#include "printwindow.h"
#include "simplejoystick.h"

// Qt includes
#include <QMenuBar>
#include <QSplitter>
#include <QTextEdit>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QDebug>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QSettings>
#include <QStyle>
#include <QLayout>
#include <QStatusBar>
#include <QLabel>
#include <QTimer>
#include <QSizePolicy>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QFont>
#include <QMap>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QProgressDialog>


// --- Debugger / Breakpoints / Symbols implementations ---

void MainWindow::setDebugger(DebuggerWindow *debugger)
{
    m_debugWin        = debugger;
}

void MainWindow::onOpenDebugger()
{
    if (!m_debugWin) return;
    m_isPaused = true;
    QMetaObject::invokeMethod(m_colecoController, "resumeEmulation",
                              Qt::QueuedConnection);
    m_debugWin->show();

    positionDebugger();

    m_debugWin->raise();
    m_debugWin->activateWindow();
    m_debugWin->updateAllViews();
}

void MainWindow::positionDebugger()
{
    if (!m_debugWin || !m_debugWin->isVisible()) return;

    if (m_snapWindows) {
        // --- SNAP AAN: Rechts ernaast plakken ---
        const int gap = 10;
        int newX = this->x() + this->width() + gap;
        int newY = this->y();

        m_debugWin->move(newX, newY);

    } else {
        // --- SNAP UIT: Centreren op het hoofdvenster ---
        // Formule: ParentX + (ParentWidth - ChildWidth) / 2
        int newX = this->x() + (this->width() - m_debugWin->width()) / 2;
        int newY = this->y() + (this->height() - m_debugWin->height()) / 2;

        m_debugWin->move(newX, newY);
    }
}

void MainWindow::onDebuggerStepCPU()
{
    m_isPaused = true;
    QMetaObject::invokeMethod(m_colecoController, "pauseEmulation",
                              Qt::QueuedConnection);

    coleco_clear_debug_flags();

    QMetaObject::invokeMethod(m_colecoController, "stepOnce",
                              Qt::QueuedConnection);
    if (m_debugWin && m_debugWin->isVisible()) {
        m_debugWin->updateAllViews();
    }
}

void MainWindow::onDebuggerRunCPU()
{
    m_isPaused = false;
    coleco_clear_debug_flags();
    QMetaObject::invokeMethod(m_colecoController, "resumeEmulation",
                              Qt::QueuedConnection);
}

void MainWindow::onDebuggerBreakCPU()
{
    QMetaObject::invokeMethod(m_colecoController, "pauseEmulation",
                              Qt::QueuedConnection);
    m_isPaused = true;
    if (m_debugWin && m_debugWin->isVisible()) {
        m_debugWin->updateAllViews();
    }
}

void MainWindow::onSaveBreakpoint()
{
    if (!m_debugWin) return;

    const QList<DebuggerBreakpoint> breakpointsToSave = m_debugWin->getBreakpointDefinitions();
    if (breakpointsToSave.isEmpty()) {
        QMessageBox::information(this, tr("Info"), tr("No breakpoints to save."));
        return;
    }

    // Basisdir bepalen: absolute path maken + default naar media/breakpoints
    QDir appDir(QCoreApplication::applicationDirPath());
    QString basePath = m_breakpointPath.trimmed();

    if (basePath.isEmpty() || basePath == ".") {
        basePath = appDir.filePath("media/breakpoints");
    } else if (QDir::isRelativePath(basePath)) {
        basePath = appDir.filePath(basePath);
    }

    basePath = QDir::cleanPath(basePath);

    QDir breakpointsDir(basePath);
    if (!breakpointsDir.exists())
        breakpointsDir.mkpath(".");

    QString baseName = "my_breakpoints.txt";

    const QString filePath = CustomFileDialog::getSaveFileName(
        this,
        tr("Save Breakpoints"),
        breakpointsDir.absolutePath(),
        tr("Breakpoint Files (*.txt);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathState,
        QFileDialog::Options()
        // (geen extra suggest-name parameter in deze overload)
        );

    if (filePath.isEmpty()) return;

    QString finalPath = filePath;
    if (!finalPath.endsWith(".txt", Qt::CaseInsensitive))
        finalPath += ".txt";

    QFileInfo fileInfo(finalPath);
    CustomFileDialog::s_lastSaveDir = fileInfo.absolutePath();

    QFile file(finalPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not open file for writing:\n%1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);
    out << "# ADAM+ Breakpoint File\n";
    out << "# Format: [E/D] <Definition>\n\n";

    const QList<DebuggerBreakpoint> list = m_debugWin->getBreakpointDefinitions();
    for (const auto& bp : list) {
        if (bp.definition_text.trimmed().isEmpty())
            continue;

        out << (bp.enabled ? "E " : "D ")
            << bp.definition_text.trimmed()
            << "\n";
    }

    file.close();
    QMessageBox::information(this, tr("Success"), tr("Breakpoints saved."));
}

void MainWindow::onLoadBreakpoint()
{
    if (!m_debugWin) return;

    // Basisdir bepalen: absolute path maken + default naar media/breakpoints
    QDir appDir(QCoreApplication::applicationDirPath());
    QString basePath = m_breakpointPath.trimmed();

    if (basePath.isEmpty() || basePath == ".") {
        basePath = appDir.filePath("media/breakpoints");
    } else if (QDir::isRelativePath(basePath)) {
        basePath = appDir.filePath(basePath);
    }

    basePath = QDir::cleanPath(basePath);

    QDir breakpointsDir(basePath);
    if (!breakpointsDir.exists())
        breakpointsDir.mkpath(".");

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Load Breakpoints"),
        breakpointsDir.absolutePath(),
        tr("Breakpoint Files (*.txt);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathState,
        QFileDialog::Options()
        );

    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not open file for reading:\n%1").arg(file.errorString()));
        return;
    }

    QTextStream in(&file);
    QList<DebuggerBreakpoint> loadedBreakpoints;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        bool enabled = true;
        QString definition = line;

        if (line.startsWith("E ") || line.startsWith("D ")) {
            enabled = (line[0] == 'E');
            definition = line.mid(2).trimmed();
        }

        if (!definition.isEmpty()) {
            DebuggerBreakpoint newBp;
            newBp.type = BreakpointType::BP_EXECUTE;   // placeholder, zie jouw TODO
            newBp.definition_text = definition;
            newBp.enabled = enabled;
            loadedBreakpoints.append(newBp);
        }
    }

    file.close();

    m_debugWin->setBreakpointDefinitions(loadedBreakpoints);
}

void MainWindow::onSaveSymbolDefinitions()
{
    if (!m_debugWin) return;

    const QList<DebuggerSymbol> symbolsToSave = m_debugWin->getSymbolDefinitions();
    if (symbolsToSave.isEmpty()) {
        QMessageBox::information(this, tr("Info"), tr("No symbols to save."));
        return;
    }

    // Basisdir bepalen (absolute path + default naar media/symbols)
    QDir appDir(QCoreApplication::applicationDirPath());
    QString basePath = m_symbolsPath.trimmed();

    if (basePath.isEmpty() || basePath == ".") {
        basePath = appDir.filePath("media/symbols");
    } else if (QDir::isRelativePath(basePath)) {
        basePath = appDir.filePath(basePath);
    }

    basePath = QDir::cleanPath(basePath);

    QDir symbolsDir(basePath);
    if (!symbolsDir.exists())
        symbolsDir.mkpath(".");

    QString baseName = "my_symbols.sym";

    const QString filePath = CustomFileDialog::getSaveFileName(
        this,
        tr("Save Symbols"),
        symbolsDir.absolutePath(),
        tr("Symbol Files (*.sym *.txt);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathSymbol,
        QFileDialog::Options()
        );

    if (filePath.isEmpty()) return;

    QString finalPath = filePath;
    if (!finalPath.endsWith(".sym", Qt::CaseInsensitive))
        finalPath += ".sym";

    QFileInfo fileInfo(finalPath);
    CustomFileDialog::s_lastSaveDir = fileInfo.absolutePath();

    QFile file(finalPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not open file for writing:\n%1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);
    out << "# ADAM+ Symbol File\n";
    out << "# Format: TYPE:ADRES:LABEL\n\n";

    for (const DebuggerSymbol& sym : symbolsToSave) {
        out << sym.definition_text << "\n";
    }

    file.close();
    QMessageBox::information(this, tr("Success"), tr("Symbols saved."));
}

void MainWindow::onLoadSymbolDefinitions()
{
    if (!m_debugWin) return;

    // Basisdir bepalen (absolute path + default naar media/symbols)
    QDir appDir(QCoreApplication::applicationDirPath());
    QString basePath = m_symbolsPath.trimmed();

    if (basePath.isEmpty() || basePath == ".") {
        basePath = appDir.filePath("media/symbols");
    } else if (QDir::isRelativePath(basePath)) {
        basePath = appDir.filePath(basePath);
    }

    basePath = QDir::cleanPath(basePath);

    QDir symbolsDir(basePath);
    if (!symbolsDir.exists())
        symbolsDir.mkpath(".");

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Load Symbols"),
        symbolsDir.absolutePath(),
        tr("Symbol Files (*.sym *.txt);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathSymbol,
        QFileDialog::Options()
        );

    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not open file for reading:\n%1").arg(file.errorString()));
        return;
    }

    QTextStream in(&file);
    QList<DebuggerSymbol> loadedSymbols;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        QString definition = line;
        QStringList parts = definition.split(':');

        if (parts.size() >= 3) {
            DebuggerSymbol newSym;
            newSym.definition_text = definition;
            newSym.type = parts[0].trimmed();

            bool ok = false;
            newSym.address = parts[1].trimmed().toUShort(&ok, 16);
            if (!ok) {
                qWarning() << "[UI] Skipping symbol line due to invalid address:" << line;
                continue;
            }

            newSym.label = parts.mid(2).join(':').trimmed();

            loadedSymbols.append(newSym);
        }
    }

    file.close();

    m_debugWin->setSymbolDefinitions(loadedSymbols);
}

