#include "settingswindow.h"
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
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


QString appDefaultPath(const QString& relativePath)
{
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(relativePath));
}

QString defaultRomPath()        { return appDefaultPath("media/roms"); }
QString defaultDiskPath()       { return appDefaultPath("media/disks"); }
QString defaultTapePath()       { return appDefaultPath("media/tapes"); }
QString defaultStatePath()      { return appDefaultPath("media/states"); }
QString defaultBreakpointPath() { return appDefaultPath("media/breakpoints"); }
QString defaultScreenshotPath() { return appDefaultPath("media/screenshots"); }
QString defaultSymbolsPath()    { return appDefaultPath("media/symbols"); }
QString defaultBezelPath()      { return appDefaultPath("media/bezels"); }

QString defaultCvBasicExePath()
{
#if defined(Q_OS_WIN)
    return appDefaultPath("tools/cvbasic/cvbasic.exe");
#else
    return appDefaultPath("tools/cvbasic/cvbasic_linux");
#endif
}

QString defaultGasm80ExePath()
{
#if defined(Q_OS_WIN)
    return appDefaultPath("tools/cvbasic/gasm80.exe");
#else
    return appDefaultPath("tools/cvbasic/gasm80_linux");
#endif
}

QString defaultCvBasicSourcePath() { return appDefaultPath("media/cvbasic/source"); }
QString defaultCvBasicBuildPath()  { return appDefaultPath("media/cvbasic/build"); }
QString defaultSpriteSourcePath()  { return appDefaultPath("media/cvbasic/source"); }
QString defaultSpriteBuildPath()   { return appDefaultPath("media/cvbasic/build/sprites"); }
QString defaultSoundSourcePath()   { return appDefaultPath("media/cvbasic/sound"); }
QString defaultSoundBuildPath()    { return appDefaultPath("media/cvbasic/build/sound"); }


QString executableFileFilter()
{
#if defined(Q_OS_WIN)
    return QObject::tr("Executable Files (*.exe);;All Files (*.*)");
#else
    return QObject::tr("Executable Files (*)");
#endif
}

} // namespace

