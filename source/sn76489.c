/* EmulTwo  - A Windows Colecovision emulator.
 * Copyright (C) 2014-2023 Alekmaul
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * sn76489.c
 *
 * Based on   SN76489 emulation by Maxim in 2001 and 2002
 *  converted from hisoriginal Delphi implementation
*/

#include <limits.h>

#include "sn76489.h"

sn76496 sn;

void sn76489_update(short *buffer, unsigned int length)
{
        int i;
        unsigned int j;
        int vol[4];
        int left, nextstep;
        unsigned int out;
        int val;

        short * out_l;
        out_l = buffer;

        for(i=0; i<4; i++)
        {
                if(sn.Volume[i]==0)
                {
                        if(sn.Count[i] <= (length*SNSTEP)) sn.Count[i] += length*SNSTEP;
                }
        }

        for(j=0; j<length; j++)
        {
                vol[0] = vol[1] = vol[2] = vol[3] = 0;
                out = 0;
                left = 0;

                for(i=0; i<3; i++)
                {
                        if(sn.Output[i]!=0) vol[i] += sn.Count[i];
                        sn.Count[i] -= SNSTEP;
                        while(sn.Count[i] <= 0)
                        {
                                sn.Count[i] += sn.Period[i];
                                if(sn.Count[i] > 0)
                                {
                                        sn.Output[i] ^= 1;
                                        if(sn.Output[i] != 0) vol[i] += sn.Period[i];
                                        break;
                                }
                                sn.Count[i] += sn.Period[i];
                                vol[i] += sn.Period[i];
                        }
                        if(sn.Output[i] != 0) vol[i] -= sn.Count[i];
                }

                left = SNSTEP;

                do {
                        nextstep = (sn.Count[3] < left)?sn.Count[3]:left;

                        if(sn.Output[3] != 0) vol[3] += sn.Count[3];
                        sn.Count[3] -= nextstep;

                        if (sn.Count[3] <= 0)
                        {
                                // white noise mode ?
                                if (sn.Register[6] & 4)
                                {
			                if (((sn.RNG & 0x06) != 0x06) && ((sn.RNG & 0x06) != 0))
                                        {
                                                sn.RNG >>= 1;
                                                sn.RNG |= 0x8000;
					}
					else
                                        {
                                                sn.RNG >>= 1;
					}
				}
                                // Periodic noise mode
				else
                                {
			                if (sn.RNG & 1)
                                        {
                                                sn.RNG >>= 1;
                                                sn.RNG |= 0x8000;
					}
					else
                                        {
                                                sn.RNG >>= 1;
					}
                                }
                                sn.Output[3] = sn.RNG&1;
                                sn.Count[3] += sn.Period[3];
                                if(sn.Output[3]) vol[3] += sn.Period[3];
                        }
                        if(sn.Output[3]) vol[3] -= sn.Count[3];

                        left -= nextstep;
                } while( left > 0 );

                out = (vol[0]*sn.Volume[0]) + (vol[1]*sn.Volume[1]) + (vol[2]*sn.Volume[2]) + (vol[3]*sn.Volume[3]);
                if (out > MAX_OUTPUT*SNSTEP) out = MAX_OUTPUT*SNSTEP;
                val = (out>>16);
                out_l[j] = val;
        }
}

void sn76489_write(int data)
{
  int r=0, c=0, n=0;

  if( (data&0x80)) {
    r = (data&0x70)>>4;
    sn.LastRegister = r;
    sn.Register[r] = (sn.Register[r]&0x3F0)|(data&0x0F);
  }
  else {
    r = sn.LastRegister;
  }
  c = r>>1;

  switch( r ) {
    // tone 0,1,2 frequency
    case 0: case 2: case 4:
      if ((data & 0x80) == 0) sn.Register[r] = (sn.Register[r] & 0x0f) | ((data & 0x3f) << 4);
      sn.Period[c] = sn.UpdateStep * sn.Register[r];
      if(sn.Period[c] == 0) sn.Period[c] = sn.UpdateStep;
      if(r == 4){
        // update noise shift frequency
        if((sn.Register[6]&0x03) == 0x03) sn.Period[3] = sn.Period[2]<<1;
      }
      break;
    // tone 0,1,2 volume ; noise volume
    case 1: case 3: case 5: case 7:
      sn.Volume[c] = sn.VolTable[data&0x0F];
      if ((data & 0x80) == 0) sn.Register[r] = (sn.Register[r] & 0x3f0) | (data & 0x0f);
      break;
    // noise: frequency, mode
    case 6:
      if ((data & 0x80) == 0) sn.Register[r] = (sn.Register[r] & 0x3f0) | (data & 0x0f);
      n = sn.Register[6];

      //JGD sn.NoiseFB = ( (n&4)!=0 )?FB_WNOISE:FB_PNOISE;

      // N/512,N/1024,N/2048,Tone #3 output
      n &= 3;
      sn.Period[3] = (n==3)?(sn.Period[2]<<1):(sn.UpdateStep<<(5+n));
      // reset noise shifter
      sn.RNG = NG_PRESET;
      sn.Output[3] = sn.RNG&1;
      break;
  }
}

void sn76489_setclock(int clock)
{
  sn.UpdateStep = (int)(((double)SNSTEP*sn.SampleRate*16)/clock);
}

void sn76489_setgain(void)
{
    int i;
    double out;
    int gain=0;

    // increase max output basing on gain (0.2 dB per step)
  out = MAX_OUTPUT/3;
  while(gain-- > 0) out *= 1.023292992;	// = (10 ^ (0.2/20))

  // build volume table (2dB per step)
  for(i=0; i<15; i++){
    // limit volume to avoid clipping
    if(out > MAX_OUTPUT/3)  sn.VolTable[i] = MAX_OUTPUT/3;
    else                    sn.VolTable[i] = (int)out;

    out /= 1.258925412;	// = 10 ^ (2/20) = 2dB
  }
  sn.VolTable[15] = 0;
}

void sn76489_reset(int clock, int sample_rate)
{
    int i;
    sn.SampleRate = sample_rate;
    sn76489_setclock(clock);

    for(i=0; i<4; i++) sn.Volume[i] = 0;

    sn.LastRegister = 0;

    for(i=0; i<8; i+=2){
        sn.Register[i] = 0;
        sn.Register[i+1] = 0x0F;	// volume = 0
    }

    for(i=0; i<4; i++){
        sn.Output[i] = 0;
        sn.Period[i] = sn.Count[i] = SNSTEP;
    }

    sn.RNG = NG_PRESET;
    sn.Output[3] = sn.RNG&1;
}

void sn76489_init(int clock, int sample_rate)
{
    sn76489_reset(clock, sample_rate);
    sn76489_setgain();
}

