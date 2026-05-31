#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QCoreApplication>

#include "cv.h"
#include "cvbank.h"

extern bool coleco_opcode_mapper;
extern BYTE coleco_joymode;

#define DO1(buf) crc = crc_table[((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
#define DO2(buf)  DO1(buf); DO1(buf);
#define DO4(buf)  DO2(buf); DO2(buf);
#define DO8(buf)  DO4(buf); DO4(buf);

// Table of CRC-32's of all single-byte values
    static const uint32_t crc_table[256] = {
        0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
        0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
        0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
        0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
        0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
        0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
        0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
        0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
        0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
        0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
        0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
        0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
        0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
        0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
        0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
        0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
        0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
        0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
        0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
        0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
        0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
        0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
        0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
        0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
        0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
        0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
        0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
        0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
        0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
        0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
        0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
        0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
        0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
        0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
        0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
        0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
        0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
        0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
        0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
        0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
        0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
        0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
        0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
        0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
        0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
        0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
        0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
        0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
        0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
        0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
        0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
        0x2d02ef8dL
};

extern "C" {
int* sn76489_get_regs();
unsigned char* ay8910_get_regs();
void sn76489_restore_reg(int r, uint8_t val);
void ay8910_set_reg(int reg, uint8_t val);
}

BYTE sgc_bank[4] = {0,0,0,0};
BYTE sgc_sst_state = 0;
BYTE sgc_sst_cmd_pos = 0;
BYTE sgc_rom_bank_mask = 0;
int  sgc_rom_size = 0;
BYTE sgc_write_map[256] = {0};
BYTE sgc_dirty = 0;
QString sgc_rom_path;

unsigned int sgm_low_addr = 0xFFFF;
BYTE sgm_neverenable = 0;
BYTE sgm_enable = 0;
BYTE sgm_firstwrite = 1;
static CvBankSgmVariant g_sgm_variant = CVBANK_SGM_VARIANT_NONE;

int  coleco_mega_layout = 1;

// ------------------------------------------------------------
// DKA bank trace - local to cvbank.cpp
// Zet op 1 om DKA bank switching te loggen.
// Zet terug op 0 voor normale build.
// ------------------------------------------------------------
#define DKA_TRACE_BANKS 1

#if DKA_TRACE_BANKS
static int g_dkaBankTraceCount = 0;

static inline void DKA_TRACE_BANK(const QString& msg)
{
    if (g_dkaBankTraceCount < 250)
    {
        qDebug().noquote() << msg;
        g_dkaBankTraceCount++;
    }
}
#else
static inline void DKA_TRACE_BANK(const QString&)
{
}
#endif

static bool isSuperGameCartCrc(DWORD crc);
static bool isSgmMegaCartCrc(DWORD crc);
static bool isSgmMegaCartFileName(const QString& path);
static void superGameCartSetup(int romSize);
static void superGameCartRestoreFlash(void);
static QString superGameCartFlashFilename(void);
static bool opcodeGamesEnabledFromSettings(void);


CvBankSgmVariant cvbank_sgm_variant(void)
{
    return g_sgm_variant;
}

const char* cvbank_sgm_variant_name(CvBankSgmVariant variant)
{
    switch (variant)
    {
    case CVBANK_SGM_VARIANT_DISABLED:         return "disabled";
    case CVBANK_SGM_VARIANT_STANDARD_RAM:     return "standard_sgm_ram";
    case CVBANK_SGM_VARIANT_OPCODE_SGC:       return "opcode_sgc";
    case CVBANK_SGM_VARIANT_DISABLED_BY_CART: return "disabled_by_cart";
    case CVBANK_SGM_VARIANT_ADAM_MODE:        return "adam_mode";
    case CVBANK_SGM_VARIANT_NONE:
    default:                                  return "none";
    }
}

void cvbank_sgm_reset_runtime_state(void)
{
    sgm_enable = 0;
    sgm_low_addr = 0x2000;
    sgm_neverenable = 0;
    g_sgm_variant = CVBANK_SGM_VARIANT_NONE;

    /*
     * NIET sgm_firstwrite hier resetten.
     *
     * DKA start al code en kan daarna via port $53 SGM activeren.
     * Als sgm_firstwrite opnieuw 1 is, wordt $2000-$7FFF gewist
     * terwijl de game al draait.
     *
     * sgm_firstwrite moet alleen op 1 bij cold start / nieuwe ROM load.
     */
}

