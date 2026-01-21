#ifndef SCREENWIDGET_H
#define SCREENWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QMutex>
#include "mainwindow.h"

extern bool m_80colEnabled;

class ScreenWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenWidget(QWidget *parent = nullptr);
    ~ScreenWidget();

    enum ScalingMode {
        ModeSharp,  // 0
        ModeSmooth, // 1
        ModeEPX     // 2
    };

    uchar* frameBits() { return m_frame.bits(); }
    int    frameStride() const { return m_frame.bytesPerLine(); }

    static constexpr int COLECO_WIDTH  = 256;
    static constexpr int COLECO_HEIGHT = 192;

   QSize sizeHint() const override;
   QSize minimumSizeHint() const override;

   void setBackgroundColor(const QColor& color);
   void setSmoothScaling(bool enabled);
   void setFullScreenMode(bool enabled);

   // 80-column mode control
   void set80ColumnMode(bool enabled);
   bool is80ColumnMode() const { return m_80colEnabled; }

public slots:
    void updateFrame(const QImage &frame);
    void setFrame(const QImage &img);
    void setScalingMode(ScalingMode mode);
    void setScanlinesMode(ScanlinesMode mode);
    void setColorFilterMode(ColorFilterMode mode);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_frame;
    QMutex m_mutex;
    QColor m_backgroundColor;
    bool m_smoothScaling;
    bool m_isFullScreen;
    ScalingMode m_scalingMode;

    QImage m_epxBuffer;
    void applyEPX(const QImage& source);

    ScanlinesMode m_scanlinesMode = ScanlinesOff;

    void applyTVScanlinesFilter(QImage& image);
    void applyLCDizeFilter(QImage& image);
    void applyRasterizeFilter(QImage& image);

    ColorFilterMode m_colorFilterMode = ColorFilterOff;

    void applyMonochromeFilter(QImage& image);
    void applySepiaFilter(QImage& image);
    void applyGreenCRTFilter(QImage& image);
    void applyAmberCRTFilter(QImage& image);
    void applyCMYRasterFilter(QImage& image);
    void applyRGBRasterFilter(QImage& image);

    // 80-column text mode support
    QFont m_80colFont;
    void render80ColumnText(QPainter& painter, const QRect& targetRect);
    void read80ColumnVRAM(char textBuffer[24][80], unsigned char colorBuffer[24][80]);
    void setText(QPainter& painter, const QRect& targetRect, int charwidth, int charHeight, unsigned char globalBgIdx);
    static const QColor TMS_COLORS[16];
};

#endif
