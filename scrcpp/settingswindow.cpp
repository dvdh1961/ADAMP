#include "settingswindow.h"
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QDialogButtonBox>

SettingsWindow::SettingsWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setMinimumWidth(500);

    QGridLayout *pathsLayout = new QGridLayout;

    // --- Rij 1: ROM Path ---
    QLabel *romLabel = new QLabel(tr("ROM Path:"));
    m_romPathEdit = new QLineEdit;
    m_romPathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(romLabel, 0, 0);
    pathsLayout->addWidget(m_romPathEdit, 0, 1);
    pathsLayout->addWidget(m_romPathBtn, 0, 2);

    // --- Rij 2: Disk Path (NIEUW) ---
    QLabel *diskLabel = new QLabel(tr("Disk Path:"));
    m_diskPathEdit = new QLineEdit;
    m_diskPathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(diskLabel, 1, 0);
    pathsLayout->addWidget(m_diskPathEdit, 1, 1);
    pathsLayout->addWidget(m_diskPathBtn, 1, 2);

    // --- Rij 3: Tape Path (NIEUW) ---
    QLabel *tapeLabel = new QLabel(tr("Tape Path:"));
    m_tapePathEdit = new QLineEdit;
    m_tapePathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(tapeLabel, 2, 0);
    pathsLayout->addWidget(m_tapePathEdit, 2, 1);
    pathsLayout->addWidget(m_tapePathBtn, 2, 2);

    // --- Knoppen ---
    m_okButton = new QPushButton(tr("OK"));
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_okButton->setDefault(true);

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
    connect(m_diskPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseDiskPath); // NIEUW
    connect(m_tapePathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseTapePath); // NIEUW
    connect(m_okButton, &QPushButton::clicked, this, &SettingsWindow::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &SettingsWindow::reject);
}

// --- Getters ---
QString SettingsWindow::romPath() const { return m_romPathEdit->text(); }
QString SettingsWindow::diskPath() const { return m_diskPathEdit->text(); } // NIEUW
QString SettingsWindow::tapePath() const { return m_tapePathEdit->text(); } // NIEUW

// --- Setters ---
void SettingsWindow::setRomPath(const QString &path) { m_romPathEdit->setText(path); }
void SettingsWindow::setDiskPath(const QString &path) { m_diskPathEdit->setText(path); } // NIEUW
void SettingsWindow::setTapePath(const QString &path) { m_tapePathEdit->setText(path); } // NIEUW

// --- Slots ---
void SettingsWindow::onBrowseRomPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select ROM Directory"), m_romPathEdit->text()
        );
    if (!dir.isEmpty()) { m_romPathEdit->setText(dir); }
}

// NIEUWE SLOT
void SettingsWindow::onBrowseDiskPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Disk Directory"), m_diskPathEdit->text()
        );
    if (!dir.isEmpty()) { m_diskPathEdit->setText(dir); }
}

// NIEUWE SLOT
void SettingsWindow::onBrowseTapePath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Tape Directory"), m_tapePathEdit->text()
        );
    if (!dir.isEmpty()) { m_tapePathEdit->setText(dir); }
}
