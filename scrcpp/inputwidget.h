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

    void setAnchor(Anchor a);
    void setOverlayVisible(bool on);
    void setOverlayScale(qreal s);
    void reloadMappings();
    bool handleKey(QKeyEvent *e, bool pressed);

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

    // Nieuw
    Anchor   m_anchor = BottomLeft;
    bool     m_overlayVisible = true;
    qreal    m_scale = 1.0;
    int      m_margin = 12;

    void stepOverlay();

    QRect hudRect() const;
    void drawHud(QPainter &p, const QRect &r);

    // 0..17: [0]UP,[1]DOWN,[2]LEFT,[3]RIGHT,[4]TRIG R,[5]TRIG L,[6]#,[7]*,[8..17]0..9
    std::array<int, 20> m_mapP1{};
    std::array<int, 20> m_mapP2{};  // klaar voor speler 2

    static int defaultKeyForIndex(int idx);
    int findIndexForQtKey(const std::array<int,20>& map, int qtKey) const;
    int m_analogXValue = 0;
    bool m_isPaddleMode = false;
};

#endif
