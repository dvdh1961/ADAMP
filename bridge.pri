# Dit bestand bevat alle C-bronbestanden van de koppelingen tussen c en c++.

SOURCES += \
    $$PWD/bridge/debug_bridge.cpp \
    $$PWD/bridge/video_bridge.c \
    $$PWD/bridge/input_bridge.c \
    $$PWD/bridge/psg_bridge.cpp \
    $$PWD/bridge/disasm_bridge.cpp \
    $$PWD/bridge/vdp_bridge.c


HEADERS += \
    $$PWD/bridge/debug_bridge.h \
    $$PWD/bridge/psg_bridge.h \
    $$PWD/bridge/video_bridge.h \
    $$PWD/bridge/input_bridge.h \
    $$PWD/bridge/disasm_bridge.h \
    $$PWD/bridge/vdp_bridge.h
