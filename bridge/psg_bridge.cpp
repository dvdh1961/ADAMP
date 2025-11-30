#include "psg_bridge.h"
#include <cstring>

// Tijdelijke buffers voor de mixer (groot genoeg voor 1 frame PAL, 882 samples)
static short sn_buffer[882];
static short ay_buffer[882];

namespace PsgBridge {

void init(int clockHz, int sampleRate)
{
    // Initialiseer BEIDE chips
    sn76489_init(clockHz, sampleRate);
    ay8910_init(clockHz, sampleRate);
}

void reset(int clockHz, int sampleRate)
{
    // Reset BEIDE chips en geef de argumenten correct door
    sn76489_reset(clockHz, sampleRate);
    ay8910_reset();
}

void getSamples(int16_t* dstMono16, unsigned int frames)
{
    // Zorg ervoor dat we niet buiten onze tijdelijke buffers schrijven
    if (frames > 882) frames = 882;

    // 1. Render de Standaard SN-chip (muziek + effecten)
    sn76489_update(sn_buffer, frames);

    // 2. Render de SGM AY-chip (SGM muziek)
    ay8910_update(ay_buffer, frames);

    // 3. Mix de twee signalen (tel ze op) in de doelbuffer
    for (unsigned int i = 0; i < frames; ++i)
    {
        // Converteer naar 32-bit int voor veilige optelling
        int32_t mixed_sample = static_cast<int32_t>(sn_buffer[i]) + static_cast<int32_t>(ay_buffer[i]);

        // Simpele clipping om audio-vervorming te voorkomen
        if (mixed_sample > 32767) mixed_sample = 32767;
        if (mixed_sample < -32768) mixed_sample = -32768;

        dstMono16[i] = static_cast<int16_t>(mixed_sample);
    }
}

} // namespace PsgBridge
