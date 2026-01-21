#include "simplejoystick.h"
#include <QDebug>
#include <algorithm> // Nodig voor std::min/max

// Nodige OS headers
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
#include <sys/ioctl.h>
#include <cstdio>
#endif

// =====================================================================
// 1. ABSTRACTE BASISKLASSE EN GEDEELDE MAPPING LOGICA (PUUR C++)
// =====================================================================

// Nieuwe struct om ruwe input door te geven (uniforme data structuur)
struct RawJoystickInput {
    int buttonID = -1;
    bool buttonPressed = false;
    int axisID = -1;
    int axisValue = 0;
    enum HatDirection { Neutral, Up, Down, Left, Right, UpLeft, UpRight, DownLeft, DownRight };
    HatDirection hatDirection = Neutral;
};


class SimpleJoystickPrivate // GEEN Q_OBJECT MEER!
{
public:
    // De constructor ontvangt een pointer naar de callback-interface (SimpleJoystick)
    explicit SimpleJoystickPrivate(IJoystickCallback* callback) : m_callback(callback) {}
    virtual ~SimpleJoystickPrivate() = default;

    // Pure virtuele functies (de interface)
    virtual bool start(int joystickIndex) = 0;
    virtual void stop() = 0;
    virtual void update() = 0; // Wordt aangeroepen door de QTimer
    void setType(int type) { m_joystickType = type; m_firstRun = true; } // Gedeeld

protected:
    IJoystickCallback* m_callback; // De callback om signalen te simuleren
    int m_joystickType = 0;
    bool m_firstRun = true;

    // Status geheugen (in de basisklasse voor de gedeelde mapping)
    bool m_lastUp = false;
    bool m_lastDown = false;
    bool m_lastLeft = false;
    bool m_lastRight = false;
    bool m_lastFireL = false;
    bool m_lastFireR = false;
    bool m_lastStart = false;
    bool m_lastSelect = false;

    // Gedeelde mapping functie!
    void mapAndEmitInput(const RawJoystickInput& input);
};


