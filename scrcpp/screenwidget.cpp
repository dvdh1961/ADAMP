#include "screenwidget.h"
#include <QMutexLocker>
#include <cstring>
#include <QtGlobal>  // voor qBound, qRed, qGreen, qBlue, qAlpha, qRgba

ScreenWidget::ScreenWidget(QWidget *parent)
    : QWidget(parent),
    m_frame(COLECO_WIDTH, COLECO_HEIGHT, QImage::Format_RGB32),
    m_backgroundColor(QColor("#323232")),
    m_smoothScaling(true),
    m_isFullScreen(false),
    m_scalingMode(ModeSmooth),
    m_epxBuffer()
{
    // Begin met een zwart scherm
    m_frame.fill(Qt::black);
}

ScreenWidget::~ScreenWidget()
{
}

void ScreenWidget::setScalingMode(ScreenWidget::ScalingMode mode)
{
    if (m_scalingMode == mode) return;
    m_scalingMode = mode;
    update();
}

// Deze functie implementeert het complete Scale2x/EPX (4-regels) algoritme
void ScreenWidget::applyEPX(const QImage& source)
{
    // Zorg dat buffer de juiste (2x) grootte heeft
    const int w = source.width();
    const int h = source.height();
    const QSize targetSize(w * 2, h * 2);

    if (m_epxBuffer.size() != targetSize) {
        m_epxBuffer = QImage(targetSize, QImage::Format_RGB32);
    }

    // Valideer bronformaat (moet 32-bit zijn voor quint32 pointers)
    if (source.format() != QImage::Format_RGB32 && source.format() != QImage::Format_ARGB32) {
        QImage convertedSource = source.convertToFormat(QImage::Format_RGB32);
        if (convertedSource.isNull()) {
            qWarning() << "EPX: Kan bron-image niet converteren naar 32-bit.";
            m_epxBuffer.fill(Qt::magenta); // Fout
            return;
        }
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
        const quint32* lineA = (y > 0)   ? (src + (y - 1) * srcPitch) : srcLine;
        const quint32* lineD = (y < h - 1) ? (src + (y + 1) * srcPitch) : srcLine;

        quint32* dstLine0 = dst + (y * 2) * dstPitch;
        quint32* dstLine1 = dst + (y * 2 + 1) * dstPitch;

        for (int x = 0; x < w; ++x) {
            const quint32 P = srcLine[x];
            const quint32 A = lineA[x];
            const quint32 B = (x < w - 1) ? srcLine[x + 1] : P;
            const quint32 C = (x > 0)   ? srcLine[x - 1] : P;
            const quint32 D = lineD[x];

            quint32 p1 = P, p2 = P, p3 = P, p4 = P;

            // Regel 1: Links & Boven
            if (C == A && C != D && A != B) p1 = A;
            // Regel 2: Boven & Rechts
            if (A == B && A != C && B != D) p2 = B;
            // Regel 3: Links & Onder
            if (C == D && C != A && D != B) p3 = C;
            // Regel 4: Rechts & Onder
            if (B == D && B != C && D != A) p4 = B;

            // 4. Schrijf naar de 2x2 doelbuffer
            dstLine0[x * 2]     = p1;
            dstLine0[x * 2 + 1] = p2;
            dstLine1[x * 2]     = p3;
            dstLine1[x * 2 + 1] = p4;
        }
    }
}

void ScreenWidget::applyLCDizeFilter(QImage& image)
{
    const int W = image.width();
    const int H = image.height();

    // Lcdize werkt op 2x2 blokken.
    // De originele code dimt de ene helft van de 2x2 blokken en verheldert de andere.
    // De logica is: Dimmen op oneven kolommen, verhelderen op even kolommen, voor alle rijen.
    const quint32 BRIGHTNESS_MASK = 0x000F0F0F;

    // LcdizeImage (in Image.c) werkt met paren (W&=~1)

    for (int y = 0; y < H; ++y)
    {
        quint32* P = reinterpret_cast<quint32*>(image.scanLine(y));

        for (int x = 0; x < W; ++x)
        {
            if (x & 1) // Oneven kolommen: Brighten
            {
                // P[X]+=(~P[X]>>4)&0x000F0F0F
                P[x] += (~P[x] >> 4) & BRIGHTNESS_MASK;
            }
            else // Even kolommen: Darken
            {
                // P[X]-=(P[X]>>4)&0x000F0F0F
                P[x] -= (P[x] >> 4) & BRIGHTNESS_MASK;
            }
        }
    }
}

