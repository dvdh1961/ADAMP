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
#include <QTabWidget>
#include <QSizePolicy>
#include <QComboBox>

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
    setFixedWidth(780);
    setFixedHeight(450);

    // Icoonknoppen niet kunstmatig omhoog/omlaag duwen:
    // de QGridLayout centreert ze verticaal op dezelfde baseline als de QLineEdit.
    const QString iconButtonStyle =
        "QPushButton { "
        "  border: none; "
        "  background: transparent; "
        "  padding: 0px; "
        "  margin: 0px; "
        "}"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }";

    // --- STIJL 2: Voor de footer knoppen (OK, CANCEL) - Neutraal ---
    const QString footerButtonStyle =
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }";

    // Grotere iconen zoals gevraagd
    const QSize iconSize(60, 32);
    const QSize btnSize = iconSize + QSize(10, 10);

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

    constexpr int labelColumnWidth = 190;
    constexpr int editColumnWidth  = 350;

    auto createTabLayout = []() {
        QGridLayout *layout = new QGridLayout;
        layout->setHorizontalSpacing(8);
        layout->setVerticalSpacing(8);
        // Data links laten starten: exact 10 px marge vanaf de tab-inhoud.
        layout->setContentsMargins(10, 18, 10, 18);

        // Vaste grid-kolommen in elke tab:
        // 0 = label, 1 = path/edit, 2 = browse, 3 = load/default, 4 = spacer.
        layout->setColumnMinimumWidth(0, 190);
        layout->setColumnMinimumWidth(1, 350);
        layout->setColumnMinimumWidth(2, 70);
        layout->setColumnMinimumWidth(3, 70);
        layout->setColumnStretch(0, 0);
        layout->setColumnStretch(1, 0);
        layout->setColumnStretch(2, 0);
        layout->setColumnStretch(3, 0);
        layout->setColumnStretch(4, 1);
        layout->setRowStretch(99, 1);
        return layout;
    };

    auto addBaseRow = [&](QGridLayout *layout, int row, const QString &label,
                          QLineEdit* &edit, QPushButton* &sBtn,
                          const QString &browseToolTip = QString()) {
        QLabel *lbl = new QLabel(label);
        lbl->setFixedWidth(labelColumnWidth);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(lbl, row, 0, Qt::AlignVCenter);

        edit = new QLineEdit;
        edit->setReadOnly(true);
        edit->setFixedWidth(editColumnWidth);
        edit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        layout->addWidget(edit, row, 1, Qt::AlignVCenter);

        setupBtn(sBtn, ":/images/images/BROWSE.png",
                 browseToolTip.isEmpty() ? tr("Browse Directory") : browseToolTip);
        layout->addWidget(sBtn, row, 2, Qt::AlignVCenter | Qt::AlignHCenter);
    };

    auto addRowNoAction = [&](QGridLayout *layout, int row, const QString &label,
                              QLineEdit* &edit, QPushButton* &sBtn,
                              const QString &browseToolTip = QString()) {
        addBaseRow(layout, row, label, edit, sBtn, browseToolTip);
    };

    auto addRowWithLoad = [&](QGridLayout *layout, int row, const QString &label,
                              QLineEdit* &edit, QPushButton* &sBtn, QPushButton* &lBtn,
                              const QString &browseToolTip = QString()) {
        addBaseRow(layout, row, label, edit, sBtn, browseToolTip);
        setupBtn(lBtn, ":/images/images/LOAD.png", tr("Load Image File"));
        layout->addWidget(lBtn, row, 3, Qt::AlignVCenter | Qt::AlignHCenter);
    };

    auto addRowWithClr = [&](QGridLayout *layout, int row, const QString &label,
                             QLineEdit* &edit, QPushButton* &sBtn, QPushButton* &cBtn,
                             const QString &browseToolTip = QString()) {
        addBaseRow(layout, row, label, edit, sBtn, browseToolTip);
        setupBtn(cBtn, ":/images/images/DEFAULT.png", tr("Reset to Default"));
        layout->addWidget(cBtn, row, 3, Qt::AlignVCenter | Qt::AlignHCenter);
    };


    auto addComboRow = [&](QGridLayout *layout, int row, const QString &label,
                           QComboBox* &combo) {
        QLabel *lbl = new QLabel(label);
        lbl->setFixedWidth(labelColumnWidth);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(lbl, row, 0, Qt::AlignVCenter);

        combo = new QComboBox;
        combo->setFixedWidth(editColumnWidth);
        combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        combo->addItem(tr("Writer"), 0);
        combo->addItem(tr("Startup DSK/DDP"), 1);
        //combo->addItem(tr("BASIC DSK/DDP"), 2);
        combo->setToolTip(tr("ADAM boot preset"));
        layout->addWidget(combo, row, 1, Qt::AlignVCenter);
    };


    auto addTabBottomIcon = [&](QGridLayout *layout, const QString &tabName) {
        QLabel *iconLabel = new QLabel;
        iconLabel->setFixedSize(128, 128);
        iconLabel->setAlignment(Qt::AlignLeft | Qt::AlignBottom);

        const QString iconPath = QString(":/images/images/TAB_%1.png").arg(tabName);
        QPixmap pix(iconPath);
        if (!pix.isNull()) {
            iconLabel->setPixmap(pix.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        iconLabel->setToolTip(tabName);

        // Rij 99 is stretch in createTabLayout(); daardoor blijft dit icoon netjes linksonder.
        layout->addWidget(iconLabel, 100, 0, 1, 1, Qt::AlignLeft | Qt::AlignBottom);
    };

    QTabWidget *tabs = new QTabWidget(this);

    QWidget *mediaTab = new QWidget(tabs);
    QGridLayout *mediaLayout = createTabLayout();
    mediaTab->setLayout(mediaLayout);
    addRowNoAction(mediaLayout, 0, tr("ROM Path:"),  m_romPathEdit,  m_romPathBtn);
    addRowNoAction(mediaLayout, 1, tr("Disk Path:"), m_diskPathEdit, m_diskPathBtn);
    addRowNoAction(mediaLayout, 2, tr("Tape Path:"), m_tapePathEdit, m_tapePathBtn);
    addTabBottomIcon(mediaLayout, "MEDIA");
    tabs->addTab(mediaTab, tr("MEDIA"));

    QWidget *debugTab = new QWidget(tabs);
    QGridLayout *debugLayout = createTabLayout();
    debugTab->setLayout(debugLayout);
    addRowNoAction(debugLayout, 0, tr("Breakpoints Path:"), m_breakpointPathEdit, m_breakpointPathBtn);
    addRowNoAction(debugLayout, 1, tr("Symbols Path:"),     m_symbolPathEdit,     m_symbolPathBtn);
    addRowNoAction(debugLayout, 2, tr("Screenshot Path:"),  m_screenshotPathEdit, m_screenshotPathBtn);
    addTabBottomIcon(debugLayout, "DEBUG");
    tabs->addTab(debugTab, tr("DEBUG"));

    QWidget *biosTab = new QWidget(tabs);
    QGridLayout *biosLayout = createTabLayout();
    biosTab->setLayout(biosLayout);
    addRowWithClr(biosLayout, 0, tr("Coleco BIOS (coleco.rom):"), m_colecoBiosPathEdit, m_colecoBiosPathBtn, m_colecoBiosDefaultBtn, tr("Browse BIOS File"));
    addRowWithClr(biosLayout, 1, tr("ADAM EOS (eos.rom):"),       m_eosBiosPathEdit,    m_eosBiosPathBtn,    m_eosBiosDefaultBtn,    tr("Browse BIOS File"));
    addRowWithClr(biosLayout, 2, tr("ADAM Writer (writer.rom):"), m_writerBiosPathEdit, m_writerBiosPathBtn, m_writerBiosDefaultBtn, tr("Browse BIOS File"));
    addTabBottomIcon(biosLayout, "BIOS");
    tabs->addTab(biosTab, tr("BIOS"));

    QWidget *generalTab = new QWidget(tabs);
    QGridLayout *generalLayout = createTabLayout();
    generalTab->setLayout(generalLayout);
    addRowWithClr(generalLayout, 0, tr("ADAM Startup DSK/DDP Image:"), m_adamStartupPathEdit, m_adamStartupPathBtn, m_adamStartupDefaultBtn, tr("Browse ADAM Startup Image"));
    addComboRow(generalLayout, 1, tr("Boot Mode:"), m_adamBootModeCombo);
    addRowWithLoad(generalLayout, 2, tr("ADAM backImage:"),           m_adamBezelPathEdit,  m_adamBezelPathBtn,  m_adamBezelLoadBtn);
    addRowWithLoad(generalLayout, 3, tr("Coleco backImage:"),         m_cvBezelPathEdit,    m_cvBezelPathBtn,    m_cvBezelLoadBtn);
    addTabBottomIcon(generalLayout, "GENERAL");
    tabs->addTab(generalTab, tr("GENERAL"));

    QWidget *statesTab = new QWidget(tabs);
    QGridLayout *statesLayout = createTabLayout();
    statesTab->setLayout(statesLayout);
    addRowNoAction(statesLayout, 0, tr("State Path:"), m_statePathEdit, m_statePathBtn);
    addTabBottomIcon(statesLayout, "STATES");
    tabs->addTab(statesTab, tr("STATES"));

    QWidget *pluginsTab = new QWidget(tabs);
    QGridLayout *pluginsLayout = createTabLayout();
    pluginsTab->setLayout(pluginsLayout);
    addTabBottomIcon(pluginsLayout, "PLUG-INS");
    tabs->addTab(pluginsTab, tr("PLUG-INS"));

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

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->addWidget(tabs);
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
int SettingsWindow::adamBootMode() const
{
    return m_adamBootModeCombo ? m_adamBootModeCombo->currentData().toInt() : 0;
}

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
void SettingsWindow::setAdamBootMode(int mode)
{
    if (!m_adamBootModeCombo) return;
    const int index = m_adamBootModeCombo->findData(mode);
    m_adamBootModeCombo->setCurrentIndex(index >= 0 ? index : 0);
}

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
    setAdamBootMode(0);
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