void SimpleJoystickPrivate::mapAndEmitInput(const RawJoystickInput& input)
{
    bool up    = m_lastUp;
    bool down  = m_lastDown;
    bool left  = m_lastLeft;
    bool right = m_lastRight;
    bool fireL = m_lastFireL;
    bool fireR = m_lastFireR;
    bool select = m_lastSelect;
    bool start = m_lastStart;

    const int TH = 16000; // Dead zone threshold

    // 1. ANALOGE SPINNER/X-AS (Axis 0)
    if (input.axisID == 0) {
        m_callback->dispatchAnalogX(input.axisValue);
    }

    // 2. AXIS MAPPING (voor D-pad/richting via analoge sticks)
    if (input.axisID != -1 && input.axisID != 0) {
        // Horizontale as
        if (input.axisID == 0 || input.axisID == 6) {
            left  = (input.axisValue < -TH);
            right = (input.axisValue >  TH);
        }
        // Verticale as
        if (input.axisID == 1 || input.axisID == 7) {
            up   = (input.axisValue < -TH);
            down = (input.axisValue >  TH);
        }
    }


    // 3. BUTTON MAPPING (voor Fire, Start/Select en D-pad knoppen)
    if (input.buttonID != -1) {
        bool pressed = input.buttonPressed;

        switch (m_joystickType) {
        case 0:
        case 1:
        {
            if (input.buttonID == 0) fireL = pressed;
            if (input.buttonID == 1) fireR = pressed;

            if (input.buttonID == 8) select = pressed;
            if (input.buttonID == 9) start = pressed;

// Platform-specifieke knoppen binnen de gedeelde functie
#ifdef Q_OS_WIN
            if (input.buttonID == 6) select = pressed;
            if (input.buttonID == 7) start = pressed;
#endif

            if (input.buttonID >= 11 && input.buttonID <= 14) {
                if (pressed) {
                    up = down = left = right = false;
                    if (input.buttonID == 11) up = true;
                    if (input.buttonID == 12) down = true;
                    if (input.buttonID == 13) left = true;
                    if (input.buttonID == 14) right = true;
                } else { up = down = left = right = false; }
            }
            break;
        }

        case 2: // XBOX 360 Mapping
        {
            if (input.buttonID == 0) fireL = pressed;
            if (input.buttonID == 1) fireR = pressed;

            if (input.buttonID == 6) select = pressed;
            if (input.buttonID == 7) start = pressed;

            if (input.buttonID >= 11 && input.buttonID <= 14) {
                if (pressed) {
                    up = down = left = right = false;
                    if (input.buttonID == 11) up = true;
                    if (input.buttonID == 12) down = true;
                    if (input.buttonID == 13) left = true;
                    if (input.buttonID == 14) right = true;
                } else { up = down = left = right = false; }
            }
            break;
        }
        } // einde switch (m_joystickType)
    }

    // 4. HAT/POV MAPPING (D-pad via hoed-switch)
    if (input.hatDirection != RawJoystickInput::Neutral) {
        up    = (input.hatDirection == RawJoystickInput::Up || input.hatDirection == RawJoystickInput::UpLeft || input.hatDirection == RawJoystickInput::UpRight);
        down  = (input.hatDirection == RawJoystickInput::Down || input.hatDirection == RawJoystickInput::DownLeft || input.hatDirection == RawJoystickInput::DownRight);
        left  = (input.hatDirection == RawJoystickInput::Left || input.hatDirection == RawJoystickInput::UpLeft || input.hatDirection == RawJoystickInput::DownLeft);
        right = (input.hatDirection == RawJoystickInput::Right || input.hatDirection == RawJoystickInput::UpRight || input.hatDirection == RawJoystickInput::DownRight);
    } else if (input.hatDirection == RawJoystickInput::Neutral) {
        if (input.axisID == -1 && input.buttonID == -1) {
            up = down = left = right = false;
        }
    }


    // --- DISPATCH (EMIT SIMULATIE) UITSLUITEND ALS DE WAARDE IS GEWIJZIGD ---

    if (up != m_lastUp || down != m_lastDown || left != m_lastLeft || right != m_lastRight) {
        m_callback->dispatchDirection(up, down, left, right);
        m_lastUp = up; m_lastDown = down; m_lastLeft = left; m_lastRight = right;
    }
    if (fireL != m_lastFireL) { m_callback->dispatchFireLeft(fireL); m_lastFireL = fireL; }
    if (fireR != m_lastFireR) { m_callback->dispatchFireRight(fireR); m_lastFireR = fireR; }
    if (start != m_lastStart) { m_callback->dispatchStart(start); m_lastStart = start; }
    if (select!= m_lastSelect) { m_callback->dispatchSelect(select); m_lastSelect = select; }
}


// =====================================================================
// 2. CONCRETE IMPLEMENTATIES (per OS) - PURE C++ KLASSEN
// =====================================================================

#ifdef Q_OS_LINUX

class SimpleJoystickLinux : public SimpleJoystickPrivate
{
public:
    // Ontvangt de IJoystickCallback* van de publieke SimpleJoystick
    SimpleJoystickLinux(IJoystickCallback* callback) : SimpleJoystickPrivate(callback) {}
    ~SimpleJoystickLinux() override { closeLinux(); }

    bool start(int joystickIndex) override;
    void stop() override { closeLinux(); }
    void update() override;

private:
    int m_fd = -1;
    // QSocketNotifier blijft nodig, maar wordt geconnecteerd buiten de private klasse
    QSocketNotifier *m_notifier = nullptr;
    bool m_usingEvdev = false;

    void closeLinux();
    void readLinuxJoystick();
    void readLinuxEvdev();
    bool tryOpenJs(int index);
    bool tryOpenEvdev();
    // onSocketActivated is niet meer nodig als interne slot
};

bool SimpleJoystickLinux::tryOpenJs(int index)
{
    QString path = QString("/dev/input/js%1").arg(index);
    m_fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (m_fd >= 0) {
        m_usingEvdev = false;
        // NIEUW: De notifier wordt NU geconnecteerd in de SimpleJoystick::startPolling
        qDebug() << "[JOY] SimpleJoystick: opened" << path;
        return true;
    }
    return false;
}