void ScreenWidget::applyRasterizeFilter(QImage& image)
{
    const int W = image.width();
    const int H = image.height();
    const quint32 BRIGHTNESS_MASK = 0x000F0F0F;

    for (int y = 0; y < H; ++y)
    {
        quint32* P = reinterpret_cast<quint32*>(image.scanLine(y));

        if (y & 1) // Oneven rijen: Dim de hele rij (zoals TV Scanlines)
        {
            // P[X]-=(P[X]>>4)&0x000F0F0F
            for (int x = 0; x < W; ++x) {
                P[x] -= (P[x] >> 4) & BRIGHTNESS_MASK;
            }
        }
        else // Even rijen: Dim/verhelder per kolom (zoals LCD)
        {
            for (int x = 0; x < W; ++x) {
                if (x & 1) {
                    // Oneven kolommen: Brighten
                    // P[X]+=(~P[X]>>4)&0x000F0F0F
                    P[x] += (~P[x] >> 4) & BRIGHTNESS_MASK;
                } else {
                    // Even kolommen: Darken
                    // P[X]-=(P[X]>>4)&0x000F0F0F
                    P[x] -= (P[x] >> 4) & BRIGHTNESS_MASK;
                }
            }
        }
    }
}

// Pas de oude TV-filter aan de nieuwe naam aan
void ScreenWidget::applyTVScanlinesFilter(QImage& image)
{
    const int W = image.width();
    const int H = image.height();

    // Masker voor snelle manipulatie van R, G en B componenten.
    // Dit maskeer 4 bits (een factor 16) van elke R/G/B component.
    const quint32 BRIGHTNESS_MASK = 0x000F0F0F;

    for (int y = 0; y < H; ++y)
    {
        quint32* P = reinterpret_cast<quint32*>(image.scanLine(y));

        if (y & 1) // Oneven lijnen: Darken (Scanline OFF)
        {
            for (int x = 0; x < W; ++x) {
                // P[X] -= (P[X] >> 4) & 0x000F0F0F;
                P[x] -= (P[x] >> 4) & BRIGHTNESS_MASK;
            }
        }
        else // Even lijnen: Brighten (Scanline ON - optionele versterking)
        {
            for (int x = 0; x < W; ++x) {
                // P[X] += (~P[X] >> 4) & 0x000F0F0F;
                P[x] += (~P[x] >> 4) & BRIGHTNESS_MASK;
            }
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
    update();
}

void ScreenWidget::setScanlinesMode(ScanlinesMode mode)
{
    if (m_scanlinesMode == mode) return;
    m_scanlinesMode = mode;
    update();
}

void ScreenWidget::updateFrame(const QImage &frame)
{
    {
        QMutexLocker locker(&m_mutex);
        m_frame = frame.copy();
    }
    update();
}

void ScreenWidget::setFrame(const QImage &img)
{
    QMutexLocker locker(&m_mutex);
    m_frame = img.copy();

    locker.unlock();
    update();
}

void ScreenWidget::setSmoothScaling(bool enabled)
{
    if (m_smoothScaling == enabled) return;

    m_smoothScaling = enabled;
    update();
}

void ScreenWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    QImage frameCopy;
    {
        QMutexLocker lock(&m_mutex);
        frameCopy = m_frame;
    }

    QColor bgColor = m_isFullScreen ? Qt::transparent : m_backgroundColor;

    if (frameCopy.isNull() || frameCopy.width() == 0 || frameCopy.height() == 0) {
        p.fillRect(rect(), bgColor);
        return;
    }

    QImage imageToDraw;
    bool useSmoothFinalScale;

    switch (m_scalingMode)
    {
    case ModeEPX:
        applyEPX(frameCopy);
        imageToDraw = m_epxBuffer;
        useSmoothFinalScale = true;
        break;

    case ModeSmooth:
        imageToDraw = frameCopy;
        useSmoothFinalScale = true;
        break;

    case ModeSharp:
    default:
        imageToDraw = frameCopy;
        useSmoothFinalScale = false;
        break;
    }

    if (m_scanlinesMode != ScanlinesOff)
    {
        QImage filteredImage = imageToDraw.copy();

        switch (m_scanlinesMode) {
        case ScanlinesTV:
            applyTVScanlinesFilter(filteredImage);
            break;
        case ScanlinesLCD:
            applyLCDizeFilter(filteredImage);
            break;
        case ScanlinesRaster:
            applyRasterizeFilter(filteredImage);
            break;
        default:
            break;
        }

        imageToDraw = filteredImage;
    }

    if (m_colorFilterMode != ColorFilterOff)
    {
        QImage filteredImage = imageToDraw.copy();

        // Zorg dat we in 32-bit werken
        if (filteredImage.format() != QImage::Format_RGB32 &&
            filteredImage.format() != QImage::Format_ARGB32)
        {
            filteredImage = filteredImage.convertToFormat(QImage::Format_RGB32);
        }

        switch (m_colorFilterMode) {
        case ColorFilterMonochrome:
            applyMonochromeFilter(filteredImage);
            break;
        case ColorFilterSepia:
            applySepiaFilter(filteredImage);
            break;
        case ColorFilterGreenCRT:
            applyGreenCRTFilter(filteredImage);
            break;
        case ColorFilterAmberCRT:
            applyAmberCRTFilter(filteredImage);
            break;
        case ColorFilterCMY:
            applyCMYRasterFilter(filteredImage);
            break;
        case ColorFilterRGB:
            applyRGBRasterFilter(filteredImage);
            break;
        default:
            break;
        }

        imageToDraw = filteredImage;
    }

    p.setRenderHint(QPainter::SmoothPixmapTransform, useSmoothFinalScale);
    p.setRenderHint(QPainter::Antialiasing, false);

    QSize widgetSize = this->size();

    // Bepaal de doelhoogte (volledige widget-hoogte)
    int targetHeight = widgetSize.height();

    // Bereken de doelbreedte met behoud van aspect ratio
    if (imageToDraw.height() == 0) return; // Voorkom delen door nul
    int targetWidth = (targetHeight * imageToDraw.width()) / imageToDraw.height();

    // Bepaal de x-positie om horizontaal te centreren
    int x = (widgetSize.width() - targetWidth) / 2;
    int y = 0; // Altijd 0, want we vullen de hoogte

    // Maak de doel-rechthoek
    QRect targetRect(x, y, targetWidth, targetHeight);

    // Teken de achtergrond (vult het hele venster)
    p.fillRect(rect(), bgColor);

    // Teken het spel
    p.drawImage(targetRect, imageToDraw);

    Q_UNUSED(event);

    // Teken de achtergrond
    p.fillRect(rect(), bgColor);

    // Teken het spel
    p.drawImage(targetRect, imageToDraw);

    QPen borderPen(Qt::black); // Kleur: Zwart
    borderPen.setWidth(4);     // Dikte: 4 pixels

    // MiterJoin zorgt voor scherpe, rechte hoeken in plaats van afgeronde
    borderPen.setJoinStyle(Qt::MiterJoin);

    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);

    // Teken de rechthoek precies op de rand van het spelbeeld
    p.drawRect(targetRect);
    // ----------------------------------------
}

