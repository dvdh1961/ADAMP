# Qt6 basisconfiguratie
QT       += core gui widgets multimedia
CONFIG   += c++17

CONFIG += console


# Projectnaam
TARGET = ADAMP_EMU
TEMPLATE = app

# Externe bibliotheken linken
LIBS += -lz

# Includepaden
# We voegen de 'source' map toe zodat de C-bestanden elkaar kunnen vinden
INCLUDEPATH += $$PWD/source \
               $$PWD/bridge \
               $$PWD/scrcpp

# Core
# We includen een apart bestand om dit overzichtelijk te houden
include(core.pri)
include(bridge.pri)
include(scrcpp.pri)

RC_FILE = app.rc


