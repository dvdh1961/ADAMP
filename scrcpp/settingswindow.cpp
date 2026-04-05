#include "settingswindow.h"
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QDebug>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>

namespace {

constexpr int PATH_DISPLAY_MAX_CHARS = 45;

void setPathText(QLineEdit *edit, const QString &fullPath)
{
    if (!edit) return;

    // Volledige path bewaren in property + tooltip
    edit->setProperty("fullPath", fullPath);
    edit->setToolTip(fullPath);

    if (fullPath.isEmpty()) {
        edit->setText("");
    } else if (fullPath.length() <= PATH_DISPLAY_MAX_CHARS) {
        edit->setText(fullPath);
    } else {
        // Gebruik elide aan de linkerkant voor paden
        edit->setText("..." + fullPath.right(PATH_DISPLAY_MAX_CHARS - 3));
    }
}

QString pathFromEdit(const QLineEdit *edit)
{
    if (!edit) return QString();
    QVariant v = edit->property("fullPath");
    return v.isValid() ? v.toString() : edit->text();
}

} // namespace

SettingsWindow::SettingsWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    setFixedWidth(750);
    setFixedHeight(500);

    QGridLayout *pathsLayout = new QGridLayout;
    pathsLayout->setSpacing(2);
    pathsLayout->setContentsMargins(15, 1, 15, 1);

    pathsLayout->setColumnStretch(1, 0);

    // --- AANGEPASTE STYLESHEET: Icoon enkele pixels omhoog ---
    const QString iconButtonStyle =
        "QPushButton { "
        "  border: none; "
        "  background: transparent; "
        "  padding-bottom: 4px; " // Dit duwt de icon naar boven
        "  margin-top: -6px; "    // Haalt de hele knop iets omhoog
        "}"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }";

    // --- STIJL 2: Voor de footer knoppen (OK, CANCEL) - Neutraal ---
    const QString footerButtonStyle =
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }";

    // Grotere iconen zoals gevraagd
    const QSize iconSize(60, 32);
    const QSize btnSize = iconSize + QSize(10, 10);

    // Helper om een knop consistent te initialiseren
    auto setupBtn = [&](QPushButton* &btn, const QString &iconPath, const QString &tooltip) {
        btn = new QPushButton;
        btn->setIcon(QIcon(iconPath));
        btn->setFixedSize(btnSize);
        btn->setIconSize(iconSize);
        btn->setFlat(true);
        btn->setStyleSheet(iconButtonStyle);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tooltip);
    };

    // Helper voor de eerste 3 kolommen (Label, Edit, SET)
    auto addBaseRow = [&](int row, const QString &label, QLineEdit* &edit, QPushButton* &sBtn) {
        pathsLayout->addWidget(new QLabel(label), row, 0);
        edit = new QLineEdit;
        edit->setReadOnly(true);
        edit->setFixedWidth(350);
        pathsLayout->addWidget(edit, row, 1);
        setupBtn(sBtn, ":/images/images/BROWSE.png", tr("Browse Directory"));
        pathsLayout->addWidget(sBtn, row, 2);
    };

    // --- Rij-types definiëren ---

    // Type 1: Rijen 0-6 (Geen actieknop in kolom 4)
    auto addRowNoAction = [&](int row, const QString &label, QLineEdit* &edit, QPushButton* &sBtn) {
        addBaseRow(row, label, edit, sBtn);
    };

    // Type 2: Rijen 7-8 (LOAD knop in kolom 4)
    auto addRowWithLoad = [&](int row, const QString &label, QLineEdit* &edit, QPushButton* &sBtn, QPushButton* &lBtn) {
        addBaseRow(row, label, edit, sBtn);
        setupBtn(lBtn, ":/images/images/LOAD.png", tr("Load Image File"));
        pathsLayout->addWidget(lBtn, row, 3);
    };

    // Type 3: Rijen 9-11 (CLR knop in kolom 4)
    auto addRowWithClr = [&](int row, const QString &label, QLineEdit* &edit, QPushButton* &sBtn, QPushButton* &cBtn) {
        addBaseRow(row, label, edit, sBtn);
        setupBtn(cBtn, ":/images/images/DEFAULT.png", tr("Reset to Default"));
        pathsLayout->addWidget(cBtn, row, 3);
    };

    // --- Het Grid opbouwen ---
    addRowNoAction(0, tr("ROM Path:"),         m_romPathEdit,        m_romPathBtn);
    addRowNoAction(1, tr("Disk Path:"),        m_diskPathEdit,       m_diskPathBtn);
    addRowNoAction(2, tr("Tape Path:"),        m_tapePathEdit,       m_tapePathBtn);
    addRowNoAction(3, tr("State Path:"),       m_statePathEdit,      m_statePathBtn);
    addRowNoAction(4, tr("Breakpoints Path:"), m_breakpointPathEdit, m_breakpointPathBtn);
    addRowNoAction(5, tr("Screenshot Path:"),  m_screenshotPathEdit, m_screenshotPathBtn);
    addRowNoAction(6, tr("Symbols Path:"),     m_symbolPathEdit,     m_symbolPathBtn);

    // Bezel paden (SET voor dir, LOAD voor file)
    addRowWithLoad(7, tr("ADAM backImage:"),   m_adamBezelPathEdit,  m_adamBezelPathBtn,  m_adamBezelLoadBtn);
    addRowWithLoad(8, tr("Coleco backImage:"), m_cvBezelPathEdit,    m_cvBezelPathBtn,    m_cvBezelLoadBtn);

    // BIOS paden (SET voor file, CLR voor reset)
    addRowWithClr(9,  tr("Coleco BIOS (coleco.rom)):"),      m_colecoBiosPathEdit, m_colecoBiosPathBtn, m_colecoBiosDefaultBtn);
    addRowWithClr(10, tr("ADAM EOS (eos.rom):"),         m_eosBiosPathEdit,    m_eosBiosPathBtn,    m_eosBiosDefaultBtn);
    addRowWithClr(11, tr("ADAM Writer (writer.rom):"),      m_writerBiosPathEdit, m_writerBiosPathBtn, m_writerBiosDefaultBtn);

    addRowWithClr(12, tr("Adam Startup DSK/DDP Image:"), m_adamStartupPathEdit, m_adamStartupPathBtn,  m_adamStartupDefaultBtn);

    // Footer knoppen
    m_resetAllPathsBtn = new QPushButton(tr("Reset All Paths"));
    m_resetAllMemoryPathsBtn = new QPushButton(tr("Reset All Sub Paths"));

    m_resetAllPathsBtn->setCursor(Qt::PointingHandCursor);
    m_resetAllMemoryPathsBtn->setCursor(Qt::PointingHandCursor);

    m_okButton = new QPushButton;
    m_cancelButton = new QPushButton;

    QPixmap okPix(":/images/images/OK.png");
    m_okButton->setIcon(QIcon(okPix));
    m_okButton->setFixedSize(okPix.size());
    m_okButton->setIconSize(okPix.size());
    m_okButton->setFlat(true);
    m_okButton->setStyleSheet(footerButtonStyle);
    m_okButton->setCursor(Qt::PointingHandCursor);

    QPixmap canPix(":/images/images/CANCEL.png");
    m_cancelButton->setIcon(QIcon(canPix));
    m_cancelButton->setFixedSize(canPix.size());
    m_cancelButton->setIconSize(canPix.size());
    m_cancelButton->setFlat(true);
    m_cancelButton->setStyleSheet(footerButtonStyle);
    m_cancelButton->setCursor(Qt::PointingHandCursor);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_resetAllPathsBtn);
    buttonLayout->addWidget(m_resetAllMemoryPathsBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);

    // Hoofdlayout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(pathsLayout);
    mainLayout->addSpacing(10);
    mainLayout->addLayout(buttonLayout);

    // --- Connecties ---
    connect(m_romPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseRomPath);
    connect(m_diskPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseDiskPath);
    connect(m_tapePathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseTapePath);
    connect(m_statePathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseStatePath);
    connect(m_breakpointPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseBreakpointPath);
    connect(m_screenshotPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseScreenshotPath);
    connect(m_symbolPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseSymbolsPath);

    // Bezels
    connect(m_adamBezelPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseAdamBezelPath);
    connect(m_cvBezelPathBtn,   &QPushButton::clicked, this, &SettingsWindow::onBrowseCvBezelPath);
    connect(m_adamBezelLoadBtn, &QPushButton::clicked, this, &SettingsWindow::onLoadAdamBezel);
    connect(m_cvBezelLoadBtn,   &QPushButton::clicked, this, &SettingsWindow::onLoadCvBezel);

    // BIOS
    connect(m_colecoBiosPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseColecoBiosPath);
    connect(m_eosBiosPathBtn,    &QPushButton::clicked, this, &SettingsWindow::onBrowseEosBiosPath);
    connect(m_writerBiosPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseWriterBiosPath);
    connect(m_colecoBiosDefaultBtn, &QPushButton::clicked, this, &SettingsWindow::onDefaultColecoBiosPath);
    connect(m_eosBiosDefaultBtn,    &QPushButton::clicked, this, &SettingsWindow::onDefaultEosBiosPath);
    connect(m_writerBiosDefaultBtn, &QPushButton::clicked, this, &SettingsWindow::onDefaultWriterBiosPath);

    // STARTUP ADAM
    connect(m_adamStartupPathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseAdamStartupPath);
    connect(m_adamStartupDefaultBtn, &QPushButton::clicked, this, &SettingsWindow::onDefaultAdamStartupPath);

    connect(m_resetAllPathsBtn, &QPushButton::clicked, this, &SettingsWindow::onResetAllPathsToDefault);
    connect(m_resetAllMemoryPathsBtn, &QPushButton::clicked,this, &SettingsWindow::onResetAllMemoryPaths);
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
QString SettingsWindow::colecoBiosPath() const  { return pathFromEdit(m_colecoBiosPathEdit); }
QString SettingsWindow::eosBiosPath() const     { return pathFromEdit(m_eosBiosPathEdit); }
QString SettingsWindow::writerBiosPath() const  { return pathFromEdit(m_writerBiosPathEdit); }
QString SettingsWindow::adamStartupPath() const  { return pathFromEdit(m_adamStartupPathEdit); }

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
void SettingsWindow::setColecoBiosPath(const QString &path) { setPathText(m_colecoBiosPathEdit, path); }
void SettingsWindow::setEosBiosPath(const QString &path)    { setPathText(m_eosBiosPathEdit, path); }
void SettingsWindow::setWriterBiosPath(const QString &path) { setPathText(m_writerBiosPathEdit, path); }
void SettingsWindow::setAdamStartupPath(const QString &path)    { setPathText(m_adamStartupPathEdit, path); }

