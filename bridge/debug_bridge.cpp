#include "debug_bridge.h"
#include "coleco.h"   // coleco_getbyte
#include "z80.h"      // Z80_Regs Z80, z80_cycle_count
#include <QDebug>
#include <QRegularExpression>
#include <algorithm>

namespace
{

// String operator ("=", "<>", "<=", "=>", "<", ">") omzetten naar int-code
int conditionFromString(const QString& condStr)
{
    QString c = condStr.trimmed().toUpper();

    if (c == "="  || c == "==") return 0;   // =
    if (c == "<>")               return 2;  // <>
    if (c == "<=")               return -1; // <=
    if (c == "=>")               return 1;  // >=
    if (c == "<")                return 3;  // <
    if (c == ">")                return 4;  // >
    return 0;
}

// Map "PC", "AF", ... naar dezelfde ID's als in getRegisterValue()
uint8_t regIdFromName(const QString& name)
{
    QString n = name.trimmed().toUpper();

    if (n == "PC")  return 0;
    if (n == "AF")  return 1;
    if (n == "BC")  return 2;
    if (n == "DE")  return 3;
    if (n == "HL")  return 4;
    if (n == "IX")  return 5;
    if (n == "IY")  return 6;
    if (n == "SP")  return 7;
    if (n == "AF'") return 8;
    if (n == "BC'") return 9;
    if (n == "DE'") return 10;
    if (n == "HL'") return 11;
    if (n == "A")   return 12;
    return 0xFF;
}

// EXECUTE breakpoints parsen: "EXE 196F", "196F", "EXE 196F FLAG Z=1"
void parseExecuteBreakpoint(CoreBreakpoint& bp)
{
    if (bp.type != BreakpointType::BP_EXECUTE)
        return;
    if (bp.definition_text.isEmpty())
        return;

    QString s = bp.definition_text.trimmed().toUpper();
    s.replace(QRegularExpression("\\s+"), " ");
    const QStringList parts = s.split(' ');
    if (parts.isEmpty())
        return;

    bool ok = false;
    quint16 addr = 0;

    // Vorm 1: "EXE 196F" of "EXE 196F FLAG Z=1"
    if (parts[0] == "EXE") {
        if (parts.size() >= 2) {
            addr = parts[1].toUShort(&ok, 16);
            if (!ok)
                return;
        } else {
            return;
        }

        // Optioneel: EXE <addr> FLAG Z=0/1
        if (parts.size() == 4 && parts[2] == "FLAG") {
            QString flagExpr = parts[3];    // "Z=1"
            int eqPos = flagExpr.indexOf('=');
            if (eqPos <= 0 || eqPos == flagExpr.size() - 1) {
                qWarning() << "[DebugBridge] Invalid EXE+FLAG expression:" << bp.definition_text;
                return;
            }

            QString flagName = flagExpr.left(eqPos);    // "Z"
            QString flagVal  = flagExpr.mid(eqPos + 1); // "1"

            // Z80 flag bits: S Z 0 H 0 P/V N C
            uint8_t mask = 0x00;
            if (flagName == "Z")                mask = 0x40;
            else if (flagName == "C")           mask = 0x01;
            else if (flagName == "S")           mask = 0x80;
            else if (flagName == "H")           mask = 0x10;
            else if (flagName == "P/V" || flagName == "PV") mask = 0x04;
            else if (flagName == "N")           mask = 0x02;
            else {
                qWarning() << "[DebugBridge] Unknown flag name in EXE+FLAG:" << flagName;
                return;
            }

            uint8_t val = 0x00;
            if      (flagVal == "0") val = 0x00;
            else if (flagVal == "1") val = mask;
            else {
                qWarning() << "[DebugBridge] Invalid flag value in EXE+FLAG:" << flagVal;
                return;
            }

            bp.flag_mask  = mask;
            bp.flag_value = val;

            qDebug() << "[DebugBridge] Parsed EXE+FLAG:"
                     << bp.definition_text
                     << "addr=" << QString::number(addr, 16).toUpper()
                     << "mask=0x" << QString::number(mask, 16).toUpper()
                     << "val=0x"  << QString::number(val, 16).toUpper();
        }
    }
    // Vorm 2: korte vorm "196F" → behandelen als EXE 196F
    else {
        addr = parts[0].toUShort(&ok, 16);
        if (!ok)
            return;
    }

    bp.address_start = addr;
    bp.address_end   = addr;
}

// FLAG-only breakpoints: "FLAG Z = 1"
void parseFlagBreakpoint(CoreBreakpoint& bp)
{
    if (bp.type != BreakpointType::BP_FLAG_VAL)
        return;
    if (bp.definition_text.isEmpty())
        return;

    QString s = bp.definition_text.trimmed().toUpper();
    s.replace(QRegularExpression("\\s+"), " ");
    const QStringList parts = s.split(' ');
    if (parts.size() < 4)
        return;
    if (parts[0] != "FLAG")
        return;

    const QString flagName = parts.at(1);
    const QString eq       = parts.at(2);
    const QString flagVal  = parts.at(3);

    if (eq != "=")
        return;

    uint8_t mask = 0x00;
    if (flagName == "Z")                mask = 0x40;
    else if (flagName == "C")           mask = 0x01;
    else if (flagName == "S")           mask = 0x80;
    else if (flagName == "H")           mask = 0x10;
    else if (flagName == "P/V" || flagName == "PV") mask = 0x04;
    else if (flagName == "N")           mask = 0x02;
    else {
        qWarning() << "[DebugBridge] Unknown flag name in FLAG bp:" << flagName;
        return;
    }

    uint8_t val = 0x00;
    if      (flagVal == "0") val = 0x00;
    else if (flagVal == "1") val = mask;
    else {
        qWarning() << "[DebugBridge] Invalid flag value in FLAG bp:" << flagVal;
        return;
    }

    bp.flag_mask  = mask;
    bp.flag_value = val;
}

// REG-breakpoints: "REG PC = 1700"
void parseRegBreakpoint(CoreBreakpoint& bp)
{
    if (bp.type != BreakpointType::BP_REG_VAL)
        return;
    if (bp.definition_text.isEmpty())
        return;

    QString s = bp.definition_text.trimmed().toUpper();
    if (!s.startsWith("REG "))
        return;

    s.replace(QRegularExpression("\\s+"), " ");
    const QStringList parts = s.split(' ');
    if (parts.size() < 4)
        return;

    const QString regName   = parts.at(1);
    const QString condStr   = parts.at(2);
    const QString valueStr  = parts.at(3);

    bool ok = false;
    uint16_t value = valueStr.toUShort(&ok, 16);
    if (!ok)
        return;

    bp.check_reg_value = true;
    bp.reg_id          = regIdFromName(regName);
    bp.reg_value       = value;
    bp.reg_mask        = 0xFFFF;
    bp.reg_condition   = conditionFromString(condStr);
}

} // namespace

