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

namespace {

constexpr int PATH_DISPLAY_MAX_CHARS = 30;  // <-- hier bepaal je zelf de zichtbare lengte

QString elideLeft(const QString &text, int maxLen)
{
    if (maxLen <= 0)
        return QString();

    if (text.length() <= maxLen)
        return text;

    if (maxLen <= 3)
        return QString(maxLen, '.'); // "..." of ".."

    // Hou de laatste (maxLen - 3) karakters en zet "..." ervoor
    return "..." + text.right(maxLen - 3);
}

void setPathText(QLineEdit *edit, const QString &fullPath)
{
    if (!edit)
        return;

    // Volledige path bewaren in property + tooltip
    edit->setProperty("fullPath", fullPath);
    edit->setToolTip(fullPath);

    // Enkel getrimde versie tonen
    edit->setText(elideLeft(fullPath, PATH_DISPLAY_MAX_CHARS));
}

QString pathFromEdit(const QLineEdit *edit)
{
    if (!edit)
        return QString();

    QVariant v = edit->property("fullPath");
    if (v.isValid())
        return v.toString();

    // fallback: als property niet gezet is
    return edit->text();
}

} // namespace

SettingsWindow::SettingsWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    setMinimumWidth(600);

    //QFont Font1("Roboto", 8);
    //QFont Font2("Roboto", 10);
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

    // --- Rij 6: Screenshot Path (NIEUW) ---
    QLabel *scLabel = new QLabel(tr("Screenshot Path:"));
    m_screenshotPathEdit = new QLineEdit;
    m_screenshotPathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(scLabel, 5, 0);
    pathsLayout->addWidget(m_screenshotPathEdit, 5, 1);
    pathsLayout->addWidget(m_screenshotPathBtn, 5, 2);

    // --- Rij 7: Symbols Path (NIEUW) ---
    QLabel *symLabel = new QLabel(tr("Symbols Path:"));
    m_symbolPathEdit = new QLineEdit;
    m_symbolPathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(symLabel, 6, 0);
    pathsLayout->addWidget(m_symbolPathEdit, 6, 1);
    pathsLayout->addWidget(m_symbolPathBtn, 6, 2);

    // --- Rij 8: ADAM Bezel Path (NIEUW) ---
    QLabel *adamBezelLabel = new QLabel(tr("ADAM backImage:"));
    m_adamBezelPathEdit = new QLineEdit;
    m_adamBezelPathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(adamBezelLabel, 7, 0);
    pathsLayout->addWidget(m_adamBezelPathEdit, 7, 1);
    pathsLayout->addWidget(m_adamBezelPathBtn, 7, 2);

    // --- Rij 9: Coleco Bezel Path (NIEUW) ---
    QLabel *cvBezelLabel = new QLabel(tr("Coleco backImage:"));
    m_cvBezelPathEdit = new QLineEdit;
    m_cvBezelPathBtn = new QPushButton(tr("Browse..."));
    pathsLayout->addWidget(cvBezelLabel, 8, 0);
    pathsLayout->addWidget(m_cvBezelPathEdit, 8, 1);
    pathsLayout->addWidget(m_cvBezelPathBtn, 8, 2);

    // --- Rij 8b: load ADAM Bezel Path (NIEUW) ---
    m_adamBezelLoadBtn = new QPushButton(tr("Load..."));
    pathsLayout->addWidget(m_adamBezelLoadBtn, 7, 3);

    // --- Rij 9b: load Coleco Bezel Path (NIEUW) ---
    m_cvBezelLoadBtn = new QPushButton(tr("Load..."));
    pathsLayout->addWidget(m_cvBezelLoadBtn, 8, 3);

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
    connect(m_breakpointPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseBreakpointPath);
    connect(m_screenshotPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseScreenshotPath);
    connect(m_symbolPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseSymbolsPath);
    connect(m_adamBezelPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseAdamBezelPath);
    connect(m_cvBezelPathBtn,   &QPushButton::clicked, this, &SettingsWindow::onBrowseCvBezelPath);
    connect(m_adamBezelLoadBtn, &QPushButton::clicked, this, &SettingsWindow::onLoadAdamBezel);
    connect(m_cvBezelLoadBtn,   &QPushButton::clicked, this, &SettingsWindow::onLoadCvBezel);
    connect(m_okButton, &QPushButton::clicked, this, &SettingsWindow::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &SettingsWindow::reject);
}

