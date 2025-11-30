#ifndef SIMPLEJOYSTICK_H
#define SIMPLEJOYSTICK_H

#include <QObject>
#include <QTimer>
#include <QSocketNotifier>

class SimpleJoystick : public QObject
{
    Q_OBJECT
public:
    explicit SimpleJoystick(QObject *parent = nullptr);
    ~SimpleJoystick();

    // joystickIndex = 0 => /dev/input/js0 of eerste beschikbare controller
    void startPolling(int joystickIndex = 0);
    void stopPolling();
    void setJoystickType(int type);

signals:
    void directionChanged(bool up, bool down, bool left, bool right);
    void fireLeftChanged(bool pressed);
    void fireRightChanged(bool pressed);
    void startPressed(bool pressed);
    void selectPressed(bool pressed);

private slots:
    void onPollTimer();
    void onSocketActivated(int fd);

private:
    int m_joystickType;
#ifdef Q_OS_LINUX
    void openLinuxJoystick(int joystickIndex);
    void closeLinux();
    void readLinuxJoystick();   // /dev/input/jsX
    void readLinuxEvdev();      // /dev/input/eventX
    bool tryOpenJs(int index);
    bool tryOpenEvdev();
#endif

#ifdef Q_OS_WIN
    void openWindowsJoystick(int joystickIndex);
    void closeWindows();
    void readWindowsJoystick();
#endif

    QTimer m_pollTimer;
    int    m_joystickIndex = 0;

#ifdef Q_OS_LINUX
    int              m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    bool             m_usingEvdev = false; // false = js*, true = event*
#endif

    // Basiswaarden voor analoge sticks (PS4 / Nintendo clone)
    bool m_firstRun = true;
    int m_joysticktype = 0;
    int  m_baseX = 0;
    int  m_baseY = 0;

    // Status geheugen
    bool m_lastUp = false;
    bool m_lastDown = false;
    bool m_lastLeft = false;
    bool m_lastRight = false;
    bool m_lastFireL = false;
    bool m_lastFireR = false;
    bool m_lastStart = false;
    bool m_lastSelect = false;
};

#endif // SIMPLEJOYSTICK_H
