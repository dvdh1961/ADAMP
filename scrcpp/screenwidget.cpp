#include "screenwidget.h"
#include <QMutexLocker>
#include <cstring> // voor std::memcpy


ScreenWidget::ScreenWidget(QWidget *parent)
    : QWidget(parent),
    m_frame(COLECO_WIDTH, COLECO_HEIGHT, QImage::Format_RGB32),
    m_backgroundColor(QColor("#323232")),
    m_smoothScaling(true),
    m_isFullScreen(false),
    m_scalingMode(ModeSmooth), // Begin met Smooth
    m_epxBuffer()
{
    // Start met 2x zoom zoals je al deed, maar via helper
    //applyFixedSize();

    // Begin met een zwart scherm
    m_frame.fill(Qt::black);
}

ScreenWidget::~ScreenWidget()
{
}

void ScreenWidget::setScalingMode(ScalingMode mode)
{
    if (m_scalingMode == mode) return;
    m_scalingMode = mode;
    update(); // Forceer repaint
}

// Dit is een complexe functie.
// Je kunt deze code als basis gebruiken of een bewezen implementatie
// uit een andere emulator (zoals ColEm) aanpassen.
// void ScreenWidget::applyEPX(const QImage& source)
// {
//     // Zorg dat de buffer de juiste grootte heeft (2x zo groot)
//     if (m_epxBuffer.size() != source.size() * 2) {
//         m_epxBuffer = QImage(source.size() * 2, QImage::Format_RGB32);
//     }

//     // Bron- en doelpointers (vereist 32-bit/4-byte pixels)
//     const quint32* src = reinterpret_cast<const quint32*>(source.bits());
//     quint32* dst = reinterpret_cast<quint32*>(m_epxBuffer.bits());

//     int w = source.width();
//     int h = source.height();
//     int dstPitch = m_epxBuffer.bytesPerLine() / 4; // in pixels (quint32)

//     for (int y = 0; y < h; ++y) {
//         for (int x = 0; x < w; ++x) {
//             // 1. Haal P en buren op (met grenscontrole)
//             quint32 P = src[y * w + x];
//             quint32 A = (y > 0) ? src[(y - 1) * w + x] : P;
//             quint32 B = (x < w - 1) ? src[y * w + (x + 1)] : P;
//             quint32 C = (x > 0) ? src[y * w + (x - 1)] : P;
//             quint32 D = (y < h - 1) ? src[(y + 1) * w + x] : P;

//             // 2. Stel de 2x2 uitvoerpixels (p1, p2, p3, p4) in
//             quint32 p1 = P, p2 = P, p3 = P, p4 = P;

//             // 3. Pas de EPX-regels toe
//             if (A == C && A != D && A != B) { p1 = A; p3 = A; }
//             if (A == B && A != D && A != C) { p1 = A; p2 = A; }
//             if (D == C && D != A && D != B) { p3 = D; p4 = D; }
//             if (D == B && D != A && D != C) { p2 = D; p4 = D; }

//             // ... (Er zijn meer regels voor 3+ gelijke buren, etc.) ...
//             // Een volledige implementatie is complexer dan deze 4 basisregels.

//             // 4. Schrijf naar de 2x2 doelbuffer
//             int dst_x = x * 2;
//             int dst_y = y * 2;
//             dst[dst_y * dstPitch + dst_x]     = p1;
//             dst[dst_y * dstPitch + (dst_x + 1)] = p2;
//             dst[(dst_y + 1) * dstPitch + dst_x] = p3;
//             dst[(dst_y + 1) * dstPitch + (dst_x + 1)] = p4;
//         }
//     }
// }

