#ifndef ADNET_MCU2_H
#define ADNET_MCU2_H

#include "6801/adnet_core.h"
#include <QByteArray>
#include <QString>
#include <functional>

using Mcu2FujiBootInterceptor =
    std::function<int(const QByteArray &headerBlocks, const QString &mountedPath)>;
using Mcu2FujiColecoRomReady = std::function<void(const QString &romPath)>;
using Mcu2FujiColecoRomProgress =
    std::function<void(qint64 loadedBytes, qint64 totalBytes, bool finished)>;
using Mcu2FujiDirectRomFetch =
    std::function<QString(const QString &host, const QString &path)>;

/* Select the physical MCU2 backend for Disk 1 (address 4) and FujiNet (15). */
void mcu2_disk_set_enabled(bool enabled);
bool mcu2_disk_is_enabled(void);
void mcu2_block_set_device_enabled(byte deviceId, bool enabled);
bool mcu2_block_device_is_enabled(byte deviceId);
void mcu2_set_fuji_boot_interceptor(Mcu2FujiBootInterceptor interceptor);
void mcu2_set_fuji_coleco_rom_ready_handler(Mcu2FujiColecoRomReady handler);
void mcu2_set_fuji_coleco_rom_progress_handler(
    Mcu2FujiColecoRomProgress handler);
void mcu2_set_fuji_direct_rom_enabled(bool enabled);
void mcu2_set_fuji_direct_rom_fetch_handler(Mcu2FujiDirectRomFetch handler);

/* Cancel the emulator-visible side of a pending transfer after PCB reset. */
void mcu2_disk_reset(void);
/* Allow exactly one retained-D5 inspection on the next PCB reset. */
void mcu2_arm_fuji_reset_boot_probe(void);
/* Run the retained-media choice/D9 transition before the core BIOS reset. */
bool mcu2_prepare_fuji_reset_boot(void);
bool mcu2_fuji_reset_boot_probe_is_armed(void);
int mcu2_take_completed_fuji_boot_profile(void);

/* Handle one EOS Disk 1 DCB command or status poll. */
void UpdateDSK_MCU2_EOS(byte Dev, int command);
/* CP/M compatibility read for temporary loader DCBs whose identity/maximum
 * length fields are not a valid physical ADAMNet disk descriptor. */
void UpdateDSK_MCU2_CPM_D5(byte Dev, int command);

/* Execute STATUS for a non-disk physical ADAMnet device through the same
 * serialized MCU2 queue used by Disk 1.
 */
void UpdateMCU2CharacterEOS(byte Dev, int command, const char *deviceName);

#endif // ADNET_MCU2_H
