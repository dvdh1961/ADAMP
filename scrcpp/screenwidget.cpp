#include "screenwidget.h"
#include <QMutexLocker>
#include <cstring> // voor std::memcpy


ScreenWidget::ScreenWidget(QWidget *parent)
    : QWidget(parent),
    m_frame(COLECO_WIDTH, COLECO_HEIGHT, QImage::Format_RGB32)
{
    // Start met 2x zoom zoals je al deed, maar via helper
    applyFixedSize();

    // Begin met een zwart scherm
    m_frame.fill(Qt::black);
}

ScreenWidget::~ScreenWidget()
{
}

QSize ScreenWidget::sizeHint() const {
    return QSize(int(256*m_scale), int(192*m_scale));
}
QSize ScreenWidget::minimumSizeHint() const {
    return QSize(256,192);
}

void ScreenWidget::applyFixedSize()
{
    // Houd dezelfde “fixed size”-aanpak aan, maar afgeleid van m_scale
    const int w = int(COLECO_WIDTH  * m_scale + 0.5);
    const int h = int(COLECO_HEIGHT * m_scale + 0.5);
    setFixedSize(w, h);
}

void ScreenWidget::setScale(qreal s)
{
    if (s < 1.0) s = 1.0;
    if (qFuzzyCompare(s, m_scale)) return;
    m_scale = s;
    applyFixedSize(); // behoud je vaste-size gedrag
    update();
}

// Dit slot wordt aangeroepen vanuit de EMULATOR THREAD
void ScreenWidget::updateFrame(const QImage &frame)
{
    {
        // Vergrendel de mutex, want we schrijven naar m_frame
        QMutexLocker locker(&m_mutex);
        m_frame = frame.copy(); // Kopieer de data
    }

    // Vraag Qt om de widget opnieuw te tekenen (in UI-thread).
    update();
}

void ScreenWidget::setFrame(const QImage &img)
{
    QMutexLocker locker(&m_mutex);
    if (img.size() == QSize(256,192) && img.format() == QImage::Format_RGB32) {
        m_frame = img; // 1:1
    } else {
        m_frame = img.convertToFormat(QImage::Format_RGB32)
        .scaled(256, 192, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    // GEEN schaal naar widget hier
    locker.unlock();
    update();
}

void ScreenWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);

    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true); // -> scherpe/zachte pixels

    // teken zwarte achtergrond
    p.fillRect(rect(), Qt::black);

    QImage frameCopy;
    {
        QMutexLocker lock(&m_mutex);
        frameCopy = m_frame;            // verwacht 256×192
    }
    if (!frameCopy.isNull()) {
        p.save();
        p.scale(m_scale, m_scale);      // >>> schaal ALLES wat we tekenen
        p.drawImage(0, 0, frameCopy);   // getekend op (0,0) maar 2× zo groot
        p.restore();
    }
}

