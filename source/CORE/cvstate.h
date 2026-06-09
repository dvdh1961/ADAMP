#ifndef CVSTATE_H
#define CVSTATE_H

#include "emu.h"

#ifdef __cplusplus
extern "C" {
#endif

BYTE coleco_savestate(char *filename);
BYTE coleco_loadstate(char *filename);

#ifdef __cplusplus
}
#endif

#endif
