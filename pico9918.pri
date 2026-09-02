# pico9918-core: the PICO9918 VDP engine, built by its own CMake as a static library.
# The generated pico9918_build_config.h records the ABI-affecting build choices and
# the public headers assert against it, so the build tree is on the include path too.

PICO9918_SRC = $$PWD/3rdparty/pico9918-core

!exists($$PICO9918_SRC/CMakeLists.txt) {
    error("pico9918-core is missing - run: git submodule update --init --recursive")
}

CONFIG(debug, debug|release) {
    PICO9918_BUILD_TYPE = Debug
} else {
    PICO9918_BUILD_TYPE = Release
}

# One build tree per configuration. The Makefile generators are single-config: the
# archive's build type is whatever the last `cmake -S -B` set, and pico9918_build_config.h
# is written once per tree. Sharing one tree between a debug and a release pass therefore
# links whichever was configured last into both.
PICO9918_BUILD = $$OUT_PWD/pico9918-core-$$lower($$PICO9918_BUILD_TYPE)

INCLUDEPATH += $$PICO9918_SRC/src \
               $$PICO9918_BUILD/src

# Static archive. Without this the headers fall through to __declspec(dllimport)
# on WIN32 and every entry point comes out as __imp_*.
DEFINES += PICO9918_STATIC

# The library's version, packed major(4) | minor(4) | patch(8) as the config stamp
# wants it. Read from the library's CMakeLists rather than written here, so an upgrade
# cannot leave ADAMP stamping a stale version and skipping the field migration.
# Anchored on project( so a comment mentioning the version cannot match first.
PICO9918_VERSION = $$cat($$PICO9918_SRC/CMakeLists.txt, lines)
PICO9918_VERSION = $$find(PICO9918_VERSION, "project\\(pico9918_core VERSION [0-9]")
PICO9918_VERSION = $$split(PICO9918_VERSION, " ")
PICO9918_VERSION = $$member(PICO9918_VERSION, 2)

# Checked rather than assumed: qmake's split keeps empty parts, so a second space before
# VERSION would otherwise hand "VERSION" to the compiler as a version number and fail
# somewhere far less obvious.
!contains(PICO9918_VERSION, "^[0-9]+\\.[0-9]+\\.[0-9]+$") {
    error("pico9918-core: could not read a three-part version from $$PICO9918_SRC/CMakeLists.txt \
- found \"$$PICO9918_VERSION\". Expected one line reading: project(pico9918_core VERSION x.y.z ...)")
}

PICO9918_VERSION_PARTS = $$split(PICO9918_VERSION, ".")
PICO9918_VER_MAJOR = $$member(PICO9918_VERSION_PARTS, 0)
PICO9918_VER_MINOR = $$member(PICO9918_VERSION_PARTS, 1)
PICO9918_VER_PATCH = $$member(PICO9918_VERSION_PARTS, 2)

DEFINES += PICO9918_CORE_VER_MAJOR=$$PICO9918_VER_MAJOR \
           PICO9918_CORE_VER_MINOR=$$PICO9918_VER_MINOR \
           PICO9918_CORE_VER_PATCH=$$PICO9918_VER_PATCH

# 80-column text at 8bpp: ECM, palette select and the bitmap layer in T80, as real
# F18A hardware does. One variable so the CMake option and the define cannot drift.
PICO9918_TEXT80_8BPP = ON
equals(PICO9918_TEXT80_8BPP, ON): DEFINES += PICO9918_TEXT80_8BPP=1

# The chip an instance answers as, picked at runtime: ADAMP renders its TMS9918A, F18A
# and PICO9918 alike out of this one archive, so the personality cannot be the build's.
# OFF is what a board builds - see the library's option, which folds every gate away.
# No matching DEFINE: the library records this one in its generated header and the
# public API gates on PICO9918_BUILD_RUNTIME_CHIP.
PICO9918_RUNTIME_CHIP = ON

# -march=native otherwise, which is wrong for anything redistributable: the archive gets
# tuned to the build machine while ADAMP's own objects do not, and an older CPU meets it
# as a SIGILL inside pico9918_* alone. Only the library's own CMake does this, and only
# on UNIX. Flip to OFF for a local build you will never hand to anyone.
PICO9918_PORTABLE_CODEGEN = ON

# The compiler, not the OS: win32 is true for win32-msvc too, and MSVC needs a
# generator it can actually drive.
msvc {
    PICO9918_GENERATOR = NMake Makefiles
} else:win32 {
    PICO9918_GENERATOR = MinGW Makefiles
} else {
    PICO9918_GENERATOR = Unix Makefiles
}

# PICO9918_MODE=1 selects the F18A: the enhanced renderer and the TMS9900 GPU.
PICO9918_CONFIGURE = cmake -S \"$$PICO9918_SRC\" -B \"$$PICO9918_BUILD\" -G \"$$PICO9918_GENERATOR\" -DCMAKE_BUILD_TYPE=$$PICO9918_BUILD_TYPE -DPICO9918_MODE=1 -DPICO9918_TEXT80_8BPP=$$PICO9918_TEXT80_8BPP -DPICO9918_RUNTIME_CHIP=$$PICO9918_RUNTIME_CHIP -DPICO9918_PORTABLE_CODEGEN=$$PICO9918_PORTABLE_CODEGEN

# Configure eagerly, while qmake parses: the configure step writes
# pico9918_build_config.h, which every TU including pico9918.h needs before the first
# compile. PRE_TARGETDEPS only orders the link, far too late under a parallel make.
!system($$PICO9918_CONFIGURE) {
    error("pico9918-core: CMake configure failed. Its own output is above this line - \
common causes are cmake not on PATH, no compiler for the \"$$PICO9918_GENERATOR\" \
generator, or a build tree left behind by a different generator (delete \
$$PICO9918_BUILD). The command was: $$PICO9918_CONFIGURE")
}

pico9918core.target   = pico9918-core-lib
pico9918core.commands = cmake --build \"$$PICO9918_BUILD\" --config $$PICO9918_BUILD_TYPE --target pico9918_core
QMAKE_EXTRA_TARGETS += pico9918core
PRE_TARGETDEPS      += pico9918-core-lib

LIBS += -L$$PICO9918_BUILD/src -lpico9918_core
