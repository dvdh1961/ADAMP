#include "disasm_bridge.h"
#include "coleco.h"
#include <QString>
#include <QVector>

#include "debuggerwindow.h" // Heeft de C++ klasse definitie
#include "emu.h"            // Heeft de 'emulator' globale struct
#include <QMetaObject>

#define B(x) QString("%1").arg((x),2,16,QChar('0')).toUpper()
#define W(x) QString("%1").arg((x),4,16,QChar('0')).toUpper()

// Deze pointer "houdt" de C++ debugger vast voor de C-functies
//static DebuggerWindow* g_debugger = nullptr;

static QString disasm_cb(unsigned short addr,int &oplen);
static QString disasm_ed(unsigned short addr,int &oplen);
static QString disasm_ddfd(unsigned short addr,int &oplen,bool iy);

QString disasmOneAt(unsigned short addr,int &oplen)
{
    const uint8_t op = coleco_ReadByte(addr);
    const uint8_t b1 = coleco_ReadByte(addr+1);
    const uint8_t b2 = coleco_ReadByte(addr+2);
    oplen = 1;

    switch(op)
    {
    case 0x00: return "NOP";
    case 0x01: oplen=3; return "LD BC,$"+W((b2<<8)|b1);
    case 0x02: return "LD (BC),A";
    case 0x03: return "INC BC";
    case 0x04: return "INC B";
    case 0x05: return "DEC B";
    case 0x06: oplen=2; return "LD B,#"+B(b1);
    case 0x07: return "RLCA";
    case 0x08: return "EX AF,AF'";
    case 0x09: return "ADD HL,BC";
    case 0x0A: return "LD A,(BC)";
    case 0x0B: return "DEC BC";
    case 0x0C: return "INC C";
    case 0x0D: return "DEC C";
    case 0x0E: oplen=2; return "LD C,#"+B(b1);
    case 0x0F: return "RRCA";

    case 0x10: oplen=2; return "DJNZ $" + W(addr+2+(int8_t)b1);
    case 0x11: oplen=3; return "LD DE,$"+W((b2<<8)|b1);
    case 0x12: return "LD (DE),A";
    case 0x13: return "INC DE";
    case 0x14: return "INC D";
    case 0x15: return "DEC D";
    case 0x16: oplen=2; return "LD D,#"+B(b1);
    case 0x17: return "RLA";
    case 0x18: oplen=2; return "JR $" + W(addr+2+(int8_t)b1);
    case 0x19: return "ADD HL,DE";
    case 0x1A: return "LD A,(DE)";
    case 0x1B: return "DEC DE";
    case 0x1C: return "INC E";
    case 0x1D: return "DEC E";
    case 0x1E: oplen=2; return "LD E,#"+B(b1);
    case 0x1F: return "RRA";

    case 0x20: oplen=2; return "JR NZ,$"+W(addr+2+(int8_t)b1);
    case 0x21: oplen=3; return "LD HL,$"+W((b2<<8)|b1);
    case 0x22: oplen=3; return "LD ($"+W((b2<<8)|b1)+"),HL";
    case 0x23: return "INC HL";
    case 0x24: return "INC H";
    case 0x25: return "DEC H";
    case 0x26: oplen=2; return "LD H,#"+B(b1);
    case 0x27: return "DAA";
    case 0x28: oplen=2; return "JR Z,$"+W(addr+2+(int8_t)b1);
    case 0x29: return "ADD HL,HL";
    case 0x2A: oplen=3; return "LD HL,($"+W((b2<<8)|b1)+")";
    case 0x2B: return "DEC HL";
    case 0x2C: return "INC L";
    case 0x2D: return "DEC L";
    case 0x2E: oplen=2; return "LD L,#"+B(b1);
    case 0x2F: return "CPL";

    case 0x30: oplen=2; return "JR NC,$"+W(addr+2+(int8_t)b1);
    case 0x31: oplen=3; return "LD SP,$"+W((b2<<8)|b1);
    case 0x32: oplen=3; return "LD ($"+W((b2<<8)|b1)+"),A";
    case 0x33: return "INC SP";
    case 0x34: return "INC (HL)";
    case 0x35: return "DEC (HL)";
    case 0x36: oplen=2; return "LD (HL),#"+B(b1);
    case 0x37: return "SCF";
    case 0x38: oplen=2; return "JR C,$"+W(addr+2+(int8_t)b1);
    case 0x39: return "ADD HL,SP";
    case 0x3A: oplen=3; return "LD A,($"+W((b2<<8)|b1)+")";
    case 0x3B: return "DEC SP";
    case 0x3C: return "INC A";
    case 0x3D: return "DEC A";
    case 0x3E: oplen=2; return "LD A,#"+B(b1);
    case 0x3F: return "CCF";

    case 0x40: return "LD B,B";
    case 0x41: return "LD B,C";
    case 0x42: return "LD B,D";
    case 0x43: return "LD B,E";
    case 0x44: return "LD B,H";
    case 0x45: return "LD B,L";
    case 0x46: return "LD B,(HL)";
    case 0x47: return "LD B,A";
    case 0x48: return "LD C,B";
    case 0x49: return "LD C,C";
    case 0x4A: return "LD C,D";
    case 0x4B: return "LD C,E";
    case 0x4C: return "LD C,H";
    case 0x4D: return "LD C,L";
    case 0x4E: return "LD C,(HL)";
    case 0x4F: return "LD C,A";
    case 0x50: return "LD D,B";
    case 0x51: return "LD D,C";
    case 0x52: return "LD D,D";
    case 0x53: return "LD D,E";
    case 0x54: return "LD D,H";
    case 0x55: return "LD D,L";
    case 0x56: return "LD D,(HL)";
    case 0x57: return "LD D,A";
    case 0x58: return "LD E,B";
    case 0x59: return "LD E,C";
    case 0x5A: return "LD E,D";
    case 0x5B: return "LD E,E";
    case 0x5C: return "LD E,H";
    case 0x5D: return "LD E,L";
    case 0x5E: return "LD E,(HL)";
    case 0x5F: return "LD E,A";
    case 0x60: return "LD H,B";
    case 0x61: return "LD H,C";
    case 0x62: return "LD H,D";
    case 0x63: return "LD H,E";
    case 0x64: return "LD H,H";
    case 0x65: return "LD H,L";
    case 0x66: return "LD H,(HL)";
    case 0x67: return "LD H,A";
    case 0x68: return "LD L,B";
    case 0x69: return "LD L,C";
    case 0x6A: return "LD L,D";
    case 0x6B: return "LD L,E";
    case 0x6C: return "LD L,H";
    case 0x6D: return "LD L,L";
    case 0x6E: return "LD L,(HL)";
    case 0x6F: return "LD L,A";
    case 0x70: return "LD (HL),B";
    case 0x71: return "LD (HL),C";
    case 0x72: return "LD (HL),D";
    case 0x73: return "LD (HL),E";
    case 0x74: return "LD (HL),H";
    case 0x75: return "LD (HL),L";
    case 0x76: return "HALT";
    case 0x77: return "LD (HL),A";
    case 0x78: return "LD A,B";
    case 0x79: return "LD A,C";
    case 0x7A: return "LD A,D";
    case 0x7B: return "LD A,E";
    case 0x7C: return "LD A,H";
    case 0x7D: return "LD A,L";
    case 0x7E: return "LD A,(HL)";
    case 0x7F: return "LD A,A";

    // arithmetic
    case 0x80: return "ADD A,B";
    case 0x81: return "ADD A,C";
    case 0x82: return "ADD A,D";
    case 0x83: return "ADD A,E";
    case 0x84: return "ADD A,H";
    case 0x85: return "ADD A,L";
    case 0x86: return "ADD A,(HL)";
    case 0x87: return "ADD A,A";
    case 0x88: return "ADC A,B";
    case 0x89: return "ADC A,C";
    case 0x8A: return "ADC A,D";
    case 0x8B: return "ADC A,E";
    case 0x8C: return "ADC A,H";
    case 0x8D: return "ADC A,L";
    case 0x8E: return "ADC A,(HL)";
    case 0x8F: return "ADC A,A";
    case 0x90: return "SUB B";
    case 0x91: return "SUB C";
    case 0x92: return "SUB D";
    case 0x93: return "SUB E";
    case 0x94: return "SUB H";
    case 0x95: return "SUB L";
    case 0x96: return "SUB (HL)";
    case 0x97: return "SUB A";
    case 0x98: return "SBC A,B";
    case 0x99: return "SBC A,C";
    case 0x9A: return "SBC A,D";
    case 0x9B: return "SBC A,E";
    case 0x9C: return "SBC A,H";
    case 0x9D: return "SBC A,L";
    case 0x9E: return "SBC A,(HL)";
    case 0x9F: return "SBC A,A";
    case 0xA0: return "AND B";
    case 0xA1: return "AND C";
    case 0xA2: return "AND D";
    case 0xA3: return "AND E";
    case 0xA4: return "AND H";
    case 0xA5: return "AND L";
    case 0xA6: return "AND (HL)";
    case 0xA7: return "AND A";
    case 0xA8: return "XOR B";
    case 0xA9: return "XOR C";
    case 0xAA: return "XOR D";
    case 0xAB: return "XOR E";
    case 0xAC: return "XOR H";
    case 0xAD: return "XOR L";
    case 0xAE: return "XOR (HL)";
    case 0xAF: return "XOR A";
    // ---- vervolg van de hoofdopcodes (deel 1 eindigde bij 0xAF) ----
    case 0xB0: return "OR B";
    case 0xB1: return "OR C";
    case 0xB2: return "OR D";
    case 0xB3: return "OR E";
    case 0xB4: return "OR H";
    case 0xB5: return "OR L";
    case 0xB6: return "OR (HL)";
    case 0xB7: return "OR A";
    case 0xB8: return "CP B";
    case 0xB9: return "CP C";
    case 0xBA: return "CP D";
    case 0xBB: return "CP E";
    case 0xBC: return "CP H";
    case 0xBD: return "CP L";
    case 0xBE: return "CP (HL)";
    case 0xBF: return "CP A";

    case 0xC0: return "RET NZ";
    case 0xC1: return "POP BC";
    case 0xC2: oplen=3; return "JP NZ,$"+W((b2<<8)|b1);
    case 0xC3: oplen=3; return "JP $"+W((b2<<8)|b1);
    case 0xC4: oplen=3; return "CALL NZ,$"+W((b2<<8)|b1);
    case 0xC5: return "PUSH BC";
    case 0xC6: oplen=2; return "ADD A,#"+B(b1);
    case 0xC7: return "RST 00H";
    case 0xC8: return "RET Z";
    case 0xC9: return "RET";
    case 0xCA: oplen=3; return "JP Z,$"+W((b2<<8)|b1);
    case 0xCB: return disasm_cb(addr,oplen);
    case 0xCC: oplen=3; return "CALL Z,$"+W((b2<<8)|b1);
    case 0xCD: oplen=3; return "CALL $"+W((b2<<8)|b1);
    case 0xCE: oplen=2; return "ADC A,#"+B(b1);
    case 0xCF: return "RST 08H";

    case 0xD0: return "RET NC";
    case 0xD1: return "POP DE";
    case 0xD2: oplen=3; return "JP NC,$"+W((b2<<8)|b1);
    case 0xD3: oplen=2; return "OUT ($"+B(b1)+"),A";
    case 0xD4: oplen=3; return "CALL NC,$"+W((b2<<8)|b1);
    case 0xD5: return "PUSH DE";
    case 0xD6: oplen=2; return "SUB #"+B(b1);
    case 0xD7: return "RST 10H";
    case 0xD8: return "RET C";
    case 0xD9: return "EXX";
    case 0xDA: oplen=3; return "JP C,$"+W((b2<<8)|b1);
    case 0xDB: oplen=2; return "IN A,($"+B(b1)+")";
    case 0xDC: oplen=3; return "CALL C,$"+W((b2<<8)|b1);
    case 0xDD: return disasm_ddfd(addr,oplen,false);
    case 0xDE: oplen=2; return "SBC A,#"+B(b1);
    case 0xDF: return "RST 18H";

    case 0xE0: return "RET PO";
    case 0xE1: return "POP HL";
    case 0xE2: oplen=3; return "JP PO,$"+W((b2<<8)|b1);
    case 0xE3: return "EX (SP),HL";
    case 0xE4: oplen=3; return "CALL PO,$"+W((b2<<8)|b1);
    case 0xE5: return "PUSH HL";
    case 0xE6: oplen=2; return "AND #"+B(b1);
    case 0xE7: return "RST 20H";
    case 0xE8: return "RET PE";
    case 0xE9: return "JP (HL)";
    case 0xEA: oplen=3; return "JP PE,$"+W((b2<<8)|b1);
    case 0xEB: return "EX DE,HL";
    case 0xEC: oplen=3; return "CALL PE,$"+W((b2<<8)|b1);
    case 0xED: return disasm_ed(addr,oplen);
    case 0xEE: oplen=2; return "XOR #"+B(b1);
    case 0xEF: return "RST 28H";

    case 0xF0: return "RET P";
    case 0xF1: return "POP AF";
    case 0xF2: oplen=3; return "JP P,$"+W((b2<<8)|b1);
    case 0xF3: return "DI";
    case 0xF4: oplen=3; return "CALL P,$"+W((b2<<8)|b1);
    case 0xF5: return "PUSH AF";
    case 0xF6: oplen=2; return "OR #"+B(b1);
    case 0xF7: return "RST 30H";
    case 0xF8: return "RET M";
    case 0xF9: return "LD SP,HL";
    case 0xFA: oplen=3; return "JP M,$"+W((b2<<8)|b1);
    case 0xFB: return "EI";
    case 0xFC: oplen=3; return "CALL M,$"+W((b2<<8)|b1);
    case 0xFD: return disasm_ddfd(addr,oplen,true);
    case 0xFE: oplen=2; return "CP #"+B(b1);
    case 0xFF: return "RST 38H";

    default:
        return "DB $" + B(op);
    }
}

