#include "screenwidget.h"
#include <QMutexLocker>
#include <cstring>
#include <QtGlobal>
#include <QtDebug>
#include <QPainterPath>
#include "coleco.h"

bool m_80colEnabled = false;

extern "C" {
    #include "coleco.h"
    #include "tms9928a.h"
}

// TMS9928A color palette
const QColor ScreenWidget::TMS_COLORS[16] = {
    QColor(0, 0, 0),         // 0: Transparent (Black)
    QColor(0, 0, 0),         // 1: Black
    QColor(33, 200, 66),     // 2: Medium Green
    QColor(94, 220, 120),    // 3: Light Green
    QColor(84, 85, 237),     // 4: Dark Blue
    QColor(125, 118, 252),   // 5: Light Blue
    QColor(212, 82, 77),     // 6: Dark Red
    QColor(66, 235, 245),    // 7: Cyan
    QColor(252, 85, 84),     // 8: Medium Red
    QColor(255, 121, 120),   // 9: Light Red
    QColor(212, 193, 84),    // A: Dark Yellow
    QColor(230, 206, 128),   // B: Light Yellow
    QColor(33, 176, 59),     // C: Dark Green
    QColor(201, 91, 186),    // D: Magenta
    QColor(204, 204, 204),   // E: Gray
    QColor(255, 255, 255)    // F: White
};

ScreenWidget::ScreenWidget(QWidget *parent)
    : QWidget(parent),
    m_frame(COLECO_WIDTH, COLECO_HEIGHT, QImage::Format_RGB32),
    m_backgroundColor(QColor("#323232")),
    m_smoothScaling(true),
    m_isFullScreen(false),
    m_scalingMode(ModeSmooth),
    m_epxBuffer()
    //m_80colEnabled(false)
{
    // Begin met een zwart scherm
    m_frame.fill(Qt::black);
    
    // Setup 80-column font
    m_80colFont = QFont("Consolas", 9);
    m_80colFont.setStyleHint(QFont::Monospace);
    m_80colFont.setFixedPitch(true);
    m_80colFont.setBold(true);
}

ScreenWidget::~ScreenWidget()
{
}

// ============================================================================
// 80-COLUMN MODE HELPERS
// ============================================================================

static bool auto80 = true;
static bool ones = false;

// --- TDOS 80-col detectie (ADAMEm-style) ---
static inline uint8_t z80rb(uint16_t a) {
    return (uint8_t)coleco_ReadByte(a);
}

static inline uint16_t z80rw(uint16_t a) {
    return (uint16_t)z80rb(a) | ((uint16_t)z80rb(a + 1) << 8);
}

// Return: startadres van TDOS 80-col buffer (0 = niet actief)
static uint16_t CheckTDOS80BufferAddr()
{
    uint16_t base = (uint16_t)z80rb(0x01) | ((uint16_t)z80rb(0x02) << 8);

    uint16_t addr = (uint16_t)z80rb(0x01) | ((uint16_t)z80rb(0x02) << 8);
    addr = (uint16_t)(addr + 0x6D - 3);

    uint16_t routinePtr = z80rw(addr);

    // Signature check (exact zoals ADAMEm)
    if ( z80rb(routinePtr + 0)  != 0xF5) return 0;
    if ( z80rb(routinePtr + 1)  != 0xC5) return 0;
    if ( z80rb(routinePtr + 2)  != 0xD5) return 0;
    if ( z80rb(routinePtr + 3)  != 0xCD) return 0;
    if ( z80rb(routinePtr + 6)  != 0x30) return 0;
    if ( z80rb(routinePtr + 8)  != 0xE1) return 0;
    if ( z80rb(routinePtr + 9)  != 0x11) return 0;
    if ( z80rb(routinePtr + 12) != 0x01) return 0;
    if ( z80rb(routinePtr + 13) != 0x00) return 0;
    if ( z80rb(routinePtr + 14) != 0x04) return 0;
    if ( z80rb(routinePtr + 15) != 0xED) return 0;
    if ( z80rb(routinePtr + 16) != 0xB0) return 0;

    if ( (tms.VR[0] & 0x02) != 0x00 ) return 0;
    if ( (tms.VR[1] & 0x18) != 0x10 ) return 0;

    uint16_t bufBase = z80rw((uint16_t)(routinePtr + 10));
    uint16_t result  = (uint16_t)(bufBase + 0x400);

    return result;
}

