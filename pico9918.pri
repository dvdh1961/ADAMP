# pico9918-core compiled directly by qmake.
# No separate CMake installation or static-library build is required.

PICO9918_ROOT = $$PWD/3rdparty/pico9918-core
PICO9918_SRC  = $$PICO9918_ROOT/src
PICO9918_GEN  = $$PICO9918_SRC/generated

!exists($$PICO9918_SRC/pico9918.c) {
    error("pico9918-core sources are missing from 3rdparty/pico9918-core")
}

INCLUDEPATH += $$PICO9918_SRC \
               $$PICO9918_SRC/gpu \
               $$PICO9918_SRC/platform \
               $$PICO9918_GEN

DEFINES += PICO9918_STATIC \
           PICO9918_MODE=1 \
           PICO9918_TEXT80_8BPP=1 \
           PICO9918_ASSET_PIXEL_SIZE=4 \
           PICO9918_CORE_VER_MAJOR=1 \
           PICO9918_CORE_VER_MINOR=3 \
           PICO9918_CORE_VER_PATCH=0

# Apply ADAMP's 32-bit ARGB pixel policy to every pico9918 translation unit.
PICO9918_POLICY = $$PWD/bridge/pico9918_pixel_policy.h
win32-g++|unix {
    QMAKE_CFLAGS += -include "$$PICO9918_POLICY"
}

SOURCES += \
    $$PICO9918_SRC/pico9918.c \
    $$PICO9918_SRC/pico9918_util.c \
    $$PICO9918_SRC/pico9918_config.c \
    $$PICO9918_SRC/pico9918_palette.c \
    $$PICO9918_SRC/pico9918_frame.c \
    $$PICO9918_SRC/overlay/splash.c \
    $$PICO9918_SRC/overlay/diag.c \
    $$PICO9918_SRC/gpu/gpu.c \
    $$PICO9918_SRC/gpu/tms9900.c \
    $$PICO9918_GEN/overlay/bmp_splash.c \
    $$PICO9918_GEN/overlay/bmp_font.c

HEADERS += \
    $$PICO9918_SRC/pico9918.h \
    $$PICO9918_SRC/pico9918_config.h \
    $$PICO9918_SRC/pico9918_frame.h \
    $$PICO9918_SRC/pico9918_util.h \
    $$PICO9918_GEN/pico9918_build_config.h \
    $$PICO9918_GEN/overlay/bmp_splash.h \
    $$PICO9918_GEN/overlay/bmp_font.h