// Deze komen uit z80.c / coleco.cpp
extern Z80_Regs Z80;
extern int z80_cycle_count;

// Singleton
DebugBridge& DebugBridge::instance()
{
    static DebugBridge instance;
    return instance;
}

// Registerwaarde ophalen
uint16_t DebugBridge::getRegisterValue(uint8_t reg_id)
{
    switch (reg_id) {
    case 0:  return Z80.pc.w.l;
    case 1:  return Z80.af.w.l;
    case 2:  return Z80.bc.w.l;
    case 3:  return Z80.de.w.l;
    case 4:  return Z80.hl.w.l;
    case 5:  return Z80.ix.w.l;
    case 6:  return Z80.iy.w.l;
    case 7:  return Z80.sp.w.l;
    case 8:  return Z80.af2.w.l;
    case 9:  return Z80.bc2.w.l;
    case 10: return Z80.de2.w.l;
    case 11: return Z80.hl2.w.l;
    case 12: return Z80.af.b.h; // A
    default: return 0x0000;
    }
}

// Conditie evalueren
bool DebugBridge::checkCondition(int condition, uint16_t actual, uint16_t expected) const
{
    switch (condition) {
    case 0:  return (actual == expected);   // =
    case 2:  return (actual != expected);   // <>
    case -1: return (actual <= expected);   // <=
    case 1:  return (actual >= expected);   // >=
    case 3:  return (actual <  expected);   // <
    case 4:  return (actual >  expected);   // >
    default: return false;
    }
}

