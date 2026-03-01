#include "adamnet.h"
#include "coleco.h"
#include <cstdio>
#include <QDebug>
#include <cstring>
#include <vector>

// -----------------------------------------------------------------------------
// CP/M ONLY: Isolated I/O fixes (disk/tape caching + safe DMA flush)
// EOS / T-DOS codepaths are untouched.
// -----------------------------------------------------------------------------

bool isFormatting = false;
static const byte interleave8[8] = { 0, 5, 2, 7, 4, 1, 6, 3 };

// NOTE: These are declared elsewhere in your project (as before).
extern word  savedBUF;
extern std::atomic_bool g_diskSoundActive;
extern std::atomic_bool g_tapeSoundActive;

// -----------------------------------------------------------------------------
// CP/M-only: sector cache (512B, write-back, tiny LRU).
// Works around LinearFDI() returning non-stable sector pointers (FDI decode buffers).
// -----------------------------------------------------------------------------
struct CPM_SectorKey {
    byte devType; // 0=disk, 1=tape
    byte unit;    // N
    uint32_t phys;
};

struct CPM_SectorEntry {
    byte data[512];
    bool dirty = false;
    uint32_t age = 0;
};

static std::vector<CPM_SectorKey>   g_cpm_keys;
static std::vector<CPM_SectorEntry> g_cpm_entries;
static uint32_t g_cpm_age = 0;
static const size_t CPM_MAX_SECTORS = 256; // 128KB

static int cpm_find(byte devType, byte unit, uint32_t phys)
{
    for (size_t i = 0; i < g_cpm_keys.size(); ++i) {
        const auto& k = g_cpm_keys[i];
        if (k.devType == devType && k.unit == unit && k.phys == phys)
            return (int)i;
    }
    return -1;
}

static byte* cpm_linear_ptr(byte devType, byte unit, uint32_t phys)
{
    if (devType == 0) return LinearFDI(&Disks[unit], phys);
    return LinearFDI(&Tapes[unit], phys);
}

static void cpm_flush_one(int idx)
{
    if (idx < 0 || (size_t)idx >= g_cpm_entries.size()) return;
    if (!g_cpm_entries[idx].dirty) return;

    const auto& k = g_cpm_keys[(size_t)idx];
    byte* dst = cpm_linear_ptr(k.devType, k.unit, k.phys);
    if (dst) memcpy(dst, g_cpm_entries[(size_t)idx].data, 512);

    g_cpm_entries[(size_t)idx].dirty = false;
}

static void cpm_evict_if_needed()
{
    if (g_cpm_keys.size() < CPM_MAX_SECTORS) return;

    size_t victim = 0;
    for (size_t i = 1; i < g_cpm_entries.size(); ++i)
        if (g_cpm_entries[i].age < g_cpm_entries[victim].age)
            victim = i;

    cpm_flush_one((int)victim);
    g_cpm_keys.erase(g_cpm_keys.begin() + (ptrdiff_t)victim);
    g_cpm_entries.erase(g_cpm_entries.begin() + (ptrdiff_t)victim);
}

static byte* cpm_get_sector(byte devType, byte unit, uint32_t phys, bool forWrite)
{
    int idx = cpm_find(devType, unit, phys);
    if (idx < 0) {
        cpm_evict_if_needed();

        CPM_SectorKey k{devType, unit, phys};
        CPM_SectorEntry e;
        byte* src = cpm_linear_ptr(devType, unit, phys);
        if (!src) return nullptr;
        memcpy(e.data, src, 512);
        e.dirty = false;
        e.age = ++g_cpm_age;

        g_cpm_keys.push_back(k);
        g_cpm_entries.push_back(e);
        idx = (int)g_cpm_entries.size() - 1;
    }

    g_cpm_entries[(size_t)idx].age = ++g_cpm_age;
    if (forWrite) g_cpm_entries[(size_t)idx].dirty = true;
    return g_cpm_entries[(size_t)idx].data;
}

static void cpm_flush_unit(byte devType, byte unit)
{
    for (size_t i = 0; i < g_cpm_keys.size(); ++i) {
        const auto& k = g_cpm_keys[i];
        if (k.devType == devType && k.unit == unit)
            cpm_flush_one((int)i);
    }
}

// -----------------------------------------------------------------------------
// CP/M-only: DMA flush into Z80 RAM (guarded)
// -----------------------------------------------------------------------------
static inline bool cpm_dma_ok(word buf, int len)
{
    if (len <= 0 || len > 0x0400) return false;

    const uint32_t start = (uint32_t)buf;
    const uint32_t end   = start + (uint32_t)len;

    if (end < start) return false;        // wrap
    if (start < 0x0080) return false;     // avoid zero page/BDOS area corruption

    // Protect PCB/DCB table region (typically at 0xFEC0..)
    const word pcbBase = GetPCBBase();    // you already have this in adamnet.h
    const byte maxDCB  = GetPCB(PCB_MAX_DCB);
    const uint32_t pcbStart = (uint32_t)pcbBase;
    const uint32_t pcbEnd   = pcbStart + PCB_SIZE + (uint32_t)maxDCB * DCB_SIZE;

    // Block DMA writes that touch PCB/DCB memory (prevents “memory flood” + DCB corruption)
    if (!(end <= pcbStart || start >= pcbEnd)) return false;

    return true;
}

