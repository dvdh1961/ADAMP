#include <cstdio>
#include <cstring>
#include <cstdint>

#include "cvstate.h"
#include "cv.h"
#include "cvbank.h"
#include "z80.h"
#include "tms9928a.h"
#include "psg_bridge.h"
#include "snd_ay8910.h"

extern BYTE coleco_joymode;

extern "C" {
int* sn76489_get_regs();
unsigned char* ay8910_get_regs();
void sn76489_restore_reg(int r, uint8_t val);
void ay8910_set_reg(int reg, uint8_t val);
}

BYTE coleco_savestate(char *filename)
{
    BYTE stateheader[25] = "adamp state\032\1\0\0\0\0\0\0\0\0\0";
    FILE *fstatefile = NULL;

    DWORD crc = emulator->cardcrc;
    stateheader[18] = crc & 0xFF; stateheader[19] = (crc>>8)&0xFF;
    stateheader[20] = (crc>>16)&0xFF; stateheader[21] = (crc>>24)&0xFF;

    fstatefile = fopen(filename,"wb");
    if(!fstatefile) return 0;

    if (fwrite(stateheader, 1, 24, fstatefile) != 24) { fclose(fstatefile); return 0; }

    fwrite(&coleco_megasize, sizeof(coleco_megasize), 1, fstatefile);
    fwrite(&coleco_megacart, sizeof(coleco_megacart), 1, fstatefile);
    fwrite(&coleco_megabank, sizeof(coleco_megabank), 1, fstatefile);
    fwrite(sgc_bank, sizeof(sgc_bank), 1, fstatefile);
    fwrite(&sgc_sst_state, sizeof(sgc_sst_state), 1, fstatefile);
    fwrite(&sgc_sst_cmd_pos, sizeof(sgc_sst_cmd_pos), 1, fstatefile);
    fwrite(&coleco_port20, sizeof(coleco_port20), 1, fstatefile);
    fwrite(&coleco_port60, sizeof(coleco_port60), 1, fstatefile);
    fwrite(&coleco_port53, sizeof(coleco_port53), 1, fstatefile);
    fwrite(&coleco_joymode, sizeof(coleco_joymode), 1, fstatefile);
    fwrite(&coleco_joystat, sizeof(coleco_joystat), 1, fstatefile);

    fwrite(&Z80, sizeof(Z80), 1, fstatefile);
    fwrite(&tms, sizeof(tms), 1, fstatefile);

    uint8_t snd_ver = 2;
    fwrite(&snd_ver, 1, 1, fstatefile);

    int clk = 3579545;
    int sr  = 44100;
    int sgm = emulator->SGM ? 1 : 0;

    fwrite(&clk, sizeof(clk), 1, fstatefile);
    fwrite(&sr,  sizeof(sr),  1, fstatefile);
    fwrite(&sgm, sizeof(sgm), 1, fstatefile);

    int* sn_regs_ptr = (int*)sn76489_get_regs();
    for(int i = 0; i < 8; i++) {
        uint8_t b = (uint8_t)(sn_regs_ptr[i] & 0xFF);
        fwrite(&b, 1, 1, fstatefile);
    }
    fwrite(ay8910_get_regs(), 1, 16, fstatefile);

    fwrite(RAM_Memory, 1, MAX_RAM_SIZE*1024, fstatefile);
    fwrite(SRAM_Memory, 1, MAX_EEPROM_SIZE*1024, fstatefile);
    fwrite(VDP_Memory, 1, 0x10000, fstatefile);

    if (emulator->romCartridgeType == ROMCARTRIDGEOPCODE) banking_supergamecart_saveflash();
    fclose(fstatefile);
    return 1;
}

static inline bool fread_one(FILE* f, void* dst, size_t elemsz)
{
    return std::fread(dst, elemsz, 1, f) == 1;
}