void cvbank_sgm_set_forced_disabled(int disabled)
{
    sgm_neverenable = disabled ? 1 : 0;
    if (sgm_neverenable)
    {
        sgm_enable = 0;
        g_sgm_variant = CVBANK_SGM_VARIANT_DISABLED_BY_CART;
    }
}

void cvbank_sgm_init_ports_for_current_machine(void)
{
    if (!emulator)
        return;

    /*
     * EmulTwo behaviour:
     * SGM may be AVAILABLE via emulator->SGM,
     * but SGM RAM is NOT enabled at reset.
     *
     * The game must enable SGM RAM itself by writing to port $53.
     */
    coleco_port53 = 0x00;

    if (emulator->currentMachineType == MACHINEADAM)
        coleco_port60 = g_adamCartridgeMode ? 0x0F : 0x00;
    else
        coleco_port60 = 0x0F;

    coleco_port20 = 0x00;
}

void cvbank_sgm_apply_mapping(void)
{
    if (!emulator)
        return;

    if (emulator->currentMachineType == MACHINEADAM)
    {
        g_sgm_variant = CVBANK_SGM_VARIANT_ADAM_MODE;
        return;
    }

    if (sgm_neverenable || !emulator->SGM)
    {
        sgm_enable = 0;
        sgm_low_addr = 0x2000;

        /*
         * EmulTwo-compatible start mapping:
         * Only force BIOS at $0000-$1FFF.
         * Do not aggressively remap $2000-$5FFF to BIOS here.
         */
        MemoryMap[0] = BIOS_Memory + 0x0000;

        g_sgm_variant = sgm_neverenable ? CVBANK_SGM_VARIANT_DISABLED_BY_CART
                                        : CVBANK_SGM_VARIANT_DISABLED;
        return;
    }

    sgm_enable = (coleco_port53 & 0x01) ? 1 : 0;

    /*
     * IMPORTANT:
     * Do not clear SGM RAM here after the game is already running.
     * The log showed DKA reaches PC=C043 and then SGM RAM gets cleared.
     *
     * Clear SGM RAM only at new cartridge load or true hard reset,
     * not inside every mapping apply.
     */

    /*
     * EmulTwo-compatible:
     * Keep MemoryMap[1..3] as they came from reset:
     *   M1 = RAM_Memory + 0x2000
     *   M2 = RAM_Memory + 0x4000
     *   M3 = RAM_Memory + 0x6000
     *
     * Do not switch them back and forth here.
     */

    if (coleco_port60 & 0x02)
    {
        MemoryMap[0] = BIOS_Memory + 0x0000;
        sgm_low_addr = 0x2000;
    }
    else
    {
        /*
         * EmulTwo behaviour:
         * If BIOS is disabled via port60, force SGM RAM mode
         * and expose RAM at $0000-$1FFF.
         */
        sgm_enable = 1;
        MemoryMap[0] = RAM_Memory + 0x0000;
        sgm_low_addr = 0x0000;
    }

    if (emulator->romCartridgeType == ROMCARTRIDGEOPCODE)
        g_sgm_variant = CVBANK_SGM_VARIANT_OPCODE_SGC;
    else
        g_sgm_variant = sgm_enable ? CVBANK_SGM_VARIANT_STANDARD_RAM
                                   : CVBANK_SGM_VARIANT_DISABLED;
}

void coleco_setupsgm(void)
{
    cvbank_sgm_apply_mapping();
}

int cvbank_sgm_write_ram(unsigned int address, int data)
{
    if (!emulator || emulator->currentMachineType == MACHINEADAM)
        return 0;

    if (emulator->SGM && sgm_enable)
    {
        /*
         * 0x0000-0x1FFF only writable when low RAM is selected.
         */
        if (address < 0x2000 && sgm_low_addr == 0x0000)
        {
            RAM_Memory[address] = (BYTE)data;
            return 1;
        }

        /*
         * 0x2000-0x7FFF is linear RAM in SGM mode.
         */
        if (address >= 0x2000 && address < 0x8000)
        {
            RAM_Memory[address] = (BYTE)data;
            return 1;
        }
    }
    else
    {
        /*
         * Standard Coleco RAM:
         * 1K RAM mirrored across 0x6000-0x7FFF.
         */
        if (address >= 0x6000 && address < 0x8000)
        {
            const unsigned int mirrored = 0x6000 + (address & 0x03FF);
            for (int i = 0; i < 8; ++i)
                RAM_Memory[mirrored + (i * 0x0400)] = (BYTE)data;
            return 1;
        }
    }

    return 0;
}