static int GetTDOSNumLines()
{
    uint16_t i = (uint16_t)z80rb(0x01) | ((uint16_t)z80rb(0x02) << 8);
    i = (uint16_t)(i + 0x64 - 3);

    uint16_t p = z80rw(i);
    uint16_t q = z80rw((uint16_t)(p + 3));
    int num = (int)z80rb(q) + 1;
    if (num < 0) num = 0;
    if (num > 24) num = 24;
    return num;
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
            m_epxBuffer.fill(Qt::magenta);
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
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            // LCD subpixel pattern effect
            if (x % 3 == 0) {
                r = qBound(0, r + 20, 255);
            } else if (x % 3 == 1) {
                g = qBound(0, g + 20, 255);
            } else {
                b = qBound(0, b + 20, 255);
            }

            line[x] = qRgb(r, g, b);
        }
    }
}

void ScreenWidget::applyTVScanlinesFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        if (y % 2 == 1) {
            QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb pixel = line[x];
                int r = qRed(pixel) * 0.7;
                int g = qGreen(pixel) * 0.7;
                int b = qBlue(pixel) * 0.7;
                line[x] = qRgb(r, g, b);
            }
        }
    }
}

void ScreenWidget::applyRasterizeFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            // Apply raster pattern
            if ((x + y) % 2 == 0) {
                r = qBound(0, r - 30, 255);
                g = qBound(0, g - 30, 255);
                b = qBound(0, b - 30, 255);
            }

            line[x] = qRgb(r, g, b);
        }
    }
}

void ScreenWidget::applyMonochromeFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int gray = (qRed(pixel) + qGreen(pixel) + qBlue(pixel)) / 3;
            line[x] = qRgb(gray, gray, gray);
        }
    }
}

void ScreenWidget::applySepiaFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            int tr = qBound(0, (int)(r * 0.393 + g * 0.769 + b * 0.189), 255);
            int tg = qBound(0, (int)(r * 0.349 + g * 0.686 + b * 0.168), 255);
            int tb = qBound(0, (int)(r * 0.272 + g * 0.534 + b * 0.131), 255);

            line[x] = qRgb(tr, tg, tb);
        }
    }
}

void ScreenWidget::applyGreenCRTFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int gray = (qRed(pixel) + qGreen(pixel) + qBlue(pixel)) / 3;
            line[x] = qRgb(0, gray, 0);
        }
    }
}

void ScreenWidget::applyAmberCRTFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int gray = (qRed(pixel) + qGreen(pixel) + qBlue(pixel)) / 3;
            int r = qBound(0, gray, 255);
            int g = qBound(0, (int)(gray * 0.7), 255);
            line[x] = qRgb(r, g, 0);
        }
    }
}

void ScreenWidget::applyCMYRasterFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            if (x % 3 == 0) {      // Cyan
                r = qBound(0, r - 50, 255);
            } else if (x % 3 == 1) { // Magenta
                g = qBound(0, g - 50, 255);
            } else {               // Yellow
                b = qBound(0, b - 50, 255);
            }

            line[x] = qRgb(r, g, b);
        }
    }
}

void ScreenWidget::applyRGBRasterFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            if (x % 3 == 0) {
                g = qBound(0, g - 50, 255);
                b = qBound(0, b - 50, 255);
            } else if (x % 3 == 1) {
                r = qBound(0, r - 50, 255);
                b = qBound(0, b - 50, 255);
            } else {
                r = qBound(0, r - 50, 255);
                g = qBound(0, g - 50, 255);
            }

            line[x] = qRgb(r, g, b);
        }
    }
}

void ScreenWidget::setBackgroundColor(const QColor& color)
{
    m_backgroundColor = color;
    update();
}

QSize ScreenWidget::sizeHint() const
{
    return QSize(COLECO_WIDTH * 2, COLECO_HEIGHT * 2);
}

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

