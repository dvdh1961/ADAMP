#pragma once

#include <stdint.h>
#include <QList>
#include <QString>

// Gelijk aan de BreakpointType uit EmulTwo (geschat op basis van functionaliteit)
enum class BreakpointType : uint8_t {
    BP_NONE     = 0,
    BP_EXECUTE  = 1, // Breek op het adres van een instructie (Program Counter)
    BP_READ     = 2, // Breek bij het lezen van een geheugenadres/bereik
    BP_WRITE    = 3, // Breek bij het schrijven naar een geheugenadres/bereik
    BP_IO_IN    = 4, // Breek bij het lezen van een I/O-poort
    BP_IO_OUT   = 5, // Breek bij het schrijven naar een I/O-poort
    BP_CLOCK    = 6, // Breek op een specifiek T-state (klokcyclus)
    BP_FLAG_VAL = 7, // Breek wanneer Z80-flags een bepaalde waarde hebben
    BP_REG_VAL  = 8, // Breek wanneer een register een bepaalde waarde heeft
    BP_MEM_VAL  = 9  // Breek wanneer een geheugenlocatie een bepaalde waarde heeft
};

// De centrale breakpoint structuur
struct CoreBreakpoint {
    BreakpointType type = BreakpointType::BP_NONE;
    bool enabled = true;
    bool temporary = false; // Voor tijdelijke breaks (bv. step-over)

    // Adres en bereik
    uint32_t address_start = 0x0000;
    uint32_t address_end = 0xFFFF; // Voor bereikcontrole

    // Waarde/Conditie (Gebruikt voor MEM, RD, WR, IO)
    bool check_value = false;
    uint8_t value = 0x00;           // De 8-bit waarde om mee te vergelijken
    uint8_t value_mask = 0xFF;      // De bitmasker voor de waarde (voor wildcards of specifieke bits)
    int value_condition = 0;        // -1: <=, 0: =, 1: >=, 2: <>, 3: <, 4: >

    // Register/Flag/Clock (Gebruikt voor REG)
    bool check_reg_value = false;
    uint8_t reg_id = 0x00;          // ID van het register
    uint16_t reg_value = 0x0000;    // 16-bit registerwaarde om mee te vergelijken
    uint16_t reg_mask = 0xFFFF;     // 16-bit mask voor registers (indien nodig)
    int reg_condition = 0;          // Conditie voor register (zelfde als value_condition)

    uint64_t tstate_count = 0;      // T-states voor BP_CLOCK

    uint8_t flag_value = 0x00;      // F-register waarde om mee te vergelijken
    uint8_t flag_mask = 0x00;       // Masker voor de F-register bits

    // Debugging en GUI
    QString definition_text;        // Originele tekstuele definitie

    // OPLOSSING VOOR DE COMPILATIEFOUT: Definieer de operator < voor std::sort
    bool operator<(const CoreBreakpoint& other) const {
        if (address_start != other.address_start) {
            return address_start < other.address_start;
        }
        // Als de adressen gelijk zijn, sorteer op type (uitvoer/lees/schrijf/etc.)
        return type < other.type;
    }
};

class DebugBridge {
public:
    static DebugBridge& instance();

    // Functies voor de GUI
    void syncBreakpoints(const QList<CoreBreakpoint>& breakpoints);

    // Functies voor de CORE (coleco.cpp/z80.c)
    bool checkExecute(uint16_t addr);
    bool checkMemAccess(BreakpointType type, uint16_t addr, uint8_t value);
    bool checkIOAccess(BreakpointType type, uint16_t port, uint8_t value);
    bool checkPostExecutionBreakpoints();

private:
    DebugBridge() = default;
    DebugBridge(const DebugBridge&) = delete;
    DebugBridge& operator=(const DebugBridge&) = delete;

    // Hulpmethode om de Z80 registerwaarde op te halen
    uint16_t getRegisterValue(uint8_t reg_id);

    // Hulpmethode voor alle conditiecontroles
    bool checkCondition(int condition, uint16_t actual, uint16_t expected) const;

    QList<CoreBreakpoint> m_breakpoints;

    // Geoptimaliseerde lijsten voor snelle controle
    QList<CoreBreakpoint> m_executeBreakpoints;
    QList<CoreBreakpoint> m_memAccessBreakpoints;
    QList<CoreBreakpoint> m_ioAccessBreakpoints;
    QList<CoreBreakpoint> m_postExecutionBreakpoints;
};

// CRUCIALE OPLOSSING VOOR COMPILATIEFOUT: Definieert de singleton-alias
#define DEBUG_BRIDGE DebugBridge::instance()
