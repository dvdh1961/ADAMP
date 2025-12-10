#ifndef DISASM_H
#define DISASM_H

#include <QString>
#include "z80.h"
#include "debuggerwindow.h"

class ColecoController;

// --- C++ functies ---
QString disasmOneAt(unsigned short addr, int &oplen);

void debug_sync_breakpoints(ColecoController* controller, const QStringList &list);
void disasm_set_symbols(const QList<DebuggerSymbol>& symbols, bool enabled);
QString disasm_get_label_for_address(uint16_t address);

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

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DISASM_H
