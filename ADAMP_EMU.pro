# Qt6 basisconfiguratie
QT       += core gui widgets multimedia printsupport
CONFIG   += c++17
CONFIG   -= console

# Projectnaam
TARGET   = ADAMP_EMU
TEMPLATE = app

# Externe bibliotheken linken
LIBS += -lz
win32 {
LIBS += -ldsound
}
unix  {
LIBS += -lasound
}
win32 {
LIBS += -lwinmm
}

# Includepaden
INCLUDEPATH += $$PWD/source \
               $$PWD/bridge \
               $$PWD/scrcpp

# Core
include(core.pri)
include(bridge.pri)
include(scrcpp.pri)

RC_FILE = app.rc
