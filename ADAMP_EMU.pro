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
               $$PWD/bridge

# Bronbestanden
# C++ bronnen voor de Qt-interface
SOURCES += main.cpp \
           cartridgeinfowindow.cpp \
           colecocontroller.cpp \
           core_globals.cpp \
           debuggerwindow.cpp \
           inputwidget.cpp \
           logwindow.cpp \
           mainwindow.cpp \
           ntablewindow.cpp \
           patternwindow.cpp \
           screenwidget.cpp \
           settingswindow.cpp \
           spritewindow.cpp

# Headers voor de Qt-interfacek
HEADERS += mainwindow.h \
           cartridgeinfowindow.h \
           colecocontroller.h \
           debuggerwindow.h \
           inputwidget.h \
           logwindow.h \
           ntablewindow.h \
           patternwindow.h \
           screenwidget.h \
           settingswindow.h \
           spritewindow.h

# Core
# We includen een apart bestand om dit overzichtelijk te houden
include(core.pri)
include(bridge.pri)

RC_FILE = app.rc

RESOURCES += \
    resources.qrc

