
#include "inputwidget.h"
#include "keypad.h"
#include <QPainter>
#include <QFont>
#include <QDebug>
#include <QApplication>
#include <QSettings>
extern "C" {
#include "input_bridge.h"
}
// State player 1
ColecoControllerState m_pad0;
// store keypad-key (0..11) pressed = -1
int m_keypadHeld = -1;

// Volgorde-indexen:
// 0 UP,1 DOWN,2 LEFT,3 RIGHT,4 TRIG R,5 TRIG L,6 BUT #,7 BUT *,
// 8..17 BUT 0..9
static constexpr int IDX_UP=0, IDX_DOWN=1, IDX_LEFT=2, IDX_RIGHT=3,
    IDX_TR=4, IDX_TL=5, IDX_HASH=6, IDX_STAR=7,
    IDX_0=8, IDX_9=17;

// Keybinding per index
int InputWidget::defaultKeyForIndex(int i)
{
    switch (i) {
    case IDX_UP:    return Qt::Key_Up;
    case IDX_DOWN:  return Qt::Key_Down;
    case IDX_LEFT:  return Qt::Key_Left;
    case IDX_RIGHT: return Qt::Key_Right;
    case IDX_TR:    return Qt::Key_X;           // Trig R
    case IDX_TL:    return Qt::Key_Z;           // Trig L
    case IDX_HASH:  return Qt::Key_NumberSign;  // '#'
    case IDX_STAR:  return Qt::Key_Asterisk;    // '*'
    default:
        // BUT 0..9
        if (i>=IDX_0 && i<=IDX_9) {
            int d = i - IDX_0;                  // 0..9
            return Qt::Key_0 + d;
        }
        return 0;
    }
}

int InputWidget::findIndexForQtKey(const std::array<int,20>& map, int qtKey) const
{
    for (int i=0;i<18;++i) {                 // we use 0..17
        if (map[i] == qtKey) return i;
    }
    return -1;
}

InputWidget::InputWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAutoFillBackground(false);
    setEnabled(true);
    setVisible(false);
    setOverlayVisible(false);
    qApp->installEventFilter(this);

    connect(&m_overlayTick, &QTimer::timeout, this, &InputWidget::stepOverlay);
    m_overlayTick.setTimerType(Qt::PreciseTimer);
    m_overlayTick.start(16);
    reloadMappings();
}

void InputWidget::reloadMappings()
{
    QSettings s;
    for (int i=0;i<18;++i) {
        int v = s.value(QString("input/p1/%1").arg(i), 0).toInt();
        if (v == 0) v = defaultKeyForIndex(i);
        m_mapP1[i] = v;
    }
    for (int i=0;i<18;++i) {
        int v = s.value(QString("input/p2/%1").arg(i), 0).toInt();
        if (v == 0) v = defaultKeyForIndex(i);
        m_mapP2[i] = v;
    }
}

void InputWidget::attachTo(QWidget *target)
{
    m_target = target;
    if (!m_target) return;

    setParent(m_target);

    m_target->installEventFilter(this);

    setGeometry(m_target->rect());

    raise();
    setFocus(Qt::OtherFocusReason);
}

bool InputWidget::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_target) {
        switch (ev->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
            setGeometry(m_target->rect());
            raise();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void InputWidget::setAnchor(Anchor a)            { m_anchor = a; update(); }
void InputWidget::setOverlayVisible(bool on)     { m_overlayVisible = on; update(); }
void InputWidget::setOverlayScale(qreal s)       { m_scale = qMax<qreal>(0.6, s); update(); }


void InputWidget::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    setFocus(Qt::OtherFocusReason);
}

void InputWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (m_target && m_target->parentWidget() == parentWidget())
        setGeometry(m_target->geometry());
}

void InputWidget::keyPressEvent(QKeyEvent *e)
{
    if (e->isAutoRepeat()) return;
    handleKey(e, true);
}

void InputWidget::keyReleaseEvent(QKeyEvent *e)
{
    if (e->isAutoRepeat()) return;
    handleKey(e, false);
}

