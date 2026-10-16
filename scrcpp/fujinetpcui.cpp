#include "mainwindow.h"
#include "colecocontroller.h"
#include "6801/adam_fujinet_pc.h"
#include <QProcess>
#include <QStyle>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QSaveFile>
#include "6801/adnet_core.h"

namespace {
QString iniPath() {return QDir(QCoreApplication::applicationDirPath()).filePath("settings.ini");}
QString defaultProgram() {
#ifdef Q_OS_WIN
    return QDir(QCoreApplication::applicationDirPath()).filePath("3rdparty/fujinet/windows/fujinet.exe");
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath("3rdparty/fujinet/linux/fujinet");
#endif
}
}
void MainWindow::setupFujiNetPCMenu(QMenu* fileMenu) {
    m_fujiNetPCMenu=fileMenu->addMenu(tr("FUJINET"));
    m_fujiNetPCStart=m_fujiNetPCMenu->addAction(tr("Start / Connect"),this,&MainWindow::startFujiNetPC);
    m_fujiNetPCStop=m_fujiNetPCMenu->addAction(tr("Disconnect"),this,[this] {
        stopFujiNetPC();
        QTimer::singleShot(250,this,[this] {
            if(m_fujiNetPCStopRequested && !AdamFujiNetPC::enabled())
                onPowerBtnClicked();
        });
    });
    m_fujiNetPCBoot=m_fujiNetPCMenu->addAction(tr("Boot FujiNet CONFIG / mounted disk"),this,&MainWindow::bootFujiNetPC);
    m_fujiNetPCMenu->addAction(tr("Open web configuration"),this,[] {
        QDesktopServices::openUrl(QUrl("http://127.0.0.1:8000"));
    });
    m_fujiNetPCMenu->addSeparator();
    m_fujiNetPCMenu->addAction(tr("Settings..."),this,&MainWindow::configureFujiNetPC);
    m_fujiNetPCState=m_fujiNetPCMenu->addAction(tr("Off"));m_fujiNetPCState->setEnabled(false);
    m_fujiNetPCStop->setEnabled(false);m_fujiNetPCBoot->setEnabled(false);
    auto timer=new QTimer(this);timer->setInterval(100);
    connect(timer,&QTimer::timeout,this,[this] {
        const bool enabled=AdamFujiNetPC::enabled(),connected=AdamFujiNetPC::connected();
        m_fujiNetPCStart->setEnabled(!enabled);
        m_fujiNetPCStop->setEnabled(enabled);
        m_fujiNetPCBoot->setEnabled(connected || enabled);
        m_fujiNetPCState->setText(QString::fromStdString(AdamFujiNetPC::state()));
        m_fujiNetPCMenu->setIcon(connected?style()->standardIcon(QStyle::SP_DialogApplyButton):QIcon());
        if(m_fujiNetPCBootPending) {
            if(++m_fujiNetPCBootTicks>=(connected?1200:300) || !enabled) {
                m_fujiNetPCBootPending=m_fujiNetPCBootProbePending=false;
                AdamFujiNetPC::cancel();
                QMessageBox::warning(this,tr("FujiNet-PC"),tr("FujiNet boot timed out. Check the program, boot disk and TCP port."));
            } else if(connected && m_colecoController) {
                if(!m_fujiNetPCBootProbePending) {
                    m_fujiNetPCBootProbe=AdamFujiNetPC::request(4,6,true);
                    m_fujiNetPCBootProbePending=true;
                } else if(m_fujiNetPCBootProbe.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                    auto reply=m_fujiNetPCBootProbe.get();
                    m_fujiNetPCBootProbePending=m_fujiNetPCBootPending=false;
                    if(reply.ok && !reply.rom.empty()) loadFujiNetPCRom(QByteArray(reinterpret_cast<const char*>(reply.rom.data()),int(reply.rom.size())));
                    else if(reply.ok) QMetaObject::invokeMethod(m_colecoController,"bootFujiNetPC",Qt::QueuedConnection,Q_ARG(int,reply.bootProfile));
                    else QMessageBox::warning(this,tr("FujiNet-PC"),tr("Cannot read the FujiNet boot disk. Enable CONFIG or mount a bootable image in disk slot 1."));
                }

            }
        }
    });timer->start();
    QSettings s(iniPath(),QSettings::IniFormat);
    if(s.value("fujiNetPC/autoConnect",false).toBool())
        QTimer::singleShot(1500,this,[this] {if(m_machineType==MACHINE_ADAM) startFujiNetPC();});
}
void MainWindow::configureFujiNetPC() {
    QSettings s(iniPath(),QSettings::IniFormat);
    QDialog dialog(this);dialog.setWindowTitle(tr("FujiNet-PC (ADAM)"));
    auto form=new QFormLayout(&dialog);
    auto program=new QLineEdit(s.value("fujiNetPC/program",defaultProgram()).toString(),&dialog);
    auto browse=new QPushButton(tr("Browse..."),&dialog);
    auto row=new QHBoxLayout;row->addWidget(program);row->addWidget(browse);
    form->addRow(tr("ADAM program:"),row);
    connect(browse,&QPushButton::clicked,&dialog,[&] {
        auto path=QFileDialog::getOpenFileName(&dialog,tr("Select ADAM FujiNet-PC executable"),program->text());
        if(!path.isEmpty()) program->setText(path);
    });
    auto port=new QSpinBox(&dialog);port->setRange(1024,65535);port->setValue(s.value("fujiNetPC/port",65216).toInt());
    form->addRow(tr("TCP port:"),port);
    auto launch=new QCheckBox(tr("Start FujiNet-PC automatically when connecting"),&dialog);
    launch->setChecked(s.value("fujiNetPC/launch",true).toBool());form->addRow(launch);
    auto automatic=new QCheckBox(tr("Connect when ADAM+ starts in ADAM mode"),&dialog);
    automatic->setChecked(s.value("fujiNetPC/autoConnect",false).toBool());form->addRow(automatic);
    auto boot=new QCheckBox(tr("Boot FujiNet after connecting"),&dialog);
    boot->setChecked(s.value("fujiNetPC/autoBoot",false).toBool());form->addRow(boot);
    auto help=new QLabel(tr("Forward D5–D8 and FujiNet network/control devices.\nKeyboard, printer, D1/D2 tapes and H: remain local.\nDisconnect before changing connection settings."),&dialog);help->setWordWrap(true);form->addRow(help);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted) return;
    s.setValue("fujiNetPC/program",program->text());s.setValue("fujiNetPC/port",port->value());
    s.setValue("fujiNetPC/launch",launch->isChecked());s.setValue("fujiNetPC/autoConnect",automatic->isChecked());s.setValue("fujiNetPC/autoBoot",boot->isChecked());
}
void MainWindow::startFujiNetPC() {
    if(AdamFujiNetPC::enabled()) return;
    QSettings s(iniPath(),QSettings::IniFormat);
    std::string error;
    if(!AdamFujiNetPC::start(uint16_t(s.value("fujiNetPC/port",65216).toUInt()),error)) {
        QMessageBox::warning(this,tr("FujiNet-PC"),QString::fromStdString(error));return;
    }
    m_fujiNetPCStopRequested=false;
    if(s.value("fujiNetPC/launch",true).toBool()) {
        QFileInfo executable(s.value("fujiNetPC/program",defaultProgram()).toString());
        if(!executable.exists() || !executable.isExecutable()) {
            AdamFujiNetPC::stop();
            QMessageBox::warning(this,tr("FujiNet-PC"),tr("ADAM FujiNet-PC executable not found or not executable. Select it in FUJINET → Settings."));return;
        }
        // FujiNet's web UI, CONFIG disk and SD folder are relative to its own directory.
        QSettings config(executable.dir().filePath("fnconfig.ini"),QSettings::IniFormat);
        config.setValue("BOIP/enabled",1);config.setValue("BOIP/host","127.0.0.1");
        config.setValue("BOIP/port",s.value("fujiNetPC/port",65216));config.sync();
        if(config.status()!=QSettings::NoError) {
            AdamFujiNetPC::stop();QMessageBox::warning(this,tr("FujiNet-PC"),tr("Cannot save FujiNet fnconfig.ini. Choose a writable FujiNet directory."));return;
        }
        if(!m_fujiNetPCProcess) {
            m_fujiNetPCProcess=new QProcess(this);
            m_fujiNetPCProcess->setProcessChannelMode(QProcess::MergedChannels);
            connect(m_fujiNetPCProcess,&QProcess::readyReadStandardOutput,this,[this] {
                // Consume logs so a long-running firmware cannot fill the pipe.
                m_fujiNetPCProcess->readAllStandardOutput();
            });
            connect(m_fujiNetPCProcess,&QProcess::errorOccurred,this,[this](QProcess::ProcessError) {
                if(m_fujiNetPCStopRequested) return;
                const QString error=m_fujiNetPCProcess->errorString();
                stopFujiNetPC();QMessageBox::warning(this,tr("FujiNet-PC"),error);
            });
            connect(m_fujiNetPCProcess,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status) {
                if(m_fujiNetPCStopRequested) return;
                if(code==75 && status==QProcess::NormalExit && AdamFujiNetPC::enabled()) {
                    // FujiNet deliberately exits 75 when CONFIG requests a firmware restart.
                    m_fujiNetPCProcess->start();
                } else if(AdamFujiNetPC::enabled()) {
                    stopFujiNetPC();
                    if(status==QProcess::CrashExit || code!=0)
                        QMessageBox::warning(this,tr("FujiNet-PC"),tr("FujiNet-PC stopped (exit %1).").arg(code));
                }
            });
        }
        m_fujiNetPCProcess->setWorkingDirectory(executable.absolutePath());
        m_fujiNetPCProcess->setProgram(executable.absoluteFilePath());
        m_fujiNetPCProcess->setArguments({"-c",executable.dir().filePath("fnconfig.ini"),"-s",executable.dir().filePath("SD")});
        m_fujiNetPCProcess->start();
    }
    if(s.value("fujiNetPC/autoBoot",false).toBool()) {m_fujiNetPCBootPending=true;m_fujiNetPCBootProbePending=false;m_fujiNetPCBootTicks=0;}
}
void MainWindow::stopFujiNetPC() {
    // terminate()/kill() can report Crashed, especially on Windows. Keep this
    // flag set until the next start, including signals delivered after stopping.
    m_fujiNetPCStopRequested=true;
    m_fujiNetPCBootPending=m_fujiNetPCBootProbePending=false;
    AdamFujiNetPC::stop();
    if(m_fujiNetPCProcess && m_fujiNetPCProcess->state()!=QProcess::NotRunning) {
        m_fujiNetPCProcess->terminate();
        if(!m_fujiNetPCProcess->waitForFinished(1000)) {m_fujiNetPCProcess->kill();m_fujiNetPCProcess->waitForFinished(1000);}
    }
}
void MainWindow::bootFujiNetPC() {
    if(!AdamFujiNetPC::enabled()) startFujiNetPC();
    if(AdamFujiNetPC::enabled()) {m_fujiNetPCBootPending=true;m_fujiNetPCBootProbePending=false;m_fujiNetPCBootTicks=0;}
}

void MainWindow::loadFujiNetPCRom(const QByteArray& rom) {
    if(rom.isEmpty()) return;
    QDir directory(QCoreApplication::applicationDirPath());
    directory.mkpath("media/roms/FujiNet");
    const QString path=directory.filePath("media/roms/FujiNet/FujiNet_PC.rom");
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(rom)!=rom.size() || !file.commit()) {
        QMessageBox::warning(this,tr("FujiNet-PC"),tr("Cannot save the downloaded cartridge ROM: %1").arg(file.errorString()));return;
    }
    m_adamGameMode=false;adamnet_set_game_mode(false);
    loadColecoRomFromPath(path,true);
}