void ScreenWidget::setColorFilterMode(ColorFilterMode mode)
{
    if (m_colorFilterMode == mode) return;
    m_colorFilterMode = mode;
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

    // 1. Haal de frame-copy op veilige wijze op
    QImage frameCopy;
    {
        QMutexLocker lock(&m_mutex);
        frameCopy = m_frame;
    }

    // Achtergrondkleur bepalen
    QColor bgColor = m_isFullScreen ? Qt::transparent : m_backgroundColor;
    p.fillRect(rect(), bgColor);

    // Controleer of er beelddata is
    if (frameCopy.isNull() || frameCopy.width() == 0 || frameCopy.height() == 0) {
        return;
    }

    // 2. Pas scaling/filters toe
    QImage imageToDraw;
    bool useSmoothFinalScale = (m_scalingMode != ModeSharp);

    if (m_scalingMode == ModeEPX) {
        applyEPX(frameCopy);
        imageToDraw = m_epxBuffer;
    } else {
        imageToDraw = frameCopy;
    }

    // Scanlines/Filters toepassen indien nodig
    if (m_scanlinesMode != ScanlinesOff) {
        QImage filteredImage = imageToDraw.copy();
        if (m_scanlinesMode == ScanlinesTV) applyTVScanlinesFilter(filteredImage);
        else if (m_scanlinesMode == ScanlinesLCD) applyLCDizeFilter(filteredImage);
        else if (m_scanlinesMode == ScanlinesRaster) applyRasterizeFilter(filteredImage);
        imageToDraw = filteredImage;
    }

    if (m_colorFilterMode != ColorFilterOff) {
        QImage filteredImage = imageToDraw.copy();
        if (filteredImage.format() != QImage::Format_RGB32)
            filteredImage = filteredImage.convertToFormat(QImage::Format_RGB32);

        if (m_colorFilterMode == ColorFilterMonochrome) applyMonochromeFilter(filteredImage);
        else if (m_colorFilterMode == ColorFilterSepia) applySepiaFilter(filteredImage);
        else if (m_colorFilterMode == ColorFilterGreenCRT) applyGreenCRTFilter(filteredImage);
        else if (m_colorFilterMode == ColorFilterAmberCRT) applyAmberCRTFilter(filteredImage);
        else if (m_colorFilterMode == ColorFilterCMY) applyCMYRasterFilter(filteredImage);
        else if (m_colorFilterMode == ColorFilterRGB) applyRGBRasterFilter(filteredImage);
        imageToDraw = filteredImage;
    }

    // 3. Berekening van de Target Rect met padding voor de border
    const int b = 4; // Border dikte (voldoende ruimte laten voor bovenkant)
    QRect availableSpace = rect().adjusted(b, b, -b, -b); // Verklein tekengebied

    double sourceAspect = (double)imageToDraw.width() / (double)imageToDraw.height();
    double targetAspect = (double)availableSpace.width() / (double)availableSpace.height();

    QRect targetRect;
    if (targetAspect > sourceAspect) {
        // Widget is breder dan beeld: gebruik volledige beschikbare hoogte
        int scaledWidth = qRound(availableSpace.height() * sourceAspect);
        int offsetX = availableSpace.left() + (availableSpace.width() - scaledWidth) / 2;
        targetRect = QRect(offsetX, availableSpace.top(), scaledWidth, availableSpace.height());
    } else {
        // Widget is smaller dan beeld: gebruik volledige beschikbare breedte
        int scaledHeight = qRound(availableSpace.width() / sourceAspect);
        int offsetY = availableSpace.top() + (availableSpace.height() - scaledHeight) / 2;
        targetRect = QRect(availableSpace.left(), offsetY, availableSpace.width(), scaledHeight);
    }

    // 4. Renderen van het beeld
    p.setRenderHint(QPainter::SmoothPixmapTransform, useSmoothFinalScale);

    // AUTO: als TDOS 80-buffer bestaat => 80-col mode aan
    uint16_t buf = CheckTDOS80BufferAddr();
    auto80 = (buf != 0);

    if (m_80colEnabled && auto80) {
        render80ColumnText(p, targetRect);
    } else {
        p.drawImage(targetRect, imageToDraw);
    }

    // 5. Teken het kader (nu precies BUITEN de targetRect)
    QColor kaderColor(44, 44, 44);
    // Teken 4 lijnen rondom de targetRect
    p.fillRect(targetRect.left() - b, targetRect.top() - b, targetRect.width() + (2 * b), b, kaderColor); // Boven
    p.fillRect(targetRect.left() - b, targetRect.bottom() + 1, targetRect.width() + (2 * b), b, kaderColor); // Onder
    p.fillRect(targetRect.left() - b, targetRect.top() - b, b, targetRect.height() + (2 * b), kaderColor); // Links
    p.fillRect(targetRect.right() + 1, targetRect.top() - b, b, targetRect.height() + (2 * b), kaderColor); // Rechts
}
// ============================================================================
// 80-COLUMN MODE FUNCTIONS
// ============================================================================

void ScreenWidget::set80ColumnMode(bool enabled)
{
    if (m_80colEnabled == enabled) return;
    m_80colEnabled = enabled;
    ones=false;
    update();
}


