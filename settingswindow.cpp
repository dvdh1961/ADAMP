#include "settingswindow.h" // <-- Aangepast

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDir>

// Hernoemd van SettingsDialog naar SettingsWindow
SettingsWindow::SettingsWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setModal(true);
    setMinimumWidth(450);

    setupUI();

    connect(m_browseButton, &QPushButton::clicked, this, &SettingsWindow::onBrowseClicked);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsWindow::setupUI() // <-- Hernoemd
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *formLayout = new QFormLayout();

    // --- ROM Path ---
    m_romPathLineEdit = new QLineEdit(this);
    m_browseButton = new QPushButton("Browse...", this);

    QHBoxLayout *romPathLayout = new QHBoxLayout();
    romPathLayout->addWidget(m_romPathLineEdit, 1); // 1 = stretch factor
    romPathLayout->addWidget(m_browseButton);
    romPathLayout->setContentsMargins(0, 0, 0, 0);

    formLayout->addRow("ROM Folder:", romPathLayout);

    mainLayout->addLayout(formLayout);

    // --- OK / Cancel knoppen ---
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(m_buttonBox);
}

void SettingsWindow::onBrowseClicked() // <-- Hernoemd
{
    // Bepaal de huidige (absolute) map om de dialoog te openen
    QString currentRelativePath = m_romPathLineEdit->text();
    QString startPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + currentRelativePath);

    // Open de bestandsdialoog
    QString absoluteNewPath = QFileDialog::getExistingDirectory(this,
                                                                "Select Default ROM Folder",
                                                                startPath,
                                                                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!absoluteNewPath.isEmpty()) {
        // Converteer het absolute pad terug naar een relatief pad
        QDir appDir(QCoreApplication::applicationDirPath());
        QString relativeNewPath = appDir.relativeFilePath(absoluteNewPath);

        // Sla het relatieve pad op
        m_romPathLineEdit->setText(relativeNewPath);
    }
}

// --- Publieke functies ---

void SettingsWindow::setRomPath(const QString &path) // <-- Hernoemd
{
    m_romPathLineEdit->setText(path);
}

QString SettingsWindow::romPath() const // <-- Hernoemd
{
    return m_romPathLineEdit->text();
}