void cvbank_sgm_write_control_port(BYTE data)
{
#if DKA_TRACE_BANKS
    if (emulator && emulator->cardcrc == 0x45345709)
    {
        DKA_TRACE_BANK(QString(
                           "[DKA SGM PORT53] PC=%1 old53=%2 new53=%3 oldEnable=%4 M0=%5 M1=%6 M2=%7 M3=%8")
                           .arg(Z80.pc.w.l, 4, 16, QChar('0'))
                           .arg(coleco_port53, 2, 16, QChar('0'))
                           .arg(data, 2, 16, QChar('0'))
                           .arg(sgm_enable)
                           .arg((quintptr)(MemoryMap[0] - RAM_Memory), 0, 16)
                           .arg((quintptr)(MemoryMap[1] - RAM_Memory), 0, 16)
                           .arg((quintptr)(MemoryMap[2] - RAM_Memory), 0, 16)
                           .arg((quintptr)(MemoryMap[3] - RAM_Memory), 0, 16));
    }
#endif

    coleco_port53 = data;
    cvbank_sgm_apply_mapping();

#if DKA_TRACE_BANKS
    if (emulator && emulator->cardcrc == 0x45345709)
    {
        DKA_TRACE_BANK(QString(
                           "[DKA SGM PORT53 ACTIVE] PC=%1 port53=%2 enable=%3 low=%4 M0=%5 M1=%6 M2=%7 M3=%8")
                           .arg(Z80.pc.w.l, 4, 16, QChar('0'))
                           .arg(coleco_port53, 2, 16, QChar('0'))
                           .arg(sgm_enable)
                           .arg(sgm_low_addr, 4, 16, QChar('0'))
                           .arg((MemoryMap[0] >= RAM_Memory && MemoryMap[0] < RAM_Memory + MAX_RAM_SIZE * 1024)
                                    ? QString::number((quintptr)(MemoryMap[0] - RAM_Memory), 16)
                                    : QString("NOT_RAM"))
                           .arg((MemoryMap[1] >= RAM_Memory && MemoryMap[1] < RAM_Memory + MAX_RAM_SIZE * 1024)
                                    ? QString::number((quintptr)(MemoryMap[1] - RAM_Memory), 16)
                                    : QString("NOT_RAM"))
                           .arg((MemoryMap[2] >= RAM_Memory && MemoryMap[2] < RAM_Memory + MAX_RAM_SIZE * 1024)
                                    ? QString::number((quintptr)(MemoryMap[2] - RAM_Memory), 16)
                                    : QString("NOT_RAM"))
                           .arg((MemoryMap[3] >= RAM_Memory && MemoryMap[3] < RAM_Memory + MAX_RAM_SIZE * 1024)
                                    ? QString::number((quintptr)(MemoryMap[3] - RAM_Memory), 16)
                                    : QString("NOT_RAM")));
    }
#endif
}

void cvbank_sgm_write_memory_port(BYTE data)
{
#if DKA_TRACE_BANKS
    if (emulator && emulator->cardcrc == 0x45345709)
    {
        DKA_TRACE_BANK(QString(
                           "[DKA SGM PORT60] PC=%1 old60=%2 new60=%3 enable=%4 low=%5")
                           .arg(Z80.pc.w.l, 4, 16, QChar('0'))
                           .arg(coleco_port60, 2, 16, QChar('0'))
                           .arg(data, 2, 16, QChar('0'))
                           .arg(sgm_enable)
                           .arg(sgm_low_addr, 4, 16, QChar('0')));
    }
#endif

    coleco_port60 = data;
    cvbank_sgm_apply_mapping();
}

