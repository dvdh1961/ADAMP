
#include "inputwidget.h"
#include "keypad.h"   // nieuwe controller-API
#include <QPainter>
#include <QFont>
#include <QDebug>
#include <QApplication>
#include <QSettings>
extern "C" {
#include "input_bridge.h"
}
// Nieuwe state voor speler 1
ColecoControllerState m_pad0;
// Houdt bij welke keypad-toets (0..11) ingedrukt is; -1 = geen
int m_keypadHeld = -1;

// Volgorde-indexen:
// 0 UP,1 DOWN,2 LEFT,3 RIGHT,4 TRIG R,5 TRIG L,6 BUT #,7 BUT *,
// 8..17 BUT 0..9
static constexpr int IDX_UP=0, IDX_DOWN=1, IDX_LEFT=2, IDX_RIGHT=3,
    IDX_TR=4, IDX_TL=5, IDX_HASH=6, IDX_STAR=7,
    IDX_0=8, IDX_9=17;

// // Helpers (lokaal in cpp)
// static inline int keyToColecoIndex(int qtKey) {
//     switch (qtKey) {
//     case Qt::Key_0: return 0;
//     case Qt::Key_1: return 1;
//     case Qt::Key_2: return 2;
//     case Qt::Key_3: return 3;
//     case Qt::Key_4: return 4;
//     case Qt::Key_5: return 5;
//     case Qt::Key_6: return 6;
//     case Qt::Key_7: return 7;
//     case Qt::Key_8: return 8;
//     case Qt::Key_9: return 9;
//     // let op: jouw code gebruikte Key codes 95/176 voor * en #
//     case Qt::Key_Asterisk:   return 10; // '*'
//     case Qt::Key_NumberSign: return 11; // '#'
//     default: return -1;
//     }
// }

// Standaard keybinding per index
int InputWidget::defaultKeyForIndex(int i)
{
    switch (i) {
    case IDX_UP:    return Qt::Key_Up;
    case IDX_DOWN:  return Qt::Key_Down;
    case IDX_LEFT:  return Qt::Key_Left;
    case IDX_RIGHT: return Qt::Key_Right;
    case IDX_TR:    return Qt::Key_X;        // Trig R
    case IDX_TL:    return Qt::Key_Z;        // Trig L
    case IDX_HASH:  return Qt::Key_NumberSign; // '#'
    case IDX_STAR:  return Qt::Key_Asterisk;   // '*'
    default:
        // BUT 0..9
        if (i>=IDX_0 && i<=IDX_9) {
            int d = i - IDX_0;               // 0..9
            return Qt::Key_0 + d;
        }
        return 0;
    }
}