void SettingsWindow::onResetAllPathsToDefault()
{
    setPathText(m_romPathEdit, QString());
    setPathText(m_diskPathEdit, QString());
    setPathText(m_tapePathEdit, QString());
    setPathText(m_statePathEdit, QString());
    setPathText(m_breakpointPathEdit, QString());
    setPathText(m_screenshotPathEdit, QString());
    setPathText(m_symbolPathEdit, QString());
    setPathText(m_adamBezelPathEdit, QString());
    setPathText(m_cvBezelPathEdit, QString());
    setPathText(m_colecoBiosPathEdit, QString());
    setPathText(m_eosBiosPathEdit, QString());
    setPathText(m_writerBiosPathEdit, QString());
    setPathText(m_adamStartupPathEdit, QString());
}

void SettingsWindow::onResetAllMemoryPaths()
{
    QString iniPath = QDir(QCoreApplication::applicationDirPath()).filePath("settings.ini");
    QSettings settings(iniPath, QSettings::IniFormat);
    settings.remove("CustomFileDialog");
    settings.sync();
}

void SettingsWindow::onBrowseRomPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select ROM Directory"), pathFromEdit(m_romPathEdit));
    if (!dir.isEmpty()) setPathText(m_romPathEdit, dir);
}

void SettingsWindow::onBrowseDiskPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Disk Directory"), pathFromEdit(m_diskPathEdit));
    if (!dir.isEmpty()) setPathText(m_diskPathEdit, dir);
}

