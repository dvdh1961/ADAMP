#ifndef JOYPADWINDOW_H
#define JOYPADWINDOW_H

#include <QDialog>
#include <QVector>

class QTabWidget;
class QLineEdit;
class QPushButton;
class QGridLayout;
class QComboBox;

class JoypadWindow : public QDialog
{
    Q_OBJECT
public:
    explicit JoypadWindow(QWidget* parent = nullptr);
    ~JoypadWindow() override = default;

    static void loadMappingsFromSettings(int (&p1)[20], int (&p2)[20]);
    static void saveMappingsToSettings(const int (&p1)[20], const int (&p2)[20]);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onCaptureClicked();
    void onClearClicked();
    void onAccept();

private:
    struct PlayerUI {
        QWidget* page = nullptr;
        QComboBox* typeCombo = nullptr;
        QGridLayout* grid = nullptr;
        QVector<QLineEdit*> edits;
        QVector<QPushButton*> captureBtns;
        QVector<QPushButton*> clearBtns;
        int keys[20] = {0};
    };

    QTabWidget* m_tabs = nullptr;
    PlayerUI m_p1, m_p2;

    bool  m_capturing = false;
    int   m_capturePlayer = 0;
    int   m_captureIndex  = -1;

    void buildUi();
    void buildPlayerPage(PlayerUI& ui, const QString& title);
    void fillFromSettings();
    void pushToSettings();

    static QString vkToName(int vk);
};

#endif
