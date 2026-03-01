#include "customiconprovider.h"
#include <QFileInfo>
#include <QDebug>

CustomIconProvider::CustomIconProvider()
{
    m_romIcon = QIcon(":/images/images/roms.png");
    m_ddpIcon = QIcon(":/images/images/ddp.png");
    m_dskIcon = QIcon(":/images/images/dsk.png");
    m_parentIcon = QIcon(":/images/images/parent.png");

    if (m_romIcon.isNull()) {
        qWarning() << "CustomIconProvider: Kon 'roms.png' niet laden.";
    }
    if (m_ddpIcon.isNull()) {
        qWarning() << "CustomIconProvider: Kon 'ddp.png' niet laden.";
    }
    if (m_dskIcon.isNull()) {
        qWarning() << "CustomIconProvider: Kon 'dsk.png' niet laden.";
    }
    if (m_parentIcon.isNull()) {
        qWarning() << "CustomIconProvider: Kon 'parent.png' niet laden.";
    }
}

QIcon CustomIconProvider::icon(const QFileInfo &info) const
{
    // STAP 1: Check eerst of het de ".." map is
    if (info.fileName() == "..") {
        return m_parentIcon;
    }
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