void ScreenWidget::read80ColumnVRAM(char textBuffer[24][80], unsigned char colorBuffer[24][80])
{
     const unsigned char fgIdx = (tms.VR[7] >> 4) & 0x0F;

    // // 1) Probeer TDOS 80-col buffer in RAM (ADAMEm manier)
     uint16_t tdosAddr = CheckTDOS80BufferAddr();
     if (tdosAddr != 0) {

        int numLines = GetTDOSNumLines();

        // Copy offscreen buffer: numLines * 80 bytes uit Z80-RAM
        for (int row = 0; row < numLines; ++row) {
            for (int col = 0; col < 80; ++col) {
                textBuffer[row][col] = (char)z80rb((uint16_t)(tdosAddr + row * 80 + col));
                colorBuffer[row][col] = fgIdx;
            }
        }

        unsigned int nameTableBase = (tms.VR[2] & 0x0F) << 10;

        if (!ones) {
            memset(RAM_Memory + 0xF900, 0, 0x284);
            ones=true;
        }

        unsigned char block = VDP_Memory[(nameTableBase + 21*40 + 0) & 0x3FFF];
        for (int r = 21; r < 24; ++r) {
            for (int c = 0; c < 40; ++c) {
                unsigned char raw = VDP_Memory[(nameTableBase + r*40 + c) & 0x3FFF];
                textBuffer[r][c] = (char)raw;
                colorBuffer[r][c] = fgIdx;
                //textBuffer[r][c+40] = (char)raw;
                //colorBuffer[r][c+40] = fgIdx;
            }
            for (int c = 40; c < 80; ++c) {
                textBuffer[r][c] = block;
                colorBuffer[r][c] = fgIdx;
            }
        }

        textBuffer[22][67]=' ';
        textBuffer[22][68]='T';
        textBuffer[22][69]='-';
        textBuffer[22][70]='D';
        textBuffer[22][71]='O';
        textBuffer[22][72]='S';
        textBuffer[22][73]=' ';
        textBuffer[22][74]='8';
        textBuffer[22][75]='0';
        textBuffer[22][76]=' ';

        return; // TDOS-pad gebruikt, klaar.
    }

    // 2) Fallback: jouw oude lineaire VRAM lezing (24x80)
    unsigned int nameTableBase = (tms.VR[2] & 0x0F) << 10;
    for (int i = 0; i < 1920; i++) {
        int row = i / 80;
        int col = i % 80;

        unsigned char rawByte = VDP_Memory[(nameTableBase + i) & 0x3FFF];
        textBuffer[row][col] = (char)rawByte;
        colorBuffer[row][col] = fgIdx;
    }
}


void ScreenWidget::setText(QPainter& painter, const QRect& targetRect,int charWidth, int charHeight, unsigned char globalBgIdx)
{

    painter.save();
    painter.translate(targetRect.left(), targetRect.top());
    // Schaal naar een virtueel canvas van 80x24
    painter.scale((double)targetRect.width() / (80.0 * charWidth),
                  (double)targetRect.height() / (24.0 * charHeight));

    globalBgIdx = tms.VR[7] & 0x0F;


    painter.fillRect(0, 0, 80 * charWidth, 24 * charHeight, TMS_COLORS[globalBgIdx]);
}

void ScreenWidget::render80ColumnText(QPainter& painter, const QRect& targetRect)
{
    char textBuffer[24][80];
    unsigned char colorBuffer[24][80];
    read80ColumnVRAM(textBuffer, colorBuffer);

    unsigned char globalBgIdx = tms.VR[7] & 0x0F;

    // font and metrics
    painter.setFont(m_80colFont);
    QFontMetrics fm(m_80colFont);
    int charWidth = fm.horizontalAdvance('M');
    int charHeight = fm.height();

    // setText scaling and background
    setText(painter, targetRect, charWidth, charHeight, globalBgIdx);

    // draw lineair 0..79 over 24 rows
    for (int row = 0; row < 24; row++) {
        for (int col = 0; col < 80; col++) {
            unsigned char rawCh = (unsigned char)textBuffer[row][col];
            bool inverted = (rawCh & 0x80);
            char displayCh = (char)(rawCh & 0x7F);

            int x = col * charWidth;
            int y = row * charHeight;

            if (inverted) {
                painter.fillRect(x, y, charWidth, charHeight, TMS_COLORS[colorBuffer[row][col]]);
                painter.setPen(TMS_COLORS[globalBgIdx]);
            } else {
                painter.setPen(TMS_COLORS[colorBuffer[row][col]]);
            }

            if ((unsigned char)displayCh > 32 || inverted) {
                QString txt = (displayCh <= 32) ? " " : QString(QChar(displayCh));
                painter.drawText(x, y + charHeight - 2, txt);
            }
        }
    }
    painter.restore();
}
