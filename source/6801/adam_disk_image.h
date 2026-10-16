#ifndef ADAM_DISK_IMAGE_H
#define ADAM_DISK_IMAGE_H

#include "fdidisk.h"
#include <cstdio>
#include <vector>

// ADAM .dsk files are raw 512-byte sectors. Geometry here is only an
// internal container: all OS handlers address linear AdamNet blocks and
// apply the ADAM interleave independently of physical tracks/sides.
namespace AdamDiskImage {
inline int sectorCount(const FDIDisk& disk) {
    return disk.Data && disk.SecSize == 512
        ? disk.Sides * disk.Tracks * disk.Sectors : 0;
}

inline bool load(FDIDisk& disk, const char* path) {
    if (!path || !*path) return false;
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    if (std::fseek(file, 0, SEEK_END)) { std::fclose(file); return false; }
    const long size = std::ftell(file);
    // Whole ADAM interleave groups. Physical dimensions are only storage
    // metadata, but must fit the FDI track IDs and 16-bit directory offset.
    if (size <= 0 || size % 4096 || size > 2880L * 1024
        || std::fseek(file, 0, SEEK_SET)) {
        std::fclose(file); return false;
    }
    FDIDisk candidate;
    InitFDI(&candidate);
    byte* allocated = nullptr;
    for (int sides = 1; sides <= 2 && !allocated; sides *= 2)
        for (int tracks = 40; tracks <= 80 && !allocated; tracks *= 2)
            for (int sectors = 8; sectors <= 36 && !allocated; ++sectors)
                if (size == long(sides) * tracks * sectors * 512)
                    allocated = NewFDI(&candidate, sides, tracks, sectors, 512);
    if (!allocated) {
        std::fclose(file); return false;
    }
    bool ok = true;
    for (int sector = 0; sector < size / 512; ++sector) {
        byte* data = LinearFDI(&candidate, sector);
        if (!data || std::fread(data, 1, 512, file) != 512) {
            ok = false; break;
        }
    }
    if (std::fclose(file)) ok = false;
    if (!ok) { EjectFDI(&candidate); return false; }
    candidate.Format = FMT_DSK;
    EjectFDI(&disk);
    disk = candidate;
    return true;
}

inline bool rawBytes(FDIDisk& disk, std::vector<byte>& bytes) {
    const int sectors = sectorCount(disk);
    if (!sectors) return false;
    bytes.clear();
    bytes.reserve(size_t(sectors) * 512);
    for (int sector = 0; sector < sectors; ++sector) {
        const byte* data = LinearFDI(&disk, sector);
        if (!data) { bytes.clear(); return false; }
        bytes.insert(bytes.end(), data, data + 512);
    }
    return true;
}
}
#endif
