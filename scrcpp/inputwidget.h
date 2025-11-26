// #endif // INPUTWIDGET_H
#ifndef INPUTWIDGET_H
#define INPUTWIDGET_H

#include <QWidget>
#include <QKeyEvent>
#include <QTimer>
#include <array>

class InputWidget : public QWidget
{
    Q_OBJECT
public:
    enum Anchor { TopLeft, TopRight, BottomLeft, BottomRight };

    explicit InputWidget(QWidget *parent = nullptr);

    void attachTo(QWidget *target);

    // Nieuw: positionering & zichtbaarheid
    void setAnchor(Anchor a);
    void setOverlayVisible(bool on);
    void setOverlayScale(qreal s);      // 1.0 = standaard
    void reloadMappings();
    bool handleKey(QKeyEvent *e, bool pressed);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;  // <-- NIEUW
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

    // --- DE API VOOR DE JOYSTICK ---
public slots:
    void setJoystickDirection(bool up, bool down, bool left, bool right);
    void setJoystickFireL(bool pressed);
    void setJoystickFireR(bool pressed);
    void setJoystickStart(bool pressed);  // Zal Keypad '1' indrukken
    void setJoystickSelect(bool pressed); // Zal Keypad '*' indrukken

private:
    QWidget *m_target = nullptr;
    QTimer   m_overlayTick;
    qreal    m_flash = 0.0;

    // Nieuw
    Anchor   m_anchor = BottomLeft;
    bool     m_overlayVisible = true;
    qreal    m_scale = 1.0;
    int      m_margin = 12;     // px

    void stepOverlay();

    // Hulpfuncties voor tekenen/positioneren
    QRect hudRect() const;                // waar de HUD komt
    void drawHud(QPainter &p, const QRect &r);

    // 0..17: [0]UP,[1]DOWN,[2]LEFT,[3]RIGHT,[4]TRIG R,[5]TRIG L,[6]#,[7]*,[8..17]0..9
    std::array<int, 20> m_mapP1{};
    std::array<int, 20> m_mapP2{};  // klaar voor speler 2 (nu nog niet gebruikt)

    // Hulpjes
    static int defaultKeyForIndex(int idx);
    int findIndexForQtKey(const std::array<int,20>& map, int qtKey) const;
};

#endif // INPUTWIDGET_H