bool SimpleJoystickLinux::tryOpenEvdev()
{
    DIR *dir = ::opendir("/dev/input");
    if (!dir) return false;

    struct dirent *entry;
    char namePath[256];
    int fdTest = -1;

    while ((entry = ::readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        snprintf(namePath, sizeof(namePath), "/dev/input/%s", entry->d_name);
        fdTest = ::open(namePath, O_RDONLY | O_NONBLOCK);
        if (fdTest < 0) continue;

        char devName[256] = {0};
        if (ioctl(fdTest, EVIOCGNAME(sizeof(devName)), devName) >= 0) {
            QString qname = QString::fromLocal8Bit(devName);
            if (qname.contains("xbox", Qt::CaseInsensitive) ||
                qname.contains("gamepad", Qt::CaseInsensitive) ||
                qname.contains("controller", Qt::CaseInsensitive)) {
                m_fd = fdTest;
                m_usingEvdev = true;
                // De notifier is in dit model nog steeds een probleem,
                // we vertrouwen op de SimpleJoystick::onPollTimer voor polling
                qDebug() << "[JOY] SimpleJoystick: opened evdev" << namePath << "name:" << qname;
                ::closedir(dir);
                return true;
            }
        }
        ::close(fdTest);
    }
    ::closedir(dir);
    return false;
}

bool SimpleJoystickLinux::start(int joystickIndex)
{
    closeLinux();
    if (tryOpenJs(joystickIndex)) return true;
    if (tryOpenEvdev()) return true;
    qWarning() << "SimpleJoystick: no joystick / evdev device opened";
    return false;
}

void SimpleJoystickLinux::closeLinux()
{
    // De notifier wordt NU VERWIJDERD EN BEHEERD IN SimpleJoystick
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

void SimpleJoystickLinux::update()
{
    if (m_fd < 0) return;
    if (m_usingEvdev)
        readLinuxEvdev();
    else
        readLinuxJoystick();
}

void SimpleJoystickLinux::readLinuxJoystick()
{
    if (m_fd < 0) return;
    struct js_event e;
    while (read(m_fd, &e, sizeof(e)) > 0) {
        e.type &= ~JS_EVENT_INIT;
        RawJoystickInput input;

        if (e.type == JS_EVENT_BUTTON) {
            input.buttonID = e.number;
            input.buttonPressed = (e.value != 0);
            mapAndEmitInput(input);
        }
        else if (e.type == JS_EVENT_AXIS) {
            input.axisID = e.number;
            input.axisValue = e.value;
            if (input.axisID == 0) {
                input.axisValue = -input.axisValue;
            }
            mapAndEmitInput(input);
        }
    }
}

void SimpleJoystickLinux::readLinuxEvdev()
{
    if (m_fd < 0) return;
    struct input_event ev;
    while (::read(m_fd, &ev, sizeof(ev)) > 0) {
        RawJoystickInput input;
        if (ev.type == EV_KEY) {
            input.buttonID = ev.code;
            input.buttonPressed = (ev.value != 0);
            mapAndEmitInput(input);
        }
        else if (ev.type == EV_ABS) {
            if (ev.code == ABS_X || ev.code == ABS_Y) {
                input.axisID = ev.code == ABS_X ? 0 : 1;
                input.axisValue = ev.value;
                mapAndEmitInput(input);
            }
            if (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y) {
                int hatX = ev.code == ABS_HAT0X ? ev.value : (m_lastLeft ? -1 : (m_lastRight ? 1 : 0));
                int hatY = ev.code == ABS_HAT0Y ? ev.value : (m_lastUp ? -1 : (m_lastDown ? 1 : 0));

                if (hatX == 0 && hatY == 0) input.hatDirection = RawJoystickInput::Neutral;
                else if (hatX == 0 && hatY < 0) input.hatDirection = RawJoystickInput::Up;
                else if (hatX == 0 && hatY > 0) input.hatDirection = RawJoystickInput::Down;
                else if (hatX < 0 && hatY == 0) input.hatDirection = RawJoystickInput::Left;
                else if (hatX > 0 && hatY == 0) input.hatDirection = RawJoystickInput::Right;
                // Diagonalen...
                mapAndEmitInput(input);
            }
        }
    }
}

#endif // Q_OS_LINUX

// =====================================================================
#ifdef Q_OS_WIN

class SimpleJoystickWindows : public SimpleJoystickPrivate
{
public:
    SimpleJoystickWindows(IJoystickCallback* callback) : SimpleJoystickPrivate(callback) {}
    ~SimpleJoystickWindows() override { closeWindows(); }

    bool start(int joystickIndex) override;
    void stop() override { closeWindows(); }
    void update() override;

private:
    int m_joystickIndex = 0;
    bool m_is_joystick_open = false;

    void openWindowsJoystick(int joystickIndex);
    void closeWindows();
    void readWindowsJoystick();
};

void SimpleJoystickWindows::openWindowsJoystick(int joystickIndex)
{
    m_is_joystick_open = false;
    m_joystickIndex = joystickIndex;

    for (int i = 0; i < 16; ++i) {
        JOYINFOEX joyInfo;
        joyInfo.dwSize = sizeof(JOYINFOEX);
        joyInfo.dwFlags = JOY_RETURNALL;

        if (joyGetPosEx(JOYSTICKID1 + i, &joyInfo) == JOYERR_NOERROR) {
            m_joystickIndex = i;
            m_is_joystick_open = true;
            qDebug() << "[JOY] Joystick: found at ID" << i;
            break;
        }
    }

    if (!m_is_joystick_open) {
        qWarning() << "Joystick: NO joystick found.";
    }
}

void SimpleJoystickWindows::closeWindows()
{
    m_is_joystick_open = false;
}

bool SimpleJoystickWindows::start(int joystickIndex)
{
    closeWindows();
    openWindowsJoystick(joystickIndex);
    return m_is_joystick_open;
}

void SimpleJoystickWindows::update()
{
    if (m_firstRun) {
        m_firstRun = false;
    }
    readWindowsJoystick();
}

void SimpleJoystickWindows::readWindowsJoystick()
{
    if (!m_is_joystick_open) return;

    JOYINFOEX joyInfo;
    joyInfo.dwSize = sizeof(JOYINFOEX);
    joyInfo.dwFlags = JOY_RETURNALL | JOY_RETURNPOV | JOY_RETURNBUTTONS;

    if (joyGetPosEx(JOYSTICKID1 + m_joystickIndex, &joyInfo) != JOYERR_NOERROR)
    {
        m_is_joystick_open = false;
        return;
    }

    const int IDEAL_CENTER = 32768;
    const int MAX_SAFE_VALUE = 32767;

    // 1. ANALOGE X-AS (SPINNER)
    RawJoystickInput analogXInput;
    analogXInput.axisID = 0; // X-as

    // Bereken de waarde (Left = +32768, Right = -32767)
    int calculatedValue = -((int)joyInfo.dwXpos - IDEAL_CENTER);

    // NIEUW: Klem de waarde om te garanderen dat 32768 niet wordt verzonden.
    if (calculatedValue > MAX_SAFE_VALUE) {
        calculatedValue = MAX_SAFE_VALUE;
    } else if (calculatedValue < -MAX_SAFE_VALUE) {
        calculatedValue = -MAX_SAFE_VALUE;
    }

    analogXInput.axisValue = calculatedValue;
    mapAndEmitInput(analogXInput);

    // 2. KNOPPEN
    int b = joyInfo.dwButtons;
    for (int i = 0; i < 16; ++i) {
        bool pressed = (b & (1 << i));
        RawJoystickInput buttonInput;
        buttonInput.buttonID = i;
        buttonInput.buttonPressed = pressed;
        mapAndEmitInput(buttonInput);
    }

    // 3. POV HAT
    RawJoystickInput hatInput;
    hatInput.axisID = -1;
    hatInput.buttonID = -1;

    if (joyInfo.dwPOV == 65535) {
        hatInput.hatDirection = RawJoystickInput::Neutral;
        mapAndEmitInput(hatInput);
    } else {
        if      (joyInfo.dwPOV == 0)     hatInput.hatDirection = RawJoystickInput::Up;
        else if (joyInfo.dwPOV == 4500)  hatInput.hatDirection = RawJoystickInput::UpRight;
        else if (joyInfo.dwPOV == 9000)  hatInput.hatDirection = RawJoystickInput::Right;
        else if (joyInfo.dwPOV == 13500) hatInput.hatDirection = RawJoystickInput::DownRight;
        else if (joyInfo.dwPOV == 18000) hatInput.hatDirection = RawJoystickInput::Down;
        else if (joyInfo.dwPOV == 22500) hatInput.hatDirection = RawJoystickInput::DownLeft;
        else if (joyInfo.dwPOV == 27000) hatInput.hatDirection = RawJoystickInput::Left;
        else if (joyInfo.dwPOV == 31500) hatInput.hatDirection = RawJoystickInput::UpLeft;
        else                             hatInput.hatDirection = RawJoystickInput::Neutral; // safety

        mapAndEmitInput(hatInput);
    }
}

#endif // Q_OS_WIN

// =====================================================================
// 3. PUBLIEKE IMPLEMENTATIE (SimpleJoystick) - DE CALLBACK FUNCTIES
// =====================================================================

// De publieke SimpleJoystick implementeert de callback-interface.

void SimpleJoystick::dispatchDirection(bool up, bool down, bool left, bool right) {
    emit directionChanged(up, down, left, right);
}
void SimpleJoystick::dispatchFireLeft(bool pressed) {
    emit fireLeftChanged(pressed);
}
void SimpleJoystick::dispatchFireRight(bool pressed) {
    emit fireRightChanged(pressed);
}
void SimpleJoystick::dispatchStart(bool pressed) {
    emit startPressed(pressed);
}
void SimpleJoystick::dispatchSelect(bool pressed) {
    emit selectPressed(pressed);
}
void SimpleJoystick::dispatchAnalogX(int value) {
    emit analogXChanged(value);
}

SimpleJoystick::SimpleJoystick(QObject *parent)
    : QObject(parent)
{
    // De constructor roept nu de juiste C++ klasse aan met 'this' als callback (IJoystickCallback*)
#ifdef Q_OS_LINUX
    m_privateImpl = new SimpleJoystickLinux(this);
#elif defined(Q_OS_WIN)
    m_privateImpl = new SimpleJoystickWindows(this);
#else
    m_privateImpl = nullptr;
    qWarning() << "SimpleJoystick: Unsupported operating system.";
#endif

    connect(&m_pollTimer, &QTimer::timeout,
            this, &SimpleJoystick::onPollTimer);

    // ALLE QObject::connect() calls van m_privateImpl zijn verwijderd.
}

SimpleJoystick::~SimpleJoystick()
{
    stopPolling();
    // Handmatige verwijdering, omdat m_privateImpl geen QObject parent meer heeft.
    delete m_privateImpl;
    m_privateImpl = nullptr;
}

void SimpleJoystick::startPolling(int joystickIndex)
{
    if (!m_privateImpl) return;

    stopPolling();
    m_privateImpl->setType(m_joystickType);

    if (m_privateImpl->start(joystickIndex)) {
        m_pollTimer.start(16); // ~60 Hz
        qDebug() << "[JOY] SimpleJoystick: Polling started.";
    } else {
        qWarning() << "SimpleJoystick: Failed to start platform device.";
    }
}

void SimpleJoystick::stopPolling()
{
    m_pollTimer.stop();
    if (m_privateImpl) {
        m_privateImpl->stop();
    }
}

void SimpleJoystick::setJoystickType(int type)
{
    if (m_joystickType == type) {
        return;
    }

    m_joystickType = type;
    qDebug() << "[JOY] SimpleJoystick: Type set to" << type;

    if (m_pollTimer.isActive()) {
        m_privateImpl->setType(m_joystickType);
    } else if (m_privateImpl) {
        m_privateImpl->setType(m_joystickType);
    }
}

void SimpleJoystick::onPollTimer()
{
    if (m_privateImpl) {
        m_privateImpl->update();
    }
}

#ifdef Q_OS_LINUX
// Dit slot is nu herbruikt en roept de update aan wanneer de socket een event detecteert.
void SimpleJoystick::onSocketActivated(int /*fd*/)
{
    if (m_privateImpl) {
        m_privateImpl->update();
    }
}
#endif
