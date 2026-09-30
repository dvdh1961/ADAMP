#include "6801/adnet_mcu2_fujinet.h"

#include "6801/adnet_mcu2.h"

void UpdateFUJINET_MCU2_EOS(byte Dev, int command)
{
    UpdateMCU2CharacterEOS(Dev, command, "FujiNet");
}
