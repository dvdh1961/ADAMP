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
    QString romPath() const;

private slots:
    // Slot voor de "Browse..." knop
    void onBrowseClicked();

private:
    void setupUI();

    QLineEdit *m_romPathLineEdit;
    QPushButton *m_browseButton;
    QDialogButtonBox *m_buttonBox;
};

#endif // SETTINGSWINDOW_H