// --- Breakpoint Synchronisatie ---

void DebugBridge::syncBreakpoints(const QList<CoreBreakpoint>& breakpoints)
{
    m_breakpoints = breakpoints;

    m_executeBreakpoints.clear();
    m_memAccessBreakpoints.clear();
    m_ioAccessBreakpoints.clear();
    m_postExecutionBreakpoints.clear();

    int activeCount = 0;

    for (CoreBreakpoint &bp : m_breakpoints) {
        // Parse op basis van tekst
        parseExecuteBreakpoint(bp);
        parseRegBreakpoint(bp);
        parseFlagBreakpoint(bp);

        qDebug() << "[DebugBridge] BP after parse:"
                 << "enabled=" << bp.enabled
                 << "type=" << int(bp.type)
                 << "addrStart=" << QString::number(bp.address_start, 16).toUpper()
                 << "addrEnd="   << QString::number(bp.address_end, 16).toUpper()
                 << "text=" << bp.definition_text;

        if (!bp.enabled)
            continue;

        activeCount++;

        switch (bp.type) {
        case BreakpointType::BP_EXECUTE:
            m_executeBreakpoints.append(bp);
            break;
        case BreakpointType::BP_READ:
        case BreakpointType::BP_WRITE:
            m_memAccessBreakpoints.append(bp);
            break;
        case BreakpointType::BP_IO_IN:
        case BreakpointType::BP_IO_OUT:
            m_ioAccessBreakpoints.append(bp);
            break;
        case BreakpointType::BP_CLOCK:
        case BreakpointType::BP_FLAG_VAL:
        case BreakpointType::BP_REG_VAL:
        case BreakpointType::BP_MEM_VAL:
            m_postExecutionBreakpoints.append(bp);
            break;
        default:
            break;
        }
    }

    qDebug() << "DebugBridge: Breakpoints gesynchroniseerd. Actief:" << activeCount;
}

// 1. EXECUTE (roepen vóór z80_do_opcode)
bool DebugBridge::checkExecute(uint16_t addr)
{
    if (m_executeBreakpoints.isEmpty())
        return false;

    qDebug() << "[DebugBridge] checkExecute(pc="
             << QString::number(addr, 16).toUpper()
             << ") EXE-BPs:" << m_executeBreakpoints.size();

    uint8_t flags = Z80.af.b.l; // F-register

    for (const auto& bp : m_executeBreakpoints) {
        if (!bp.enabled)
            continue;

        if (addr >= bp.address_start && addr <= bp.address_end) {
            // Optionele EXE+FLAG conditie
            if (bp.flag_mask != 0x00) {
                uint8_t masked_flags    = flags & bp.flag_mask;
                uint8_t masked_bp_value = bp.flag_value & bp.flag_mask;

                if (masked_flags != masked_bp_value)
                    continue;
            }

            qDebug() << "BP_EXECUTE HIT at"
                     << QString::number(addr, 16).toUpper()
                     << "def:" << bp.definition_text;
            return true;
        }
    }
    return false;
}

// 2. MEMORY ACCESS (cpu_readbyte / cpu_writebyte)
bool DebugBridge::checkMemAccess(BreakpointType type, uint16_t addr, uint8_t value)
{
    if (m_memAccessBreakpoints.isEmpty())
        return false;

    for (const auto& bp : m_memAccessBreakpoints) {
        if (!bp.enabled)
            continue;
        if (bp.type != type)
            continue;

        if (addr >= bp.address_start && addr <= bp.address_end) {

            if (!bp.check_value) {
                qDebug() << (type == BreakpointType::BP_READ ? "BP_READ" : "BP_WRITE")
                << "HIT at addr"
                << QString::number(addr, 16).toUpper()
                << "def:" << bp.definition_text;
                return true;
            }

            uint8_t masked_value    = value & bp.value_mask;
            uint8_t masked_bp_value = bp.value & bp.value_mask;

            bool condition_met = checkCondition(bp.value_condition,
                                                masked_value,
                                                masked_bp_value);
            if (condition_met) {
                qDebug() << (type == BreakpointType::BP_READ ? "BP_READ (with value)" : "BP_WRITE (with value)")
                << "HIT at addr"
                << QString::number(addr, 16).toUpper()
                << "val:" << QString::number(value, 16).toUpper()
                << "def:" << bp.definition_text;
                return true;
            }
        }
    }
    return false;
}

