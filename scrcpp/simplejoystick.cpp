#include "simplejoystick.h"
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <linux/joystick.h>
#include <linux/input.h>
#include <dirent.h>
#include <cstring>
#endif

SimpleJoystick::SimpleJoystick(QObject *parent)
    : QObject(parent)
{
    connect(&m_pollTimer, &QTimer::timeout,
            this, &SimpleJoystick::onPollTimer);
}

SimpleJoystick::~SimpleJoystick()
{
    stopPolling();
}

void SimpleJoystick::startPolling(int joystickIndex)
{
    stopPolling();
    m_joystickIndex = joystickIndex;
    m_firstRun = true; // Reset kalibratie

#ifdef Q_OS_LINUX
    openLinuxJoystick(joystickIndex);
    qDebug() << "SimpleJoystick: openLinuxJoystick called";
    qDebug() << "Joystick index =" << joystickIndex;
#endif

#ifdef Q_OS_WIN
    openWindowsJoystick(joystickIndex);
#endif

    m_pollTimer.start(16); // ~60 Hz
}

void SimpleJoystick::stopPolling()
{
    m_pollTimer.stop();

#ifdef Q_OS_LINUX
    closeLinux();
#endif

#ifdef Q_OS_WIN
    closeWindows();
#endif
}

void SimpleJoystick::onPollTimer()
{
#ifdef Q_OS_LINUX
    if (m_fd < 0) return;

    if (m_usingEvdev)
        readLinuxEvdev();
    else
        readLinuxJoystick();
#endif

#ifdef Q_OS_WIN
    readWindowsJoystick();
#endif
}

void SimpleJoystick::onSocketActivated(int /*fd*/)
{
    // Indien je QSocketNotifier wilt gebruiken i.p.v. timer
    onPollTimer();
}

// =====================================================================
// LINUX-IMPLEMENTATIE
// =====================================================================
#ifdef Q_OS_LINUX

// Probeer /dev/input/jsX te openen (oude joystick-API)
bool SimpleJoystick::tryOpenJs(int index)
{
    qDebug() << "Trying to get js device";
    QString path = QString("/dev/input/js%1").arg(index);
    qDebug() << "Trying js device:" << QString("/dev/input/js%1").arg(index);
    m_fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (m_fd >= 0) {
        m_usingEvdev = false;

        // eventueel basiswaarden inlezen (optioneel)
        m_baseX = 0;
        m_baseY = 0;

        if (!m_notifier) {
            m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
            connect(m_notifier, &QSocketNotifier::activated,
                    this, &SimpleJoystick::onSocketActivated);
        } else {
            m_notifier->setSocket(m_fd);
            m_notifier->setEnabled(true);
        }

        qDebug() << "SimpleJoystick: opened" << path;
        return true;
    }
    return false;
}

// Zoek een geschikt /dev/input/eventX device (Bluetooth / evdev)
bool SimpleJoystick::tryOpenEvdev()
{
    DIR *dir = ::opendir("/dev/input");
    if (!dir) return false;

    struct dirent *entry;
    char namePath[256];
    int fdTest = -1;

    while ((entry = ::readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        snprintf(namePath, sizeof(namePath), "/dev/input/%s", entry->d_name);
        fdTest = ::open(namePath, O_RDONLY | O_NONBLOCK);
        if (fdTest < 0)
            continue;

        // Check apparaatnaam
        char devName[256] = {0};
        if (ioctl(fdTest, EVIOCGNAME(sizeof(devName)), devName) >= 0) {
            QString qname = QString::fromLocal8Bit(devName);
            // heuristiek: controller / xbox / gamepad
            if (qname.contains("xbox", Qt::CaseInsensitive) ||
                qname.contains("gamepad", Qt::CaseInsensitive) ||
                qname.contains("controller", Qt::CaseInsensitive)) {

                // Dit is onze jongen
                m_fd = fdTest;
                m_usingEvdev = true;

                if (!m_notifier) {
                    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
                    connect(m_notifier, &QSocketNotifier::activated,
                            this, &SimpleJoystick::onSocketActivated);
                } else {
                    m_notifier->setSocket(m_fd);
                    m_notifier->setEnabled(true);
                }

                qDebug() << "SimpleJoystick: opened evdev" << namePath << "name:" << qname;
                ::closedir(dir);
                return true;
            }
        }

        ::close(fdTest);
    }

    ::closedir(dir);
    return false;
}

void SimpleJoystick::openLinuxJoystick(int joystickIndex)
{
    closeLinux();

    // 1) Eerst proberen jsX (PS4, Nintendo clone, USB Xbox, ...)
    if (tryOpenJs(joystickIndex))
        return;

    // 2) Als dat faalt, proberen evdev voor bv. Bluetooth Xbox
    if (tryOpenEvdev())
        return;

    qWarning() << "SimpleJoystick: no joystick / evdev device opened";
}

void SimpleJoystick::closeLinux()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }

    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

