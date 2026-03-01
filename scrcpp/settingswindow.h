#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

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

private:
    // Edits
    QLineEdit *m_romPathEdit, *m_diskPathEdit, *m_tapePathEdit, *m_statePathEdit;
    QLineEdit *m_breakpointPathEdit, *m_screenshotPathEdit, *m_symbolPathEdit;
    QLineEdit *m_adamBezelPathEdit, *m_cvBezelPathEdit;
    QLineEdit *m_colecoBiosPathEdit, *m_eosBiosPathEdit, *m_writerBiosPathEdit;

    // SET Knoppen
    QPushButton *m_romPathBtn, *m_diskPathBtn, *m_tapePathBtn, *m_statePathBtn;
    QPushButton *m_breakpointPathBtn, *m_screenshotPathBtn, *m_symbolPathBtn;
    QPushButton *m_adamBezelPathBtn, *m_cvBezelPathBtn;
    QPushButton *m_colecoBiosPathBtn, *m_eosBiosPathBtn, *m_writerBiosPathBtn;

    // LOAD Knoppen
    QPushButton *m_adamBezelLoadBtn, *m_cvBezelLoadBtn;

    // CLR Knoppen (Enkel voor de laatste rijen)
    QPushButton *m_colecoBiosDefaultBtn;
    QPushButton *m_eosBiosDefaultBtn;
    QPushButton *m_writerBiosDefaultBtn;

    // Footer
    QPushButton *m_resetAllPathsBtn;
    QPushButton *m_resetAllMemoryPathsBtn;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
};

#endif
