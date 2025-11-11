#ifndef KBWIDGET_H
#define KBWIDGET_H

#include <QWidget>
#include <QObject>
#include <map>

// Forward-declaratie van de controller
class ColecoController;
class QKeyEvent;

/**
 * @brief KbWidget (Keyboard Widget)
 * Een niet-zichtbaar widget dat key-events ontvangt (van MainWindow)
 * en vertaalt naar Adam-specifieke key-codes.
 */
class KbWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KbWidget(QWidget *parent = nullptr);

    /**
     * @brief Koppelt deze widget aan de emulator-controller.
     */
    void setController(ColecoController* controller);

    /**
     * @brief Verwerkt een enkele toets-druk of -loslaat.
     * (Aangeroepen door MainWindow::keyPress/ReleaseEvent)
     */
    void handleKey(QKeyEvent *e, bool pressed);
    uint8_t getAdamCodeForQtKey(int qtKey) const;

public slots:
    /**
     * @brief Herlaadt de toets-map vanuit de QSettings.
     */
    void reloadMappings();

protected:
    /**
     * @brief Vangt events af (wordt niet meer gebruikt voor keys).
     */
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    /**
     * @brief Bepaalt de standaard (fabrieks) Adam-code voor een Qt-toets.
     */
    uint8_t defaultAdamCodeForKey(int qtKey);

     // De actieve map: [Qt::Key] -> [Adam Key Code]
     // Bv. [Qt::Key_Up] -> 0x0B
    std::map<int, uint8_t> m_keyMap;

     //Pointer naar de controller in de emulator-thread.
    ColecoController* m_controller;
};

#endif // KBWIDGET_H
