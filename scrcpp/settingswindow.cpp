#include "settingswindow.h"
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QDebug>

SettingsWindow::SettingsWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    setMinimumWidth(500);

    QGridLayout *pathsLayout = new QGridLayout;

    // --- Rij 1: ROM Path ---
    QLabel *romLabel = new QLabel(tr("ROM Path:"));
    m_romPathEdit = new QLineEdit;
    m_romPathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(romLabel, 0, 0);
    pathsLayout->addWidget(m_romPathEdit, 0, 1);
    pathsLayout->addWidget(m_romPathBtn, 0, 2);

    // --- Rij 2: Disk Path ---
    QLabel *diskLabel = new QLabel(tr("Disk Path:"));
    m_diskPathEdit = new QLineEdit;
    m_diskPathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(diskLabel, 1, 0);
    pathsLayout->addWidget(m_diskPathEdit, 1, 1);
    pathsLayout->addWidget(m_diskPathBtn, 1, 2);

    // --- Rij 3: Tape Path ---
    QLabel *tapeLabel = new QLabel(tr("Tape Path:"));
    m_tapePathEdit = new QLineEdit;
    m_tapePathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(tapeLabel, 2, 0);
    pathsLayout->addWidget(m_tapePathEdit, 2, 1);
    pathsLayout->addWidget(m_tapePathBtn, 2, 2);

    // --- Rij 4: State Path ---
    QLabel *stateLabel = new QLabel(tr("State Path:"));
    m_statePathEdit = new QLineEdit;
    m_statePathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(stateLabel, 3, 0);
    pathsLayout->addWidget(m_statePathEdit, 3, 1);
    pathsLayout->addWidget(m_statePathBtn, 3, 2);

    // --- Rij 5: Breakpoint Path (NIEUW) ---
    QLabel *bpLabel = new QLabel(tr("Breakpoints Path:"));
    m_breakpointPathEdit = new QLineEdit;
    m_breakpointPathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(bpLabel, 4, 0);
    pathsLayout->addWidget(m_breakpointPathEdit, 4, 1);
    pathsLayout->addWidget(m_breakpointPathBtn, 4, 2);


    // --- Knoppen (AANGEPAST BLOK) ---
    m_okButton = new QPushButton(this);
    m_cancelButton = new QPushButton(this);

    QIcon okIcon(":/images/images/OK.png");
    QIcon cancelIcon(":/images/images/CANCEL.png");
    QPixmap okPixmap(":/images/images/OK.png");
    QPixmap cancelPixmap(":/images/images/CANCEL.png");

    if (okIcon.isNull()) { qWarning() << "SettingsWindow: Kon OK.png niet laden."; }
    if (cancelIcon.isNull()) { qWarning() << "SettingsWindow: Kon CANCEL.png niet laden."; }

    QSize okSize = okPixmap.size();
    QSize cancelSize = cancelPixmap.size();

    m_okButton->setIcon(okIcon);
    m_okButton->setIconSize(okSize);
    m_okButton->setFixedSize(okSize);
    m_okButton->setText("");
    m_okButton->setFlat(true);
    m_okButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    m_cancelButton->setIcon(cancelIcon);
    m_cancelButton->setIconSize(cancelSize);
    m_cancelButton->setFixedSize(cancelSize);
    m_cancelButton->setText("");
    m_cancelButton->setFlat(true);
    m_cancelButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(0);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(Qt::Horizontal);
    buttonBox->addButton(m_okButton, QDialogButtonBox::AcceptRole);
    buttonBox->addButton(m_cancelButton, QDialogButtonBox::RejectRole);

    // --- Hoofdlayout ---
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(pathsLayout);
    mainLayout->addSpacing(15);
    mainLayout->addWidget(buttonBox);

    // --- Connecties ---
    connect(m_romPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseRomPath);
    connect(m_diskPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseDiskPath);
    connect(m_tapePathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseTapePath);
    connect(m_statePathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseStatePath);
    connect(m_breakpointPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseBreakpointPath); // <-- NIEUW
    connect(m_okButton, &QPushButton::clicked, this, &SettingsWindow::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &SettingsWindow::reject);
}

// --- Getters ---
QString SettingsWindow::romPath() const { return m_romPathEdit->text(); }
QString SettingsWindow::diskPath() const { return m_diskPathEdit->text(); }
QString SettingsWindow::tapePath() const { return m_tapePathEdit->text(); }
QString SettingsWindow::statePath() const { return m_statePathEdit->text(); }
QString SettingsWindow::breakpointPath() const { return m_breakpointPathEdit->text(); } // <-- NIEUW

// --- Setters ---
void SettingsWindow::setRomPath(const QString &path) { m_romPathEdit->setText(path); }
void SettingsWindow::setDiskPath(const QString &path) { m_diskPathEdit->setText(path); }
void SettingsWindow::setTapePath(const QString &path) { m_tapePathEdit->setText(path); }
void SettingsWindow::setStatePath(const QString &path) { m_statePathEdit->setText(path); }
void SettingsWindow::setBreakpointPath(const QString &path) { m_breakpointPathEdit->setText(path); } // <-- NIEUW

// --- Slots ---
void SettingsWindow::onBrowseRomPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select ROM Directory"), m_romPathEdit->text()
        );
    if (!dir.isEmpty()) { m_romPathEdit->setText(dir); }
}

void SettingsWindow::onBrowseDiskPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Disk Directory"), m_diskPathEdit->text()
        );
    if (!dir.isEmpty()) { m_diskPathEdit->setText(dir); }
}

void SettingsWindow::onBrowseTapePath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Tape Directory"), m_tapePathEdit->text()
        );
    if (!dir.isEmpty()) { m_tapePathEdit->setText(dir); }
}

void SettingsWindow::onBrowseStatePath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select State Directory"), m_statePathEdit->text()
        );
    if (!dir.isEmpty()) {
        m_statePathEdit->setText(dir);
    }
}

// NIEUWE SLOT
void SettingsWindow::onBrowseBreakpointPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Breakpoints Directory"), m_breakpointPathEdit->text()
        );
    if (!dir.isEmpty()) {
        m_breakpointPathEdit->setText(dir);
    }
}
