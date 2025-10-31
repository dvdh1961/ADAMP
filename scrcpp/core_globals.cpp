/*
 * core_globals.cpp
 * * Dit bestand definieert de globale objecten die de C-core
 * verwacht te vinden, zoals gedeclareerd in emu.h.
 * Dit vervangt de rol van het Pascal 'TMainForm'.
 */

#include "emu.h" // Dit *declareert* 'extern TEmul2Form* emul2' en 'extern struct EmuMachine machine'

// =========================================================================
// 1. Definieer de 'machine' struct
// =========================================================================
// emu.h zegt: extern struct EmuMachine machine;
// Hier is de *definitie* ervan:
struct EmuMachine machine;

// =========================================================================
// 2. Definieer de 'emul2' instantie en pointer
// =========================================================================
// emu.h zegt: extern TEmul2Form* emul2;
// We moeten dus een TEmul2Form object aanmaken en de pointer ernaar laten wijzen.

// Dit is onze *enige* instantie van de UI-configuratie struct:
TEmul2Form g_emul2_instance;

// Dit is de *pointer* die de hele C-core gebruikt:
TEmul2Form* emul2 = &g_emul2_instance;
// =========================================================================
