#ifndef SCREENWIDGET_H
#define SCREENWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QMutex> // Nodig voor thread-veiligheid

// 1. Definieer de modi
enum ScalingMode {
    ModeSharp,  // 0
    ModeSmooth, // 1
    ModeEPX     // 2
};

class ScreenWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenWidget(QWidget *parent = nullptr);
    ~ScreenWidget();

    // Handig voor zero-copy in de toekomst (optioneel)
    uchar* frameBits() { return m_frame.bits(); }
    int    frameStride() const { return m_frame.bytesPerLine(); }

    // Standaard Coleco resolutie
    static constexpr int COLECO_WIDTH  = 256;
    static constexpr int COLECO_HEIGHT = 192;

   QSize sizeHint() const override;
   QSize minimumSizeHint() const override;

   void setBackgroundColor(const QColor& color);
   void setSmoothScaling(bool enabled);
   void setFullScreenMode(bool enabled);
   void setScalingMode(ScalingMode mode);

public slots:
    // Dit is het slot dat het signaal van de ColecoController ontvangt
    void updateFrame(const QImage &frame);
    void setFrame(const QImage &img);

protected:
    // We overschrijven de paint-functie
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_frame; // De huidige afbeelding die we tonen
    QMutex m_mutex; // Beveiliging voor toegang vanuit meerdere threads
    QColor m_backgroundColor;
    bool m_smoothScaling;
    bool m_isFullScreen;
    ScalingMode m_scalingMode;

    QImage m_epxBuffer;
    void applyEPX(const QImage& source);
};

#endif // SCREENWIDGET_H
