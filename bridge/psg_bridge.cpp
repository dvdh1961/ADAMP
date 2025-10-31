#include "psg_bridge.h"

namespace PsgBridge {

void init(int clockHz, int sampleRate)
{
    sn76489_init(clockHz, sampleRate);
}

void reset()
{
    sn76489_reset();
}

void write(uint8_t data)
{
    sn76489_write(data);
}

void getSamples(int16_t* dstMono16, unsigned int frames)
{
    // De C-implementatie verwacht (short* buffer, unsigned length)
    // en vult MONO 16-bit PCM data.
    sn76489_update(reinterpret_cast<short*>(dstMono16), frames);
}

} // namespace PsgBridge