// Deze functie implementeert het complete Scale2x/EPX (4-regels) algoritme
void ScreenWidget::applyEPX(const QImage& source)
{
    // 1. Zorg dat buffer de juiste (2x) grootte heeft
    const int w = source.width();
    const int h = source.height();
    const QSize targetSize(w * 2, h * 2);

    if (m_epxBuffer.size() != targetSize) {
        m_epxBuffer = QImage(targetSize, QImage::Format_RGB32);
    }

    // Valideer bronformaat (moet 32-bit zijn voor quint32 pointers)
    if (source.format() != QImage::Format_RGB32 && source.format() != QImage::Format_ARGB32) {
        // Converteer naar 32-bit als het een ander formaat is
        QImage convertedSource = source.convertToFormat(QImage::Format_RGB32);
        if (convertedSource.isNull()) {
            qWarning() << "EPX: Kan bron-image niet converteren naar 32-bit.";
            m_epxBuffer.fill(Qt::magenta); // Fout
            return;
        }
        // Roep onszelf opnieuw aan met de geconverteerde image
        applyEPX(convertedSource);
        return;
    }

    // Gebruik pointers voor snelheid
    const quint32* src = reinterpret_cast<const quint32*>(source.bits());
    quint32* dst = reinterpret_cast<quint32*>(m_epxBuffer.bits());

    // Pitch in pixels (aantal quint32 per scanline)
    const int srcPitch = source.bytesPerLine() / 4;
    const int dstPitch = m_epxBuffer.bytesPerLine() / 4;

    for (int y = 0; y < h; ++y) {
        const quint32* srcLine = src + y * srcPitch;

        // Lijnen voor buren (met grenscontrole)
        const quint32* lineA = (y > 0)   ? (src + (y - 1) * srcPitch) : srcLine; // Boven
        const quint32* lineD = (y < h - 1) ? (src + (y + 1) * srcPitch) : srcLine; // Onder

        quint32* dstLine0 = dst + (y * 2) * dstPitch;
        quint32* dstLine1 = dst + (y * 2 + 1) * dstPitch;

        for (int x = 0; x < w; ++x) {
            // 1. Haal P en buren op
            const quint32 P = srcLine[x];
            const quint32 A = lineA[x];
            const quint32 B = (x < w - 1) ? srcLine[x + 1] : P;
            const quint32 C = (x > 0)   ? srcLine[x - 1] : P;
            const quint32 D = lineD[x];

            // 2. Stel de 2x2 uitvoerpixels (p1, p2, p3, p4) in op de standaard (P)
            quint32 p1 = P, p2 = P, p3 = P, p4 = P;

            // --- DE VOLLEDIGE 4 REGELS ---
            // Dit was de fout in mijn vorige code.

            // Regel 1: Links & Boven
            if (C == A && C != D && A != B) p1 = A;
            // Regel 2: Boven & Rechts
            if (A == B && A != C && B != D) p2 = B;
            // Regel 3: Links & Onder
            if (C == D && C != A && D != B) p3 = C;
            // Regel 4: Rechts & Onder
            if (B == D && B != C && D != A) p4 = B;
            // --- EINDE CORRECTIE ---

            // 4. Schrijf naar de 2x2 doelbuffer
            dstLine0[x * 2]     = p1;
            dstLine0[x * 2 + 1] = p2;
            dstLine1[x * 2]     = p3;
            dstLine1[x * 2 + 1] = p4;
        }
    }
}

// Ideale/standaard grootte is de basisresolutie
QSize ScreenWidget::sizeHint() const {
    return QSize(COLECO_WIDTH, COLECO_HEIGHT);
}

// Minimale grootte is de basisresolutie
QSize ScreenWidget::minimumSizeHint() const {
    return QSize(COLECO_WIDTH, COLECO_HEIGHT);
}

void ScreenWidget::setFullScreenMode(bool enabled)
{
    if (m_isFullScreen == enabled) return;
    m_isFullScreen = enabled;
    update(); // Forceer repaint
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
    // VERWIJDERD: QColor m_backgroundColor = "#323232";
    // Dit moet de member 'm_backgroundColor' zijn, ingesteld in de constructor.

    // Pak de kleine 256x192 frame
    QImage frameCopy;
    {
        QMutexLocker lock(&m_mutex);
        frameCopy = m_frame;
    }

    // Bepaal de achtergrondkleur (zoals voorheen)
    QColor bgColor = m_isFullScreen ? Qt::transparent : m_backgroundColor;

    if (frameCopy.isNull() || frameCopy.width() == 0 || frameCopy.height() == 0) {
        p.fillRect(rect(), bgColor);
        return;
    }

    // --- NIEUWE EPX SCHAAL-LOGICA ---

    QImage imageToDraw; // De image die we *echt* gaan schalen
    bool useSmoothFinalScale;

    // We gaan ervan uit dat m_scalingMode nu een enum is (zie stap 3)
    switch (m_scalingMode)
    {
    case ModeEPX:
        // 1. Genereer de 2x (512x384) EPX image
        //    (Vereist applyEPX() en m_epxBuffer in deze class)
        applyEPX(frameCopy);
        imageToDraw = m_epxBuffer;

        // 2. EPX ziet er het beste uit als het *daarna* vloeiend
        //    naar de eindresolutie wordt geschaald.
        useSmoothFinalScale = true;
        break;

    case ModeSmooth:
        imageToDraw = frameCopy; // Schaal de 256x192 image
        useSmoothFinalScale = true;
        break;

    case ModeSharp:
    default:
        imageToDraw = frameCopy; // Schaal de 256x192 image
        useSmoothFinalScale = false;
        break;
    }

    // Stel de *definitieve* schaalmodus in
    p.setRenderHint(QPainter::SmoothPixmapTransform, useSmoothFinalScale);
    p.setRenderHint(QPainter::Antialiasing, false);

    // --- EINDE NIEUWE LOGICA ---


    // --- UW BESTAANDE "VUL HOOGTE" LOGICA ---
    // (Deze wordt nu toegepast op 'imageToDraw')

    QSize widgetSize = this->size();

    // 1. Bepaal de doelhoogte (volledige widget-hoogte)
    int targetHeight = widgetSize.height();

    // 2. Bereken de doelbreedte met behoud van aspect ratio
    if (imageToDraw.height() == 0) return; // Voorkom delen door nul
    int targetWidth = (targetHeight * imageToDraw.width()) / imageToDraw.height();

    // 3. Bepaal de x-positie om horizontaal te centreren
    int x = (widgetSize.width() - targetWidth) / 2;
    int y = 0; // Altijd 0, want we vullen de hoogte

    // 4. Maak de doel-rechthoek
    QRect targetRect(x, y, targetWidth, targetHeight);

    // 5. Teken de achtergrond (vult het hele venster)
    p.fillRect(rect(), bgColor);

    // 6. Teken het spel
    p.drawImage(targetRect, imageToDraw);
}
