#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>

class SettingsWindow : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget *parent = nullptr);

    // Getters
    QString romPath() const;
    QString diskPath() const;
    QString tapePath() const;
    QString statePath() const;
    QString breakpointPath() const;
    QString screenshotPath() const;
    QString symbolPath() const;
    QString adamBezelPath() const;
    QString cvBezelPath() const;
    QString colecoBiosPath() const;
    QString eosBiosPath() const;
    QString writerBiosPath() const;
    QString adamStartupPath() const;
    QString cvbasicSourcePath() const;
    QString cvbasicBuildPath() const;
    QString cvbasicExePath() const;
    QString gasm80ExePath() const;
    QString spriteSourcePath() const;
    QString spriteBuildPath() const;
    QString soundSourcePath() const;
    QString soundBuildPath() const;
    int adamBootMode() const;

    // Setters
    void setRomPath(const QString &path);
    void setDiskPath(const QString &path);
    void setTapePath(const QString &path);
    void setStatePath(const QString &path);
    void setBreakpointPath(const QString &path);
    void setScreenshotPath(const QString &path);
    void setSymbolPath(const QString &path);
    void setAdamBezelPath(const QString &path);
    void setCvBezelPath(const QString &path);
    void setColecoBiosPath(const QString &path);
    void setEosBiosPath(const QString &path);
    void setWriterBiosPath(const QString &path);
    void setAdamStartupPath(const QString &path);
    void setCvBasicSourcePath(const QString &path);
    void setCvBasicBuildPath(const QString &path);
    void setCvBasicExePath(const QString &path);
    void setGasm80ExePath(const QString &path);
    void setSpriteSourcePath(const QString &path);
    void setSpriteBuildPath(const QString &path);
    void setSoundSourcePath(const QString &path);
    void setSoundBuildPath(const QString &path);
    void setAdamBootMode(int mode);

private slots:
    void onBrowseRomPath();
    void onBrowseDiskPath();
    void onBrowseTapePath();
    void onBrowseStatePath();
    void onBrowseBreakpointPath();
    void onBrowseScreenshotPath();
    void onBrowseSymbolsPath();
    void onBrowseAdamBezelPath();
    void onBrowseCvBezelPath();
    void onLoadAdamBezel();
    void onLoadCvBezel();
    void onBrowseColecoBiosPath();
    void onBrowseEosBiosPath();
    void onBrowseWriterBiosPath();
    void onDefaultColecoBiosPath();
    void onDefaultEosBiosPath();
    void onDefaultWriterBiosPath();
    void onResetAllPathsToDefault();
    void onResetAllMemoryPaths();
    void onBrowseAdamStartupPath();
    void onDefaultAdamStartupPath();
    void onBrowseCvBasicSourcePath();
    void onBrowseCvBasicBuildPath();
    void onBrowseCvBasicExePath();
    void onBrowseGasm80ExePath();
    void onBrowseSpriteSourcePath();
    void onBrowseSpriteBuildPath();
    void onBrowseSoundSourcePath();
    void onBrowseSoundBuildPath();
    void onDefaultCvBasicSourcePath();
    void onDefaultCvBasicBuildPath();
    void onDefaultCvBasicExePath();
    void onDefaultGasm80ExePath();
    void onDefaultSpriteSourcePath();
    void onDefaultSpriteBuildPath();
    void onDefaultSoundSourcePath();
    void onDefaultSoundBuildPath();

private:
    // Edits
    QLineEdit *m_romPathEdit, *m_diskPathEdit, *m_tapePathEdit, *m_statePathEdit;
    QLineEdit *m_breakpointPathEdit, *m_screenshotPathEdit, *m_symbolPathEdit;
    QLineEdit *m_adamBezelPathEdit, *m_cvBezelPathEdit;
    QLineEdit *m_colecoBiosPathEdit, *m_eosBiosPathEdit, *m_writerBiosPathEdit;
    QLineEdit *m_adamStartupPathEdit;
    QLineEdit *m_cvbasicSourcePathEdit = nullptr;
    QLineEdit *m_cvbasicBuildPathEdit = nullptr;
    QLineEdit *m_cvbasicExePathEdit = nullptr;
    QLineEdit *m_gasm80ExePathEdit = nullptr;
    QLineEdit *m_spriteSourcePathEdit = nullptr;
    QLineEdit *m_spriteBuildPathEdit = nullptr;
    QLineEdit *m_soundSourcePathEdit = nullptr;
    QLineEdit *m_soundBuildPathEdit = nullptr;
    QComboBox *m_adamBootModeCombo = nullptr;

    // SET Knoppen
    QPushButton *m_romPathBtn, *m_diskPathBtn, *m_tapePathBtn, *m_statePathBtn;
    QPushButton *m_breakpointPathBtn, *m_screenshotPathBtn, *m_symbolPathBtn;
    QPushButton *m_adamBezelPathBtn, *m_cvBezelPathBtn;
    QPushButton *m_colecoBiosPathBtn, *m_eosBiosPathBtn, *m_writerBiosPathBtn;
    QPushButton *m_adamStartupPathBtn;
    QPushButton *m_cvbasicSourcePathBtn = nullptr;
    QPushButton *m_cvbasicBuildPathBtn = nullptr;
    QPushButton *m_cvbasicExePathBtn = nullptr;
    QPushButton *m_gasm80ExePathBtn = nullptr;
    QPushButton *m_spriteSourcePathBtn = nullptr;
    QPushButton *m_spriteBuildPathBtn = nullptr;
    QPushButton *m_soundSourcePathBtn = nullptr;
    QPushButton *m_soundBuildPathBtn = nullptr;

    // LOAD Knoppen
    QPushButton *m_adamBezelLoadBtn, *m_cvBezelLoadBtn;

    // CLR Knoppen (Enkel voor de laatste rijen)
    QPushButton *m_colecoBiosDefaultBtn;
    QPushButton *m_eosBiosDefaultBtn;
    QPushButton *m_writerBiosDefaultBtn;
    QPushButton *m_adamStartupDefaultBtn;
    QPushButton *m_cvbasicSourceDefaultBtn = nullptr;
    QPushButton *m_cvbasicBuildDefaultBtn = nullptr;
    QPushButton *m_cvbasicExeDefaultBtn = nullptr;
    QPushButton *m_gasm80ExeDefaultBtn = nullptr;
    QPushButton *m_spriteSourceDefaultBtn = nullptr;
    QPushButton *m_spriteBuildDefaultBtn = nullptr;
    QPushButton *m_soundSourceDefaultBtn = nullptr;
    QPushButton *m_soundBuildDefaultBtn = nullptr;

    // Footer
    QPushButton *m_resetAllPathsBtn;
    QPushButton *m_resetAllMemoryPathsBtn;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
};

#endif