void SettingsWindow::onBrowseTapePath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Tape Directory"), pathFromEdit(m_tapePathEdit));
    if (!dir.isEmpty()) setPathText(m_tapePathEdit, dir);
}

void SettingsWindow::onBrowseStatePath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select State Directory"), pathFromEdit(m_statePathEdit));
    if (!dir.isEmpty()) setPathText(m_statePathEdit, dir);
}

void SettingsWindow::onBrowseBreakpointPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Breakpoint Directory"), pathFromEdit(m_breakpointPathEdit));
    if (!dir.isEmpty()) setPathText(m_breakpointPathEdit, dir);
}

void SettingsWindow::onBrowseScreenshotPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Screenshot Directory"), pathFromEdit(m_screenshotPathEdit));
    if (!dir.isEmpty()) setPathText(m_screenshotPathEdit, dir);
}

void SettingsWindow::onBrowseSymbolsPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Symbols Directory"), pathFromEdit(m_symbolPathEdit));
    if (!dir.isEmpty()) setPathText(m_symbolPathEdit, dir);
}

void SettingsWindow::onBrowseAdamBezelPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select ADAM Bezel Directory"), pathFromEdit(m_adamBezelPathEdit));
    if (!dir.isEmpty()) setPathText(m_adamBezelPathEdit, dir);
}

