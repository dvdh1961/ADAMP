#include "customiconprovider.h"
#include <QFileInfo>
#include <QDebug> // Optioneel, voor debuggen

CustomIconProvider::CustomIconProvider()
{
    // --- BELANGRIJK ---
    // Laad de iconen vanuit je Qt Resource-bestand (.qrc).
    // Ik baseer deze paden op de paden die ik in je 'printwindow.cpp' zag.
    // Pas deze aan als je ze op een andere plek in je .qrc zet.

    m_romIcon = QIcon(":/images/images/roms.png");
    m_ddpIcon = QIcon(":/images/images/ddp.png");
    m_dskIcon = QIcon(":/images/images/dsk.png");

    // Optionele check om te zien of het laden gelukt is
    if (m_romIcon.isNull()) {
        qWarning() << "CustomIconProvider: Kon 'roms.png' niet laden.";
    }
    if (m_ddpIcon.isNull()) {
        qWarning() << "CustomIconProvider: Kon 'ddp.png' niet laden.";
    }
    if (m_dskIcon.isNull()) {
        qWarning() << "CustomIconProvider: Kon 'dsk.png' niet laden.";
    }
}

QIcon CustomIconProvider::icon(const QFileInfo &info) const
{
    // Haal de extensie op, altijd in kleine letters
    const QString ext = info.suffix().toLower();

    // 1. Check voor onze aangepaste types
    if (ext == "rom" || ext == "col" || ext == "bin") {
        return m_romIcon;
    }
    if (ext == "ddp") {
        return m_ddpIcon;
    }
    if (ext == "dsk") {
        return m_dskIcon;
    }

    // 2. Voor al het andere (mappen, .txt, .exe, onbekend)...
    //    ...vraag het standaard systeemicoon op.
    return QFileIconProvider::icon(info);
}
