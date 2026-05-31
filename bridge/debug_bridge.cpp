#include "debug_bridge.h"
#include "cv.h"   // coleco_getbyte
#include "z80.h"      // Z80_Regs Z80, z80_cycle_count
#include <QDebug>
#include <QRegularExpression>

ColecoController* g_controller_instance = nullptr;

//----------------------------------------------------------------------------
// Connected with z80.c breakpoints OUTs  & Ins
//----------------------------------------------------------------------------

extern "C" void debugBridge_checkIOAccess(int type, uint16_t port, uint8_t value, uint16_t pc_start)
{
    DebugBridge::instance().checkIOAccess(static_cast<BreakpointType>(type), port, value, pc_start);
}


//----------------------------------------------------------------------------

namespace
{

// String operator ("=", "<>", "<=", "=>", "<", ">") omzetten naar int-code
int conditionFromString(const QString& condStr)
{
    QString c = condStr.trimmed().toUpper();

    if (c == "="  || c == "==")  return  0;  // =
    if (c == "<>")               return  2;  // <>
    if (c == "<=")               return -1;  // <=
    if (c == "=>")               return  1;  // >=
    if (c == "<")                return  3;  // <
    if (c == ">")                return  4;  // >
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
    static const QRegularExpression reWhitespace(QStringLiteral("\\s+"));
    s.replace(reWhitespace, " ");
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
    static const QRegularExpression reWhitespace(QStringLiteral("\\s+"));
    s.replace(reWhitespace, " ");
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
    if      (flagName == "Z")           mask = 0x40;
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

// IO breakpoints: "OUTL 98", "OUTH F1", "OUT F198", "INL 98", ...
void parseIOBreakpoint(CoreBreakpoint& bp)
{
    // We accept IO-related types only
    if (bp.type != BreakpointType::BP_IO_IN && bp.type != BreakpointType::BP_IO_OUT)
        return;
    if (bp.definition_text.isEmpty())
        return;

    QString s = bp.definition_text.trimmed().toUpper();
    static const QRegularExpression reWhitespace(QStringLiteral("\\s+"));
    s.replace(reWhitespace, " ");
    const QStringList parts = s.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return;

    const QString op = parts.at(0);   // IN/OUT/INL/OUTL/INH/OUTH
    QString p1 = parts.at(1);
    if (p1.startsWith("0X")) p1 = p1.mid(2);

    bool ok = false;
    uint32_t v = p1.toUInt(&ok, 16);
    if (!ok || v > 0xFFFF)
        return;

    bp.port_mask  = 0x0000;
    bp.port_match = 0x0000;

    // Default exact port (or range)
    bp.address_start = static_cast<uint16_t>(v);
    bp.address_end   = static_cast<uint16_t>(v);

    if (op == "OUTL" || op == "INL") {
        bp.port_mask  = 0x00FF;
        bp.port_match = static_cast<uint16_t>(v & 0x00FF);
        return;
    }
    if (op == "OUTH" || op == "INH") {
        bp.port_mask  = 0xFF00;
        bp.port_match = static_cast<uint16_t>((v & 0x00FF) << 8);
        return;
    }

    // Range form: "<OP> <start> ... <end>"
    if (parts.size() >= 4 && parts.at(2) == "...") {
        QString p2 = parts.at(3);
        if (p2.startsWith("0X")) p2 = p2.mid(2);
        uint32_t v2 = p2.toUInt(&ok, 16);
        if (!ok || v2 > 0xFFFF)
            return;
        bp.address_start = static_cast<uint16_t>(v);
        bp.address_end   = static_cast<uint16_t>(v2);
    }
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

    static const QRegularExpression reWhitespace(QStringLiteral("\\s+"));
    s.replace(reWhitespace, " ");
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

// MEM-breakpoints: "MEM 6000 <> 00"
void parseMemBreakpoint(CoreBreakpoint& bp)
{
    if (bp.type != BreakpointType::BP_MEM_VAL)
        return;
    if (bp.definition_text.isEmpty())
        return;

    QString s = bp.definition_text.trimmed().toUpper();
    if (!s.startsWith("MEM "))
        return;

    static const QRegularExpression reWhitespace(QStringLiteral("\\s+"));
    s.replace(reWhitespace, " ");
    const QStringList parts = s.split(' ');

    // Verwachte vorm: MEM <addr> <cond> <value> (4 delen)
    if (parts.size() < 4)
        return;

    const QString addrStr   = parts.at(1);
    const QString condStr   = parts.at(2);
    const QString valueStr  = parts.at(3);

    bool ok = false;
    uint16_t addr = addrStr.toUShort(&ok, 16);
    if (!ok)
        return;

    // Gebruik toUShort om hex strings te parsen, daarna casten naar 8-bit
    uint8_t value = valueStr.toUShort(&ok, 16);
    if (!ok)
        return;

    // Range-breakpoints (zoals bij RD/WR) worden hier genegeerd,
    // dus start = eind (simpel adres)
    bp.address_start    = addr;
    bp.address_end      = addr;

    bp.check_value      = true; // CRUCIAAL: Vlag zetten dat op waarde moet worden gecontroleerd
    bp.value            = value;
    bp.value_mask       = 0xFF; // Standaard volledige byte
    bp.value_condition  = conditionFromString(condStr); // Hergebruik de helper

    qDebug() << "[DebugBridge] Parsed MEM BP:"
             << bp.definition_text
             << "addr=" << QString::number(addr, 16).toUpper()
             << "cond=" << condStr
             << "val="  << QString::number(value, 16).toUpper();
}

// RD/WR breakpoints:
//  - "RD 1000"
//  - "WR 1000 = BA"
//  - "RD 1000 ... 1FFF"
//  - "WR 1000 ... 1FFF"
void parseMemAccessBreakpoint(CoreBreakpoint& bp)
{
    if (bp.type != BreakpointType::BP_READ && bp.type != BreakpointType::BP_WRITE)
        return;
    if (bp.definition_text.isEmpty())
        return;

    QString s = bp.definition_text.trimmed().toUpper();

    const bool isRD = s.startsWith("RD ");
    const bool isWR = s.startsWith("WR ");
    if (!isRD && !isWR)
        return;

    static const QRegularExpression reWhitespace(QStringLiteral("\\s+"));
    s.replace(reWhitespace, " ");
    const QStringList parts = s.split(' ');

    // Min: "RD <addr>" -> 2 delen
    if (parts.size() < 2)
        return;

    bool ok = false;
    const uint16_t a1 = parts.at(1).toUShort(&ok, 16);
    if (!ok)
        return;

    // Default: enkel adres
    bp.address_start = a1;
    bp.address_end   = a1;

    // Range: "RD <start> ... <end>"  (4 delen)
    if (parts.size() >= 4 && parts.at(2) == "...") {
        const uint16_t a2 = parts.at(3).toUShort(&ok, 16);
        if (!ok)
            return;

        bp.address_start = qMin(a1, a2);
        bp.address_end   = qMax(a1, a2);

        bp.check_value = false;
        bp.value = 0;
        bp.value_mask = 0xFF;
        bp.value_condition = 0;
        return;
    }

    // Value compare: "WR <addr> <cond> <value>" (4 delen)
    // bvb: "WR 1000 = BA"
    if (parts.size() >= 4) {
        const QString condStr  = parts.at(2);
        const QString valueStr = parts.at(3);

        uint8_t v = (uint8_t)valueStr.toUShort(&ok, 16);
        if (!ok)
            return;

        bp.check_value     = true;
        bp.value           = v;
        bp.value_mask      = 0xFF;
        bp.value_condition = conditionFromString(condStr);
        return;
    }

    // Alleen adres (geen value-check)
    bp.check_value     = false;
    bp.value           = 0;
    bp.value_mask      = 0xFF;
    bp.value_condition = 0;
}

// CLK-breakpoints: "CLK = <addr> <> <tstates>"
static void parseClockBreakpoint(CoreBreakpoint& bp)
{
    if (bp.type != BreakpointType::BP_CLOCK)
        return;
    if (bp.definition_text.isEmpty())
        return;

    QString s = bp.definition_text.trimmed().toUpper();
    static const QRegularExpression reWhitespace(QStringLiteral("\\s+"));
    s.replace(reWhitespace, " ");
    const QStringList parts = s.split(' ');

    // Verwacht: CLK = <addr> <> <tstates>
    if (parts.size() < 5) return;
    if (parts[0] != "CLK") return;

    bool ok = false;

    // tstates staat op het einde (parts[4])
    // (adres parts[2] negeren we, jij gebruikt dat alleen als "label")
    quint64 t = parts[4].toULongLong(&ok, 16);  // hex input
    if (!ok) {
        // fallback: decimal
        t = parts[4].toULongLong(&ok, 10);
        if (!ok) return;
    }

    bp.tstate_count = t;
}

} // namespace

//----------------------------------------------------------------------------

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

    for (CoreBreakpoint &bp : m_breakpoints) {
        // Parse op basis van tekst
        parseExecuteBreakpoint(bp);
        parseRegBreakpoint(bp);
        parseFlagBreakpoint(bp);
        parseIOBreakpoint(bp);
        parseMemBreakpoint(bp);
        parseMemAccessBreakpoint(bp);
        parseClockBreakpoint(bp);

        auto needsText = [](BreakpointType t) {
            return t == BreakpointType::BP_EXECUTE ||
                   t == BreakpointType::BP_READ    ||
                   t == BreakpointType::BP_WRITE   ||
                   t == BreakpointType::BP_IO_IN   ||
                   t == BreakpointType::BP_IO_OUT  ||
                   t == BreakpointType::BP_CLOCK   ||
                   t == BreakpointType::BP_FLAG_VAL||
                   t == BreakpointType::BP_REG_VAL ||
                   t == BreakpointType::BP_MEM_VAL;
        };

        if (needsText(bp.type) && bp.definition_text.trimmed().isEmpty()) {
            bp.enabled = false;     // of: continue;
        }

        if (!bp.enabled)
            continue;

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
        qDebug() << "BP after parse: enabled=" << bp.enabled
                 << "type=" << int(bp.type)
                 << "addrStart=" << QString::number(bp.address_start,16)
                 << "addrEnd=" << QString::number(bp.address_end,16)
                 << "text=" << bp.definition_text;
    }

}

// 1. EXECUTE (roepen vóór z80_do_opcode)
bool DebugBridge::checkExecute(uint16_t addr)
{
    if (m_executeBreakpoints.isEmpty())
        return false;

    uint8_t flags = Z80.af.b.l; // F-register

    for (const auto& bp : std::as_const(m_executeBreakpoints)) {
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

            if (emulator) emulator->stop = 1;
            // Vraag aan de ColecoController (op de GUI-thread) om veilig te pauzeren.
            if (g_controller_instance) {
                QMetaObject::invokeMethod(
                    g_controller_instance,
                    &ColecoController::pauseEmulation,
                    Qt::QueuedConnection
                    );
            }

            // We retourneren TRUE, zodat de aanroepende C-code weet dat er een break is
            return true;
         }
    }
    return false;
}