/* ============================================================
   CB-prefix: rotaties, shifts, bit-tests
   ============================================================ */
static QString disasm_cb(unsigned short addr,int &oplen)
{
    const uint8_t op = coleco_ReadByte(addr+1);
    oplen = 2;

    static const char* r8[8] = {"B","C","D","E","H","L","(HL)","A"};
    const QString r = r8[op & 7];
    const int top = op >> 3;

    if(op < 0x40)
    {
        static const char* rot[8] =
            {"RLC","RRC","RL","RR","SLA","SRA","SLL","SRL"};
        return QString("%1 %2").arg(rot[top]).arg(r);
    }
    else if(op < 0x80)
    {
        return QString("BIT %1,%2").arg((top)&7).arg(r);
    }
    else if(op < 0xC0)
    {
        return QString("RES %1,%2").arg((top)&7).arg(r);
    }
    else
    {
        return QString("SET %1,%2").arg((top)&7).arg(r);
    }
}

/* ============================================================
   ED-prefix: I/O, block moves, interrupt modes, RRD/RLD
   ============================================================ */
static QString disasm_ed(unsigned short addr,int &oplen)
{
    const uint8_t op = coleco_ReadByte(addr+1);
    const uint8_t b2 = coleco_ReadByte(addr+2);
    oplen = 2;

    switch(op)
    {
    // --- I/O groep ---
    case 0x40: return "IN B,(C)";
    case 0x41: return "OUT (C),B";
    case 0x42: return "SBC HL,BC";
    case 0x43: oplen=4; return "LD ($"+W((b2<<8)|coleco_ReadByte(addr+2-1))+"),BC";
    case 0x44: return "NEG";
    case 0x45: return "RETN";
    case 0x46: return "IM 0";
    case 0x47: return "LD I,A";

    case 0x48: return "IN C,(C)";
    case 0x49: return "OUT (C),C";
    case 0x4A: return "ADC HL,BC";
    case 0x4B: oplen=4; return "LD BC,($"+W((coleco_ReadByte(addr+3)<<8)|coleco_ReadByte(addr+2))+")";
    case 0x4C: return "NEG";
    case 0x4D: return "RETI";
    case 0x4E: return "IM 0";
    case 0x4F: return "LD R,A";

    case 0x50: return "IN D,(C)";
    case 0x51: return "OUT (C),D";
    case 0x52: return "SBC HL,DE";
    case 0x53: oplen=4; return "LD ($"+W((coleco_ReadByte(addr+3)<<8)|coleco_ReadByte(addr+2))+"),DE";
    case 0x56: return "IM 1";
    case 0x57: return "LD A,I";

    case 0x58: return "IN E,(C)";
    case 0x59: return "OUT (C),E";
    case 0x5A: return "ADC HL,DE";
    case 0x5B: oplen=4; return "LD DE,($"+W((coleco_ReadByte(addr+3)<<8)|coleco_ReadByte(addr+2))+")";
    case 0x5E: return "IM 2";
    case 0x5F: return "LD A,R";

    case 0x60: return "IN H,(C)";
    case 0x61: return "OUT (C),H";
    case 0x62: return "SBC HL,HL";
    case 0x63: oplen=4; return "LD ($"+W((coleco_ReadByte(addr+3)<<8)|coleco_ReadByte(addr+2))+"),HL";
    case 0x67: return "RRD";

    case 0x68: return "IN L,(C)";
    case 0x69: return "OUT (C),L";
    case 0x6A: return "ADC HL,HL";
    case 0x6B: oplen=4; return "LD HL,($"+W((coleco_ReadByte(addr+3)<<8)|coleco_ReadByte(addr+2))+")";
    case 0x6F: return "RLD";

    case 0x70: return "IN (C)";
    case 0x71: return "OUT (C)";
    case 0x72: return "SBC HL,SP";
    case 0x73: oplen=4; return "LD ($"+W((coleco_ReadByte(addr+3)<<8)|coleco_ReadByte(addr+2))+"),SP";
    case 0x78: return "IN A,(C)";
    case 0x79: return "OUT (C),A";
    case 0x7A: return "ADC HL,SP";
    case 0x7B: oplen=4; return "LD SP,($"+W((coleco_ReadByte(addr+3)<<8)|coleco_ReadByte(addr+2))+")";

    // --- block move & compare ---
    case 0xA0: return "LDI";
    case 0xA1: return "CPI";
    case 0xA2: return "INI";
    case 0xA3: return "OUTI";
    case 0xA8: return "LDD";
    case 0xA9: return "CPD";
    case 0xAA: return "IND";
    case 0xAB: return "OUTD";
    case 0xB0: return "LDIR";
    case 0xB1: return "CPIR";
    case 0xB2: return "INIR";
    case 0xB3: return "OTIR";
    case 0xB8: return "LDDR";
    case 0xB9: return "CPDR";
    case 0xBA: return "INDR";
    case 0xBB: return "OTDR";

    // --- ongedefinieerde (rest) ---
    default:
        return QString("DB $ED,$%1").arg(B(op));
    }
}

