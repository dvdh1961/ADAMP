#ifndef SCREENWIDGET_H
#define SCREENWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QMutex>
#include "mainwindow.h"

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

public slots:
    void updateFrame(const QImage &frame);
    void setFrame(const QImage &img);
    void setScalingMode(ScalingMode mode);
    void setScanlinesMode(ScanlinesMode mode);

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
};

#endif
