#include "customiconprovider.h"
#include <QFileInfo>
#include <QDebug>

CustomIconProvider::CustomIconProvider()
{
    m_romIcon = QIcon(":/images/images/roms.png");
    m_ddpIcon = QIcon(":/images/images/ddp.png");
    m_dskIcon = QIcon(":/images/images/dsk.png");

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
    const QString ext = info.suffix().toLower();

    if (ext == "rom" || ext == "col" || ext == "bin") {
        return m_romIcon;
    }
    if (ext == "ddp") {
        return m_ddpIcon;
    }
    if (ext == "dsk") {
        return m_dskIcon;
    }

    return QFileIconProvider::icon(info);
}