void ScreenWidget::setColorFilterMode(ColorFilterMode mode)
{
    if (m_colorFilterMode == mode) return;
    m_colorFilterMode = mode;
    update();
}

void ScreenWidget::applyMonochromeFilter(QImage& image)
{
    const int W = image.width();
    const int H = image.height();

    for (int y = 0; y < H; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < W; ++x) {
            QRgb px = line[x];
            int r = qRed(px);
            int g = qGreen(px);
            int b = qBlue(px);

            // 77+151+28 = 256 => zelfde stijl als EMULib (Image.c)
            int gray = (77 * r + 151 * g + 28 * b) >> 8;
            gray = qBound(0, gray, 255);

            line[x] = qRgba(gray, gray, gray, qAlpha(px));
        }
    }
}

void ScreenWidget::applySepiaFilter(QImage& image)
{
    const int W = image.width();
    const int H = image.height();

    for (int y = 0; y < H; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < W; ++x) {
            QRgb px = line[x];
            int r = qRed(px);
            int g = qGreen(px);
            int b = qBlue(px);

            // Int-versie van klassieke sepia (ongeveer wat SepiaImage doet)
            int sr = (393 * r + 769 * g + 189 * b) / 1000;
            int sg = (349 * r + 686 * g + 168 * b) / 1000;
            int sb = (272 * r + 534 * g + 131 * b) / 1000;

            sr = qBound(0, sr, 255);
            sg = qBound(0, sg, 255);
            sb = qBound(0, sb, 255);

            line[x] = qRgba(sr, sg, sb, qAlpha(px));
        }
    }
}

