# Dit bestand bevat alle C-bronbestanden van de koppelingen tussen c en c++.

SOURCES += \
    $$PWD/bridge/video_bridge.c \
    $$PWD/bridge/input_bridge.c \
    $$PWD/bridge/psg_bridge.cpp \
    $$PWD/bridge/disasm_bridge.cpp


HEADERS += \
    $$PWD/bridge/psg_bridge.h \
    $$PWD/bridge/video_bridge.h \
    $$PWD/bridge/input_bridge.h \
    $$PWD/bridge/disasm_bridge.h
