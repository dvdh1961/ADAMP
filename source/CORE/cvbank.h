#ifndef CVBANK_H
#define CVBANK_H

#include "emu.h"
#include "z80.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CvBankSgmVariant
{
    CVBANK_SGM_VARIANT_NONE = 0,
    CVBANK_SGM_VARIANT_DISABLED,
    CVBANK_SGM_VARIANT_STANDARD_RAM,
    CVBANK_SGM_VARIANT_OPCODE_SGC,
    CVBANK_SGM_VARIANT_DISABLED_BY_CART,
    CVBANK_SGM_VARIANT_ADAM_MODE
} CvBankSgmVariant;

extern int  coleco_mega_layout;


extern BYTE sgc_bank[4];
extern BYTE sgc_sst_state;
extern BYTE sgc_sst_cmd_pos;
extern BYTE sgc_rom_bank_mask;
extern int  sgc_rom_size;
extern BYTE sgc_write_map[256];
extern BYTE sgc_dirty;

BYTE coleco_loadcart(char *filename);
void megacart_bankswitch(BYTE bank);

void banking_apply_boot_mapping(void);
void banking_supergamecart_saveflash(void);
void banking_reset_state(void);

CvBankSgmVariant cvbank_sgm_variant(void);
const char* cvbank_sgm_variant_name(CvBankSgmVariant variant);
void cvbank_sgm_reset_runtime_state(void);
void cvbank_sgm_init_ports_for_current_machine(void);
void cvbank_sgm_set_forced_disabled(int disabled);
void cvbank_sgm_apply_mapping(void);
int  cvbank_sgm_write_ram(unsigned int address, int data);
void cvbank_sgm_write_control_port(BYTE data);
void cvbank_sgm_write_memory_port(BYTE data);
void banking_apply_megacart_reset_mapping(void);

void superGameCartWrite(unsigned int address, BYTE value);
BYTE superGameCartRead(unsigned int address);

uint32_t CRC32Block(const unsigned char *buf, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif
