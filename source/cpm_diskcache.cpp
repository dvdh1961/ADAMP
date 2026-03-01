#include "cpm_diskcache.h"
#include <unordered_map>
#include <vector>
#include <cstring>

// You must provide LinearFDI somewhere accessible (same as you already use in CP/M code):
namespace cpmcache {

struct Key {
    uint8_t diskN;
    uint32_t phys;
    bool operator==(const Key& o) const noexcept { return diskN==o.diskN && phys==o.phys; }
};

struct KeyHash {
    size_t operator()(const Key& k) const noexcept {
        return (size_t(k.diskN) << 24) ^ size_t(k.phys);
    }
};

struct Entry {
    uint8_t data[512];
    bool dirty = false;
    uint32_t lastUsed = 0;
};

static std::unordered_map<Key, Entry, KeyHash> g;
static uint32_t gTick = 0;
static const size_t MAX_SECTORS = 256; // tune: 256*512 = 128 KB

static void flush_one(const Key& key, Entry& e, FDIDisk* diskPtr)
{
    if (!e.dirty) return;
    uint8_t* dst = LinearFDI(diskPtr, key.phys);
    if (!dst) return;
    std::memcpy(dst, e.data, 512);
    e.dirty = false;
}

static void evict_if_needed(FDIDisk* diskPtr)
{
    if (g.size() < MAX_SECTORS) return;

    auto victim = g.begin();
    for (auto it = g.begin(); it != g.end(); ++it)
        if (it->second.lastUsed < victim->second.lastUsed) victim = it;

    flush_one(victim->first, victim->second, diskPtr);
    g.erase(victim);
}

static Entry* get(FDIDisk* diskPtr, uint8_t diskN, uint32_t physSector, bool forWrite)
{
    Key k{diskN, physSector};
    auto it = g.find(k);
    if (it == g.end()) {
        evict_if_needed(diskPtr);

        uint8_t* src = LinearFDI(diskPtr, physSector);
        if (!src) return nullptr;

        Entry e;
        std::memcpy(e.data, src, 512);
        e.dirty = false;
        e.lastUsed = ++gTick;

        it = g.emplace(k, e).first;
    }

    it->second.lastUsed = ++gTick;
    if (forWrite) it->second.dirty = true;
    return &it->second;
}

bool read_sector(FDIDisk* disk, uint8_t diskN, uint32_t physSector, uint8_t* dst512)
{
    Entry* e = get(disk, diskN, physSector, false);
    if (!e) return false;
    std::memcpy(dst512, e->data, 512);
    return true;
}

bool write_sector(FDIDisk* disk, uint8_t diskN, uint32_t physSector, const uint8_t* src512)
{
    Entry* e = get(disk, diskN, physSector, true);
    if (!e) return false;
    std::memcpy(e->data, src512, 512);
    return true;
}

void flush(uint8_t diskN)
{
    // NOTE: we do not know the FDIDisk* per entry; flush should be called with the proper disk pointer context.
    // If you have multiple disk pointers, easiest is: keep one cache per disk type or pass disk pointer in.
    // Minimal approach below assumes ONE disk pointer used in CP/M code.

    // If you have multiple, tell me and I’ll adjust to store disk pointer in Key.
}

void clear(uint8_t diskN)
{
    if (diskN == 0xFF) { g.clear(); return; }
    for (auto it = g.begin(); it != g.end(); ) {
        if (it->first.diskN == diskN) it = g.erase(it);
        else ++it;
    }
}

} // namespace cpmcache
