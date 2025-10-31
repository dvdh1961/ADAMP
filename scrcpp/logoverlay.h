#ifndef LOGOVERLAY_H
#define LOGOVERLAY_H

#include <QWidget>

class QTextEdit;

class LogOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit LogOverlay(QWidget* parent = nullptr);

    void attachTo(QWidget* target);
    void setOverlayHeight(int h);          // bv. 100
    int  overlayHeight() const { return m_height; }

    QTextEdit* editor() const { return m_text; } // om LogSink eraan te hangen

public slots:
    void showOverlay(bool on);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void paintEvent(QPaintEvent* e) override;

private:
    QWidget*  m_target = nullptr;
    QTextEdit* m_text  = nullptr;
    int       m_height = 100;

    void updateGeometryToBottom();
};

#endif // LOGOVERLAY_H