// ---------------------------------------------------------
// /dev/input/jsX : klassieke joystick-API
// Houdt PS4 / Nintendo-clone gedrag in stand
// ---------------------------------------------------------
void SimpleJoystick::readLinuxJoystick()
{
#ifdef Q_OS_LINUX
    if (m_fd < 0)
        return;

    struct js_event e;
    const int TH = 16000;

    while (read(m_fd, &e, sizeof(e)) > 0) {
        // Init-bit wegfilteren
        e.type &= ~JS_EVENT_INIT;

        if (e.type == JS_EVENT_BUTTON) {
            bool pressed = (e.value != 0);

            switch (e.number) {
            // ---- bestaande knoppen (laten we gewoon zo) ----
            case 0: // Fire links
                if (pressed != m_lastFireL) {
                    emit fireLeftChanged(pressed);
                    m_lastFireL = pressed;
                }
                break;
            case 1: // Fire rechts
                if (pressed != m_lastFireR) {
                    emit fireRightChanged(pressed);
                    m_lastFireR = pressed;
                }
                break;
            case 8: // Select
                if (pressed != m_lastSelect) {
                    emit selectPressed(pressed);
                    m_lastSelect = pressed;
                }
                break;
            case 9: // Start
                if (pressed != m_lastStart) {
                    emit startPressed(pressed);
                    m_lastStart = pressed;
                }
                break;

            // ---- XBOX 360 D-pad als buttons 11–14 ----
            // Dit breekt PS4/Nintendo niet, die gebruiken deze knoppen normaal niet
            case 11: // UP
            case 12: // DOWN
            case 13: // LEFT
            case 14: // RIGHT
            {
                bool up    = m_lastUp;
                bool down  = m_lastDown;
                bool left  = m_lastLeft;
                bool right = m_lastRight;

                if (pressed) {
                    // simpele mapping: 1 richting tegelijk actief
                    up = down = left = right = false;
                    if (e.number == 11) up    = true;
                    if (e.number == 12) down  = true;
                    if (e.number == 13) left  = true;
                    if (e.number == 14) right = true;
                } else {
                    // bij loslaten: alle D-pad richtingen uit
                    up = down = left = right = false;
                }

                if (up != m_lastUp || down != m_lastDown ||
                    left != m_lastLeft || right != m_lastRight) {

                    emit directionChanged(up, down, left, right);
                    m_lastUp    = up;
                    m_lastDown  = down;
                    m_lastLeft  = left;
                    m_lastRight = right;
                }
                break;
            }

            default:
                break;
            }
        }
        else if (e.type == JS_EVENT_AXIS) {
            // ---- ANALOGE STICKS / HAT-AXES ----
            // PS4 + Nintendo-clone gebruiken meestal axis 0/1 voor D-pad of stick
            // Xbox 360 kan 0/1 voor stick, 6/7 voor D-pad zijn.
            bool up    = m_lastUp;
            bool down  = m_lastDown;
            bool left  = m_lastLeft;
            bool right = m_lastRight;

            switch (e.number) {
            // horizontaal: links/rechts
            case 0: // klassieke X-as
            case 6: // vaak D-pad X bij Xbox-drivers
                left  = (e.value < -TH);
                right = (e.value >  TH);
                break;

            // verticaal: boven/onder
            case 1: // klassieke Y-as
            case 7: // vaak D-pad Y bij Xbox-drivers
                up   = (e.value < -TH);
                down = (e.value >  TH);
                break;

            default:
                break;
            }

            if (up != m_lastUp || down != m_lastDown ||
                left != m_lastLeft || right != m_lastRight) {

                emit directionChanged(up, down, left, right);
                m_lastUp    = up;
                m_lastDown  = down;
                m_lastLeft  = left;
                m_lastRight = right;
            }
        }
    }
#endif
}