BYTE coleco_loadstate(char *filename)
{
    BYTE stateheader[24];
    FILE *fstatefile = NULL;

    fstatefile = fopen(filename,"rb");
    if(!fstatefile) return 0;

    if (fread(stateheader, 1, 24, fstatefile) != 24) { fclose(fstatefile); return 0; }
    if (memcmp(stateheader,"adamp state\032\1\0\0",17) != 0) { fclose(fstatefile); return 0; }

    if (!fread_one(fstatefile, &coleco_megasize, sizeof(coleco_megasize))) { std::fclose(fstatefile); return 0; }
    if (!fread_one(fstatefile, &coleco_megacart, sizeof(coleco_megacart))) { std::fclose(fstatefile); return 0; }
    if (!fread_one(fstatefile, &coleco_megabank, sizeof(coleco_megabank))) { std::fclose(fstatefile); return 0; }
    if (std::fread(sgc_bank, sizeof(sgc_bank), 1, fstatefile) != 1) { std::fclose(fstatefile); return 0; }
    if (!fread_one(fstatefile, &sgc_sst_state, sizeof(sgc_sst_state))) { std::fclose(fstatefile); return 0; }
    if (!fread_one(fstatefile, &sgc_sst_cmd_pos, sizeof(sgc_sst_cmd_pos))) { std::fclose(fstatefile); return 0; }
    if (!fread_one(fstatefile, &coleco_port20, sizeof(coleco_port20))) { std::fclose(fstatefile); return 0; }
    if (!fread_one(fstatefile, &coleco_port60, sizeof(coleco_port60))) { std::fclose(fstatefile); return 0; }
    if (!fread_one(fstatefile, &coleco_port53, sizeof(coleco_port53))) { std::fclose(fstatefile); return 0; }
    if (!fread_one(fstatefile, &coleco_joymode, sizeof(coleco_joymode))) { std::fclose(fstatefile); return 0; }
    if (!fread_one(fstatefile, &coleco_joystat, sizeof(coleco_joystat))) { std::fclose(fstatefile); return 0; }

    z80_init();
    if (!fread_one(fstatefile, &Z80, sizeof(Z80))) { std::fclose(fstatefile); return 0; }
    if (!fread_one(fstatefile, &tms, sizeof(tms))) { std::fclose(fstatefile); return 0; }

    uint8_t snd_ver = 0;
    if (fread(&snd_ver, 1, 1, fstatefile) != 1) { fclose(fstatefile); return 0; }

    int clk = 0, sr = 0, sgm = 0;
    if (fread(&clk, sizeof(clk), 1, fstatefile) != 1) { fclose(fstatefile); return 0; }
    if (fread(&sr,  sizeof(sr),  1, fstatefile) != 1) { fclose(fstatefile); return 0; }
    if (fread(&sgm, sizeof(sgm), 1, fstatefile) != 1) { fclose(fstatefile); return 0; }

    if (snd_ver >= 2) {
        uint8_t loaded_sn[8];
        uint8_t loaded_ay[16];
        fread(loaded_sn, 1, 8, fstatefile);
        fread(loaded_ay, 1, 16, fstatefile);

        sn76489_init(clk, sr);
        for(int i = 0; i < 8; i++) sn76489_restore_reg(i, loaded_sn[i]);

        ay8910_init(clk, sr);
        for(int i = 0; i < 16; i++) ay8910_set_reg(i, loaded_ay[i]);
    } else {
        sn76489_init(3579545, 44100);
        if (sgm) ay8910_init(3579545, 44100);
    }
    emulator->SGM = sgm;

    fread(RAM_Memory, 1, MAX_RAM_SIZE*1024, fstatefile);
    fread(SRAM_Memory, 1, MAX_EEPROM_SIZE*1024, fstatefile);
    fread(VDP_Memory, 1, 0x10000, fstatefile);

    if (emulator->currentMachineType == MACHINEADAM) coleco_setadammemory(false);
    else coleco_setupsgm();

    if (emulator->romCartridgeType == ROMCARTRIDGEOPCODE) {
        MemoryMap[4] = ROM_Memory + (0x2000 * sgc_bank[0]);
        MemoryMap[5] = ROM_Memory + (0x2000 * sgc_bank[1]);
        MemoryMap[6] = ROM_Memory + (0x2000 * sgc_bank[2]);
        MemoryMap[7] = ROM_Memory + (0x2000 * sgc_bank[3]);
    } else if (coleco_megacart) {
        megacart_bankswitch(coleco_megabank);
    }

    fclose(fstatefile);
    return 1;
}