bool InputWidget::handleKey(QKeyEvent *e, bool pressed)
{
    // F1..F6 lock (Used in MainWindow through ADAMNET)
    if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F6) {
        return true;
    }

    const int key = e->key();
    const int idx = findIndexForQtKey(m_mapP1, key);
    if (idx < 0) return false;

    // Richting / triggers / keypad
    if      (idx == IDX_UP)    m_pad0.up    = pressed;
    else if (idx == IDX_DOWN)  m_pad0.down  = pressed;
    else if (idx == IDX_LEFT)  m_pad0.left  = pressed;
    else if (idx == IDX_RIGHT) m_pad0.right = pressed;
    else if (idx == IDX_TL)    m_pad0.fireL = pressed;
    else if (idx == IDX_TR)    m_pad0.fireR = pressed;
    else {
        // Keypad: exact één tegelijk
        int kp = -1;
        if (idx == IDX_HASH)      kp = 11;     // '#'
        else if (idx == IDX_STAR) kp = 10;     // '*'
        else if (idx >= IDX_0 && idx <= IDX_9) kp = (idx - IDX_0); // 0..9

        if (kp >= 0) {
            ib_set_keypad_bit(kp, pressed);
            if (pressed) { m_pad0.keypad = kp; m_keypadHeld = kp; }
            else if (m_keypadHeld == kp) { m_pad0.keypad = -1; m_keypadHeld = -1; }
        }
    }

    // Push naar core
    coleco_setController(0, m_pad0);

    if (pressed) m_flash = 1.0;  // kleine HUD-flash

    return true;
}

void InputWidget::stepOverlay()
{
    // laat flash langzaam uitdoven
    if (m_flash > 0.0) m_flash = qMax(0.0, m_flash - 0.08);
    update(); // triggert paintEvent
}

QRect InputWidget::hudRect() const
{
    const int w = int(220 * m_scale);
    const int h = int(150 * m_scale);

    const int M = m_margin;
    QRect r(0,0,w,h);

    switch (m_anchor) {
    case TopLeft:      r.moveTopLeft(QPoint(M, M)); break;
    case TopRight:     r.moveTopRight(QPoint(width()-M, M)); break;
    case BottomLeft:   r.moveBottomLeft(QPoint(M, height()-M)); break;
    case BottomRight:  r.moveBottomRight(QPoint(width()-M, height()-M)); break;
    }
    return r;
}

static QColor mixA(const QColor &c, int alpha) {
    QColor k = c; k.setAlpha(alpha); return k;
}

