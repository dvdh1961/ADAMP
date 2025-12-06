#ifndef SIMPLEJOYSTICK_H
#define SIMPLEJOYSTICK_H

#include <QObject>
#include <QTimer>
#include <QSocketNotifier>

// Forward declaration van de privé-interface
class SimpleJoystickPrivate;

// Nieuw: Definieer IJoystickCallback hier zodat SimpleJoystick ervan kan erven
class IJoystickCallback {
public:
    virtual ~IJoystickCallback() = default;
    virtual void dispatchDirection(bool up, bool down, bool left, bool right) = 0;
    virtual void dispatchFireLeft(bool pressed) = 0;
    virtual void dispatchFireRight(bool pressed) = 0;
    virtual void dispatchStart(bool pressed) = 0;
    virtual void dispatchSelect(bool pressed) = 0;
    virtual void dispatchAnalogX(int value) = 0;
};

class SimpleJoystick : public QObject, public IJoystickCallback
{
    Q_OBJECT

public:
    explicit SimpleJoystick(QObject *parent = nullptr);
    ~SimpleJoystick();

    // joystickIndex = 0 => /dev/input/js0 of eerste beschikbare controller
    void startPolling(int joystickIndex = 0);
    void stopPolling();
    /*
      * 0: Generiek (Default)
      * 1: PS (PlayStation/Nintendo Clone)
      * 2: XBOX 360/XInput
    */
    void setJoystickType(int type);

signals:
    // De publieke signalen blijven hier
    void directionChanged(bool up, bool down, bool left, bool right);
    void fireLeftChanged(bool pressed);
    void fireRightChanged(bool pressed);
    void startPressed(bool pressed);
    void selectPressed(bool pressed);
    void analogXChanged(int value);

private slots:
    void onPollTimer();
#ifdef Q_OS_LINUX
    // Dit is nu een gewoon slot/functie, niet geconnecteerd via QSocketNotifier connect
    // De QSocketNotifier wordt binnen de Linux implementatie geconnecteerd aan de update() functie.
    void onSocketActivated(int fd);
#endif

private:
    int m_joystickType = 0; // Default op 0
    QTimer m_pollTimer;

    // Pointer naar de platform-specifieke implementatie (PIMPL)
    SimpleJoystickPrivate *m_privateImpl = nullptr;

    // Nieuwe callback functie om de signalen van SimpleJoystickPrivate te vervangen
    void dispatchDirection(bool up, bool down, bool left, bool right);
    void dispatchFireLeft(bool pressed);
    void dispatchFireRight(bool pressed);
    void dispatchStart(bool pressed);
    void dispatchSelect(bool pressed);
    void dispatchAnalogX(int value);
};

#endif // SIMPLEJOYSTICK_H