BYTE coleco_loadcart(char *filename)
{
    long size;
    int j;
    BYTE *hdr;
    BYTE retf = ROM_LOAD_FAIL;
    FILE *fRomfile = fopen(filename, "rb");

    coleco_opcode_mapper = false;

    if (!fRomfile) return retf;

    fseek(fRomfile, 0, SEEK_END);
    size = ftell(fRomfile);
    fseek(fRomfile, 0, SEEK_SET);

    if (size <= 0 || size > (MAX_CART_SIZE * 1024)) {
        fclose(fRomfile);
        return retf;
    }

    memset(ROM_Memory, 0xFF, (MAX_CART_SIZE * 1024));
    if (fread((void*)ROM_Memory, 1, size, fRomfile) != (size_t)size) {
        fclose(fRomfile);
        return retf;
    }
    fclose(fRomfile);

    sgc_rom_path = QString::fromLocal8Bit(filename);

    emulator->cardsize = (DWORD)size;
    emulator->cardcrc  = CRC32Block(ROM_Memory, emulator->cardsize);

    const BYTE userSgmSetting = emulator->SGM ? 1 : 0;

    /*
     * Start every cartridge from a clean mapper/SGM state.
     * A previous SGM cartridge must never leave SGM RAM enabled for a
     * normal Coleco/MegaCart game.
     */
    sgm_enable = 0;
    sgm_firstwrite = 1;
    sgm_low_addr = 0x2000;
    sgm_neverenable = 0;
    coleco_port53 = 0x00;

    // Bewaar de manuele/UI SGM setting.
    // Bekende SGM ROMs zoals DKA mogen later nog steeds SGM forceren.
    emulator->SGM = userSgmSetting;

    coleco_megacart = 0x00;
    coleco_megasize = 2;
    coleco_megabank = 199;

    hdr = NULL;
    if ((ROM_Memory[0]==0x55 && ROM_Memory[1]==0xAA) ||
        (ROM_Memory[0]==0xAA && ROM_Memory[1]==0x55)) {
        hdr = ROM_Memory;
    } else if (size >= 0x4000) {
        int lastBankOffset = (int)(size - 0x4000);
        if ((ROM_Memory[lastBankOffset]==0x55 && ROM_Memory[lastBankOffset+1]==0xAA) ||
            (ROM_Memory[lastBankOffset]==0xAA && ROM_Memory[lastBankOffset+1]==0x55)) {
            hdr = ROM_Memory + lastBankOffset;
        }
    }

    if (!hdr && size == 128 * 1024) {
        qDebug() << "[CART] 128K cart detected without standard header.";
        hdr = ROM_Memory;
    }

    if (!hdr && size <= 32768) hdr = ROM_Memory;
    if (!hdr) return ROM_VERIFY_FAIL;

    BYTE *p = RAM_Memory + 0x8000;
    memset(p, 0xFF, 0x8000);

    if (size <= 32 * 1024) {
        long copied = 0;
        while (copied < 0x8000) {
            long chunk = size;
            if (copied + chunk > 0x8000) chunk = 0x8000 - copied;
            memcpy(p + copied, ROM_Memory, chunk);
            copied += chunk;
        }
        emulator->romCartridgeType = ROMCARTRIDGESTD;
        qDebug() << "[CART] standard ROM loaded"
                 << "crc=" << QString::number(emulator->cardcrc, 16)
                 << "SGM=" << emulator->SGM;
        return ROM_LOAD_PASS;
    }

    {
        long pages = ((size + 0x3FFF) & ~0x3FFF) >> 14;
        for (j = 2; j < pages; j <<= 1) {}
        coleco_megasize = (BYTE)j;

        bool isOpcodeHeader = (ROM_Memory[0]==0x55 && ROM_Memory[1]==0xAA &&
                               ROM_Memory[2]==0x4F && ROM_Memory[3]==0x50);
        bool isKnownSuperGameCart = isSuperGameCartCrc(emulator->cardcrc);
        bool isSgmMegaCart = isSgmMegaCartCrc(emulator->cardcrc) || isSgmMegaCartFileName(sgc_rom_path);
        bool isOpcode = isOpcodeHeader || isKnownSuperGameCart;

        if (isOpcode && !opcodeGamesEnabledFromSettings()) {
            qWarning() << "[CART] Opcode/SGC game blocked. "
                       << "crc=" << QString::number(emulator->cardcrc, 16);
            emulator->romCartridgeType = ROMCARTRIDGESTD;
            return ROM_VERIFY_FAIL;
        }

        /*
         * DKA/Mr.Do Run Run are plain MegaCart ROMs, but they need SGM RAM.
         * Opcode/Super Game Cart also uses the SGM hardware path.
         */
        if (isSgmMegaCart || isOpcode)
        {
            emulator->SGM = 1;
            sgm_neverenable = 0;
            //coleco_port53 = 0x01;

            qDebug() << "[CART] SGM enabled for cartridge"
                     << "crc=" << QString::number(emulator->cardcrc, 16)
                     << "size=" << size
                     << "sgmMega=" << isSgmMegaCart
                     << "opcode=" << isOpcode;
        }

        /*
         * Gearcoleco-style MegaCart model:
         *   $8000-$BFFF is always the fixed last 16K ROM bank.
         *   $C000-$FFFF is the switchable 16K bank.
         *   Reset starts with bank 0 at $C000.
         *
         * SGM is orthogonal to the ROM mapper: DKA/Mr.Do are still normal
         * MegaCart ROMs, they simply need SGM hardware/RAM available.
         */
        coleco_mega_layout = 0;
        coleco_megacart = (BYTE)(j - 1);
        unsigned int lastBase = (unsigned int)coleco_megacart * 0x4000;

        emulator->romCartridgeType = isOpcode ? ROMCARTRIDGEOPCODE : ROMCARTRIDGEMEGA;

        if (isOpcode) {
            superGameCartSetup((int)size);
            coleco_megabank = 0;
        } else {
            MemoryMap[4] = ROM_Memory + lastBase;
            MemoryMap[5] = MemoryMap[4] + 0x2000;
            MemoryMap[6] = ROM_Memory + 0x0000;
            MemoryMap[7] = MemoryMap[6] + 0x2000;
            coleco_megabank = 0;
        }

        qDebug() << "[CART] romCartridgeType=" << emulator->romCartridgeType
                 << "mapper=" << (isOpcode ? "opcode_sgc" : "gearcoleco_megacart")
                 << "SGM=" << emulator->SGM
                 << "isOpcode=" << isOpcode
                 << "isKnownSuperGameCart=" << isKnownSuperGameCart
                 << "isSgmMegaCart=" << isSgmMegaCart
                 << "crc=" << QString::number(emulator->cardcrc, 16)
                 << "M4=" << QString::number((quintptr)(MemoryMap[4] - ROM_Memory), 16)
                 << "M6=" << QString::number((quintptr)(MemoryMap[6] - ROM_Memory), 16);
        return ROM_LOAD_PASS;
    }
}


