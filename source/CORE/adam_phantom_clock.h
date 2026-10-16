#ifndef ADAMP_ADAM_PHANTOM_CLOCK_H
#define ADAMP_ADAM_PHANTOM_CLOCK_H

#include <chrono>
#include <ctime>
#include <cstdint>
#include <cstring>

// ADAM ROM-socket phantom clock. Serial bus: reads at 0/1 send A0,
// reads at 4 receive D0. All other reads retain normal memory semantics.
// Standard Dallas key plus the extended bank used by ADAM CP/M BIOS v72.
class AdamPhantomClock {
    unsigned match_ = 0, bit_ = 0;
    bool blocked_ = false, active_ = false, extended_ = false;
    bool written_ = false;
    uint8_t regs_[8] = {}, pending_[8] = {};
    std::chrono::system_clock::duration offset_{};
    static unsigned bcd(unsigned n) { return (n / 10) * 16 + n % 10; }
    static unsigned decimal(unsigned n) { return (n >> 4) * 10 + (n & 15); }
    void snapshot() {
        const auto now = std::chrono::system_clock::now() + offset_;
        const std::time_t value = std::chrono::system_clock::to_time_t(now);
        std::tm t{};
#ifdef _WIN32
        localtime_s(&t, &value);
#else
        localtime_r(&value, &t);
#endif
        regs_[0] = uint8_t(bcd(unsigned(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000) / 10));
        regs_[1] = uint8_t(bcd(t.tm_sec));
        regs_[2] = uint8_t(bcd(t.tm_min));
        regs_[3] = uint8_t(bcd(t.tm_hour));
        regs_[4] = uint8_t(t.tm_wday + 1);
        regs_[5] = uint8_t(bcd(t.tm_mday));
        regs_[6] = uint8_t(bcd(t.tm_mon + 1));
        regs_[7] = uint8_t(bcd((t.tm_year + 1900) % 100));
        // v72's extended key selects a command stream; command 32h reads
        // the BCD century in the following byte. This is an ADAM BIOS
        // compatibility extension, not a register of the basic DS1216.
        if (extended_) {
            std::memset(regs_, 0, sizeof(regs_));
            regs_[1] = uint8_t(bcd((t.tm_year + 1900) / 100));
        }
        std::memcpy(pending_, regs_, sizeof(regs_));
    }
    void commit() {
        if (!written_ || extended_) return;
        const auto now = std::chrono::system_clock::now();
        const std::time_t current = std::chrono::system_clock::to_time_t(now + offset_);
        std::tm t{};
#ifdef _WIN32
        localtime_s(&t, &current);
#else
        localtime_r(&current, &t);
#endif
        unsigned hour = decimal(pending_[3] & 0x3f);
        if (pending_[3] & 0x80) {
            hour = decimal(pending_[3] & 0x1f);
            if (hour < 1 || hour > 12) return;
            hour = hour % 12 + ((pending_[3] & 0x20) ? 12 : 0);
        }
        const unsigned sec = decimal(pending_[1] & 0x7f);
        const unsigned min = decimal(pending_[2] & 0x7f);
        const unsigned day = decimal(pending_[5] & 0x3f);
        const unsigned month = decimal(pending_[6] & 0x1f);
        const unsigned year = decimal(pending_[7]);
        if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12 || year > 99) return;
        t.tm_sec = int(sec); t.tm_min = int(min); t.tm_hour = int(hour);
        t.tm_mday = int(day); t.tm_mon = int(month) - 1;
        t.tm_year = ((t.tm_year + 1900) / 100) * 100 + int(year) - 1900;
        t.tm_isdst = -1;
        const std::time_t target = std::mktime(&t);
        if (target != std::time_t(-1)) offset_ = std::chrono::system_clock::from_time_t(target) - now;
    }
public:
    // Reset only the bus; a real battery-backed clock retains its time.
    void resetBus() { match_ = bit_ = 0; blocked_ = active_ = extended_ = written_ = false; }
    uint8_t read(unsigned address, uint8_t memory) {
        if (address != 0 && address != 1 && address != 4) return memory;
        if (active_) {
            const unsigned byte = bit_ / 8, shift = bit_ % 8;
            if (address == 4) memory = uint8_t((memory & 0xfe) | ((regs_[byte] >> shift) & 1));
            else {
                pending_[byte] = uint8_t((pending_[byte] & ~(1u << shift)) | ((address & 1u) << shift));
                written_ = true;
                if (extended_ && bit_ == 7 && pending_[0] != 0x32) regs_[1] = 0;
            }
            if (++bit_ == 64) { commit(); resetBus(); }
            return memory;
        }
        if (address == 4) { match_ = 0; blocked_ = false; return memory; }
        if (blocked_) return memory;
        static const uint8_t key[8] = {0xc5,0x3a,0xa3,0x5c,0xc5,0x3a,0xa3,0x5c};
        const unsigned input = address & 1;
        if (match_ == 0) extended_ = input != (key[0] & 1);
        const unsigned expected = ((key[match_ / 8] >> (match_ % 8)) & 1) ^ unsigned(extended_);
        if (input != expected) { blocked_ = true; return memory; }
        if (++match_ == 64) { active_ = true; bit_ = 0; written_ = false; snapshot(); }
        return memory;
    }
};
#endif
