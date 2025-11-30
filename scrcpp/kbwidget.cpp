#include "kbwidget.h"
#include "colecocontroller.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QSettings>
#include <QMetaObject>
#include <QDebug>
#include <vector>

typedef unsigned char byte;

// --- Adam Key Code Definities ---
constexpr uint8_t ADAM_KEY_UP        = 0x0B;
constexpr uint8_t ADAM_KEY_DOWN      = 0x0A;
constexpr uint8_t ADAM_KEY_LEFT      = 0x08;
constexpr uint8_t ADAM_KEY_RIGHT     = 0x09;
constexpr uint8_t ADAM_KEY_BACKSPACE = 0x08;
constexpr uint8_t ADAM_KEY_RETURN    = 0x0D;
constexpr uint8_t ADAM_KEY_SPACE     = 0x20;
constexpr uint8_t ADAM_KEY_I         = 0x81; // F1
constexpr uint8_t ADAM_KEY_II        = 0x82; // F2
constexpr uint8_t ADAM_KEY_III       = 0x83; // F3
constexpr uint8_t ADAM_KEY_IV        = 0x84; // F4
constexpr uint8_t ADAM_KEY_V         = 0x85; // F5
constexpr uint8_t ADAM_KEY_VI        = 0x86; // F6

KbWidget::KbWidget(QWidget *parent)
    : QWidget(parent)
    , m_controller(nullptr)
{
    setVisible(false);
    reloadMappings();
}

void KbWidget::setController(ColecoController *controller)
{
    m_controller = controller;
}

void KbWidget::reloadMappings()
{
    QSettings s;
    m_keyMap.clear();

    // 'keysToMap'
    std::vector<int> keysToMap = {
        Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right,
        Qt::Key_Backspace, Qt::Key_Return, Qt::Key_Enter, Qt::Key_Space,
        Qt::Key_F1, Qt::Key_F2, Qt::Key_F3, Qt::Key_F4, Qt::Key_F5, Qt::Key_F6
    };

    for (int defaultQtKey : keysToMap)
    {
        uint8_t adamCode = defaultAdamCodeForKey(defaultQtKey);
        if (adamCode == 0) continue;

        QString settingName = QString("AdamKeyboard/v3/adam_%1").arg(adamCode); // of "v4"
        int mappedQtKey = s.value(settingName, defaultQtKey).toInt();
        m_keyMap[mappedQtKey] = adamCode;
    }

    qDebug() << "[KbWidget] Special key-mappings reloaded:" << m_keyMap.size() << "keys maped.";
}

uint8_t KbWidget::defaultAdamCodeForKey(int qtKey)
{
    // Speciale toetsen
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

bool KbWidget::eventFilter(QObject *obj, QEvent *ev)
{
    return QWidget::eventFilter(obj, ev);
}

void KbWidget::handleKey(QKeyEvent *e, bool pressed)
{
    if (!m_controller) return;

    auto it = m_keyMap.find(e->key());
    if (it == m_keyMap.end()) {
        return;
    }

    uint8_t finalCode = it->second;

    if (!pressed) {
        finalCode |= 0x80;
    }

    qDebug() << "[KbWidget] sent of SPECIAL Key Code:" << Qt::hex << finalCode;

    QMetaObject::invokeMethod(
        m_controller,
        "onAdamKeyEvent",
        Qt::QueuedConnection,
        Q_ARG(int, (int)finalCode)
        );
}

uint8_t KbWidget::getAdamCodeForQtKey(int qtKey) const
{
    auto it = m_keyMap.find(qtKey);
    if (it != m_keyMap.end()) {
        return it->second;
    }
    return 0;
}