static bool opcodeGamesEnabledFromSettings(void)
{
    const QString iniPath = QDir(QCoreApplication::applicationDirPath()).filePath("settings.ini");
    QSettings settings(iniPath, QSettings::IniFormat);

    // Intentionally hidden/manual setting.
    // Add this line manually in settings.ini to allow protected Opcode/SGC games:
    // OPCODE=1
    const QString value = settings.value("OPCODE", "0").toString().trimmed();
    return value == "1";
}

static bool isSuperGameCartCrc(DWORD crc)
{
    switch (crc)
    {
    case 0x30d337e4: // Gradius
    case 0x6831ad48:
    case 0xbdae4248: // Mooncresta
    case 0x80586cc5:
    case 0x6c8113c1:
    case 0xcf803ddc: // Time Pilot
    case 0x2426c300:
        return true;
    default:
        return false;
    }
}

static bool isSgmMegaCartCrc(DWORD crc)
{
    switch (crc)
    {
    case 0x45345709: // Donkey Kong Arcade SGM
    case 0x13d53b3c: // Mr. Do! Run Run SGM
        return true;
    default:
        return false;
    }
}

static bool isSgmMegaCartFileName(const QString& path)
{
    const QString n = QFileInfo(path).completeBaseName().toLower();
    return n.contains("donkey kong arcade") ||
           n.contains("dka") ||
           n.contains("45345709") ||
           n.contains("mr do run") ||
           n.contains("mr.do run") ||
           n.contains("13d53b3c");
}