SettingsWindow::SettingsWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    setFixedWidth(780);
    setFixedHeight(560);

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

    addRowWithClr(pluginsLayout, 0, tr("CVBasic source path:"), m_cvbasicSourcePathEdit, m_cvbasicSourcePathBtn, m_cvbasicSourceDefaultBtn, tr("Browse CVBasic source directory"));
    addRowWithClr(pluginsLayout, 1, tr("CVBasic build path:"),  m_cvbasicBuildPathEdit,  m_cvbasicBuildPathBtn,  m_cvbasicBuildDefaultBtn,  tr("Browse CVBasic build directory"));
    addRowWithClr(pluginsLayout, 2, tr("CVBasic compiler:"),    m_cvbasicExePathEdit,    m_cvbasicExePathBtn,    m_cvbasicExeDefaultBtn,    tr("Browse CVBasic executable"));
    addRowWithClr(pluginsLayout, 3, tr("GASM80 assembler:"),    m_gasm80ExePathEdit,     m_gasm80ExePathBtn,     m_gasm80ExeDefaultBtn,     tr("Browse GASM80 executable"));
    addRowWithClr(pluginsLayout, 4, tr("Sprite source path:"),  m_spriteSourcePathEdit,  m_spriteSourcePathBtn,  m_spriteSourceDefaultBtn,  tr("Browse Sprite Editor source directory"));
    addRowWithClr(pluginsLayout, 5, tr("Sprite build path:"),   m_spriteBuildPathEdit,   m_spriteBuildPathBtn,   m_spriteBuildDefaultBtn,   tr("Browse Sprite Editor build directory"));
    addRowWithClr(pluginsLayout, 6, tr("Sound source path:"),   m_soundSourcePathEdit,   m_soundSourcePathBtn,   m_soundSourceDefaultBtn,   tr("Browse Sound Editor source directory"));
    addRowWithClr(pluginsLayout, 7, tr("Sound build path:"),    m_soundBuildPathEdit,    m_soundBuildPathBtn,    m_soundBuildDefaultBtn,    tr("Browse Sound Editor build directory"));

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

    // PLUG-INS / CVBasic suite
    connect(m_cvbasicSourcePathBtn, &QPushButton::clicked, this, &SettingsWindow::onBrowseCvBasicSourcePath);
    connect(m_cvbasicBuildPathBtn,  &QPushButton::clicked, this, &SettingsWindow::onBrowseCvBasicBuildPath);
    connect(m_cvbasicExePathBtn,    &QPushButton::clicked, this, &SettingsWindow::onBrowseCvBasicExePath);
    connect(m_gasm80ExePathBtn,     &QPushButton::clicked, this, &SettingsWindow::onBrowseGasm80ExePath);
    connect(m_spriteSourcePathBtn,  &QPushButton::clicked, this, &SettingsWindow::onBrowseSpriteSourcePath);
    connect(m_spriteBuildPathBtn,   &QPushButton::clicked, this, &SettingsWindow::onBrowseSpriteBuildPath);
    connect(m_soundSourcePathBtn,   &QPushButton::clicked, this, &SettingsWindow::onBrowseSoundSourcePath);
    connect(m_soundBuildPathBtn,    &QPushButton::clicked, this, &SettingsWindow::onBrowseSoundBuildPath);

    connect(m_cvbasicSourceDefaultBtn, &QPushButton::clicked, this, &SettingsWindow::onDefaultCvBasicSourcePath);
    connect(m_cvbasicBuildDefaultBtn,  &QPushButton::clicked, this, &SettingsWindow::onDefaultCvBasicBuildPath);
    connect(m_cvbasicExeDefaultBtn,    &QPushButton::clicked, this, &SettingsWindow::onDefaultCvBasicExePath);
    connect(m_gasm80ExeDefaultBtn,     &QPushButton::clicked, this, &SettingsWindow::onDefaultGasm80ExePath);
    connect(m_spriteSourceDefaultBtn,  &QPushButton::clicked, this, &SettingsWindow::onDefaultSpriteSourcePath);
    connect(m_spriteBuildDefaultBtn,   &QPushButton::clicked, this, &SettingsWindow::onDefaultSpriteBuildPath);
    connect(m_soundSourceDefaultBtn,   &QPushButton::clicked, this, &SettingsWindow::onDefaultSoundSourcePath);
    connect(m_soundBuildDefaultBtn,    &QPushButton::clicked, this, &SettingsWindow::onDefaultSoundBuildPath);

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
QString SettingsWindow::cvbasicSourcePath() const { return pathFromEdit(m_cvbasicSourcePathEdit); }
QString SettingsWindow::cvbasicBuildPath() const  { return pathFromEdit(m_cvbasicBuildPathEdit); }
QString SettingsWindow::cvbasicExePath() const    { return pathFromEdit(m_cvbasicExePathEdit); }
QString SettingsWindow::gasm80ExePath() const     { return pathFromEdit(m_gasm80ExePathEdit); }
QString SettingsWindow::spriteSourcePath() const  { return pathFromEdit(m_spriteSourcePathEdit); }
QString SettingsWindow::spriteBuildPath() const   { return pathFromEdit(m_spriteBuildPathEdit); }
QString SettingsWindow::soundSourcePath() const   { return pathFromEdit(m_soundSourcePathEdit); }
QString SettingsWindow::soundBuildPath() const    { return pathFromEdit(m_soundBuildPathEdit); }
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
void SettingsWindow::setCvBasicSourcePath(const QString &path)
{
    setPathText(m_cvbasicSourcePathEdit, path.isEmpty() ? defaultCvBasicSourcePath() : path);
}
void SettingsWindow::setCvBasicBuildPath(const QString &path)
{
    setPathText(m_cvbasicBuildPathEdit, path.isEmpty() ? defaultCvBasicBuildPath() : path);
}
void SettingsWindow::setCvBasicExePath(const QString &path)
{
    setPathText(m_cvbasicExePathEdit, path.isEmpty() ? defaultCvBasicExePath() : path);
}
void SettingsWindow::setGasm80ExePath(const QString &path)
{
    setPathText(m_gasm80ExePathEdit, path.isEmpty() ? defaultGasm80ExePath() : path);
}
void SettingsWindow::setSpriteSourcePath(const QString &path)
{
    setPathText(m_spriteSourcePathEdit, path.isEmpty() ? defaultSpriteSourcePath() : path);
}
void SettingsWindow::setSpriteBuildPath(const QString &path)
{
    setPathText(m_spriteBuildPathEdit, path.isEmpty() ? defaultSpriteBuildPath() : path);
}
void SettingsWindow::setSoundSourcePath(const QString &path)
{
    setPathText(m_soundSourcePathEdit, path.isEmpty() ? defaultSoundSourcePath() : path);
}
void SettingsWindow::setSoundBuildPath(const QString &path)
{
    setPathText(m_soundBuildPathEdit, path.isEmpty() ? defaultSoundBuildPath() : path);
}
void SettingsWindow::setAdamBootMode(int mode)
{
    if (!m_adamBootModeCombo) return;
    const int index = m_adamBootModeCombo->findData(mode);
    m_adamBootModeCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void SettingsWindow::onResetAllPathsToDefault()
{
    setPathText(m_romPathEdit, defaultRomPath());
    setPathText(m_diskPathEdit, defaultDiskPath());
    setPathText(m_tapePathEdit, defaultTapePath());
    setPathText(m_statePathEdit, defaultStatePath());
    setPathText(m_breakpointPathEdit, defaultBreakpointPath());
    setPathText(m_screenshotPathEdit, defaultScreenshotPath());
    setPathText(m_symbolPathEdit, defaultSymbolsPath());
    setPathText(m_adamBezelPathEdit, defaultBezelPath());
    setPathText(m_cvBezelPathEdit, defaultBezelPath());

    // BIOS/startup blijven leeg = interne/default boot zonder externe ROM/image.
    setPathText(m_colecoBiosPathEdit, QString());
    setPathText(m_eosBiosPathEdit, QString());
    setPathText(m_writerBiosPathEdit, QString());
    setPathText(m_adamStartupPathEdit, QString());

    setPathText(m_cvbasicSourcePathEdit, defaultCvBasicSourcePath());
    setPathText(m_cvbasicBuildPathEdit, defaultCvBasicBuildPath());
    setPathText(m_cvbasicExePathEdit, defaultCvBasicExePath());
    setPathText(m_gasm80ExePathEdit, defaultGasm80ExePath());
    setPathText(m_spriteSourcePathEdit, defaultSpriteSourcePath());
    setPathText(m_spriteBuildPathEdit, defaultSpriteBuildPath());
    setPathText(m_soundSourcePathEdit, defaultSoundSourcePath());
    setPathText(m_soundBuildPathEdit, defaultSoundBuildPath());
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

void SettingsWindow::onBrowseCvBasicSourcePath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select CVBasic Source Directory"), pathFromEdit(m_cvbasicSourcePathEdit).isEmpty() ? defaultCvBasicSourcePath() : pathFromEdit(m_cvbasicSourcePathEdit));
    if (!dir.isEmpty()) setPathText(m_cvbasicSourcePathEdit, QDir::cleanPath(dir));
}

void SettingsWindow::onBrowseCvBasicBuildPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select CVBasic Build Directory"), pathFromEdit(m_cvbasicBuildPathEdit).isEmpty() ? defaultCvBasicBuildPath() : pathFromEdit(m_cvbasicBuildPathEdit));
    if (!dir.isEmpty()) setPathText(m_cvbasicBuildPathEdit, QDir::cleanPath(dir));
}

