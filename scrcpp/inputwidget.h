#ifndef INPUTWIDGET_H
#define INPUTWIDGET_H

#include <QWidget>
#include <QKeyEvent>
#include <QTimer>
#include <array>
#include <map>  // TOEVOEGEN

class ColecoController;  // TOEVOEGEN - Forward declaration

class InputWidget : public QWidget
{
    Q_OBJECT
public:
    enum Anchor { TopLeft, TopRight, BottomLeft, BottomRight };

    explicit InputWidget(QWidget *parent = nullptr);

    void attachTo(QWidget *target);
    void setAnchor(Anchor a);
    void setOverlayVisible(bool on);
    void setOverlayScale(qreal s);
    void reloadMappings();
    bool handleKey(QKeyEvent *e, bool pressed);
    void processHardwareRoute(int idx, bool jStorage, bool kpStorage, bool trStorage, bool pressed);
    // TOEVOEGEN - ADAM keyboard support:
    void setController(ColecoController* controller);
    void setMachineType(int type);      // 0=Coleco, 1=ADAM
    void setAdamGameMode(bool enabled);  // true=game mode, false=keyboard mode
    void setKeyboardOverlay(bool enabled);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

public slots:
    void setJoystickDirection(bool up, bool down, bool left, bool right);
    void setJoystickFireL(bool pressed);
    void setJoystickFireR(bool pressed);
    void setJoystickStart(bool pressed);
    void setJoystickSelect(bool pressed);
    void setJoystickAnalogX(int value);
    void setPaddleMode(bool usePaddle);

private:
    QWidget *m_target = nullptr;
    QTimer   m_overlayTick;
    qreal    m_flash = 0.0;

    Anchor   m_anchor = BottomLeft;
    bool     m_overlayVisible = true;
    qreal    m_scale = 1.0;
    int      m_margin = 12;

    void stepOverlay();
    QRect hudRect() const;
    void drawHud(QPainter &p, const QRect &r);

    // Joystick mapping (bestaand)
    std::array<int, 20> m_mapP1{};
    std::array<int, 20> m_mapP2{};
    static int defaultKeyForIndex(int idx);
    int findIndexForQtKey(const std::array<int,20>& map, int qtKey) const;

    int m_analogXValue = 0;
    bool m_isPaddleMode = false;

    // TOEVOEGEN - ADAM keyboard support:
    ColecoController* m_controller = nullptr;
    int m_machineType = 0;           // 0=Coleco, 1=ADAM
    bool m_adamGameMode = false;     // In ADAM: false=keyboard, true=game
    bool m_keyboardOverlay = false;  // Keyboard overlay enabled

    // Special keys mapping (van KbWidget)
    std::map<int, uint8_t> m_specialKeyMap;

    // Pressed keys tracking (voor ASCII release events)
    QMap<int, uint8_t> m_pressedKeyMap;

    // Helper methods
    void handleAdamKeyPress(QKeyEvent *e);
    void handleAdamKeyRelease(QKeyEvent *e);
    void sendAdamKeyEvent(int code, bool make);
    uint8_t defaultAdamCodeForKey(int qtKey);
    void handleSpecialKey(QKeyEvent *e, bool pressed);
    uint8_t getAdamCodeForQtKey(int qtKey) const;
    void updatePadAndBridge(int idx, bool jStorage, bool kpStorage, bool trStorage, bool pressed);

};

#endif
