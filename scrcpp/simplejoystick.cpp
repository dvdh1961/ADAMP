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
#include <sys/ioctl.h>
#include <errno.h>
#endif

SimpleJoystick::SimpleJoystick(QObject *parent) : QObject(parent), m_pollTimer(nullptr), m_fd(-1), m_notifier(nullptr)
{
}

SimpleJoystick::~SimpleJoystick()
{
    stopPolling();
}

void SimpleJoystick::startPolling(int /*joystickIndex*/)
{
    stopPolling();
    m_firstRun = true; // Reset kalibratie

#ifdef Q_OS_WIN
    bool found = false;
    for (int i = 0; i < 16; ++i) {
        JOYINFOEX joyInfo;
        joyInfo.dwSize = sizeof(JOYINFOEX);
        joyInfo.dwFlags = JOY_RETURNALL;

        if (joyGetPosEx(JOYSTICKID1 + i, &joyInfo) == JOYERR_NOERROR) {
            m_joystickIndex = i;
            qDebug() << "Joystick: found at ID" << i;
            found = true;
            break;
        }
    }

    if (!found) {
        qWarning() << "Joystick: NO joystick found.";
        return;
    }

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &SimpleJoystick::pollWindows);
    m_pollTimer->start(20);
#endif

#ifdef Q_OS_LINUX
    QString devName = QString("/dev/input/js%1").arg(joystickIndex);
    m_fd = open(devName.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (m_fd >= 0) {
        m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &SimpleJoystick::readLinux);
    }
#endif
}

void SimpleJoystick::stopPolling()
{
    if (m_pollTimer) { m_pollTimer->stop(); delete m_pollTimer; m_pollTimer = nullptr; }
    if (m_notifier) { delete m_notifier; m_notifier = nullptr; }
#ifdef Q_OS_LINUX
    if (m_fd >= 0) { close(m_fd); m_fd = -1; }
#endif
}

void SimpleJoystick::pollWindows()
{
#ifdef Q_OS_WIN
    JOYINFOEX joyInfo;
    joyInfo.dwSize = sizeof(JOYINFOEX);
    joyInfo.dwFlags = JOY_RETURNALL;

    if (joyGetPosEx(JOYSTICKID1 + m_joystickIndex, &joyInfo) == JOYERR_NOERROR) {

        // --- AUTO-KALIBRATIE (Voor analoge sticks die niet op 32768 staan) ---
        if (m_firstRun) {
            m_baseX = joyInfo.dwXpos;
            m_baseY = joyInfo.dwYpos;
            m_firstRun = false;
            qDebug() << "Joystick CALIBRATION: Center set at X:" << m_baseX << " Y:" << m_baseY;
        }

        // We beginnen met alles op FALSE
        bool up = false, down = false, left = false, right = false;

        // --- METHODE 1: ANALOGE ASSEN (Met relatieve kalibratie) ---
        int deltaX = (int)joyInfo.dwXpos - m_baseX;
        int deltaY = (int)joyInfo.dwYpos - m_baseY;
        const int TH = 15000;

        if (deltaY < -TH) up = true;
        if (deltaY >  TH) down = true;
        if (deltaX < -TH) left = true;
        if (deltaX >  TH) right = true;

        // --- METHODE 2: POV HAT (Digitaal Kruisje) ---
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

        // --- METHODE 3: KNOPPEN 13-16 (Playstation Clone D-Pad) ---
        int b = joyInfo.dwButtons;

        // Knop 13 (Bit 12) = Vaak OMHOOG
        if (b & 0x1000) up = true;
        // Knop 14 (Bit 13) = Vaak RECHTS
        if (b & 0x2000) right = true;
        // Knop 15 (Bit 14) = Vaak OMLAAG
        if (b & 0x4000) down = true;
        // Knop 16 (Bit 15) = Vaak LINKS
        if (b & 0x8000) left = true;


        // Verstuur als er iets verandert (ongeacht welke methode het triggerde)
        if (up != m_lastUp || down != m_lastDown || left != m_lastLeft || right != m_lastRight) {
            emit directionChanged(up, down, left, right);
            m_lastUp = up; m_lastDown = down; m_lastLeft = left; m_lastRight = right;
        }

        // --- ACTIE KNOPPEN ---
        // A / FireL (Knop 1, 3, 5)
        bool fireL  = (b & 1) || (b & 4) || (b & 16);

        // B / FireR (Knop 2, 4, 6)
        bool fireR  = (b & 2) || (b & 8) || (b & 32);

        // Select (Knop 5, 7, 9)
        bool select = (b & 16) || (b & 64) || (b & 256);

        // Start (Knop 6, 8, 10)
        bool start  = (b & 32) || (b & 128) || (b & 512);

        if (fireL != m_lastFireL) { emit fireLeftChanged(fireL); m_lastFireL = fireL; }
        if (fireR != m_lastFireR) { emit fireRightChanged(fireR); m_lastFireR = fireR; }
        if (start != m_lastStart) { emit startPressed(start); m_lastStart = start; }
        if (select!= m_lastSelect){ emit selectPressed(select); m_lastSelect = select; }
    }
#endif
}

void SimpleJoystick::readLinux() {
#ifdef Q_OS_LINUX
    // Linux deel
    struct js_event e;
    while (read(m_fd, &e, sizeof(e)) > 0) {
        e.type &= ~JS_EVENT_INIT;
        if (e.type == JS_EVENT_BUTTON) {
            bool p = e.value;
            if (e.number == 0) emit fireLeftChanged(p);
            if (e.number == 1) emit fireRightChanged(p);
            if (e.number == 8) emit selectPressed(p);
            if (e.number == 9) emit startPressed(p);
        } else if (e.type == JS_EVENT_AXIS) {
            const int TH = 16000;
            if (e.number == 0) emit directionChanged(m_lastUp, m_lastDown, e.value < -TH, e.value > TH);
            if (e.number == 1) emit directionChanged(e.value < -TH, e.value > TH, m_lastLeft, m_lastRight);
        }
    }
#endif
}
