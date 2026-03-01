#ifndef CUSTOMICONPROVIDER_H
#define CUSTOMICONPROVIDER_H

#include <QFileIconProvider>
#include <QIcon>

class CustomIconProvider : public QFileIconProvider
{
public:
    CustomIconProvider();

    QIcon icon(const QFileInfo &info) const override;

private:
    QIcon m_romIcon;
    QIcon m_ddpIcon;
    QIcon m_dskIcon;
    QIcon m_parentIcon;
};

#endif
