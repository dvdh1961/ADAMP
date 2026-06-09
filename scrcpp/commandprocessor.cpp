#include "commandprocessor.h"

#include <QStringList>
#include <QRegularExpression>
#include <QStringBuilder>
#include <functional>

CommandProcessor::CommandProcessor(QObject* parent)
    : QObject(parent)
{
}

QString CommandProcessor::execute(const QString& commandLine)
{
    const QString cmd = commandLine.trimmed();
    if (cmd.isEmpty()) return {};

    QStringList args = cmd.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (args.isEmpty()) return {};

    const QString c0 = args.value(0).toLower();
    const QString c1 = args.value(1).toLower();

    if (c0 == "help" || c0 == "?")
        return cmdHelp();

    if (c0 == "clear" || c0 == "cls")
        return "__ADAMP_TERMINAL_CLEAR__";

    if ((c0 == "start" && c1 == "emu") || c0 == "start")
        return m_startCb ? m_startCb() : "No start callback connected.";

    if ((c0 == "stop" && c1 == "emu") || c0 == "stop")
        return m_stopCb ? m_stopCb() : "No stop callback connected.";

    if (c0 == "reset" && c1 == "adam")
        return m_resetAdamCb ? m_resetAdamCb() : "No reset ADAM callback connected.";

    if (c0 == "reset" && (c1 == "coleco" || c1 == "cart" || c1 == "cartridge"))
        return m_resetColecoCb ? m_resetColecoCb() : "No reset Coleco callback connected.";

    if (c0 == "poff" || c0 == "poweroff")
        return m_powerOffCb ? m_powerOffCb() : "No poweroff callback connected.";

    if (c0 == "regs" || c0 == "r")
        return m_cpuRegsCb ? m_cpuRegsCb() : "No CPU register callback connected.";

    if (c0 == "mem" || c0 == "m" || c0 == "memory")
        return cmdMemory(args);

    if (c0 == "peek")
        return cmdPeek(args);

    if (c0 == "poke")
        return cmdPoke(args);

    if (c0 == "dasm" || c0 == "d" || c0 == "disasm" || c0 == "opcode" || c0 == "op")
        return cmdDisasm(args);

    return QString("Unknown command: %1\nType help for commands.").arg(commandLine);
}

QString CommandProcessor::cmdHelp() const
{
    return
        "ADAMP Monitor commands:\n"
        "\n"
        "  start emu                 start emulation\n"
        "  stop  emu                 stop emulation\n"
        "  reset adam                reset to ADAM mode\n"
        "  reset coleco              reset to Coleco mode\n"
        "  poff                      power off / hard reset hook\n"
        "\n"
        "  regs                      show Z80 registers\n"
        "  mem  7000 40              memory dump from hex address, hex length bytes\n"
        "  peek 7000                 show one hex byte\n"
        "  poke 7000 42              set one hex byte\n"
        "\n"
        "  dasm 8000 40              disassemble from hex address, hex length bytes\n"
        "\n"
        "  clear                     clear terminal\n\n";
}

QString CommandProcessor::cmdMemory(const QStringList& args) const
{
    uint32_t from = 0;
    uint32_t length = 0;

    // Nieuw formaat:
    //   mem <address> <length>
    // Voorbeeld:
    //   mem 8000 40
    // Dit betekent: dump vanaf $8000, lengte $40 bytes.
    if (args.size() == 3)
    {
        if (!parseAddress(args[1], from, 0xFFFFFFFFu))
            return "Invalid start address. Usage: mem <address> <length>";

        if (!parseAddress(args[2], length, 0xFFFFFFFFu))
            return "Invalid length. Usage: mem <address> <length>";
    }
    else
    {
        return "Usage: mem <address> <length>";
    }

    if (length == 0)
        return "Invalid length. Usage: mem <address> <length>";

    if (length > 0x1000u)
        return "Range too large. Max 4096 bytes per command.";

    if (!m_memoryReadCb)
        return "No memory reader connected.";

    const uint32_t to = from + length - 1u;

    if (to < from)
        return "Range wraps around. Use a smaller length.";

    return formatHexDump(from, to);
}

