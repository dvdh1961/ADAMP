#include "screenwidget.h"
#include <QMutexLocker>
#include <cstring> // voor std::memcpy


ScreenWidget::ScreenWidget(QWidget *parent)
    : QWidget(parent),
    m_frame(COLECO_WIDTH, COLECO_HEIGHT, QImage::Format_RGB32),
    m_backgroundColor(QColor("#323232")),
    m_smoothScaling(true)
{
    // Start met 2x zoom zoals je al deed, maar via helper
    //applyFixedSize();

    // Begin met een zwart scherm
    m_frame.fill(Qt::black);
}

ScreenWidget::~ScreenWidget()
{
}

// Ideale/standaard grootte is de basisresolutie
QSize ScreenWidget::sizeHint() const {
    return QSize(COLECO_WIDTH, COLECO_HEIGHT);
}

// Minimale grootte is de basisresolutie
QSize ScreenWidget::minimumSizeHint() const {
    return QSize(COLECO_WIDTH, COLECO_HEIGHT);
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
    // Accepteer ELKE image. De grootte-controle is weg.
    // De schaal-logica is weg.
    m_frame = img.copy(); // Sla de (al geschaalde) image op

    locker.unlock();
    update(); // Vraag om repaint
}

void ScreenWidget::setSmoothScaling(bool enabled)
{
    if (m_smoothScaling == enabled) return; // Geen wijziging

    m_smoothScaling = enabled;
    update(); // Forceer een repaint om de nieuwe instelling toe te passen
}

void ScreenWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    QColor m_backgroundColor = "#323232";

    // Vraag om vloeiende (bilineaire) scaling.
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_smoothScaling);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Pak de kleine 256x192 frame
    QImage frameCopy;
    {
        QMutexLocker lock(&m_mutex);
        frameCopy = m_frame;
    }

    if (frameCopy.isNull()) {
        p.fillRect(rect(), m_backgroundColor);
        return;
    }

    // --- NIEUWE LOGICA: VUL HOOGTE, BEHOUD ASPECT RATIO ---
    // Dit garandeert GEEN boven/onderranden (letterboxing).
    // Het produceert WEL zijranden (pillarboxing).

    QSize widgetSize = this->size();

    // 1. Bepaal de doelhoogte (volledige widget-hoogte)
    int targetHeight = widgetSize.height();

    // 2. Bereken de doelbreedte met behoud van aspect ratio
    //    (We gaan ervan uit dat frameCopy.height() > 0 is)
    if (frameCopy.height() == 0) return; // Voorkom delen door nul
    int targetWidth = (targetHeight * frameCopy.width()) / frameCopy.height();

    // 3. Bepaal de x-positie om horizontaal te centreren
    int x = (widgetSize.width() - targetWidth) / 2;
    int y = 0; // Altijd 0, want we vullen de hoogte

    // 4. Maak de doel-rechthoek
    QRect targetRect(x, y, targetWidth, targetHeight);

    // 5. Teken de achtergrond (vult het hele venster)
    p.fillRect(rect(), m_backgroundColor);

    // 6. Teken het spel
    p.drawImage(targetRect, frameCopy);
}
