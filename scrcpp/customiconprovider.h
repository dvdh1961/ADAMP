#ifndef CUSTOMICONPROVIDER_H
#define CUSTOMICONPROVIDER_H

#include <QFileIconProvider>
#include <QIcon>

/**
 * @brief Deze klasse levert aangepaste iconen voor specifieke extensies.
 *
 * We overschrijven de 'icon' functie. Voor onze ROM/DDP/DSK-bestanden
 * geven we ons eigen icoon terug. Voor al het andere roepen we de
 * basisklasse aan om het standaard systeemicoon te krijgen (mappen, etc.)
 */
class CustomIconProvider : public QFileIconProvider
{
public:
    CustomIconProvider();

    // De kernfunctie die we overschrijven
    QIcon icon(const QFileInfo &info) const override;

private:
    // We cachen de iconen in het geheugen voor snelheid.
    // Ze worden één keer geladen in de constructor.
    QIcon m_romIcon;
    QIcon m_ddpIcon;
    QIcon m_dskIcon;
};

#endif // CUSTOMICONPROVIDER_H
