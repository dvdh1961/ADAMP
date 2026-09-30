# Qt6 basisconfiguratie
QT       += core gui widgets multimedia printsupport concurrent network
CONFIG   += c++17
CONFIG   -= console

# Projectnaam
TARGET   = ADAMP_EMU
TEMPLATE = app

# Externe bibliotheken linken
LIBS += -lz
win32 {
LIBS += -ldsound -lsetupapi
}
unix  {
LIBS += -lasound
}
win32 {
LIBS += -lwinmm
}

DEFINES += ADAMP_CPM_TRAP
# Build-generation marker: changing this forces Qt Creator/qmake to rebuild
# every translation unit after the MCU2 all-block-device routing update.
DEFINES += ADAMP_CORE_R9454_SGM_COMPILE

# Includepaden
INCLUDEPATH += $$PWD/source \
               $$PWD/bridge \
               $$PWD/scrcpp

# Core
include(core.pri)
include(bridge.pri)
include(scrcpp.pri)
include(pico9918.pri)

RC_FILE = app.rc