// --- Getters ---
QString SettingsWindow::romPath() const         { return pathFromEdit(m_romPathEdit); }
QString SettingsWindow::diskPath() const        { return pathFromEdit(m_diskPathEdit); }
QString SettingsWindow::tapePath() const        { return pathFromEdit(m_tapePathEdit); }
QString SettingsWindow::statePath() const       { return pathFromEdit(m_statePathEdit); }
QString SettingsWindow::breakpointPath() const  { return pathFromEdit(m_breakpointPathEdit); }
QString SettingsWindow::screenshotPath() const  { return pathFromEdit(m_screenshotPathEdit); }
QString SettingsWindow::symbolPath() const      { return pathFromEdit(m_symbolPathEdit); }
QString SettingsWindow::adamBezelPath() const   { return pathFromEdit(m_adamBezelPathEdit); }
QString SettingsWindow::cvBezelPath() const     { return pathFromEdit(m_cvBezelPathEdit); }
// --- Setters ---
void SettingsWindow::setRomPath(const QString &path)          { setPathText(m_romPathEdit, path); }
void SettingsWindow::setDiskPath(const QString &path)         { setPathText(m_diskPathEdit, path); }
void SettingsWindow::setTapePath(const QString &path)         { setPathText(m_tapePathEdit, path); }
void SettingsWindow::setStatePath(const QString &path)        { setPathText(m_statePathEdit, path); }
void SettingsWindow::setBreakpointPath(const QString &path)   { setPathText(m_breakpointPathEdit, path); }
void SettingsWindow::setScreenshotPath(const QString &path)   { setPathText(m_screenshotPathEdit, path); }
void SettingsWindow::setSymbolPath(const QString &path)       { setPathText(m_symbolPathEdit, path); }
void SettingsWindow::setAdamBezelPath(const QString &path)    { setPathText(m_adamBezelPathEdit, path); }
void SettingsWindow::setCvBezelPath(const QString &path)      { setPathText(m_cvBezelPathEdit, path); }

// ROM path
void SettingsWindow::onBrowseRomPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select ROM Directory"),
        pathFromEdit(m_romPathEdit)
        );
    if (!dir.isEmpty()) {
        setPathText(m_romPathEdit, dir);
    }
}

// Disk path
void SettingsWindow::onBrowseDiskPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Disk Directory"),
        pathFromEdit(m_diskPathEdit)
        );
    if (!dir.isEmpty()) {
        setPathText(m_diskPathEdit, dir);
    }
}

// Tape path
void SettingsWindow::onBrowseTapePath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Tape Directory"),
        pathFromEdit(m_tapePathEdit)
        );
    if (!dir.isEmpty()) {
        setPathText(m_tapePathEdit, dir);
    }
}

// State path
void SettingsWindow::onBrowseStatePath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select State Directory"),
        pathFromEdit(m_statePathEdit)
        );
    if (!dir.isEmpty()) {
        setPathText(m_statePathEdit, dir);
    }
}

// Breakpoint path
void SettingsWindow::onBrowseBreakpointPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Breakpoint Directory"),
        pathFromEdit(m_breakpointPathEdit)
        );
    if (!dir.isEmpty()) {
        setPathText(m_breakpointPathEdit, dir);
    }
}

// Screenshot path
void SettingsWindow::onBrowseScreenshotPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Screenshot Directory"),
        pathFromEdit(m_screenshotPathEdit)
        );
    if (!dir.isEmpty()) {
        setPathText(m_screenshotPathEdit, dir);
    }
}

// Symbols path
void SettingsWindow::onBrowseSymbolsPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Symbols Directory"),
        pathFromEdit(m_symbolPathEdit)
        );
    if (!dir.isEmpty()) {
        setPathText(m_symbolPathEdit, dir);
    }
}

// ADAM Bezel directory (Browse...)
void SettingsWindow::onBrowseAdamBezelPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select ADAM Bezel Directory"),
        pathFromEdit(m_adamBezelPathEdit)
        );
    if (!dir.isEmpty()) {
        setPathText(m_adamBezelPathEdit, dir);
    }
}

// Coleco Bezel directory (Browse...)
void SettingsWindow::onBrowseCvBezelPath()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Coleco Bezel Directory"),
        pathFromEdit(m_cvBezelPathEdit)
        );
    if (!dir.isEmpty()) {
        setPathText(m_cvBezelPathEdit, dir);
    }
}

// ADAM Bezel image (Load...)
void SettingsWindow::onLoadAdamBezel()
{
    QString startDir = pathFromEdit(m_adamBezelPathEdit);
    QString file = QFileDialog::getOpenFileName(
        this,
        tr("Select ADAM Bezel Image"),
        startDir,
        tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*.*)")
        );
    if (!file.isEmpty()) {
        setPathText(m_adamBezelPathEdit, file);
    }
}

// Coleco Bezel image (Load...)
void SettingsWindow::onLoadCvBezel()
{
    QString startDir = pathFromEdit(m_cvBezelPathEdit);
    QString file = QFileDialog::getOpenFileName(
        this,
        tr("Select Coleco Bezel Image"),
        startDir,
        tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*.*)")
        );
    if (!file.isEmpty()) {
        setPathText(m_cvBezelPathEdit, file);
    }
}
