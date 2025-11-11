#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include <QDialog>

// Forward declarations
class QLineEdit;
class QPushButton;
class QDialogButtonBox;

class SettingsWindow : public QDialog // <-- Hernoemd
{
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget *parent = nullptr); // <-- Hernoemd

    // Functies om de data in en uit het dialoogvenster te krijgen
    void setRomPath(const QString &path);
    void setDiskPath(const QString &path);
    void setTapePath(const QString &path);
    QString romPath() const;
    QString diskPath() const;
    QString tapePath() const;

private slots:
    // Slot voor de "Browse..." knop
    void onBrowseRomPath();
    void onBrowseDiskPath();
    void onBrowseTapePath();

private:
    void setupUI();

    QLineEdit *m_romPathEdit;
    QPushButton *m_romPathBtn;
    QDialogButtonBox *m_buttonBox;
    QLineEdit *m_diskPathEdit;
    QPushButton *m_diskPathBtn;
    QLineEdit *m_tapePathEdit;
    QPushButton *m_tapePathBtn;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
};

#endif // SETTINGSWINDOW_H