static QString superGameCartFlashFilename(void)
{
    QFileInfo fi(sgc_rom_path);
    QDir dir = fi.dir();
    QString savDir = dir.filePath("sav");
    QDir().mkpath(savDir);
    return QDir(savDir).filePath(fi.completeBaseName() + ".sst");
}

void banking_apply_boot_mapping(void)
{
    sgc_bank[0] = 0;
    sgc_bank[1] = 3 & sgc_rom_bank_mask;
    sgc_bank[2] = 2 & sgc_rom_bank_mask;
    sgc_bank[3] = 1 & sgc_rom_bank_mask;

    MemoryMap[4] = ROM_Memory + (0x2000 * sgc_bank[0]);
    MemoryMap[5] = ROM_Memory + (0x2000 * sgc_bank[1]);
    MemoryMap[6] = ROM_Memory + (0x2000 * sgc_bank[2]);
    MemoryMap[7] = ROM_Memory + (0x2000 * sgc_bank[3]);

    qDebug() << "[SGC] Boot mapping applied"
             << sgc_bank[0] << sgc_bank[1] << sgc_bank[2] << sgc_bank[3];
}

static void superGameCartRestoreFlash(void)
{
    memset(sgc_write_map, 0x00, sizeof(sgc_write_map));

    if (sgc_rom_path.isEmpty()) return;

    QFile f(superGameCartFlashFilename());
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) return;

    if (f.read((char*)sgc_write_map, sizeof(sgc_write_map)) != (qint64)sizeof(sgc_write_map))
    {
        f.close();
        memset(sgc_write_map, 0x00, sizeof(sgc_write_map));
        return;
    }

    if (sgc_write_map[0] == 0x55 || sgc_write_map[0] == 0xAA)
    {
        memset(sgc_write_map, 0x00, sizeof(sgc_write_map));
        f.close();
        return;
    }

    for (int i = 0; i < 256; ++i)
    {
        if (sgc_write_map[i])
        {
            if (f.read((char*)(ROM_Memory + (4096 * i)), 4096) != 4096)
                break;
        }
    }
    f.close();
}

void banking_supergamecart_saveflash(void)
{
    if (!sgc_dirty || sgc_rom_path.isEmpty()) return;

    QFile f(superGameCartFlashFilename());
    if (!f.open(QIODevice::WriteOnly))
    {
        qDebug() << "[SGC] Could not save flash file" << f.fileName();
        return;
    }

    f.write((const char*)sgc_write_map, sizeof(sgc_write_map));
    for (int i = 0; i < 256; ++i)
    {
        if (sgc_write_map[i])
            f.write((const char*)(ROM_Memory + (4096 * i)), 4096);
    }
    f.close();
    sgc_dirty = 0;
    qDebug() << "[SGC] Flash saved to" << f.fileName();
}

static void superGameCartSetup(int romSize)
{
    if      (romSize <= (128  * 1024)) sgc_rom_bank_mask = 0x0f;
    else if (romSize <= (256  * 1024)) sgc_rom_bank_mask = 0x1f;
    else if (romSize <= (512  * 1024)) sgc_rom_bank_mask = 0x3f;
    else if (romSize <= (1024 * 1024)) sgc_rom_bank_mask = 0x7f;
    else                               sgc_rom_bank_mask = 0xff;

    sgc_rom_size = romSize;
    sgc_sst_state = 0;
    sgc_sst_cmd_pos = 0;
    sgc_dirty = 0;
    g_sgm_variant = CVBANK_SGM_VARIANT_OPCODE_SGC;

    superGameCartRestoreFlash();
    banking_apply_boot_mapping();
}

