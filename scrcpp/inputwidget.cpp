#include "inputwidget.h"
#include "CORE/cvkpad.h"
#include "colecocontroller.h"
#include <QPainter>
#include <QFont>
#include <QDebug>
#include <QApplication>
#include <QSettings>
#include <QMetaObject>
extern "C" {
#include "input_bridge.h"
}

// --- Adam Key Code Definities ---
constexpr uint8_t ADAM_KEY_UP        = 0xA0;
constexpr uint8_t ADAM_KEY_RIGHT     = 0xA1;
constexpr uint8_t ADAM_KEY_DOWN      = 0xA2;
constexpr uint8_t ADAM_KEY_LEFT      = 0xA3;

constexpr uint8_t ADAM_KEY_BACKSPACE = 0x08;
constexpr uint8_t ADAM_KEY_TAB = 0x09;
constexpr uint8_t ADAM_KEY_RETURN    = 0x0D;
constexpr uint8_t ADAM_KEY_SPACE     = 0x20;

constexpr uint8_t ADAM_KEY_I         = 0x81; // F1
constexpr uint8_t ADAM_KEY_II        = 0x82; // F2
constexpr uint8_t ADAM_KEY_III       = 0x83; // F3
constexpr uint8_t ADAM_KEY_IV        = 0x84; // F4
constexpr uint8_t ADAM_KEY_V         = 0x85; // F5
constexpr uint8_t ADAM_KEY_VI        = 0x86; // F6

// Function key codes (F1-F10)
static const uint8_t ADAM_FUNCTION_KEYS[10] = {
    0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D
};

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

