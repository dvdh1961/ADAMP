#ifndef ADAM_FUJINET_PC_H
#define ADAM_FUJINET_PC_H
#include <array>
#include <vector>
#include <string>
#include <future>
#include <cstdint>

// A dedicated worker owns the TCP stream. Only the emulation thread touches DCB/RAM.
namespace AdamFujiNetPC {
struct Reply {
    bool ok = false;
    bool empty = false;
    bool endOfMedia = false;
    int bootProfile = -1;
    std::array<uint8_t,4> status{};
    std::vector<uint8_t> data;
    std::vector<uint8_t> rom;
    std::string error;
};
bool start(uint16_t port, std::string& error);
void stop();
bool enabled();
bool eosSoundSessionActive();
void resetEosSoundSession();
bool connected(); // true only after a successful ADAM status probe
std::string state();
std::future<Reply> request(uint8_t device, int command, bool block,
                           uint32_t sector = 0, std::vector<uint8_t> data = {});
void cancel();
unsigned session();
bool forwards(uint8_t device);
}
void UpdateFUJINET_PC(unsigned char dev, int command, int forcedDevice = -1);
void UpdateFUJINET_PC_CPM_D5(unsigned char dev, int command);
void fujinet_pc_reset();
int fujinet_pc_take_boot_profile();
std::vector<uint8_t> fujinet_pc_take_rom();
#endif