void SettingsWindow::onBrowseCvBezelPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Coleco Bezel Directory"), pathFromEdit(m_cvBezelPathEdit));
    if (!dir.isEmpty()) setPathText(m_cvBezelPathEdit, dir);
}

void SettingsWindow::onLoadAdamBezel()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Select ADAM Bezel Image"), pathFromEdit(m_adamBezelPathEdit),
                                                tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*.*)"));
    if (!file.isEmpty()) setPathText(m_adamBezelPathEdit, file);
}

void SettingsWindow::onLoadCvBezel()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Select Coleco Bezel Image"), pathFromEdit(m_cvBezelPathEdit),
                                                tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*.*)"));
    if (!file.isEmpty()) setPathText(m_cvBezelPathEdit, file);
}

void SettingsWindow::onBrowseColecoBiosPath()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Select ColecoVision BIOS"), pathFromEdit(m_colecoBiosPathEdit),
                                                tr("ROM Files (coleco.rom *.rom *.bin);;All Files (*.*)"));
    if (!file.isEmpty()) setPathText(m_colecoBiosPathEdit, file);
}

void SettingsWindow::onBrowseEosBiosPath()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Select ADAM EOS ROM"), pathFromEdit(m_eosBiosPathEdit),
                                                tr("ROM Files (eos.rom *.rom *.bin);;All Files (*.*)"));
    if (!file.isEmpty()) setPathText(m_eosBiosPathEdit, file);
}

void SettingsWindow::onBrowseWriterBiosPath()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Select ADAM SmartWriter ROM"), pathFromEdit(m_writerBiosPathEdit),
                                                tr("ROM Files (writer.rom *.rom *.bin);;All Files (*.*)"));
    if (!file.isEmpty()) setPathText(m_writerBiosPathEdit, file);
}

void SettingsWindow::onBrowseAdamStartupPath()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Select ADAM Startup DSK/DDP Image"), pathFromEdit(m_adamStartupPathEdit),
                                                tr("ADAM Images (*.dsk *.ddp);;All Files (*.*)"));
    if (!file.isEmpty()) setPathText(m_adamStartupPathEdit, file);
}

void SettingsWindow::onDefaultColecoBiosPath() { setPathText(m_colecoBiosPathEdit, QString()); }
void SettingsWindow::onDefaultEosBiosPath()    { setPathText(m_eosBiosPathEdit, QString()); }
void SettingsWindow::onDefaultWriterBiosPath() { setPathText(m_writerBiosPathEdit, QString()); }
void SettingsWindow::onDefaultAdamStartupPath() { setPathText(m_adamStartupPathEdit, QString()); }
