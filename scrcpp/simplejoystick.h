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

    void startPolling(int joystickIndex = 0);
    void stopPolling();

signals:
    void directionChanged(bool up, bool down, bool left, bool right);
    void fireLeftChanged(bool pressed);
    void fireRightChanged(bool pressed);
    void startPressed(bool pressed);
    void selectPressed(bool pressed);

private slots:
    void pollWindows();
    void readLinux();

private:
    QTimer *m_pollTimer;
    int m_joystickIndex;
    int m_fd;
    QSocketNotifier *m_notifier;

    // Kalibratie
    bool m_firstRun = true;
    int  m_baseX = 32768;
    int  m_baseY = 32768;

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
