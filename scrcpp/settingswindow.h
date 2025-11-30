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

    QString romPath() const;
    QString diskPath() const;
    QString tapePath() const;
    QString statePath() const;
    QString breakpointPath() const;
    QString screenshotPath() const;

private slots:
    void onBrowseRomPath();
    void onBrowseDiskPath();
    void onBrowseTapePath();
    void onBrowseStatePath();
    void onBrowseBreakpointPath();
    void onBrowseScreenshotPath();

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

    QDialogButtonBox *m_buttonBox;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
};

#endif
