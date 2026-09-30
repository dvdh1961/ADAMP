#include "psg_bridge.h"
#include "CORE/emu.h"
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

    // 2. Render de SGM AY-chip only when SGM hardware is actually enabled.
    // ADAM has no AY audio path; always mixing it can expose stale register
    // state as a continuous tone during slow physical ADAMnet operation.
    if (emulator && emulator->SGM)
        ay8910_update(ay_buffer, frames);
    else
        std::memset(ay_buffer, 0, frames * sizeof(ay_buffer[0]));

    // 3. Mix de twee signalen (tel ze op) in de doelbuffer.
    //
    // V8.68: globale emulator master-gain.
    // Dit zit NA de SN+AY mix, zodat de onderlinge chip/kanaalverhoudingen
    // volledig identiek blijven. Alleen het uiteindelijke emulatorniveau
    // wordt verhoogd om beter overeen te komen met andere ColecoVision-emulators.
    static constexpr float kEmulatorMasterGain = 1.75f;

    for (unsigned int i = 0; i < frames; ++i)
    {
        // Eerst de bestaande hardwaremix behouden.
        const int32_t raw_mix =
            static_cast<int32_t>(sn_buffer[i]) +
            static_cast<int32_t>(ay_buffer[i]);

        // Daarna één globale master-gain toepassen.
        int32_t mixed_sample =
            static_cast<int32_t>(static_cast<float>(raw_mix) * kEmulatorMasterGain);

        // Veilige 16-bit clipping.
        if (mixed_sample > 32767) mixed_sample = 32767;
        if (mixed_sample < -32768) mixed_sample = -32768;

        dstMono16[i] = static_cast<int16_t>(mixed_sample);
    }
}

} // namespace PsgBridge