/* ============================================================
   DD / FD prefix: IX / IY variants
   ============================================================ */
static QString disasm_ddfd(unsigned short addr,int &oplen,bool iy)
{
    const uint8_t op1 = coleco_ReadByte(addr+1);
    const uint8_t op2 = coleco_ReadByte(addr+2);
    const int8_t  d   = static_cast<int8_t>(op2);
    oplen = 2;

    const QString IX = iy ? "IY" : "IX";
    auto disp = [&](int8_t v){
        if(v>=0) return QString("+%1").arg(v,0,16).toUpper();
        return QString("-%1").arg(-v,0,16).toUpper();
    };

    // Handhaaf CB-prefix (bit-/rot-opcodes met displacement)
    if(op1 == 0xCB)
    {
        uint8_t op3 = coleco_ReadByte(addr+3);
        oplen = 4;
        static const char* r8[8] = {"B","C","D","E","H","L","(HL)","A"};
        QString reg = r8[op3 & 7];
        int top = op3 >> 3;
        if(op3 < 0x40)
        {
            static const char* rot[8] = {"RLC","RRC","RL","RR","SLA","SRA","SLL","SRL"};
            return QString("%1 (%2%3)").arg(rot[top]).arg(IX).arg(disp(d));
        }
        else if(op3 < 0x80)
            return QString("BIT %1,(%2%3)").arg(top&7).arg(IX).arg(disp(d));
        else if(op3 < 0xC0)
            return QString("RES %1,(%2%3)").arg(top&7).arg(IX).arg(disp(d));
        else
            return QString("SET %1,(%2%3)").arg(top&7).arg(IX).arg(disp(d));
    }

    switch(op1)
    {
    // 16-bit loads / arithmetic
    case 0x21: oplen=4; return QString("LD %1,$%2").arg(IX).arg(W((coleco_ReadByte(addr+3)<<8)|op2));
    case 0x22: oplen=4; return QString("LD ($%1),%2").arg(W((coleco_ReadByte(addr+3)<<8)|op2)).arg(IX);
    case 0x2A: oplen=4; return QString("LD %1,($%2)").arg(IX).arg(W((coleco_ReadByte(addr+3)<<8)|op2));
    case 0x23: return QString("INC %1").arg(IX);
    case 0x24: return QString("INC %1H").arg(IX);
    case 0x25: return QString("DEC %1H").arg(IX);
    case 0x26: oplen=3; return QString("LD %1H,#%2").arg(IX).arg(B(op2));
    case 0x29: return QString("ADD %1,%1").arg(IX);
    case 0x2B: return QString("DEC %1").arg(IX);
    case 0x2C: return QString("INC %1L").arg(IX);
    case 0x2D: return QString("DEC %1L").arg(IX);
    case 0x2E: oplen=3; return QString("LD %1L,#%2").arg(IX).arg(B(op2));
    case 0x34: oplen=3; return QString("INC (%1%2)").arg(IX).arg(disp(d));
    case 0x35: oplen=3; return QString("DEC (%1%2)").arg(IX).arg(disp(d));
    case 0x36: oplen=4; return QString("LD (%1%2),#%3").arg(IX).arg(disp(static_cast<int8_t>(op2))).arg(B(coleco_ReadByte(addr+3)));
    case 0x39: return QString("ADD %1,SP").arg(IX);

    // 8-bit loads with displacement
    case 0x46: oplen=3; return QString("LD B,(%1%2)").arg(IX).arg(disp(d));
    case 0x4E: oplen=3; return QString("LD C,(%1%2)").arg(IX).arg(disp(d));
    case 0x56: oplen=3; return QString("LD D,(%1%2)").arg(IX).arg(disp(d));
    case 0x5E: oplen=3; return QString("LD E,(%1%2)").arg(IX).arg(disp(d));
    case 0x66: oplen=3; return QString("LD H,(%1%2)").arg(IX).arg(disp(d));
    case 0x6E: oplen=3; return QString("LD L,(%1%2)").arg(IX).arg(disp(d));
    case 0x70: oplen=3; return QString("LD (%1%2),B").arg(IX).arg(disp(d));
    case 0x71: oplen=3; return QString("LD (%1%2),C").arg(IX).arg(disp(d));
    case 0x72: oplen=3; return QString("LD (%1%2),D").arg(IX).arg(disp(d));
    case 0x73: oplen=3; return QString("LD (%1%2),E").arg(IX).arg(disp(d));
    case 0x74: oplen=3; return QString("LD (%1%2),H").arg(IX).arg(disp(d));
    case 0x75: oplen=3; return QString("LD (%1%2),L").arg(IX).arg(disp(d));
    case 0x77: oplen=3; return QString("LD (%1%2),A").arg(IX).arg(disp(d));
    case 0x7E: oplen=3; return QString("LD A,(%1%2)").arg(IX).arg(disp(d));

    // arithmetic / logic with displacement
    case 0x86: oplen=3; return QString("ADD A,(%1%2)").arg(IX).arg(disp(d));
    case 0x8E: oplen=3; return QString("ADC A,(%1%2)").arg(IX).arg(disp(d));
    case 0x96: oplen=3; return QString("SUB (%1%2)").arg(IX).arg(disp(d));
    case 0x9E: oplen=3; return QString("SBC A,(%1%2)").arg(IX).arg(disp(d));
    case 0xA6: oplen=3; return QString("AND (%1%2)").arg(IX).arg(disp(d));
    case 0xAE: oplen=3; return QString("XOR (%1%2)").arg(IX).arg(disp(d));
    case 0xB6: oplen=3; return QString("OR (%1%2)").arg(IX).arg(disp(d));
    case 0xBE: oplen=3; return QString("CP (%1%2)").arg(IX).arg(disp(d));

    // Stack and jumps
    case 0xE1: return QString("POP %1").arg(IX);
    case 0xE3: return QString("EX (SP),%1").arg(IX);
    case 0xE5: return QString("PUSH %1").arg(IX);
    case 0xE9: return QString("JP (%1)").arg(IX);
    case 0xF9: return QString("LD SP,%1").arg(IX);

    default:
        return QString("DB $%1,$%2").arg(iy?"FD":"DD").arg(B(op1));
    }
}