void DebugBridge::setCurrentOpcodeStartPC(uint16_t pc)
{
    m_currentOpcodeStartPC.store(pc, std::memory_order_relaxed);
}

uint16_t DebugBridge::currentOpcodeStartPC() const
{
    return m_currentOpcodeStartPC.load(std::memory_order_relaxed);
}

// 2. MEMORY ACCESS (cpu_readbyte / cpu_writebyte)
bool DebugBridge::checkMemAccess(BreakpointType type, uint16_t addr, uint8_t value)
{
    static int cnt = 0;
    if (cnt++ < 50 && (addr == 0x6000 || addr == 0x6040)) {
        qDebug() << "[MEMACCESS] type=" << int(type)
        << "addr=" << QString::number(addr,16)
        << "val=" << QString::number(value,16)
        << "bps=" << m_memAccessBreakpoints.size();
        for (const auto& bp : std::as_const(m_memAccessBreakpoints)) {
            if (bp.address_start <= addr && addr <= bp.address_end) {
                qDebug() << "   BP:" << bp.definition_text
                         << "enabled=" << bp.enabled
                         << "t=" << int(bp.type)
                         << "range="
                         << QString::number(bp.address_start,16) << ".."
                         << QString::number(bp.address_end,16)
                         << "check=" << bp.check_value
                         << "v=" << QString::number(bp.value,16);
            }
        }
    }

    if (m_memAccessBreakpoints.isEmpty())
        return false;


    for (const auto& bp : std::as_const(m_memAccessBreakpoints)) {
        if (!bp.enabled)
            continue;
        if (bp.type != type)
            continue;
        if (addr < bp.address_start || addr > bp.address_end)
            continue;

        // 1) Zonder value-check: altijd breaken bij access
        if (!bp.check_value) {

            uint16_t pc_start = currentOpcodeStartPC();
            if (pc_start == 0)
                pc_start = Z80.pc.w.l;

            setLastBreakPC(pc_start);

            if (emulator)
                emulator->stop = 1;

            if (g_controller_instance) {
                QMetaObject::invokeMethod(
                    g_controller_instance,
                    &ColecoController::pauseEmulation,
                    Qt::QueuedConnection
                    );
            }
            return true;
        }

        // 2) Met value-check: pas breaken als condition klopt
        const uint8_t masked_value    = value & bp.value_mask;
        const uint8_t masked_bp_value = bp.value & bp.value_mask;

        if (checkCondition(bp.value_condition, masked_value, masked_bp_value)) {

            uint16_t pc_start = currentOpcodeStartPC();
            if (pc_start == 0)
                pc_start = Z80.pc.w.l;

            setLastBreakPC(pc_start);

            if (emulator)
                emulator->stop = 1;

            if (g_controller_instance) {
                QMetaObject::invokeMethod(
                    g_controller_instance,
                    &ColecoController::pauseEmulation,
                    Qt::QueuedConnection
                    );
            }
            return true;
        }
    }
    return false;
}


