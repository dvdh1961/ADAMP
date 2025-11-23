#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include <QDialog>

// Forward declarations
class QLineEdit;
class QPushButton;
class QDialogButtonBox;

class SettingsWindow : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget *parent = nullptr);

    // Functies om de data in en uit het dialoogvenster te krijgen
    void setRomPath(const QString &path);
    void setDiskPath(const QString &path);
    void setTapePath(const QString &path);
    void setStatePath(const QString &path);
    void setBreakpointPath(const QString &path); // <-- NIEUW

    QString romPath() const;
    QString diskPath() const;
    QString tapePath() const;
    QString statePath() const;
    QString breakpointPath() const; // <-- NIEUW

private slots:
    // Slot voor de "Browse..." knop
    void onBrowseRomPath();
    void onBrowseDiskPath();
    void onBrowseTapePath();
    void onBrowseStatePath();
    void onBrowseBreakpointPath(); // <-- NIEUW

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
    QLineEdit *m_breakpointPathEdit; // <-- NIEUW
    QPushButton *m_breakpointPathBtn;  // <-- NIEUW

    QDialogButtonBox *m_buttonBox;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
};

#endif // SETTINGSWINDOW_H