uint8_t InputWidget::defaultAdamCodeForKey(int qtKey)
{
    switch (qtKey) {
    case Qt::Key_Up:        return ADAM_KEY_UP;
    case Qt::Key_Down:      return ADAM_KEY_DOWN;
    case Qt::Key_Left:      return ADAM_KEY_LEFT;
    case Qt::Key_Right:     return ADAM_KEY_RIGHT;
    case Qt::Key_Backspace: return ADAM_KEY_BACKSPACE;
    case Qt::Key_Return:    return ADAM_KEY_RETURN;
    case Qt::Key_Enter:     return ADAM_KEY_RETURN;
    case Qt::Key_Space:     return ADAM_KEY_SPACE;
    case Qt::Key_F1:        return ADAM_KEY_I;
    case Qt::Key_F2:        return ADAM_KEY_II;
    case Qt::Key_F3:        return ADAM_KEY_III;
    case Qt::Key_F4:        return ADAM_KEY_IV;
    case Qt::Key_F5:        return ADAM_KEY_V;
    case Qt::Key_F6:        return ADAM_KEY_VI;
    }
    return 0;
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
    , m_controller(nullptr)
    , m_machineType(0)
    , m_adamGameMode(false)
    , m_keyboardOverlay(false)
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

void InputWidget::setController(ColecoController *controller)
{
    m_controller = controller;
}
void InputWidget::setMachineType(int type)
{
    m_machineType = type;
}

void InputWidget::setAdamGameMode(bool enabled)
{
    m_adamGameMode = enabled;
}

void InputWidget::setKeyboardOverlay(bool enabled)
{
    m_keyboardOverlay = enabled;
}


void InputWidget::reloadMappings()
{
    QSettings s;

    // Joystick mappings / m_mapP1 en m_mapP2:
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

    // Special keys mapping (van KbWidget):
    m_specialKeyMap.clear();

    std::vector<int> keysToMap = {
        Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right,
        Qt::Key_Backspace, Qt::Key_Return, Qt::Key_Enter, Qt::Key_Space,
        Qt::Key_F1, Qt::Key_F2, Qt::Key_F3, Qt::Key_F4, Qt::Key_F5, Qt::Key_F6
    };

    for (int defaultQtKey : keysToMap)
    {
        uint8_t adamCode = defaultAdamCodeForKey(defaultQtKey);
        if (adamCode == 0) continue;

        QString settingName = QString("AdamKeyboard/v3/adam_%1").arg(adamCode);
        int mappedQtKey = s.value(settingName, defaultQtKey).toInt();
        m_specialKeyMap[mappedQtKey] = adamCode;
    }
    //qDebug() << "[INPUTWIDGET] Special key mapping:" << m_specialKeyMap.size() << "keys mapped.";
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
    //if (e->isAutoRepeat()) return;
    handleKey(e, true);
}

void InputWidget::keyReleaseEvent(QKeyEvent *e)
{
    //if (e->isAutoRepeat()) return;
    handleKey(e, false);
}

bool InputWidget::handleKey(QKeyEvent *e, bool pressed)
{
    //if (e->isAutoRepeat())
    //    return true;

    const int key = e->key();
    const int idx = findIndexForQtKey(m_mapP1, key);

    const bool isMapped  = (idx >= 0);
    const bool jStorage  = (idx >= IDX_UP && idx <= IDX_RIGHT);
    const bool kpStorage = (idx >= IDX_HASH && idx <= IDX_9);
    const bool trStorage = (idx == IDX_TL || idx == IDX_TR);

    // ------------------------------------------------------------
    // ADAM Writer/BASIC cursor fix:
    // A0..A3 blijven keyboard-only, anders dubbele cursorstap.
    // ------------------------------------------------------------
    if (m_machineType == 1 && !m_adamGameMode && jStorage)
    {
        if (pressed)
            handleAdamKeyPress(e);
        else
            handleAdamKeyRelease(e);

        return true;
    }

    // ------------------------------------------------------------
    // Altijd pad-state bijwerken voor gemapte controller keys.
    // Dit is de oude basisroute die games nodig hebben.
    // ------------------------------------------------------------
    if (isMapped)
    {
        if (pressed)
            m_flash = 1.0;

        if (jStorage)
        {
            if      (idx == IDX_UP)    m_pad0.up    = pressed;
            else if (idx == IDX_DOWN)  m_pad0.down  = pressed;
            else if (idx == IDX_LEFT)  m_pad0.left  = pressed;
            else if (idx == IDX_RIGHT) m_pad0.right = pressed;
        }
        else if (trStorage)
        {
            if (idx == IDX_TL) m_pad0.fireL = pressed;
            if (idx == IDX_TR) m_pad0.fireR = pressed;
        }
        else if (kpStorage)
        {
            int kp = (idx == IDX_HASH) ? 11 :
                         (idx == IDX_STAR) ? 10 :
                         (idx - IDX_0);

            if (pressed)
            {
                m_pad0.keypad = kp;
                m_keypadHeld = kp;
            }
            else if (m_keypadHeld == kp)
            {
                m_pad0.keypad = -1;
                m_keypadHeld = -1;
            }
        }

        updatePadAndBridge(idx, jStorage, kpStorage, trStorage, pressed);
        this->update();
    }

    // ------------------------------------------------------------
    // ADAM mode
    // ------------------------------------------------------------
    if (m_machineType == 1)
    {
        // ADAM game mode: controller/bridge only.
        if (m_adamGameMode && isMapped && (jStorage || trStorage || kpStorage))
        {
            processHardwareRoute(idx, jStorage, kpStorage, trStorage, pressed);
            return true;
        }

        // ADAM Writer/BASIC gewone keyboardroute.
        if (pressed)
            handleAdamKeyPress(e);
        else
            handleAdamKeyRelease(e);

        return true;
    }

    // ------------------------------------------------------------
    // Coleco mode
    // ------------------------------------------------------------
    if (isMapped)
    {
        coleco_setController(0, m_pad0);
        return true;
    }

    return false;
}

void InputWidget::updatePadAndBridge(int idx, bool jStorage, bool kpStorage, bool trStorage, bool pressed)
{
    if (jStorage) {
        int dir = (idx == IDX_UP) ? IB_UP : (idx == IDX_DOWN) ? IB_DOWN :
                                            (idx == IDX_LEFT) ? IB_LEFT : IB_RIGHT; //
        ib_set_joy1_dir(dir, pressed); //
    } else if (trStorage) {
        if (idx == IDX_TL) ib_set_joy1_btn(IB_BTN1, pressed); //
        if (idx == IDX_TR) ib_set_joy1_btn(IB_BTN2, pressed); //
    } else if (kpStorage) {
        // Gebruik de berekende keypad waarde
        int kp = (idx == IDX_HASH) ? 11 : (idx == IDX_STAR) ? 10 : (idx - IDX_0);
        ib_set_keypad_bit(kp, pressed); //
    }
}

void InputWidget::processHardwareRoute(int idx, bool jStorage, bool kpStorage, bool trStorage, bool pressed)
{
    qDebug() << "[INPUT] processHardwareRoute"
             << "idx=" << idx
             << "j=" << jStorage
             << "kp=" << kpStorage
             << "tr=" << trStorage
             << "pressed=" << pressed
             << "controller=" << (m_controller ? "OK" : "NULL");

    if (jStorage)
    {
        m_pad0.up    = (idx == IDX_UP)    ? pressed : m_pad0.up;
        m_pad0.down  = (idx == IDX_DOWN)  ? pressed : m_pad0.down;
        m_pad0.left  = (idx == IDX_LEFT)  ? pressed : m_pad0.left;
        m_pad0.right = (idx == IDX_RIGHT) ? pressed : m_pad0.right;

        int dir =
            (idx == IDX_UP)   ? IB_UP :
                (idx == IDX_DOWN) ? IB_DOWN :
                (idx == IDX_LEFT) ? IB_LEFT :
                IB_RIGHT;

        qDebug() << "[INPUT] bridge direction dir=" << dir << "pressed=" << pressed;

        ib_set_joy1_dir(dir, pressed);
    }
    else if (trStorage)
    {
        if (idx == IDX_TL) {
            m_pad0.fireL = pressed;
            ib_set_joy1_btn(IB_BTN1, pressed);
        }

        if (idx == IDX_TR) {
            m_pad0.fireR = pressed;
            ib_set_joy1_btn(IB_BTN2, pressed);
        }

        qDebug() << "[INPUT] bridge fire idx=" << idx << "pressed=" << pressed;
    }
    else if (kpStorage)
    {
        int kp =
            (idx == IDX_HASH) ? 11 :
                (idx == IDX_STAR) ? 10 :
                (idx - IDX_0);

        if (pressed) {
            m_pad0.keypad = kp;
            m_keypadHeld = kp;
        }
        else if (m_keypadHeld == kp) {
            m_pad0.keypad = -1;
            m_keypadHeld = -1;
        }

        qDebug() << "[INPUT] bridge keypad kp=" << kp
                 << "pressed=" << pressed
                 << "m_pad0.keypad=" << m_pad0.keypad;

        ib_set_keypad_bit(kp, pressed);
    }

    coleco_setController(0, m_pad0);

    qDebug() << "[INPUT] coleco_setController:"
             << "up=" << m_pad0.up
             << "down=" << m_pad0.down
             << "left=" << m_pad0.left
             << "right=" << m_pad0.right
             << "fireL=" << m_pad0.fireL
             << "fireR=" << m_pad0.fireR
             << "keypad=" << m_pad0.keypad;
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


    // --- KEYPAD EN BUTTONS ---

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

void InputWidget::handleSpecialKey(QKeyEvent *e, bool pressed)
{
    if (!m_controller) return;

    auto it = m_specialKeyMap.find(e->key());
    if (it == m_specialKeyMap.end()) {
        return;
    }

    uint8_t finalCode = it->second;

    if (!pressed) {
        finalCode |= 0x80;
    }

    QMetaObject::invokeMethod(
        m_controller,
        "onAdamKeyEvent",
        Qt::QueuedConnection,
        Q_ARG(int, (int)finalCode)
        );
}

uint8_t InputWidget::getAdamCodeForQtKey(int qtKey) const
{
    auto it = m_specialKeyMap.find(qtKey);
    if (it != m_specialKeyMap.end()) {
        return it->second;
    }
    return 0;
}


void InputWidget::sendAdamKeyEvent(int code, bool make)
{
    // qDebug() << "    [INPUTWIDGET] sendAdamKeyEvent START";
    // qDebug() << "      code:" << Qt::hex << code;
    // qDebug() << "      make:" << make;
    // qDebug() << "      m_controller:" << (void*)m_controller;

    if (!m_controller) {
        qDebug() << "      ERROR: m_controller is NULL!";
        return;
    }

    // make=true => marked event (0x100 | code)
    // make=false => unmarked event (code only)
    const int marked = make ? (0x100 | (code & 0xFF)) : (code & 0xFF);

   // qDebug() << "      marked code:" << Qt::hex << marked;
   // qDebug() << "      Calling QMetaObject::invokeMethod on" << (void*)m_controller;

    QMetaObject::invokeMethod(
        m_controller,
        "onAdamKeyEvent",
        Qt::QueuedConnection,
        Q_ARG(int, marked)
        );

   // qDebug() << "      invokeMethod returned";
   // qDebug() << "    [INPUTWIDGET] sendAdamKeyEvent END";
}

void InputWidget::handleAdamKeyPress(QKeyEvent *e)
{
    if (!m_controller) return;
    const int key = e->key();

    // BASIC BREAK: Ctrl+C moet altijd als control-code 0x03 binnenkomen.
    // Vertrouw hiervoor niet op QKeyEvent::text(), omdat Qt
    // bij toetscombinaties met Ctrl niet op elk platform tekst teruggeeft.
    if (key == Qt::Key_C && (e->modifiers() & Qt::ControlModifier)) {
        // Via de ADAMNET-queue sturen zodat ook software die tijdens RUN
        // rechtstreeks op de keyboard-DCB wacht (zoals UltraBasic) BREAK ziet.
        sendAdamKeyEvent(0x03, true);
        return;
    }

    // GAME MODE PRIORITEIT: Gebruik de mapper om ASCII codes te forceren voor cijfers.
    // Hierdoor typt '1' altijd een '1' in de writer, ongeacht shift.
    if (m_adamGameMode) {
        const int idx = findIndexForQtKey(m_mapP1, key);
        if (idx >= 0) {
            uint8_t code = 0;
            if (idx >= IDX_0 && idx <= IDX_9) code = '0' + (idx - IDX_0);
            else if (idx == IDX_UP)    code = ADAM_KEY_UP;
            else if (idx == IDX_DOWN)  code = ADAM_KEY_DOWN;
            else if (idx == IDX_LEFT)  code = ADAM_KEY_LEFT;
            else if (idx == IDX_RIGHT) code = ADAM_KEY_RIGHT;
            else if (idx == IDX_STAR)  code = '*';
            else if (idx == IDX_HASH)  code = '#';

            if (code) {
                sendAdamKeyEvent(int(code), false);
                m_pressedKeyMap.insert(key, code);
                return; // Stop verdere afhandeling om dubbele of foutieve tekens te voorkomen.
            }
        }
    }

    // Standaard afhandeling voor Function keys en Tab
    if (key >= Qt::Key_F1 && key <= Qt::Key_F10) {
        sendAdamKeyEvent(ADAM_FUNCTION_KEYS[key - Qt::Key_F1], true);
        return;
    }
    if (key == Qt::Key_Tab)
    {
        sendAdamKeyEvent(ADAM_KEY_TAB, false);  // echte Tab
        m_pressedKeyMap.insert(key, ADAM_KEY_TAB);
        return;
    }

    // Cursortoetsen afhandeling
    // Belangrijk: via scancode-route sturen, niet als gewone ASCII/control-code.
    // Rechts mag 0x09 blijven, maar mag NIET als Tab/ASCII binnenkomen.
    if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right)
    {
        uint8_t code =
            (key == Qt::Key_Up)    ? ADAM_KEY_UP :
                (key == Qt::Key_Down)  ? ADAM_KEY_DOWN :
                (key == Qt::Key_Left)  ? ADAM_KEY_LEFT :
                ADAM_KEY_RIGHT;

        sendAdamKeyEvent(code, true);   // true = gemarkeerd/scancode-route
        m_pressedKeyMap.insert(key, code);
        return;
    }

    auto it_spec = m_specialKeyMap.find(key);
    if (it_spec != m_specialKeyMap.end()) { handleSpecialKey(e, true); return; }

    // Standaard tekstinvoer
    const QString text = e->text();
    if (!text.isEmpty()) {
        const uint8_t code = uint8_t(text.at(0).toLatin1());
        if (code > 0 && code < 0x80) {
            sendAdamKeyEvent(int(code), false);
            m_pressedKeyMap.insert(key, code);
        }
    }
}

void InputWidget::handleAdamKeyRelease(QKeyEvent *e)
{
    if (!m_controller) return;
    const int key = e->key();

    if (key >= Qt::Key_F1 && key <= Qt::Key_F10) {
        sendAdamKeyEvent(int(ADAM_FUNCTION_KEYS[key - Qt::Key_F1] | 0x80), true);
        return;
    }
    if (key == Qt::Key_Tab) { sendAdamKeyEvent(0x09 | 0x80, true); return; }

    // Release van cursortoetsen ook via scancode-route.
    // Niet via gewone ASCII-route, anders wordt 0x89/0x88 verkeerd behandeld.
    if (key == Qt::Key_Up || key == Qt::Key_Down ||
        key == Qt::Key_Left || key == Qt::Key_Right)
    {
        m_pressedKeyMap.remove(key);
        return;
    }
    auto it = m_pressedKeyMap.find(key);
    if (it != m_pressedKeyMap.end()) {
        sendAdamKeyEvent(int(it.value() | 0x80), false);
        m_pressedKeyMap.erase(it);
        return;
    }

    auto it_spec = m_specialKeyMap.find(key);
    if (it_spec != m_specialKeyMap.end()) handleSpecialKey(e, false);
}