// 3. PORT ACCESS (OUTs / INs)

void DebugBridge::setLastBreakPC(uint16_t pc)
{
    if (pc == 0)
        return;

    uint16_t expected = 0;
    // Alleen de eerste setter wint totdat jij bewust reset (maar jij reset niet)
    m_lastBreakPC.compare_exchange_strong(expected, pc, std::memory_order_relaxed);
    // qDebug() << "setLastBreakPC called with" << QString::number(pc,16)
    //          << "current=" << QString::number(m_lastBreakPC.load(),16);
}

uint16_t DebugBridge::lastBreakPC() const
{
    return m_lastBreakPC.load(std::memory_order_relaxed);
}

void DebugBridge::clearLastBreakPC()
{
    m_lastBreakPC.store(0, std::memory_order_relaxed);
}

bool DebugBridge::checkIOAccess(BreakpointType type, uint16_t port, uint8_t value, uint16_t pc_start)
{
    if (m_ioAccessBreakpoints.isEmpty())
        return false;

    for (const auto& bp : std::as_const(m_ioAccessBreakpoints)) {
        if (!bp.enabled)
            continue;
        if (bp.type != type)
            continue;

        bool portMatch = false;
        if (bp.port_mask != 0x0000) {
            portMatch = ((port & bp.port_mask) == bp.port_match);
        } else {
            portMatch = (port >= bp.address_start && port <= bp.address_end);
        }
        if (portMatch) {
            // Altijd deze logica uitvoeren als het een match is
            bool match = false;

            if (!bp.check_value) {
                match = true;
            } else {
                uint8_t masked_value    = value & bp.value_mask;
                uint8_t masked_bp_value = bp.value & bp.value_mask;

                if (checkCondition(bp.value_condition, masked_value, masked_bp_value)) {
                    match = true;
                }
            }

            if (match) {
                QString dir = (type == BreakpointType::BP_IO_IN) ? "IN" : "OUT";
                QString msg = QString("%1 breakpoint hit at port 0x%2 (value=0x%3) pc_start=%4")
                                  .arg(dir)
                                  .arg(port, 4, 16, QChar('0'))
                                  .arg(value, 2, 16, QChar('0'))
                                  .arg(pc_start, 4, 16, QChar('0'));

                qDebug() << "[DEBUG][I/O BREAK]" << msg;

                // Store the exact opcode-start PC for UI highlighting
                if (pc_start != 0 && lastBreakPC() == 0)
                    setLastBreakPC(pc_start);

                if (emulator)
                    emulator->stop = 1;

                if (g_controller_instance) {
                    QMetaObject::invokeMethod(
                        g_controller_instance,
                        &ColecoController::pauseEmulation,
                        Qt::QueuedConnection
                        );
                }
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

    // We hergebruiken deze declaratie: extern ColecoController* g_controller_instance;

    uint64_t current_tstates = z80_cycle_count;
    uint8_t flags            = Z80.af.b.l;

    for (const auto& bp : std::as_const(m_postExecutionBreakpoints)) {
        if (!bp.enabled)
            continue;

        switch (bp.type) {
        case BreakpointType::BP_CLOCK:
            if (bp.tstate_count == 0) break; // <-- voorkomt "altijd-hit"
            if (current_tstates >= bp.tstate_count) {

                    if (emulator) emulator->stop = 1;
                    if (g_controller_instance) {
                    QMetaObject::invokeMethod(
                        g_controller_instance,
                        &ColecoController::pauseEmulation,
                        Qt::QueuedConnection
                        );
                }
                return true;
            }
            break;

        case BreakpointType::BP_FLAG_VAL:
            if (bp.flag_mask != 0x00) {
                uint8_t masked_flags    = flags & bp.flag_mask;
                uint8_t masked_bp_value = bp.flag_value & bp.flag_mask;

                if (masked_flags == masked_bp_value) {

                        if (emulator) emulator->stop = 1;
                        if (g_controller_instance) {
                        QMetaObject::invokeMethod(
                            g_controller_instance,
                            &ColecoController::pauseEmulation,
                            Qt::QueuedConnection
                            );
                    }
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

                        if (emulator) emulator->stop = 1;
                        if (g_controller_instance) {
                        QMetaObject::invokeMethod(
                            g_controller_instance,
                            &ColecoController::pauseEmulation,
                            Qt::QueuedConnection
                            );
                    }
                    return true;
                }
            }
            break;
        }

        case BreakpointType::BP_MEM_VAL:
            if (bp.check_value) {
                // De logica voor MEM-check is hier correct ingebouwd
                uint8_t mem_val         = coleco_getbyte(bp.address_start);
                uint8_t masked_mem_val  = mem_val & bp.value_mask;
                uint8_t masked_bp_value = bp.value & bp.value_mask;

                bool condition_met = checkCondition(bp.value_condition,
                                                    masked_mem_val,
                                                    masked_bp_value);
                if (condition_met) {

                    if (emulator) emulator->stop = 1;
                    if (g_controller_instance) {
                        QMetaObject::invokeMethod(
                            g_controller_instance,
                            &ColecoController::pauseEmulation,
                            Qt::QueuedConnection
                            );
                    }
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