void InputWidget::drawHud(QPainter &p, const QRect &r)
{
    // Aangenomen dat mixA(color, alpha) ergens gedefinieerd is
    auto mixA = [](const QColor &c, int alpha) -> QColor {
        QColor result = c;
        result.setAlpha(alpha);
        return result;
    };

    // --- Digitale Invoer Status ---
    const bool dirUp     = m_pad0.up;
    const bool dirDown   = m_pad0.down;
    const bool dirLeft   = m_pad0.left;
    const bool dirRight  = m_pad0.right;
    const bool btnL      = m_pad0.fireL;
    const bool btnR      = m_pad0.fireR;

    auto keyPressed = [&](int bit)->bool {
        return (m_pad0.keypad == bit);
    };

    p.setRenderHint(QPainter::Antialiasing, true);

    p.setPen(Qt::NoPen);
    p.setBrush(mixA(QColor(0x3d, 0xa9, 0xfc), 0xA0 + int(10*m_flash)));
    p.drawRoundedRect(r, 10*m_scale, 10*m_scale);

    const int pad = int(10 * m_scale);
    const QRect left = r.adjusted(pad, pad, -r.width()/2 - pad/2, -pad);
    const QRect right= QRect(r.center().x()+pad/2, r.y()+pad, r.width()/2 - 2*pad, r.height()-2*pad);

    //const QPoint lc_text = left.center() + QPoint(0, int(1 * m_scale));
    const QPoint lc_text = left.center() + QPoint(0, int(1 * m_scale) - int(15 * m_scale));

    const QPoint lc_pad  = lc_text + QPoint(0, int(14 * m_scale)); // Dit is het centrum van de D-pad
    const QSize  asz1(int(30*m_scale), int(30*m_scale));

    QFont font = p.font();
    font.setPointSizeF(10 * m_scale);
    font.setBold(true);
    p.setFont(font);

    QString label = "PLAYER 1";
    int yOffset = int(asz1.height()/2 + 15*m_scale);
    QRect textRect(lc_text.x() - int(60*m_scale),
                   lc_text.y() - yOffset - int(20*m_scale),
                   int(120*m_scale),
                   int(20*m_scale));

    p.setPen(QColor(0,0,0,180));
    p.drawText(textRect.translated(1,1), Qt::AlignCenter, label);

    p.setPen(mixA(Qt::white, 160));
    p.drawText(textRect, Qt::AlignCenter, label);


    auto drawArrow = [&](const QPoint &c, const QSize &sz, bool on, Qt::ArrowType type){
        QPolygon arrow;
        int w = sz.width();
        int h = sz.height();

        switch (type) {
        case Qt::UpArrow:
            arrow << QPoint(c.x(), c.y() - h/2)
                  << QPoint(c.x() - w/2, c.y() + h/2)
                  << QPoint(c.x() + w/2, c.y() + h/2);
            break;
        case Qt::DownArrow:
            arrow << QPoint(c.x(), c.y() + h/2)
                  << QPoint(c.x() - w/2, c.y() - h/2)
                  << QPoint(c.x() + w/2, c.y() - h/2);
            break;
        case Qt::LeftArrow:
            arrow << QPoint(c.x() - w/2, c.y())
                  << QPoint(c.x() + w/2, c.y() - h/2)
                  << QPoint(c.x() + w/2, c.y() + h/2);
            break;
        case Qt::RightArrow:
            arrow << QPoint(c.x() + w/2, c.y())
                  << QPoint(c.x() - w/2, c.y() - h/2)
                  << QPoint(c.x() - w/2, c.y() + h/2);
            break;
        default:
            return;
        }

        p.setBrush(on ? mixA(QColor(0x3d, 0xa9, 0xfc), 230) : mixA(Qt::white, 120));
        p.setPen(mixA(Qt::black, 160));
        p.drawPolygon(arrow);
    };

    const QSize asz(int(20*m_scale), int(20*m_scale));

    // D-PAD TEKENEN
    drawArrow(lc_pad + QPoint(0, -asz.height()-6*m_scale), asz, dirUp,    Qt::UpArrow);
    drawArrow(lc_pad + QPoint(0,  asz.height()+6*m_scale), asz, dirDown,  Qt::DownArrow);
    drawArrow(lc_pad + QPoint(-asz.width()-6*m_scale, 0),  asz, dirLeft,  Qt::LeftArrow);
    drawArrow(lc_pad + QPoint( asz.width()+6*m_scale, 0),  asz, dirRight, Qt::RightArrow);

    p.setBrush(mixA(Qt::white, 80));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRect(lc_pad - QPoint(int(8*m_scale),int(8*m_scale)),
                        QSize(int(16*m_scale),int(16*m_scale))));


    // --- ANALOGE SPINNER VISUALISATIE (NIEUW) ---

    // Bereken de verticale offset: D-pad centrum + uiterste punt van de Down arrow + marge
    const int DPadBottomY = lc_pad.y() + asz.height() + 9*m_scale;
    const int analogGap   = int(15 * m_scale);
    int barY = DPadBottomY + analogGap;

    int barHeight = int(15 * m_scale);
    int barWidth  = left.width() * 0.9; // Neem 90% van de linker helft

    // Midden van de balk
    QPoint analogCenter(left.center().x(), barY + barHeight / 2);

    // Maximale ruwe analoge waarde is 32767
    const int MAX_VAL = 32767;

    // 1. Achtergrondbalk (Neutrale zone)
    p.setPen(QPen(mixA(Qt::white, 100), 1));
    p.setBrush(QColor(40, 40, 40, 150));
    QRect barRect(analogCenter.x() - barWidth / 2, barY, barWidth, barHeight);
    p.drawRoundedRect(barRect, 0, 0);

    // 2. De Indicator (Verplaatsing)
    if (m_analogXValue != 0)
    {
        qreal ratio = (qreal)m_analogXValue / MAX_VAL;
        int indicatorWidth = qAbs(ratio) * (barWidth / 2);

        QColor color = (m_analogXValue < 0) ? QColor(50, 255, 50, 200) : QColor(50, 255, 50, 200);
        p.setPen(Qt::NoPen);
        p.setBrush(color);

        QRect indicatorRect;
        if (m_analogXValue > 0) {
            // Naar links
            indicatorRect.setRect(analogCenter.x() - indicatorWidth, barY, indicatorWidth, barHeight);
        } else if (m_analogXValue < 0){
            // Naar rechts
            indicatorRect.setRect(analogCenter.x(), barY, indicatorWidth, barHeight);
        }

        p.drawRect(indicatorRect);
    }

    // 3. Middenlijn (Nul-punt)
    p.setPen(QPen(mixA(Qt::white, 255), 1));
    p.drawLine(analogCenter.x(), barY, analogCenter.x(), barY + barHeight);

    int scaledValue = qRound((qreal)m_analogXValue * 100.0 / MAX_VAL);
    // 1. Clip de inkomende waarde: Garandeer dat deze binnen [-MAX_VAL, +MAX_VAL] blijft.
    int clippedValue = qBound(-100, scaledValue, 100); //

    // 4. Tekstuele waarde
    p.setPen(mixA(Qt::white, 200));
    QFont tf = p.font();
    tf.setBold(false);
    tf.setPointSizeF(7 * m_scale);
    p.setFont(tf);
    p.drawText(barRect.left()+60, barY - 5, QString("X: %1").arg(clippedValue));
    p.setFont(font); // Herstel oorspronkelijke font


    // --- KEYPAD EN BUTTONS (Bestaande Code) ---

    const int rightH     = right.height();
    const int buttonsH = int(20 * m_scale);
    const int gap        = int(8  * m_scale);
    const QRect abRect = QRect(right.left(), right.top(), right.width(), buttonsH);

    auto drawButton = [&](const QPoint &c, const QString &lbl, bool on){
        const int rad = int(12*m_scale);
        p.setPen(on ? mixA(Qt::black, 220) : mixA(Qt::black,150));
        p.setBrush(on ? mixA(QColor(0xef,0x45,0x65), 220) : mixA(Qt::white, 160));
        p.drawEllipse(QRect(c - QPoint(rad,rad), QSize(2*rad,2*rad)));
        QFont f = p.font(); f.setBold(true); f.setPointSizeF(10*m_scale);
        p.setFont(f);
        p.setPen(on ? Qt::white : Qt::black);
        p.drawText(QRect(c - QPoint(rad,rad), QSize(2*rad,2*rad)), Qt::AlignCenter, lbl);
    };
    const QPoint rc = abRect.center();
    drawButton(rc + QPoint( int(-20*m_scale), 0), "A", btnL);
    drawButton(rc + QPoint( int( 25*m_scale), 0), "B", btnR);

    const QRect kpRect = QRect(right.left(), abRect.bottom()+gap, right.width(),
                               rightH - buttonsH - gap);

    auto keyLabelFor = [&](int bit)->QString {
        if (bit == 11) return "#";
        if (bit == 10) return "*";
        if (bit == 0)  return "0";
        return QString::number(bit); // 1..9
    };

    const int rows = 4, cols = 3;
    const int cellW = kpRect.width()  / cols;
    const int cellH = kpRect.height() / rows;
    const int inset = int(2 * m_scale);

    const int bits[rows][cols] = {
        { 1, 2, 3 },
        { 4, 5, 6 },
        { 7, 8, 9 },
        { 11, 0, 10 }  // #, 0, *
    };

    QFont kf = p.font();
    kf.setBold(true);
    kf.setPointSizeF(8 * m_scale);
    p.setFont(kf);

    for (int rIdx = 0; rIdx < rows; ++rIdx) {
        for (int cIdx = 0; cIdx < cols; ++cIdx) {
            const int bit = bits[rIdx][cIdx];
            const bool on = keyPressed(bit);

            QRect cell(kpRect.left() + cIdx*cellW,
                       kpRect.top()  + rIdx*cellH,
                       cellW, cellH);
            cell = cell.marginsRemoved(QMargins(inset, inset, inset, inset));

            const int rad = int(2 * m_scale);
            p.setPen(mixA(Qt::black, on ? 180 : 120));
            p.setBrush(on ? mixA(QColor(0x3d,0xa9,0xfc), 230) : mixA(Qt::white, 150));
            p.drawRoundedRect(cell, rad, rad);

            p.setPen(on ? Qt::white : Qt::black);
            p.drawText(cell, Qt::AlignCenter, keyLabelFor(bit));
        }
    }

    //p.restore();
}