void superGameCartWrite(unsigned int address, BYTE value)
{
    if ((address & 0x1fff) >= 0x1ffc)
    {
        bool updateMap = false;
        switch (address & 0x1fff)
        {
        case 0x1ffc: sgc_bank[1] = value & sgc_rom_bank_mask; updateMap = true; break;
        case 0x1ffd: sgc_bank[2] = value & sgc_rom_bank_mask; updateMap = true; break;
        case 0x1ffe: sgc_bank[3] = value & sgc_rom_bank_mask; updateMap = true; break;
        case 0x1fff: sgc_bank[0] = value & sgc_rom_bank_mask; updateMap = true; break;
        default: break;
        }

        if (updateMap)
        {
            MemoryMap[4] = ROM_Memory + (0x2000 * sgc_bank[0]);
            MemoryMap[5] = ROM_Memory + (0x2000 * sgc_bank[1]);
            MemoryMap[6] = ROM_Memory + (0x2000 * sgc_bank[2]);
            MemoryMap[7] = ROM_Memory + (0x2000 * sgc_bank[3]);
        }
        return;
    }

    enum { SST_NONE = 0, SST_INIT, SST_STATUS, SST_WRITE };

    unsigned int page = (address >> 13) & 0x03;
    unsigned int sst_address = (address & 0x1fff) | ((unsigned int)sgc_bank[page] << 13);
    unsigned int sst_4k_sector = sst_address >> 12;

    if      (value == 0xaa && sst_address == 0x5555 && sgc_sst_cmd_pos == 0) sgc_sst_cmd_pos++;
    else if (value == 0x55 && sst_address == 0x2aaa && sgc_sst_cmd_pos == 1) sgc_sst_cmd_pos++;
    else if (sgc_sst_cmd_pos == 2)
    {
        switch (value)
        {
        case 0x80:
            if (sst_address == 0x5555) sgc_sst_state = SST_INIT;
            break;
        case 0x30:
            if (sgc_sst_state == SST_INIT && sst_4k_sector < 256)
            {
                sgc_sst_state = SST_STATUS;
                memset(ROM_Memory + (4096 * sst_4k_sector), 0xFF, 4096);
                sgc_write_map[sst_4k_sector] = 1;
                sgc_dirty = 1;
                banking_supergamecart_saveflash();
            }
            break;
        case 0xa0:
            if (sst_address == 0x5555) sgc_sst_state = SST_WRITE;
            break;
        }
        sgc_sst_cmd_pos = 0;
    }
    else if (sgc_sst_state == SST_WRITE && sst_address < (unsigned int)sgc_rom_size)
    {
        ROM_Memory[sst_address] = value;
        if (sst_4k_sector < 256) sgc_write_map[sst_4k_sector] = 1;
        sgc_sst_state = SST_NONE;
        sgc_dirty = 1;
        banking_supergamecart_saveflash();
    }
}

BYTE superGameCartRead(unsigned int address)
{
    return *(MemoryMap[address >> 13] + (address & 0x1FFF));
}

void megacart_bankswitch(BYTE bank)
{
    if (!coleco_megacart)
        return;

    if (emulator && emulator->romCartridgeType == ROMCARTRIDGEOPCODE)
        return;

    bank &= coleco_megacart;

    if (coleco_megabank == bank)
        return;

    const unsigned int lastBase = (unsigned int)coleco_megacart * 0x4000;
    const unsigned int bankBase = (unsigned int)bank * 0x4000;

#if DKA_TRACE_BANKS
    if (emulator &&
        emulator->cardcrc == 0x45345709 &&
        coleco_megabank != bank)
    {
        DKA_TRACE_BANK(QString(
                           "[DKA BANK SWITCH] PC=%1 oldBank=%2 newBank=%3 M4=%4 M6_before=%5")
                           .arg(Z80.pc.w.l, 4, 16, QChar('0'))
                           .arg(coleco_megabank)
                           .arg(bank)
                           .arg((MemoryMap[4] >= ROM_Memory &&
                                 MemoryMap[4] < ROM_Memory + MAX_CART_SIZE * 1024)
                                    ? QString::number((quintptr)(MemoryMap[4] - ROM_Memory), 16)
                                    : QString("NOT_ROM"))
                           .arg((MemoryMap[6] >= ROM_Memory &&
                                 MemoryMap[6] < ROM_Memory + MAX_CART_SIZE * 1024)
                                    ? QString::number((quintptr)(MemoryMap[6] - ROM_Memory), 16)
                                    : QString("NOT_ROM")));
    }
#endif

    MemoryMap[4] = ROM_Memory + lastBase;
    MemoryMap[5] = MemoryMap[4] + 0x2000;

    MemoryMap[6] = ROM_Memory + bankBase;
    MemoryMap[7] = MemoryMap[6] + 0x2000;

    coleco_megabank = bank;

#if DKA_TRACE_BANKS
    if (emulator && emulator->cardcrc == 0x45345709)
    {
        DKA_TRACE_BANK(QString(
                           "[DKA BANK ACTIVE] PC=%1 bank=%2 M4=%3 M6=%4")
                           .arg(Z80.pc.w.l, 4, 16, QChar('0'))
                           .arg(coleco_megabank)
                           .arg((quintptr)(MemoryMap[4] - ROM_Memory), 0, 16)
                           .arg((quintptr)(MemoryMap[6] - ROM_Memory), 0, 16));
    }
#endif
}

