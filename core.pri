# Dit bestand bevat alle C-bronbestanden van de core.

SOURCES += \
    $$PWD/source/6801/adnet_core.cpp \
    $$PWD/source/6801/adnet_ddp.cpp \
    $$PWD/source/6801/adnet_dsk.cpp \
    $$PWD/source/6801/adnet_kb.cpp \
    $$PWD/source/6801/adnet_prn.cpp \
    $$PWD/source/6801/fdidisk.c \
    $$PWD/source/CORE/c24xx.c \
    $$PWD/source/CORE/cv.cpp \
    $$PWD/source/CORE/cvbank.cpp \
    $$PWD/source/CORE/cvkpad.cpp \
    $$PWD/source/CORE/cvstate.cpp \
    $$PWD/source/CORE/z80.c \
    $$PWD/source/GRAPH/f18a.c \
    $$PWD/source/GRAPH/f18a_term80.c \
    $$PWD/source/GRAPH/f18a_term80_cpm.cpp \
    $$PWD/source/GRAPH/f18a_term80_tdos.cpp \
    $$PWD/source/GRAPH/tms9928a.c \
    $$PWD/source/SOUND/snd_ay8910.c \
    $$PWD/source/SOUND/snd_sn76489.c

HEADERS += \
    $$PWD/source/6801/adnet_core.h \
    $$PWD/source/6801/fdidisk.h \
    $$PWD/source/BIOS/bios_adam.h \
    $$PWD/source/BIOS/bios_coleco.h \
    $$PWD/source/CORE/c24xx.h \
    $$PWD/source/CORE/cv.h \
    $$PWD/source/CORE/cvbank.h \
    $$PWD/source/CORE/cvkpad.h \
    $$PWD/source/CORE/cvstate.h \
    $$PWD/source/CORE/emu.h \
    $$PWD/source/CORE/z80.h \
    $$PWD/source/GRAPH/f18a.h \
    $$PWD/source/GRAPH/f18a_term80.h \
    $$PWD/source/GRAPH/f18a_term80_cpm.h \
    $$PWD/source/GRAPH/f18a_term80_tdos.h \
    $$PWD/source/GRAPH/tms9928a.h \
    $$PWD/source/SOUND/snd_ay8910.h \
    $$PWD/source/SOUND/snd_sn76489.h