void AdamFlushCache_CPM()
{
    if (!cpm_dma_ok(savedBUF, savedLEN)) {
        qDebug() << "[CPM-DMA] BLOCKED flush to"
                 << QString("0x%1").arg(savedBUF,4,16,QChar('0'))
                 << "len=" << savedLEN;
        return;
    }

    word dst = savedBUF;
    for (int i = 0; i < savedLEN; ++i)
        RAM_Memory[(word)(dst + i)] = HoldingBuf[i];
}

// -----------------------------------------------------------------------------
// CP/M DISK (isolated)
// -----------------------------------------------------------------------------
void UpdateDSK_CPM(byte N, byte Dev, int V)
{
    int I, LEN, SEC;
    int byte_idx;
    int K;
    byte *Data;
    uint32_t block;

    if (N >= MAX_DISKS) return;

    // CP/M ONLY: only block READ/WRITE while busy.
    // Allow STATUS and other control commands to pass.
    if (io_busy > 0 && (V == CMD_READ || V == CMD_WRITE)) {
        SetDCB(Dev, DCB_CMD_STAT, 0x00); // still busy
        return;
    }

    static byte active_disk = 0xFF;

    // ---------------------------------------------------------------------
    // Polling / busy  (CP/M only)
    // ---------------------------------------------------------------------
    if (V < 0)
    {
        // Controller/status poll: always service the currently active disk (if any)
        byte polled = (active_disk != 0xFF) ? active_disk : N;

        if (active_disk != 0xFF && io_busy > 0) {
            io_busy--;
            if (io_busy == 0) {
                // Complete I/O for the active disk
                if (last_command_read) {
                    last_command_read = false;
                    AdamFlushCache_CPM();     // DMA -> RAM
                } else {
                    cpm_flush_unit(0, polled); // 0 = disk: commit cached writes
                }

                active_disk = 0xFF;
                SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS); // DONE
            } else {
                SetDCB(Dev, DCB_CMD_STAT, 0x00);       // BUSY
            }
            return;
        }

        // Not busy: always ready
        SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        return;
    }
    // ---------------------------------------------------------------------
    // Command handling
    // ---------------------------------------------------------------------
    switch (V) {
    case CMD_STATUS:
        io_busy = 0;
        ReportDevice(Dev, 0x0400, 1);
        SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        break;

    case CMD_SOFT_RESET:
        io_busy = 0;
        // CP/M-only: flush any dirty cache for this disk before reset
        cpm_flush_unit(0, N);
        SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        break;

    case CMD_READ:
    case CMD_WRITE:
        // reset per-command state (prevents stale flush overwriting RAM)
        last_command_read = false;
        savedLEN = 0;
        savedBUF = 0;

        g_diskSoundActive.store(true, std::memory_order_relaxed);
        io_show_status = (V == CMD_READ) ? 1 : 2;

        SetDCB(Dev, DCB_CMD_STAT, 0x00);

        // CP/M ONLY: clear previous error latch (otherwise BIOS retries forever)
        SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) & (byte)~0x02);

        active_disk = N;
        io_busy = DELAY_IO * 100;

        if (!Disks[N].Data) {
            qDebug() << "[CPM-DSK] No disk data! N=" << N;
            // signal "not ready" via node type bit 0x02 (same pattern as tape code)
            SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x02);
            break;
        }

        {
            word BUF = GetDCBBase(Dev);
            LEN = GetDCBLen(Dev);
            LEN = (LEN < 0x0400) ? LEN : 0x0400;
            SEC = GetDCBSector(Dev);

            // Guard: never allow CP/M DMA to destroy BIOS area
            if (!cpm_dma_ok(BUF, LEN)) {
                qDebug() << "[CPM-DSK] BAD DMA BUF"
                         << QString("0x%1").arg(BUF,4,16,QChar('0'))
                         << "LEN=" << LEN << "SEC=" << SEC;
                SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x02);
                SetDCB(Dev, DCB_SEC_LO, 0);
                SetDCB(Dev, DCB_SEC_HI, 0);
                // Complete quickly
                io_busy = 1;
                break;
            }

            savedBUF = BUF;
            savedLEN = LEN;

            // CP/M block number -> 512B sector index
            block = (uint32_t)SEC * 2;

            for (I = 0; I < LEN; I += 512) {
                uint32_t physical_sector;

                // Interleave ONLY for known 8-sector layout.
                // If your disk images aren't 8 sectors/track, applying this will read garbage.
                if (Disks[N].Sectors == 8) {
                    physical_sector = (block & (~7u)) | (uint32_t)interleave8[block & 7u];
                } else {
                    physical_sector = block; // linear
                }

                K = (I + 512 > LEN) ? (LEN - I) : 512;

                if (V == CMD_READ) {
                    Data = cpm_get_sector(0, N, physical_sector, false);
                    if (!Data) {
                        SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x02);
                        break;
                    }
                    last_command_read = true;
                    memcpy(&HoldingBuf[I], Data, (size_t)K);
                } else {
                    Data = cpm_get_sector(0, N, physical_sector, true);
                    if (!Data) {
                        SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x02);
                        break;
                    }
                    // write from Z80 RAM into cache sector
                    for (byte_idx = 0; byte_idx < K; ++byte_idx) {
                        Data[byte_idx] = RAM_Memory[(word)(BUF + byte_idx)];
                    }
                }

                BUF += (word)K;
                block++;
            }
        }
        break;
    }
}

