#ifndef PSG_BRIDGE_H
#define PSG_BRIDGE_H

#include <cstdint>

    // De C-headers voor BEIDE chips
           extern "C" {
    // --- Standaard SN-chip (van sn76489.c) ---
    void sn76489_init(int clock, int sample_rate);
    void sn76489_reset(int clock, int sample_rate);
    void sn76489_update(short *buffer, unsigned int length);
    void sn76489_write(int data);

    // --- SGM AY-chip (van ay8910.c) ---
    void ay8910_init(int clock, int sample_rate);
    void ay8910_reset(void);
    void ay8910_update(short *buffer, unsigned int length);
    void ay8910_write(int a, int data);
}

namespace PsgBridge {

void init(int clockHz = 3579545, int sampleRate = 44100);
void reset(int clockHz = 3579545, int sampleRate = 44100);
void getSamples(int16_t* dstMono16, unsigned int frames);

}
#endif

