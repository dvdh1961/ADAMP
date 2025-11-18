#ifndef EMU_H
#define EMU_H

// =========================================================================
// >> START: C/C++ Specifieke Headers & Dummy Structs <<
// =========================================================================

#ifdef __cplusplus
// --- Dit ziet de C++ COMPILER (voor .cpp bestanden) ---
#include <cstdint>  // C++ header voor types

// Dummy VCL-style menu item
struct TMenuItem {
    bool Checked;
};

// Dummy class voor 'emulator'
class TAdamP {
public:
    bool NTSC;
    bool PAL;
    bool F18A;
    uint32_t cardcrc;  // Gebruik hier direct cstdint types
    uint32_t cardsize; // Gebruik hier direct cstdint types
    char romCartridge[1024];
    int romCartridgeType;
    int currentMachineType;
    bool hackbiospal;
    bool biosnodelay;
    int typebackup;
    bool SGM;
    bool singlestep;
    bool stop;
    int palette;
    char colecobios[1024];  // Pad naar BIOS-bestanden
    char adameos[1024];
    char adamwriter[1024];
    char currentrom[1024]; // Pad naar de huidige ROM
};

extern TAdamP* emulator; // Externe declaratie voor C++

#else
// --- Dit ziet de C COMPILER (voor .c bestanden) ---
#include <stdint.h>   // C header voor types
#include <stdbool.h>  // C header voor 'bool'

struct TMenuItem {
    bool Checked;
};
struct EmuMachine machine;

// Dummy *struct* voor 'emulator' (C kent geen 'class')
struct TAdamP {
    bool NTSC;
    bool PAL;
    bool F18A;
    uint32_t cardcrc;  // Gebruik hier direct stdint types
    uint32_t cardsize; // Gebruik hier direct stdint types
    char romCartridge[1024];
    int romCartridgeType;
    int currentMachineType;
    bool hackbiospal;
    bool biosnodelay;
    int typebackup;
    bool SGM;
    bool singlestep;
    bool stop;
    int palette;
    char colecobios[1024];  // Pad naar BIOS-bestanden
    char adameos[1024];
    char adamwriter[1024];
    char currentrom[1024]; // Pad naar de huidige ROM
};

extern struct TAdamP* emulator; // Externe declaratie voor C

// Dit is onze *enige* instantie van de UI-configuratie struct:
struct TAdamP g_emulator_instance;
// Dit is de *pointer* die de hele C-core gebruikt:
struct TAdamP* emulator = &g_emulator_instance;


#endif // __cplusplus

// =========================================================================
// >> START: Algemene Type Definities (voor C én C++) <<
// =========================================================================
// Deze typedefs zijn nu zichtbaar voor ALLE bestanden omdat ze
// BUITEN de #ifdef __cplusplus blokken staan.
// =========================================================================

typedef uint8_t  BYTE;
typedef uint16_t WORD;
#ifndef _WINDOWS_
typedef uint32_t DWORD;
#endif
typedef int      BOOL;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

// Clock frequencies for sound chips (approximate)
#define CLOCK_MASTER_NTSC 10738635
#define CLOCK_MASTER_PAL  10640000 // PAL master clock is slightly different
#define CLOCK_NTSC        (CLOCK_MASTER_NTSC / 3) // Approx 3.58 MHz
#define CLOCK_PAL         (CLOCK_MASTER_PAL / 3)  // Approx 3.55 MHz
// EEPROM Type defines (guessing values, adjust if needed)
#define NOBACKUP    -1 // Geen backup type gedefinieerd
#define EEP24C08    0
#define EEP24C256   1
#define EEPSRAM      2 // Voor Lord Of The Dungeon SRAM
// Machine Type defines
#define MACHINECOLECO 0 // Standard ColecoVision
#define MACHINEADAM   1 // ADAM Computer
// ROM status codes
#define ROM_LOAD_OK   0
#define ROM_LOAD_FAIL 1
#define ROM_VERIFY_FAIL 2
#define ROM_LOAD_PASS     ROM_LOAD_OK

// Cartridge Type defines
#define ROMCARTRIDGENONE -1 // Geen cartridge geladen
#define ROMCARTRIDGESTD   0 // <-- ADD THIS
#define ROMCARTRIDGEMEGA  1 // <-- ADD THIS

// =========================================================================
// >> START: Core Emulator Machine Definition <<
// =========================================================================
// Dit is de hoofdstructuur voor de geëmuleerde machine-status.
// Zowel C als C++ kunnen deze struct-definitie begrijpen.
struct EmuMachine {
    int speed;                  // Machine speed (in %)
    int romsize;                // ROM size (in Ko)
    int nmistatus;              // NMI status
    int interrupt;              // The Z80 interrupt vector
    int autostart;              // 1 = autostart
    int vkey;                   // 1 = virtual keyboard
    int megacart;               // 1 = megacart
    int cartmagic;              // 1 = cart magic
    int memmap;                 // Memory mapping
    int keys[16];               // Keyboard
    int joy[2][8];              // Joysticks
    char MachineName[256];      // Machine name
    char RomName[256];          // ROM name
    char RomDir[256];           // ROM directory
    char StateDir[256];         // State directory
    int tperscanline;           // T-states per scanline
};

// Declareer de enige, globale instantie van de machine
extern struct EmuMachine machine;
// =========================================================================
// >> END: Core Emulator Machine Definition <<
// =========================================================================

#endif // EMU_H