void ScreenWidget::applyGreenCRTFilter(QImage& image)
{
    const int W = image.width();
    const int H = image.height();

    for (int y = 0; y < H; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < W; ++x) {
            QRgb px = line[x];
            int r = qRed(px);
            int g = qGreen(px);
            int b = qBlue(px);

            // Eerst naar grijs (zoals MonochromeImage)
            int gray = (77 * r + 151 * g + 28 * b) >> 8;
            gray = qBound(0, gray, 255);

            int nr = qBound(0, (92 * gray) >> 8, 255);   // ~0.36 * gray
            int ng = gray;                               // hoofdcomponent
            int nb = qBound(0, (51 * gray) >> 8, 255);   // ~0.2 * gray

            line[x] = qRgba(nr, ng, nb, qAlpha(px));
        }
    }
}

void ScreenWidget::applyAmberCRTFilter(QImage& image)
{
    const int W = image.width();
    const int H = image.height();

    for (int y = 0; y < H; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < W; ++x) {
            QRgb px = line[x];
            int r = qRed(px);
            int g = qGreen(px);
            int b = qBlue(px);

            // 1. Calculate weighted grayscale (Luminance approximation)
            int gray = (77 * r + 151 * g + 28 * b) >> 8;
            gray = qBound(0, gray, 255);

            // 2. Apply Orange/Amber color filter

            // Red component: Maximale intensiteit voor een diepe oranje tint
            int nr = gray;

            // Green component: Iets verlaagd om het van groen naar oranje te verschuiven
            // Gebruik een factor rond 180 (180/256 ≈ 0.7) voor een oranjere kleur.
            int ng = qBound(0, (180 * gray) >> 8, 255);

            // Blue component: Lage intensiteit om blauwe of paarse tinten te vermijden
            int nb = qBound(0, (51 * gray) >> 8, 255);

            line[x] = qRgba(nr, ng, nb, qAlpha(px));
        }
    }
}

void ScreenWidget::applyCMYRasterFilter(QImage& image)
{
    const int W = image.width();
    const int H = image.height();

    for (int y = 0; y < H; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < W; ++x) {
            QRgb px = line[x];
            int r = qRed(px);
            int g = qGreen(px);
            int b = qBlue(px);

            switch (x % 3) {
            case 0: // C (Cyan = G+B, R wat dimmen)
                r = r * 5 / 10;
                g = qMin(255, g * 12 / 10);
                b = qMin(255, b * 12 / 10);
                break;
            case 1: // M (Magenta = R+B, G dimmen)
                r = qMin(255, r * 12 / 10);
                g = g * 5 / 10;
                b = qMin(255, b * 12 / 10);
                break;
            case 2: // Y (Yellow = R+G, B dimmen)
                r = qMin(255, r * 12 / 10);
                g = qMin(255, g * 12 / 10);
                b = b * 5 / 10;
                break;
            }

            line[x] = qRgba(r, g, b, qAlpha(px));
        }
    }
}

void ScreenWidget::applyRGBRasterFilter(QImage& image)
{
    const int W = image.width();
    const int H = image.height();

    for (int y = 0; y < H; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < W; ++x) {
            QRgb px = line[x];
            int r = qRed(px);
            int g = qGreen(px);
            int b = qBlue(px);

            switch (x % 3) {
            case 0: // R
                r = qMin(255, r * 13 / 10);
                g = g * 6 / 10;
                b = b * 6 / 10;
                break;
            case 1: // G
                r = r * 6 / 10;
                g = qMin(255, g * 13 / 10);
                b = b * 6 / 10;
                break;
            case 2: // B
                r = r * 6 / 10;
                g = g * 6 / 10;
                b = qMin(255, b * 13 / 10);
                break;
            }

            line[x] = qRgba(r, g, b, qAlpha(px));
        }
    }
}
