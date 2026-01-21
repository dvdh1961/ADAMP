#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include <QDialog>

class QLineEdit;
class QPushButton;
class QDialogButtonBox;

class SettingsWindow : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget *parent = nullptr);

    void setRomPath(const QString &path);
    void setDiskPath(const QString &path);
    void setTapePath(const QString &path);
    void setStatePath(const QString &path);
    void setBreakpointPath(const QString &path);
    void setScreenshotPath(const QString &path);
    void setSymbolPath(const QString& path);
    void setAdamBezelPath(const QString &path);
    void setCvBezelPath(const QString &path);
    void setColecoBiosPath(const QString& path);
    void setEosBiosPath(const QString& path);
    void setWriterBiosPath(const QString& path);

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

private:
    void setupUI();

    QLineEdit *m_romPathEdit;
    QPushButton *m_romPathBtn;
    QLineEdit *m_diskPathEdit;
    QPushButton *m_diskPathBtn;
    QLineEdit *m_tapePathEdit;
    QPushButton *m_tapePathBtn;
    QLineEdit *m_statePathEdit;
    QPushButton *m_statePathBtn;
    QLineEdit *m_breakpointPathEdit;
    QPushButton *m_breakpointPathBtn;
    QLineEdit *m_screenshotPathEdit;
    QPushButton *m_screenshotPathBtn;
    QLineEdit *m_symbolPathEdit;
    QPushButton *m_symbolPathBtn;
    QLineEdit *m_adamBezelPathEdit;
    QPushButton *m_adamBezelPathBtn;
    QLineEdit *m_cvBezelPathEdit;
    QPushButton *m_cvBezelPathBtn;
    QPushButton *m_adamBezelLoadBtn;
    QPushButton *m_cvBezelLoadBtn;
    QDialogButtonBox *m_buttonBox;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    QLineEdit *m_colecoBiosPathEdit;
    QPushButton *m_colecoBiosPathBtn;
    QPushButton *m_colecoBiosDefaultBtn;
    QPushButton *m_resetAllPathsBtn;

    QLineEdit *m_eosBiosPathEdit;
    QPushButton *m_eosBiosPathBtn;
    QPushButton *m_eosBiosDefaultBtn;

    QLineEdit *m_writerBiosPathEdit;
    QPushButton *m_writerBiosPathBtn;
    QPushButton *m_writerBiosDefaultBtn;
};

#endif
