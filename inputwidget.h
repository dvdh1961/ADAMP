// #endif // INPUTWIDGET_H
#ifndef INPUTWIDGET_H
#define INPUTWIDGET_H

#include <QWidget>
#include <QKeyEvent>
#include <QTimer>

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

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;  // <-- NIEUW
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

private:
    QWidget *m_target = nullptr;
    QTimer   m_overlayTick;
    qreal    m_flash = 0.0;

    // Nieuw
    Anchor   m_anchor = BottomLeft;
    bool     m_overlayVisible = true;
    qreal    m_scale = 1.0;
    int      m_margin = 12;     // px

    void handleKey(QKeyEvent *e, bool pressed);
    void stepOverlay();

    // Hulpfuncties voor tekenen/positioneren
    QRect hudRect() const;                // waar de HUD komt
    void drawHud(QPainter &p, const QRect &r);
};

#endif // INPUTWIDGET_H