// -----------------------------------------------------------------------------
// CP/M TAPE (isolated)
// -----------------------------------------------------------------------------
void UpdateTAP_CPM(byte N, byte Dev, int V)
{
    int I, J, K, LEN, SEC;
    word BUF;
    byte *Data;

    static byte active_tape = 0xFF;

    // Polling / busy
    if (V < 0) {
        if (active_tape == N && io_busy > 0) {
            io_busy--;
            if (io_busy == 0) {
                if (last_command_read) {
                    last_command_read = false;
                    AdamFlushCache_CPM(); // CP/M-only flush
                } else {
                    cpm_flush_unit(1, N); // commit tape writes
                }
                active_tape = 0xFF;
                SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
            } else {
                SetDCB(Dev, DCB_CMD_STAT, 0x00);
            }
        }
        return;
    }

    // Reset errors, report missing tapes
    SetDCB(Dev, DCB_NODE_TYPE,
           (Tapes[N & 2].Data ? 0x00 : 0x03) | (Tapes[(N & 2) + 1].Data ? 0x00 : 0x30));

    switch (V) {
    case CMD_STATUS:
        ReportDevice(Dev, 0x0400, 1);
        break;

    case CMD_SOFT_RESET:
        io_busy = 0;
        cpm_flush_unit(1, N);
        SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        break;

    case CMD_WRITE:
    case CMD_READ:
        last_command_read = false;
        savedLEN = 0;
        savedBUF = 0;

        g_tapeSoundActive.store(true, std::memory_order_relaxed);
        io_show_status = (V == CMD_READ) ? 1 : 2;

        SetDCB(Dev, DCB_CMD_STAT, 0x00);
        active_tape = N;
        io_busy = DELAY_IO;

        if (!Tapes[N].Data) {
            SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x02);
            break;
        }

        BUF = GetDCBBase(Dev);
        LEN = GetDCBLen(Dev);
        LEN = LEN < 0x0400 ? LEN : 0x0400;
        SEC = GetDCBSector(Dev);

        if (!cpm_dma_ok(BUF, LEN)) {
            qDebug() << "[CPM-TAP] BAD DMA BUF"
                     << QString("0x%1").arg(BUF,4,16,QChar('0'))
                     << "LEN=" << LEN << "SEC=" << SEC;
            SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x02);
            SetDCB(Dev, DCB_SEC_LO, 0);
            SetDCB(Dev, DCB_SEC_HI, 0);
            io_busy = 1;
            break;
        }

        savedBUF = BUF;
        savedLEN = LEN;

        // For each 512B sector... (SEC is 1k blocks -> *2)
        for (I = 0, SEC <<= 1; I < LEN; ++SEC, I += 0x200) {
            K = (I + 0x200 > LEN) ? (LEN - I) : 0x200;

            if (V == CMD_READ) {
                Data = cpm_get_sector(1, N, (uint32_t)SEC, false);
                if (!Data) {
                    SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x02);
                    SetDCB(Dev, DCB_SEC_LO, 0);
                    SetDCB(Dev, DCB_SEC_HI, 0);
                    break;
                }
                last_command_read = true;
                memcpy(&HoldingBuf[I], Data, (size_t)K);
            } else {
                Data = cpm_get_sector(1, N, (uint32_t)SEC, true);
                if (!Data) {
                    SetDCB(Dev, DCB_NODE_TYPE, GetDCB(Dev, DCB_NODE_TYPE) | 0x02);
                    SetDCB(Dev, DCB_SEC_LO, 0);
                    SetDCB(Dev, DCB_SEC_HI, 0);
                    break;
                }
                for (J = 0; J < K; ++J) {
                    Data[J] = RAM_Memory[(word)(BUF + J)];
                }
            }

            BUF += (word)K;
        }
        break;
    }
}

void UpdateDCB_CPM(byte Dev, int V)
{

    byte DevID;

    if(!V) return;

    DevID = (GetDCB(Dev,DCB_DEV_NUM)<<4) + (GetDCB(Dev,DCB_ADD_CODE)&0x0F);


    switch(DevID)
    {
    case 0x01: UpdateKBD(Dev,V);break;
    case 0x02: UpdatePRN(Dev,V);break;
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: UpdateDSK_CPM(DiskID=DevID-4,Dev,V);break;
    case 0x08:
    case 0x09:
    case 0x18:
    case 0x19: UpdateTAP_CPM((DevID>>4)+((DevID&1)<<1),Dev,V);break;
    case 0x52: UpdateDSK_CPM(DiskID,Dev,-2);break;
    default:
        SetDCB(Dev,DCB_CMD_STAT,RSP_ACK+0x0B);
        break;
    }
}