QString CommandProcessor::cmdPeek(const QStringList& args) const
{
    if (args.size() != 2)
        return "Usage: peek 7000";

    if (!m_memoryReadCb)
        return "No memory reader connected.";

    uint32_t addr = 0;
    if (!parseAddress(args[1], addr, 0xFFFFFFFFu))
        return "Invalid address.";

    uint8_t value = 0;
    if (!m_memoryReadCb(addr, value))
        return "Memory read failed.";

    return QString("%1: %2")
        .arg(addr, 4, 16, QLatin1Char('0'))
        .arg(value, 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString CommandProcessor::cmdPoke(const QStringList& args) const
{
    if (args.size() != 3)
        return "Usage: poke 7000 42";

    if (!m_memoryWriteCb)
        return "No memory writer connected.";

    uint32_t addr = 0;
    uint32_t value32 = 0;

    if (!parseAddress(args[1], addr, 0xFFFFFFFFu))
        return "Invalid address.";

    if (!parseAddress(args[2], value32, 0xFFu))
        return "Invalid byte value.";

    const uint8_t value = static_cast<uint8_t>(value32 & 0xFFu);

    if (!m_memoryWriteCb(addr, value))
        return "Memory write failed.";

    return QString("%1 <= %2")
        .arg(addr, 4, 16, QLatin1Char('0'))
        .arg(value, 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString CommandProcessor::cmdDisasm(const QStringList& args) const
{
    uint32_t from = 0;
    uint32_t length = 0;

    // Zelfde nieuw formaat als mem:
    //   dasm <address> <length>
    // Voorbeeld:
    //   dasm 8000 40
    if (args.size() == 3)
    {
        if (!parseAddress(args[1], from))
            return "Invalid start address. Usage: dasm <address> <length>";

        if (!parseAddress(args[2], length))
            return "Invalid length. Usage: dasm <address> <length>";
    }
    else
    {
        return "Usage: dasm <address> <length>";
    }

    if (length == 0)
        return "Invalid length. Usage: dasm <address> <length>";

    if (!m_disasmCb)
        return "No disassembler connected.";

    // Belangrijk:
    // De callback krijgt nu de lengte als tweede parameter.
    // In maingui.cpp is setDisasmCallback daarvoor al aangepast.
    return m_disasmCb(static_cast<uint16_t>(from), static_cast<uint16_t>(length));
}

QString CommandProcessor::cmdBreakpoint(const QStringList& /*args*/) const
{
    return "Breakpoint command not connected.";
}

bool CommandProcessor::parseAddress(const QString& text, uint32_t& value, uint32_t maxValue) const
{
    QString s = text.trimmed().toLower();
    bool ok = false;
    uint number = 0;

    if (s.startsWith("0x"))
    {
        number = s.mid(2).toUInt(&ok, 16);
    }
    else if (s.startsWith("$"))
    {
        number = s.mid(1).toUInt(&ok, 16);
    }
    else if (s.endsWith('h'))
    {
        number = s.left(s.length() - 1).toUInt(&ok, 16);
    }
    else
    {
        // Monitor-style default: hex.
        // Dus 7000 betekent $7000, niet decimal 7000.
        number = s.toUInt(&ok, 16);
    }

    if (!ok || number > maxValue)
        return false;

    value = number;
    return true;
}

QString CommandProcessor::formatHexDump(uint32_t from, uint32_t to) const
{
    QString out;

    for (uint32_t row = from; row <= to; row += 16)
    {
        QString hex;
        QString ascii;

        for (uint32_t i = 0; i < 16; ++i)
        {
            const uint32_t addr = row + i;

            if (addr > to)
            {
                hex += "   ";
                ascii += ' ';
                continue;
            }

            uint8_t value = 0;
            if (!m_memoryReadCb(addr, value))
                value = 0xFF;

            hex += QString("%1 ")
                       .arg(value, 2, 16, QLatin1Char('0'))
                       .toUpper();

            ascii += (value >= 32 && value <= 126)
                         ? QChar(static_cast<char>(value))
                         : QChar('.');
        }

        out += QString("%1: %2  %3\n")
                   .arg(row & 0xFFFFu, 4, 16, QLatin1Char('0'))
                   .arg(hex.leftJustified(48, ' '))
                   .arg(ascii)
                   .toUpper();

        if (row > 0xFFFFFFFFu - 16u)
            break;
    }

    return out.trimmed();
}