void SettingsWindow::onBrowseSpriteSourcePath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Sprite Editor Source Directory"), pathFromEdit(m_spriteSourcePathEdit).isEmpty() ? defaultSpriteSourcePath() : pathFromEdit(m_spriteSourcePathEdit));
    if (!dir.isEmpty()) setPathText(m_spriteSourcePathEdit, QDir::cleanPath(dir));
}

void SettingsWindow::onBrowseSpriteBuildPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Sprite Editor Build Directory"), pathFromEdit(m_spriteBuildPathEdit).isEmpty() ? defaultSpriteBuildPath() : pathFromEdit(m_spriteBuildPathEdit));
    if (!dir.isEmpty()) setPathText(m_spriteBuildPathEdit, QDir::cleanPath(dir));
}

void SettingsWindow::onBrowseSoundSourcePath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Sound Editor Source Directory"), pathFromEdit(m_soundSourcePathEdit).isEmpty() ? defaultSoundSourcePath() : pathFromEdit(m_soundSourcePathEdit));
    if (!dir.isEmpty()) setPathText(m_soundSourcePathEdit, QDir::cleanPath(dir));
}

void SettingsWindow::onBrowseSoundBuildPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Sound Editor Build Directory"), pathFromEdit(m_soundBuildPathEdit).isEmpty() ? defaultSoundBuildPath() : pathFromEdit(m_soundBuildPathEdit));
    if (!dir.isEmpty()) setPathText(m_soundBuildPathEdit, QDir::cleanPath(dir));
}

