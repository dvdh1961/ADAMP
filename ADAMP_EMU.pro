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

# Force a rebuild for the Windows General joystick X/Y correction.
DEFINES += ADAMP_JOYSTICK_MSX_XY_FIX2

# Rebuild all core/router objects after adding the PC AdamNet backend.
DEFINES += ADAMP_FUJINET_PC_BACKEND_4

# Keep third-party runtime files under 3rdparty next to the built app.
# This also handles Qt Creator shadow builds; no separate manual download is needed.
# Qt Creator selects build/release or build/debug as the shadow-build directory.
# Avoid qmake's additional release/debug executable subdirectory on Windows.
CONFIG -= debug_and_release debug_and_release_target
DESTDIR = $$OUT_PWD
FUJINET_APP_DIR = $$OUT_PWD
!isEmpty(DESTDIR): FUJINET_APP_DIR = $$absolute_path($$DESTDIR, $$OUT_PWD)
FUJINET_RUNTIME_DEST = $$FUJINET_APP_DIR/3rdparty/fujinet
# Create the parent before COPY_DIR; copying does not create missing parents.
!mkpath($$FUJINET_RUNTIME_DEST): error(Cannot create FujiNet runtime directory)
win32 {
    QMAKE_POST_LINK += $$QMAKE_COPY_DIR $$shell_quote($$shell_path($$PWD/3rdparty/fujinet/windows)) $$shell_quote($$shell_path($$FUJINET_RUNTIME_DEST/windows))
}
unix {
    QMAKE_POST_LINK += mkdir -p $$shell_quote($$FUJINET_RUNTIME_DEST/linux) && cp -R $$shell_quote($$PWD/3rdparty/fujinet/linux/.) $$shell_quote($$FUJINET_RUNTIME_DEST/linux/)
}

# Deploy the CVBasic tools and assembler support files for shadow builds.
CVBASIC_RUNTIME_DEST = $$FUJINET_APP_DIR/3rdparty/cvbasic
!mkpath($$CVBASIC_RUNTIME_DEST): error(Cannot create CVBasic runtime directory)
win32 {
    QMAKE_POST_LINK += $$escape_expand(\n\t) $$QMAKE_COPY_DIR $$shell_quote($$shell_path($$PWD/3rdparty/cvbasic)) $$shell_quote($$shell_path($$CVBASIC_RUNTIME_DEST))
}
unix {
    QMAKE_POST_LINK += $$escape_expand(\n\t) cp -R $$shell_quote($$PWD/3rdparty/cvbasic/.) $$shell_quote($$CVBASIC_RUNTIME_DEST/)
}
