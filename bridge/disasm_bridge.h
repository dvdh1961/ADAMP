#ifndef DISASM_H
#define DISASM_H

#include <QString>
#include "z80.h"

class ColecoController; // <-- Forward declaration

// --- C++ functies ---
QString disasmOneAt(unsigned short addr, int &oplen);

/**
 * @brief Registreert de C++ DebuggerWindow instantie bij de C-bridge.
 * @param debugger_instance Een void pointer naar de DebuggerWindow.
 */
//void debug_register_debugger(void* debugger_instance);
void debug_sync_breakpoints(ColecoController* controller, const QStringList &list); // <-- NIEUW

// --- C-Linkable functies ---
#ifdef __cplusplus
extern "C" {
#endif

extern Z80_Regs Z80;

// ... (alle z80_get_... functies blijven hier staan) ...
inline unsigned short z80_get_pc()   { return Z80.pc.w.l; }
inline unsigned short z80_get_sp()   { return Z80.sp.w.l; }
inline unsigned short z80_get_af()   { return Z80.af.w.l; }
inline unsigned short z80_get_bc()   { return Z80.bc.w.l; }
inline unsigned short z80_get_de()   { return Z80.de.w.l; }
inline unsigned short z80_get_hl()   { return Z80.hl.w.l; }
inline unsigned short z80_get_ix()   { return Z80.ix.w.l; }
inline unsigned short z80_get_iy()   { return Z80.iy.w.l; }
inline unsigned short z80_get_af2()  { return Z80.af2.w.l; }
inline unsigned short z80_get_bc2()  { return Z80.bc2.w.l; }
inline unsigned short z80_get_de2()  { return Z80.de2.w.l; }
inline unsigned short z80_get_hl2()  { return Z80.hl2.w.l; }
inline unsigned char  z80_get_r()    { return Z80.r; }
inline unsigned char  z80_get_i()    { return Z80.i; }
inline unsigned char  z80_get_im()   { return Z80.im; }
inline unsigned char  z80_get_iff1() { return Z80.iff1; }
inline unsigned char  z80_get_iff2() { return Z80.iff2; }
inline unsigned char  z80_get_halt() { return Z80.halt; }


// --- NIEUWE FUNCTIE (voor de C-core) ---
/**
 * @brief Wordt aangeroepen door de Z80-loop (via DebugUpdate) om te checken op breakpoints.
 * @param pc De huidige Program Counter.
 * @return 1 als een breakpoint is geraakt, anders 0.
 */
//int debug_check_breakpoint(unsigned short pc);
// --- EINDE NIEUW ---

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DISASM_H
