#ifndef PSG_BRIDGE_H
#define PSG_BRIDGE_H

#include <cstdint>

extern "C" {
// --- exact uit sn76489.h ---
void sn76489_init(int clock, int sample_rate);
void sn76489_reset(int clock, int sample_rate);
void sn76489_update(short *buffer, unsigned int length);
void sn76489_write(int data);
}

// --- nette C++ inline wrapper ---
namespace PsgBridge {

inline void init(int clockHz = 3579545, int sampleRate = 44100) //3579545
{
    sn76489_init(clockHz, sampleRate);
}

inline void reset(int clockHz = 3579545, int sampleRate = 44100)
{
    sn76489_reset(clockHz, sampleRate);
}

inline void write(uint8_t data)
{
    sn76489_write(static_cast<int>(data));
}

inline void getSamples(int16_t* dstMono16, unsigned int frames)
{
    sn76489_update(reinterpret_cast<short*>(dstMono16), frames);
}

} // namespace PsgBridge
#endif // PSG_BRIDGE_H
