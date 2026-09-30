# Dit bestand bevat de C/C++-bronbestanden van de emulatorcore.


SOURCES += \
    $$PWD/scrc/CORE/c24xx.c \
    $$PWD/scrc/CORE/msxp.cpp \
    $$PWD/scrc/CORE/msxpbank.cpp \
    $$PWD/scrc/CORE/msxpkpad.cpp \
    $$PWD/scrc/CORE/z80.c \
    $$PWD/scrc/GRAPH/msx2_vdp_port_table.c \
    $$PWD/scrc/GRAPH/msx2_vdp_shims.c \
    $$PWD/scrc/GRAPH/tms9928a.c \
    $$PWD/scrc/GRAPH/VDP.c \
    $$PWD/scrc/GRAPH/V9938.c \
    $$PWD/scrc/SOUND/snd_ay8910.c \
    $$PWD/scrc/SOUND/snd_sn76489.c

HEADERS += \
    $$PWD/scrc/CORE/c24xx.h \
    $$PWD/scrc/CORE/emu.h \
    $$PWD/scrc/CORE/msxp.h \
    $$PWD/scrc/CORE/msxpbank.h \
    $$PWD/scrc/CORE/msxpkpad.h \
    $$PWD/scrc/CORE/z80.h \
    $$PWD/scrc/GRAPH/msx2_vdp_port_table.h \
    $$PWD/scrc/GRAPH/msx2_vdp_shims.h \
    $$PWD/scrc/GRAPH/tms9928a.h \
    $$PWD/scrc/GRAPH/Common.h \
    $$PWD/scrc/GRAPH/SpriteLine.h \
    $$PWD/scrc/GRAPH/VDP.h \
    $$PWD/scrc/GRAPH/V9938.h \
    $$PWD/scrc/GRAPH/ArchVideoIn.h \
    $$PWD/scrc/GRAPH/Board.h \
    $$PWD/scrc/GRAPH/msx2_vdp_shim_common.h \
    $$PWD/scrc/GRAPH/DebugDeviceManager.h \
    $$PWD/scrc/GRAPH/DeviceManager.h \
    $$PWD/scrc/GRAPH/FrameBuffer.h \
    $$PWD/scrc/GRAPH/IoPort.h \
    $$PWD/scrc/GRAPH/Language.h \
    $$PWD/scrc/GRAPH/MsxTypes.h \
    $$PWD/scrc/GRAPH/SaveState.h \
    $$PWD/scrc/GRAPH/msx2_vdp_shim_sprite_line.h \
    $$PWD/scrc/GRAPH/VideoManager.h \
    $$PWD/scrc/SOUND/snd_ay8910.h \
    $$PWD/scrc/SOUND/snd_sn76489.h