void debug_sync_breakpoints(ColecoController* controller, const QStringList &list)
{
    if (!controller) return;

    // 1. Parse de QStringList naar een C-compatibele lijst (QVector<int>)
    //    Dit gebeurt op de GUI-thread.
    QVector<int> bp_list;
    for (const QString &s : list) {
        // Parse "EXE BBA3"
        if (s.startsWith("EXE ")) {
            bool ok;
            int addr = s.mid(4).toInt(&ok, 16);
            if (ok) {
                bp_list.append(addr);
            }
        }
        // TODO: Parse hier de andere types (REG, MEM, WR...)
    }

    // 2. Stuur deze C-lijst naar de emulator-thread via een QueuedConnection.
    //    De lambda-functie wordt uitgevoerd op de emulator-thread.
    QMetaObject::invokeMethod((QObject*)controller, [bp_list]() {

        // --- DEZE CODE DRAAIT VEILIG OP DE EMU-THREAD ---
        // (De Z80-loop is gepauzeerd terwijl dit blokje draait)

        qDebug() << "[EmuThread] Synchroniseer C-breakpoint array...";
        breakpoint_count = 0;
        for (int addr : bp_list) {
            if (breakpoint_count < MAX_BREAKPOINTS) {
                breakpoints[breakpoint_count] = addr;
                breakpoint_count++;
            } else {
                break; // Array is vol
            }
        }
        qDebug() << "[EmuThread] Synchronisatie voltooid." << breakpoint_count << "breakpoints ingesteld.";

    }, Qt::QueuedConnection);
}