void banking_reset_state(void)
{
    sgm_enable = 0;
    sgm_firstwrite = 1;
    sgm_low_addr = 0x2000;
    sgm_neverenable = 0;
    coleco_port53 = 0x00;

    coleco_megacart = 0;
    coleco_megasize = 2;
    coleco_megabank = 0;
    memset(sgc_bank, 0, sizeof(sgc_bank));
    sgc_sst_state = 0;
    sgc_sst_cmd_pos = 0;
    sgc_rom_bank_mask = 0;
    sgc_rom_size = 0;
    memset(sgc_write_map, 0, sizeof(sgc_write_map));
    sgc_dirty = 0;
    sgc_rom_path.clear();
    cvbank_sgm_reset_runtime_state();
}

void banking_apply_megacart_reset_mapping(void)
{
    if (!emulator)
        return;

    if (!coleco_megacart)
        return;

    if (emulator->romCartridgeType != ROMCARTRIDGEMEGA)
        return;

    const unsigned int lastBase = (unsigned int)coleco_megacart * 0x4000;

    /*
     * Algemene MegaCart mapping:
     *   $8000-$BFFF = fixed last 16K bank
     *   $C000-$FFFF = switchable bank 0
     */
    MemoryMap[4] = ROM_Memory + lastBase;
    MemoryMap[5] = MemoryMap[4] + 0x2000;

    MemoryMap[6] = ROM_Memory + 0x0000;
    MemoryMap[7] = MemoryMap[6] + 0x2000;

    coleco_megabank = 0;

    qDebug() << "[MEGACART RESET MAP]"
             << "mega=" << coleco_megacart
             << "lastBase=" << QString::number(lastBase, 16)
             << "M4=" << QString::number((quintptr)(MemoryMap[4] - ROM_Memory), 16)
             << "M6=" << QString::number((quintptr)(MemoryMap[6] - ROM_Memory), 16);
}

static inline bool coleco_is_dka2018()
{
    return emulator && emulator->cardcrc == 0x45345709;
}

static inline void coleco_apply_dka2018_mapping()
{
    if (!coleco_is_dka2018())
        return;

    if (emulator->currentMachineType == MACHINEADAM)
        return;

    emulator->SGM = 1;
    emulator->romCartridgeType = ROMCARTRIDGEMEGA;

    coleco_megacart = 0x07;      // 128K = banks 0..7
    coleco_megasize = 8;
    coleco_megabank = 6;

    // $8000-$BFFF = fixed last bank, bank 7
    MemoryMap[4] = ROM_Memory + 0x1C000;
    MemoryMap[5] = ROM_Memory + 0x1E000;

    // $C000-$FFFF = selected boot bank, bank 6
    MemoryMap[6] = ROM_Memory + 0x18000;
    MemoryMap[7] = ROM_Memory + 0x1A000;

    qDebug().noquote() << QString(
                              "[DKA2018 MAP] M4=%1 M6=%2 bank=%3")
                              .arg((quintptr)(MemoryMap[4] - ROM_Memory), 0, 16)
                              .arg((quintptr)(MemoryMap[6] - ROM_Memory), 0, 16)
                              .arg(coleco_megabank);
}

uint32_t CRC32Block(const unsigned char *buf, unsigned int len) {
    uint32_t crc = 0;
    if (buf == 0) return 0L;
    crc = crc ^ 0xffffffffL;
    while (len >= 8) {
        DO8(buf);
        len -= 8;
    }
    if (len) do {
            DO1(buf);
        } while (--len);
    return crc ^ 0xffffffffL;
}