// Vind index in map voor deze Qt-key; -1 = niet gevonden
int InputWidget::findIndexForQtKey(const std::array<int,20>& map, int qtKey) const
{
    for (int i=0;i<18;++i) {                 // we gebruiken 0..17
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
    setVisible(false); // start ALTIJD onzichtbaar
    setOverlayVisible(false);
    // Tekenen uit/aan blijft via m_overlayVisible (HUD), maar input willen we altijd
    qApp->installEventFilter(this);   // ⟵ VANG KEY EVENTS ALTIJD, OOK ALS WIJ VERBORGEN ZIJN

    // ~60 fps overlay
    connect(&m_overlayTick, &QTimer::timeout, this, &InputWidget::stepOverlay);
    m_overlayTick.setTimerType(Qt::PreciseTimer);
    m_overlayTick.start(16);
    reloadMappings();
}

void InputWidget::reloadMappings()
{
    // Lees QSettings uit JoypadWindow: "input/p1/<idx>"
    QSettings s;
    for (int i=0;i<18;++i) {
        int v = s.value(QString("input/p1/%1").arg(i), 0).toInt();
        if (v == 0) v = defaultKeyForIndex(i);   // fallback naar standaard
        m_mapP1[i] = v;
    }
    // optioneel P2 alvast klaarzetten
    for (int i=0;i<18;++i) {
        int v = s.value(QString("input/p2/%1").arg(i), 0).toInt();
        if (v == 0) v = defaultKeyForIndex(i);   // voorlopig dezelfde defaults
        m_mapP2[i] = v;
    }
}

void InputWidget::attachTo(QWidget *target)
{
    // 1) Onthoud target
    m_target = target;
    if (!m_target) return;

    // 2) Word KIND van het scherm (niet van MainWindow/splitter)
    //    → coördinaten zijn dan lokaal aan het scherm
    setParent(m_target);

    // 3) Zorg dat we meeveranderen met het scherm
    m_target->installEventFilter(this);

    // 4) Vul het hele schermoppervlak
    setGeometry(m_target->rect());

    // 5) Bovenop
    raise();
    //show();
    setFocus(Qt::OtherFocusReason);
}

bool InputWidget::eventFilter(QObject *obj, QEvent *ev)
{
    // // 0) App-breed key handling (los van zichtbaarheid en focus)
    // if (ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease) {
    //     auto *ke = static_cast<QKeyEvent*>(ev);
    //     if (!ke->isAutoRepeat()) {
    //         const bool pressed = (ev->type() == QEvent::KeyPress);
    //         handleKey(ke, pressed);      // ⟵ gebruikt je bestaande mapping
    //         // Laat andere widgets nog steeds hun key krijgen:
    //         // return true = event "geconsumeerd", return false = doorlaten.
    //         // Voor emulator is "doorlaten" meestal OK (MainWindow shortcuts blijven werken).
    //         // Wil je exclusief? Zet hier 'return true;'.
    //     }
    //     // niet retourneren; we laten 'm door tenzij je exclusief wil
    // }

    // 1) Bestaande overlay-positie/raise logic
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
    // F1..F6 blokkeren (deze worden al afgehandeld in MainWindow via ADAMNET)
    if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F6) {
        // markeer als afgehandeld, maar stuur niets door
        return true;
    }

    const int key = e->key();
    // Zoek welke *actie-index* bij deze toets hoort
    const int idx = findIndexForQtKey(m_mapP1, key);
    if (idx < 0) return false; // niet voor ons

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
    // basisafmeting: ~220x120 @ scale 1.0
    const int w = int(220 * m_scale);
    const int h = int(120 * m_scale);

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
    // Lees input
    // Virtuele flags voor tekenen (geen bitmasks meer nodig)

    const bool dirUp    = m_pad0.up;
    const bool dirDown  = m_pad0.down;
    const bool dirLeft  = m_pad0.left;
    const bool dirRight = m_pad0.right;
    const bool btnL     = m_pad0.fireL;
    const bool btnR     = m_pad0.fireR;

    // Helper om HUD-“bit” als pressed te tonen:
    auto keyPressed = [&](int bit)->bool {
        // bit: 0..9=digits, 10='*', 11='#' — komt overeen met onze keypad index
        return (m_pad0.keypad == bit);
    };

    p.setRenderHint(QPainter::Antialiasing, true);

    // Achtergrond paneel
    p.setPen(Qt::NoPen);
    p.setBrush(mixA(QColor(0x3d, 0xa9, 0xfc), 0xA0 + int(10*m_flash)));
    p.drawRoundedRect(r, 10*m_scale, 10*m_scale);

    // Lay-out: links D-Pad, rechts twee knoppen
    const int pad = int(10 * m_scale);
    const QRect left = r.adjusted(pad, pad, -r.width()/2 - pad/2, -pad);
    const QRect right= QRect(r.center().x()+pad/2, r.y()+pad, r.width()/2 - 2*pad, r.height()-2*pad);

    // D-Pad (kruis)
    // Tekst "PLAYER 1" boven de joystick
    const QPoint lc_text = left.center() + QPoint(0, int(1 * m_scale));     // tekst blijft hier
    const QPoint lc_pad  = lc_text + QPoint(0, int(14 * m_scale));           // pijlen 10 extra omlaag
    const QSize  asz1(int(30*m_scale), int(30*m_scale));

    QFont font = p.font();
    font.setPointSizeF(10 * m_scale);
    font.setBold(true);
    p.setFont(font);

    QString label = "PLAYER 1";
    // Positie: bijna tegen de pijl aan (slechts 2 pixels ruimte)
    int yOffset = int(asz1.height()/2 + 15*m_scale); // hoogte pijl + kleine marge
    QRect textRect(lc_text.x() - int(60*m_scale),
                   lc_text.y() - yOffset - int(20*m_scale),
                   int(120*m_scale),
                   int(20*m_scale));

    // lichte schaduw
    p.setPen(QColor(0,0,0,180));
    p.drawText(textRect.translated(1,1), Qt::AlignCenter, label);

    // hoofdtekst
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

    // Grootte en positie
    const QSize asz(int(20*m_scale), int(20*m_scale));

    drawArrow(lc_pad + QPoint(0, -asz.height()-6*m_scale), asz, dirUp,    Qt::UpArrow);
    drawArrow(lc_pad + QPoint(0,  asz.height()+6*m_scale), asz, dirDown,  Qt::DownArrow);
    drawArrow(lc_pad + QPoint(-asz.width()-6*m_scale, 0),  asz, dirLeft,  Qt::LeftArrow);
    drawArrow(lc_pad + QPoint( asz.width()+6*m_scale, 0),  asz, dirRight, Qt::RightArrow);


    // center
    p.setBrush(mixA(Qt::white, 80));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRect(lc_pad - QPoint(int(8*m_scale),int(8*m_scale)),
                        QSize(int(16*m_scale),int(16*m_scale))));

    // Rechts: A/B boven
    const int rightH   = right.height();
    const int buttonsH = int(20 * m_scale);
    const int gap      = int(8  * m_scale);
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

    // Keypad (rechts onder): 1 2 3 / 4 5 6 / 7 8 9 / # 0 *
    const QRect kpRect = QRect(right.left(), abRect.bottom()+gap, right.width(),
                               rightH - buttonsH - gap);

    // auto keyPressed = [&](int bit)->bool {
    //     return (kp & (1u << bit)) != 0;
    // };
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

void InputWidget::setJoystickDirection(bool up, bool down, bool left, bool right)
{
    m_pad0.up = up;
    m_pad0.down = down;
    m_pad0.left = left;
    m_pad0.right = right;
    coleco_setController(0, m_pad0);
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