void InputWidget::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);
    if (!m_overlayVisible) return;

    QPainter p(this);
    drawHud(p, hudRect());
}

// inputwidget.cpp (Binnen setJoystickDirection)

void InputWidget::setJoystickDirection(bool up, bool down, bool left, bool right)
{
    // A. Verticale Invoer (Wordt ALTIJD bewerkt)
    m_pad0.up = up;
    m_pad0.down = down;

    // We updaten de bridge ALLEEN als de pulstrein actief is, OF als we de verticale as nodig hebben.
    ib_set_joy1_dir(IB_UP, up ? 1 : 0);
    ib_set_joy1_dir(IB_DOWN, down ? 1 : 0);


    if (m_isPaddleMode) {
        // PADDLE MODE AAN: Digitale horizontale input negeren
        m_pad0.left = false;
        m_pad0.right = false;

        // Bridge: Zet horizontale bits op 0 (pulstrein zal overschrijven)
        ib_set_joy1_dir(IB_LEFT, 0);
        ib_set_joy1_dir(IB_RIGHT, 0);

    } else {
        // --- PADDLE MODE UIT: HERSTEL VOLLEDIGE DIGITALE CONTROLE ---
        m_pad0.left = left;
        m_pad0.right = right;

        // CRUCIAAL: Bridge wordt nu gesynchroniseerd met de digitale status.
        // DIT IS HET ENIGE PUNT WAAR DE DIGITALE WAARDEN DE BRIDGE MOGEN RAKEN!
        ib_set_joy1_dir(IB_LEFT, left ? 1 : 0);
        ib_set_joy1_dir(IB_RIGHT, right ? 1 : 0);

        // **VERWIJDER DEZE PUSH:** coleco_push_direction_from_bridge(0);
        // De s_pad structuur moet nu volledig hersteld worden door coleco_setController.
    }

    // 2. PUSH KNOPPEN/KEYPAD: Hierdoor wordt de D-pad status van m_pad0 naar s_pad gepusht.
    coleco_setController(0, m_pad0);

    // 3. De Bridge-synchronisatie vindt ALLEEN plaats in coleco_paddle() nu.
}