void SettingsWindow::onBrowseCvBasicExePath()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        tr("Select CVBasic compiler"),
        pathFromEdit(m_cvbasicExePathEdit).isEmpty() ? QFileInfo(defaultCvBasicExePath()).absolutePath() : QFileInfo(pathFromEdit(m_cvbasicExePathEdit)).absolutePath(),
        executableFileFilter()
    );

    if (!file.isEmpty())
        setPathText(m_cvbasicExePathEdit, QDir::cleanPath(file));
}

void SettingsWindow::onBrowseGasm80ExePath()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        tr("Select GASM80 assembler"),
        pathFromEdit(m_gasm80ExePathEdit).isEmpty() ? QFileInfo(defaultGasm80ExePath()).absolutePath() : QFileInfo(pathFromEdit(m_gasm80ExePathEdit)).absolutePath(),
        executableFileFilter()
    );

    if (!file.isEmpty())
        setPathText(m_gasm80ExePathEdit, QDir::cleanPath(file));
}

void SettingsWindow::onDefaultCvBasicSourcePath()
{
    setPathText(m_cvbasicSourcePathEdit, defaultCvBasicSourcePath());
}

void SettingsWindow::onDefaultCvBasicBuildPath()
{
    setPathText(m_cvbasicBuildPathEdit, defaultCvBasicBuildPath());
}

void SettingsWindow::onDefaultCvBasicExePath()
{
    setPathText(m_cvbasicExePathEdit, defaultCvBasicExePath());
}

void SettingsWindow::onDefaultGasm80ExePath()
{
    setPathText(m_gasm80ExePathEdit, defaultGasm80ExePath());
}

void SettingsWindow::onDefaultSpriteSourcePath()
{
    setPathText(m_spriteSourcePathEdit, defaultSpriteSourcePath());
}

void SettingsWindow::onDefaultSpriteBuildPath()
{
    setPathText(m_spriteBuildPathEdit, defaultSpriteBuildPath());
}

void SettingsWindow::onDefaultSoundSourcePath()
{
    setPathText(m_soundSourcePathEdit, defaultSoundSourcePath());
}

void SettingsWindow::onDefaultSoundBuildPath()
{
    setPathText(m_soundBuildPathEdit, defaultSoundBuildPath());
}