// ---------------------------------------------------------
// /dev/input/eventX : evdev (Bluetooth Xbox 360, enz.)
// ---------------------------------------------------------
void SimpleJoystick::readLinuxEvdev()
{
    if (m_fd < 0) return;

    struct input_event ev;
    while (::read(m_fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == EV_KEY) {
            bool pressed = (ev.value != 0);

            switch (ev.code) {
            // --- D-pad via EV_KEY BTN_DPAD_* (sommige drivers) ---
            case BTN_DPAD_UP:
            case BTN_DPAD_DOWN:
            case BTN_DPAD_LEFT:
            case BTN_DPAD_RIGHT:
            {
                bool up    = m_lastUp;
                bool down  = m_lastDown;
                bool left  = m_lastLeft;
                bool right = m_lastRight;

                // Simpel model: 1 richting per keer
                if (pressed) {
                    up = down = left = right = false;
                    if (ev.code == BTN_DPAD_UP)    up    = true;
                    if (ev.code == BTN_DPAD_DOWN)  down  = true;
                    if (ev.code == BTN_DPAD_LEFT)  left  = true;
                    if (ev.code == BTN_DPAD_RIGHT) right = true;
                } else {
                    // loslaten -> alles uit
                    up = down = left = right = false;
                }

                if (up != m_lastUp || down != m_lastDown ||
                    left != m_lastLeft || right != m_lastRight) {

                    emit directionChanged(up, down, left, right);
                    m_lastUp    = up;
                    m_lastDown  = down;
                    m_lastLeft  = left;
                    m_lastRight = right;
                }
                break;
            }

            // --- Basis knoppen mapping (optioneel, Xbox A/B/Start/Select) ---
            case BTN_A: // Fire L
                if (pressed != m_lastFireL) {
                    emit fireLeftChanged(pressed);
                    m_lastFireL = pressed;
                }
                break;
            case BTN_B: // Fire R
                if (pressed != m_lastFireR) {
                    emit fireRightChanged(pressed);
                    m_lastFireR = pressed;
                }
                break;
            case BTN_START:
                if (pressed != m_lastStart) {
                    emit startPressed(pressed);
                    m_lastStart = pressed;
                }
                break;
            case BTN_SELECT:
            case BTN_BACK:
                if (pressed != m_lastSelect) {
                    emit selectPressed(pressed);
                    m_lastSelect = pressed;
                }
                break;

            default:
                break;
            }
        }
        else if (ev.type == EV_ABS) {
            // D-pad via ABS_HAT0X / ABS_HAT0Y (zeer typisch Bluetooth)
            if (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y) {
                int hatX = 0;
                int hatY = 0;

                // We hebben alleen de laatste waardes nodig
                if (ev.code == ABS_HAT0X) {
                    hatX = ev.value; // -1 links, 0 neutraal, 1 rechts
                    // hou Y zoals hij was, we reconstrueren uit m_last*
                    hatY = (m_lastUp ? -1 : (m_lastDown ? 1 : 0));
                } else {
                    hatY = ev.value; // -1 boven, 0, 1 onder
                    hatX = (m_lastLeft ? -1 : (m_lastRight ? 1 : 0));
                }

                bool up    = (hatY < 0);
                bool down  = (hatY > 0);
                bool left  = (hatX < 0);
                bool right = (hatX > 0);

                if (up != m_lastUp || down != m_lastDown ||
                    left != m_lastLeft || right != m_lastRight) {

                    emit directionChanged(up, down, left, right);
                    m_lastUp    = up;
                    m_lastDown  = down;
                    m_lastLeft  = left;
                    m_lastRight = right;
                }
            }
        }
    }
}

#endif // Q_OS_LINUX

// =====================================================================
// WINDOWS-IMPLEMENTATIE
// =====================================================================
#ifdef Q_OS_WIN

// Hulpvariabele om te weten of de joystick geopend is
static bool s_is_windows_joystick_open = false;

