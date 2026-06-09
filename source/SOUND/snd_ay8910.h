#ifndef SND_AY8910_H
#define SND_AY8910_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AY_AFINE      0
#define AY_ACOARSE    1
#define AY_BFINE      2
#define AY_BCOARSE    3
#define AY_CFINE      4
#define AY_CCOARSE    5
#define AY_NOISEPER   6
#define AY_ENABLE     7
#define AY_AVOL       8
#define AY_BVOL       9
#define AY_CVOL       10
#define AY_EFINE      11
#define AY_ECOARSE    12
#define AY_ESHAPE     13
#define AY_PORTA      14
#define AY_PORTB      15

#define AYSTEP        0x8000
#define AYMAX_OUTPUT  0x7fff

typedef struct ay8910_s {
    unsigned char Regs[16];
    int register_latch;

    int SampleRate;
    int UpdateStep;

    int PeriodA;
    int PeriodB;
    int PeriodC;
    int PeriodN;
    int PeriodE;

    int CountA;
    int CountB;
    int CountC;
    int CountN;
    int CountE;
    int CountEnv;

    unsigned int RNG;
    int OutputA;
    int OutputB;
    int OutputC;
    int OutputN;

    int VolA;
    int VolB;
    int VolC;
    int VolE;
    int VolTable[32];

    int EnvelopeA;
    int EnvelopeB;
    int EnvelopeC;
    int Attack;
    int Hold;
    int Alternate;
    int Holding;
} ay8910;

extern ay8910 ay;

void ay8910_init(int clock, int sample_rate);
void ay8910_update(short *buffer, unsigned int length);
void ay8910_write(int a, int data);
unsigned short ay8910_read(void);
unsigned char* ay8910_get_regs(void);
void ay8910_set_reg(int reg, uint8_t val);
void ay8910_reset(void);
void ay8910_setclock(int clock);
void ay8910_buildmixertable(void);

#ifdef __cplusplus
}
#endif

#endif // SND_AY8910_H
