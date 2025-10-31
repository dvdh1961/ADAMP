#ifndef SCREENWIDGET_H
#define SCREENWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QMutex> // Nodig voor thread-veiligheid

class ScreenWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenWidget(QWidget *parent = nullptr);
    ~ScreenWidget();

    // Handig voor zero-copy in de toekomst (optioneel)
    uchar* frameBits() { return m_frame.bits(); }
    int    frameStride() const { return m_frame.bytesPerLine(); }

    // Nieuw: eenvoudige zoom (1.0 = 256x192, 2.0 = 512x384, ...)
    void setScale(qreal s);
    qreal scale() const { return m_scale; }

    // Standaard Coleco resolutie
    static constexpr int COLECO_WIDTH  = 256;
    static constexpr int COLECO_HEIGHT = 192;

   QSize sizeHint() const override;
   QSize minimumSizeHint() const override;


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
    qreal  m_scale = 2.0; // standaard 2x weergave

    void applyFixedSize(); // helper om vaste grootte te zetten op basis van m_scale
};

#endif // SCREENWIDGET_H