void SimpleJoystick::openWindowsJoystick(int joystickIndex)
{
    s_is_windows_joystick_open = false;
    m_joystickIndex = joystickIndex;

    for (int i = 0; i < 16; ++i) {
        // Probeer de joystick te detecteren
        JOYINFOEX joyInfo;
        joyInfo.dwSize = sizeof(JOYINFOEX);
        joyInfo.dwFlags = JOY_RETURNALL;

        if (joyGetPosEx(JOYSTICKID1 + i, &joyInfo) == JOYERR_NOERROR) {
            m_joystickIndex = i;
            s_is_windows_joystick_open = true;
            qDebug() << "Joystick: found at ID" << i;
            break;
        }
    }

    if (!s_is_windows_joystick_open) {
        qWarning() << "Joystick: NO joystick found.";
    }
}

void SimpleJoystick::closeWindows()
{
    // Geen expliciete sluiting nodig voor de Windows Multimedia API
    s_is_windows_joystick_open = false;
}

void SimpleJoystick::readWindowsJoystick()
{
    if (!s_is_windows_joystick_open) return;

    // We gebruiken JOYINFOEX om POV en knoppen uit te lezen
    JOYINFOEX joyInfo;
    joyInfo.dwSize = sizeof(JOYINFOEX);
    joyInfo.dwFlags = JOY_RETURNALL | JOY_RETURNPOV | JOY_RETURNBUTTONS;

    if (joyGetPosEx(JOYSTICKID1 + m_joystickIndex, &joyInfo) != JOYERR_NOERROR)
    {
        qWarning() << "Joystick: Error reading position from ID" << m_joystickIndex;
        s_is_windows_joystick_open = false;
        return;
    }

    // We beginnen met alles op FALSE
    bool up = false, down = false, left = false, right = false;
    int b = joyInfo.dwButtons;
    // De knopbits 0-9 zijn 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, etc.
    bool fireL;     // A
    bool fireR;     // B
    bool select;    // Select
    bool start;     // Start

    // JOYSTICK 1 GENERAL
    if (m_joystickType == 0)
    {
        // --- AUTO-KALIBRATIE ---
        if (m_firstRun)
        {
            m_baseX = joyInfo.dwXpos;
            m_baseY = joyInfo.dwYpos;
            m_firstRun = false;
            qDebug() << "Joystick CALIBRATION: Center set at X:" << m_baseX << " Y:" << m_baseY;
        }

        int deltaX = (int)joyInfo.dwXpos - m_baseX;
        int deltaY = (int)joyInfo.dwYpos - m_baseY;
        const int TH = 15000;

        if (deltaY < -TH) up = true;
        if (deltaY >  TH) down = true;
        if (deltaX < -TH) left = true;
        if (deltaX >  TH) right = true;

        // Knop 13 (Bit 12) = Vaak OMHOOG
        if (b & 0x1000) up = true;
        // Knop 14 (Bit 13) = Vaak RECHTS
        if (b & 0x2000) right = true;
        // Knop 15 (Bit 14) = Vaak OMLAAG
        if (b & 0x4000) down = true;
        // Knop 16 (Bit 15) = Vaak LINKS
        if (b & 0x8000) left = true;

        // A / FireL (Knop 1)
        fireL  = (b & 1);
        // B / FireR (Knop 2)
        fireR  = (b & 2);
        // Select (Vaak knop 7 of 9)
        select = (b & 64) || (b & 256);
        // Start (Vaak knop 8 of 10)
        start  = (b & 128) || (b & 512);
    }

    // JOYSTICK 2 PS
    if (m_joystickType == 1)
    {
        // POV HAT (Digitaal Kruisje)
        if (joyInfo.dwPOV != 65535) { // 65535 = JOY_POVCENTERED
        if (joyInfo.dwPOV == 0)     up = true;
        if (joyInfo.dwPOV == 18000) down = true;
        if (joyInfo.dwPOV == 27000) left = true;
        if (joyInfo.dwPOV == 9000)  right = true;
        // Diagonalen
        if (joyInfo.dwPOV == 4500)  { up=true; right=true; }
        if (joyInfo.dwPOV == 13500) { down=true; right=true; }
        if (joyInfo.dwPOV == 22500) { down=true; left=true; }
        if (joyInfo.dwPOV == 31500) { up=true; left=true; }
        }

        //  D-Pad via Knoppen

        // Knop 13 (Bit 12) = Vaak OMHOOG
        if (b & 0x1000) up = true;
        // Knop 14 (Bit 13) = Vaak RECHTS
        if (b & 0x2000) right = true;
        // Knop 15 (Bit 14) = Vaak OMLAAG
        if (b & 0x4000) down = true;
        // Knop 16 (Bit 15) = Vaak LINKS
        if (b & 0x8000) left = true;

        // A / FireL (Knop 1)
        fireL  = (b & 1);
        // B / FireR (Knop 2)
        fireR  = (b & 2);
        // Select (Vaak knop 7 of 9)
        select = (b & 64) || (b & 256);
        // Start (Vaak knop 8 of 10)
        start  = (b & 128) || (b & 512);
    }

    // JOYSTICK 3 XBOX 360
    if (m_joystickType == 2) // Xbox 360/XInput
    {
        // --- AUTO-KALIBRATIE (Sticks) ---
        if (m_firstRun)
        {
            m_baseX = joyInfo.dwXpos;
            m_baseY = joyInfo.dwYpos;
            m_firstRun = false;
            qDebug() << "Joystick CALIBRATION: Center set at X:" << m_baseX << " Y:" << m_baseY;
        }

        int deltaX = (int)joyInfo.dwXpos - m_baseX;
        int deltaY = (int)joyInfo.dwYpos - m_baseY;
        const int TH = 15000;

        // Beweging via de Linker Analog Stick (Axis 0/1)
        if (deltaY < -TH) up = true;
        if (deltaY >  TH) down = true;
        if (deltaX < -TH) left = true;
        if (deltaX >  TH) right = true;

        // D-Pad via POV HAT (Dit is de meest betrouwbare D-pad input voor XInput op Windows)
        if (joyInfo.dwPOV != 65535) {
            if (joyInfo.dwPOV == 0)     up = true;
            if (joyInfo.dwPOV == 18000) down = true;
            if (joyInfo.dwPOV == 27000) left = true;
            if (joyInfo.dwPOV == 9000)  right = true;
            // Diagonalen
            if (joyInfo.dwPOV == 4500)  { up=true; right=true; }
            if (joyInfo.dwPOV == 13500) { down=true; right=true; }
            if (joyInfo.dwPOV == 22500) { down=true; left=true; }
            if (joyInfo.dwPOV == 31500) { up=true; left=true; }
        }

        // Knoppen Mapping (gebaseerd op veelgebruikte DirectInput indices)
        // A (FireL) is Bit 0
        fireL  = (b & 1);
        // B (FireR) is Bit 1
        fireR  = (b & 2);
        // Back/Select is Bit 6 (64)
        select = (b & 64);
        // Start is Bit 7 (128)
        start  = (b & 128);
    }

    // Verstuur als er iets verandert (ongeacht welke methode het triggerde)
    if (up != m_lastUp || down != m_lastDown || left != m_lastLeft || right != m_lastRight)
        {
            emit directionChanged(up, down, left, right);
            m_lastUp = up; m_lastDown = down; m_lastLeft = left; m_lastRight = right;
        }
    if (fireL != m_lastFireL)
        {
            emit fireLeftChanged(fireL);
            m_lastFireL = fireL;
        }
    if (fireR != m_lastFireR)
        {
            emit fireRightChanged(fireR);
            m_lastFireR = fireR;
        }
    if (start != m_lastStart)
        {
            emit startPressed(start);
            m_lastStart = start;
        }
    if (select!= m_lastSelect)
        {
            emit selectPressed(select);
            m_lastSelect = select;
        }
}
#endif // Q_OS_WIN

void SimpleJoystick::setJoystickType(int type)
{
    if (m_joystickType == type) {
        return;
    }

    m_joystickType = type;
    qDebug() << "SimpleJoystick: Type set to" << type;

    // Als de timer actief is, sluiten we de driver/device om de verbinding te resetten,
    // en laten we de MainWindow de herstart doen.
    if (m_pollTimer.isActive()) {

// Timer loopt, dus we sluiten ALLEEN de driver, NIET de timer.
#ifdef Q_OS_LINUX
        closeLinux(); // Dit sluit de file descriptor m_fd
#endif

#ifdef Q_OS_WIN
        closeWindows(); // Dit reset de Windows handle
#endif

        // De MainWindow moet nu de startPolling opnieuw aanroepen.
    }
}
