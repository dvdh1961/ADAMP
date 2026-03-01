#pragma once
#include <cstdint>
#include <cstddef>

// CP/M-only sector cache for FDI-backed disks.
// Designed to be used ONLY by adamnet_cpm.cpp.

struct FDIDisk; // forward decl (whatever your type is)

// LinearFDI is implemented in C (fdidisk.c). Ensure C linkage when used from C++.
#ifdef __cplusplus
extern "C" {
#endif
unsigned char* LinearFDI(struct FDIDisk* D, unsigned int Sector);
#ifdef __cplusplus
}
#endif

namespace cpmcache {

// Read one 512-byte physical sector into dst (must be 512 bytes)
bool read_sector(FDIDisk* disk, uint8_t diskN, uint32_t physSector, uint8_t* dst512);

// Write one 512-byte physical sector from src (must be 512 bytes). Write-back cached.
bool write_sector(FDIDisk* disk, uint8_t diskN, uint32_t physSector, const uint8_t* src512);

// Flush all dirty sectors for diskN (or all if diskN == 0xFF)
void flush(uint8_t diskN = 0xFF);

// Optional: clear cache entries for diskN (or all if diskN == 0xFF)
void clear(uint8_t diskN = 0xFF);

} // namespace cpmcache