void InputWidget::setJoystickFireL(bool pressed)
{
    m_pad0.fireL = pressed;
    coleco_setController(0, m_pad0);
}

void InputWidget::setJoystickFireR(bool pressed)
{
    m_pad0.fireR = pressed;
    coleco_setController(0, m_pad0);
}

void InputWidget::setJoystickStart(bool pressed)
{
    // Start knop -> Druk Keypad '1' in
    if (pressed) { m_pad0.keypad = 1; }
    else if (m_pad0.keypad == 1) { m_pad0.keypad = -1; }

    ib_set_keypad_bit(1, pressed);
    coleco_setController(0, m_pad0);
}

void InputWidget::setJoystickSelect(bool pressed)
{
    // Select knop -> Druk Keypad '*' (index 10) in
    if (pressed) { m_pad0.keypad = 10; }
    else if (m_pad0.keypad == 10) { m_pad0.keypad = -1; }

    ib_set_keypad_bit(10, pressed);
    coleco_setController(0, m_pad0);
}

// NIEUW: Slot om de analoge X-waarde van de joystick te ontvangen
void InputWidget::setJoystickAnalogX(int value)
{
    // Stuur de ruwe waarde naar de bridge.
    // De bridge is gedefinieerd met int16_t, dus casten we.
    ib_set_analog_x1((int16_t)value);

    // De core (coleco.cpp) zal de waarde in zijn update cyclus lezen.
    // Voor Windows (die geen event-loop heeft) zou je eventueel hier direct
    // coleco_setSpinner(0, value) kunnen aanroepen, maar de bridge is beter.

    // Optioneel: visuele feedback in de HUD
    // const int ANALOG_TH = 4000;
    // if (value > ANALOG_TH || value < -ANALOG_TH) m_flash = 1.0;
    // Sla de waarde op voor de visualisatie in paintEvent
    m_analogXValue = value;

    // Hertekenen forceren (om de balk bij te werken)
    update();
}

void InputWidget::setPaddleMode(bool usePaddle)
{
    m_isPaddleMode = usePaddle;
    // We kunnen hier optioneel een visuele indicatie geven
    // update();
}

