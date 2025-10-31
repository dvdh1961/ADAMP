#include "LogOverlay.h"
#include <QTextEdit>
#include <QPainter>
#include <QEvent>

LogOverlay::LogOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true); // geen clicks nodig; zet op false als je selectie wil
    setFocusPolicy(Qt::NoFocus);

    m_text = new QTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setFrameStyle(QFrame::NoFrame);
    m_text->setStyleSheet("QTextEdit { background: transparent; color: white; }");
}

void LogOverlay::attachTo(QWidget* target)
{
    m_target = target;
    if (!m_target) return;

    setParent(m_target);
    m_target->installEventFilter(this);
    updateGeometryToBottom();
    raise();
    hide(); // start onzichtbaar
}

void LogOverlay::setOverlayHeight(int h)
{
    m_height = qMax(0, h);
    updateGeometryToBottom();
    update();
}

void LogOverlay::showOverlay(bool on)
{
    if (on) {
        updateGeometryToBottom();
        show();
        raise();
    } else {
        hide();
    }
}

bool LogOverlay::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_target) {
        switch (ev->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
            updateGeometryToBottom();
            raise();
            break;
        default: break;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void LogOverlay::updateGeometryToBottom()
{
    if (!m_target) return;
    const int w = m_target->width();
    const int h = m_height;
    setGeometry(0, m_target->height() - h, w, h);
    if (m_text) m_text->setGeometry(rect().adjusted(8, 6, -8, -6));
}

void LogOverlay::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);
    // semi-transparant paneel achter de tekst
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRect r = rect().adjusted(2, 2, -2, -2);
    QColor bg(0,0,0,150);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, 8, 8);
}
