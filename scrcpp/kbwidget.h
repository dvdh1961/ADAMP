#ifndef KBWIDGET_H
#define KBWIDGET_H

#include <QWidget>
#include <QObject>
#include <map>

class ColecoController;
class QKeyEvent;

class KbWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KbWidget(QWidget *parent = nullptr);

    void setController(ColecoController* controller);

    void handleKey(QKeyEvent *e, bool pressed);
    uint8_t getAdamCodeForQtKey(int qtKey) const;

public slots:
    void reloadMappings();

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    uint8_t defaultAdamCodeForKey(int qtKey);

    std::map<int, uint8_t> m_keyMap;

    ColecoController* m_controller;
};

#endif