// 3. I/O ACCESS (cpu_readport / cpu_writeport)
bool DebugBridge::checkIOAccess(BreakpointType type, uint16_t port, uint8_t value)
{
    if (m_ioAccessBreakpoints.isEmpty())
        return false;

    for (const auto& bp : m_ioAccessBreakpoints) {
        if (!bp.enabled)
            continue;
        if (bp.type != type)
            continue;

        if (port >= bp.address_start && port <= bp.address_end) {

            if (!bp.check_value) {
                qDebug() << (type == BreakpointType::BP_IO_IN ? "BP_IO_IN" : "BP_IO_OUT")
                << "HIT at port"
                << QString::number(port, 16).toUpper()
                << "def:" << bp.definition_text;
                return true;
            }

            uint8_t masked_value    = value & bp.value_mask;
            uint8_t masked_bp_value = bp.value & bp.value_mask;

            bool condition_met = checkCondition(bp.value_condition,
                                                masked_value,
                                                masked_bp_value);
            if (condition_met) {
                qDebug() << (type == BreakpointType::BP_IO_IN ? "BP_IO_IN (with value)" : "BP_IO_OUT (with value)")
                << "HIT at port"
                << QString::number(port, 16).toUpper()
                << "val:" << QString::number(value, 16).toUpper()
                << "def:" << bp.definition_text;
                return true;
            }
        }
    }
    return false;
}

// 4. POST-EXECUTION (na z80_do_opcode)
bool DebugBridge::checkPostExecutionBreakpoints()
{
    if (m_postExecutionBreakpoints.isEmpty())
        return false;

    uint64_t current_tstates = z80_cycle_count;
    uint8_t flags            = Z80.af.b.l;

    for (const auto& bp : m_postExecutionBreakpoints) {
        if (!bp.enabled)
            continue;

        switch (bp.type) {
        case BreakpointType::BP_CLOCK:
            if (current_tstates >= bp.tstate_count) {
                qDebug() << "BP_CLOCK HIT at T-state"
                         << current_tstates
                         << "def:" << bp.definition_text;
                return true;
            }
            break;

        case BreakpointType::BP_FLAG_VAL:
            if (bp.flag_mask != 0x00) {
                uint8_t masked_flags    = flags & bp.flag_mask;
                uint8_t masked_bp_value = bp.flag_value & bp.flag_mask;

                if (masked_flags == masked_bp_value) {
                    qDebug() << "BP_FLAG_VAL HIT def:" << bp.definition_text;
                    return true;
                }
            }
            break;

        case BreakpointType::BP_REG_VAL: {
            if (bp.check_reg_value) {
                uint16_t reg_val        = getRegisterValue(bp.reg_id);
                uint16_t masked_reg_val = reg_val & bp.reg_mask;
                uint16_t masked_bp_val  = bp.reg_value & bp.reg_mask;

                bool condition_met = checkCondition(bp.reg_condition,
                                                    masked_reg_val,
                                                    masked_bp_val);
                if (condition_met) {
                    qDebug() << "BP_REG_VAL HIT def:" << bp.definition_text;
                    return true;
                }
            }
            break;
        }

        case BreakpointType::BP_MEM_VAL:
            if (bp.check_value) {
                uint8_t mem_val         = coleco_getbyte(bp.address_start);
                uint8_t masked_mem_val  = mem_val & bp.value_mask;
                uint8_t masked_bp_value = bp.value & bp.value_mask;

                bool condition_met = checkCondition(bp.value_condition,
                                                    masked_mem_val,
                                                    masked_bp_value);
                if (condition_met) {
                    qDebug() << "BP_MEM_VAL HIT at"
                             << QString::number(bp.address_start, 16).toUpper()
                             << "def:" << bp.definition_text;
                    return true;
                }
            }
            break;

        default:
            break;
        }
    }

    return false;
}
