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
 * ay8910.c
 *
*/
#include "ay8910.h"

ay8910 ay;

void ay8910_update(short *buffer, unsigned int length)
{
        unsigned int index;
        int outn;
        int prelen=length*AYSTEP;
        unsigned int out;
        int val;

        int vola,volb,volc;
        int left;

        short *out_l;
        out_l = buffer;

        // The 8910 has three outputs, each output is the mix of one of the three
        // tone generators and of the (single) noise generator. The two are mixed
        // BEFORE going into the DAC. The formula to mix each channel is:
        // (ToneOn | ToneDisable) & (NoiseOn | NoiseDisable).
        // Note that this means that if both tone and noise are disabled, the output
        // is 1, not 0, and can be modulated changing the volume.

        // If the channels are disabled, set their output to 1, and increase the
        // counter, if necessary, so they will not be inverted during this update.
        // Setting the output to 1 is necessary because a disabled channel is locked
        // into the ON state (see above); and it has no effect if the volume is 0.
        // If the volume is 0, increase the counter, but don't touch the output.
        if (ay.Regs[AY_ENABLE]&0x01)
        {
                if (ay.CountA <= prelen) ay.CountA += prelen;
                ay.OutputA = 1;
        }
        else if (ay.Regs[AY_AVOL] == 0)
        {
                // note that I do count += length, NOT count = length + 1. You might think
                // it's the same since the volume is 0, but doing the latter could cause
                // interferencies when the program is rapidly modulating the volume.
                if (ay.CountA <= prelen) ay.CountA += prelen;
        }
        if (ay.Regs[AY_ENABLE] & 0x02)
        {
                if (ay.CountB <= prelen) ay.CountB += prelen;
                ay.OutputB = 1;
        }
        else if (ay.Regs[AY_BVOL] == 0)
        {
                if (ay.CountB <= prelen) ay.CountB += prelen;
        }
        if (ay.Regs[AY_ENABLE] & 0x04)
        {
                if (ay.CountC <= prelen) ay.CountC += prelen;
                ay.OutputC = 1;
        }
        else if (ay.Regs[AY_CVOL] == 0)
        {
                if (ay.CountC <= prelen) ay.CountC += prelen;
        }

        // for the noise channel we must not touch OutputN - it's also not necessary
        // since we use outn.
        if ((ay.Regs[AY_ENABLE] & 0x38) == 0x38)	//all off
                if (ay.CountN <= prelen) ay.CountN += prelen;

        outn = (ay.OutputN | ay.Regs[AY_ENABLE]);

        // buffering loop
        for (index = 0; index < length; index++)
        {
                // vola, volb and volc keep track of how long each square wave stays
                // in the 1 position during the sample period.
                vola = volb = volc = 0;
                left = AYSTEP;
                do {
                        int nextevent;
                        if (ay.CountN < left) nextevent = ay.CountN;
                        else nextevent = left;

                        if (outn & 0x08)
                        {
                                if (ay.OutputA) vola += ay.CountA;
                                ay.CountA -= nextevent;
                                while (ay.CountA <= 0)
                                {
                                        ay.CountA += ay.PeriodA;
                                        if (ay.CountA > 0)
                                        {
                                                ay.OutputA ^= 1;
                                                if (ay.OutputA) vola += ay.PeriodA;
                                                break;
                                        }
                                        ay.CountA += ay.PeriodA;
                                        vola += ay.PeriodA;
                                }
                                if (ay.OutputA) vola -= ay.CountA;
                        }
                        else
                        {
                                ay.CountA -= nextevent;
                                while (ay.CountA <= 0)
                                {
                                        ay.CountA += ay.PeriodA;
                                        if (ay.CountA > 0)
                                        {
                                                ay.OutputA ^= 1;
                                                break;
                                        }
                                        ay.CountA += ay.PeriodA;
                                }
                        }

                        if (outn & 0x10)
                        {
                                if (ay.OutputB) volb += ay.CountB;
                                ay.CountB -= nextevent;
                                while (ay.CountB <= 0)
                                {
                                        ay.CountB += ay.PeriodB;
                                        if (ay.CountB > 0)
                                        {
                                                ay.OutputB ^= 1;
                                                if (ay.OutputB) volb += ay.PeriodB;
			                        break;
                                        }
                                        ay.CountB += ay.PeriodB;
                                        volb += ay.PeriodB;
                                }
                                if (ay.OutputB) volb -= ay.CountB;
                        }
                        else
                        {
                                ay.CountB -= nextevent;
                                while (ay.CountB <= 0)
                                {
                                        ay.CountB += ay.PeriodB;
                                        if (ay.CountB > 0)
                                        {
                                                ay.OutputB ^= 1;
                                                break;
                                        }
                                        ay.CountB += ay.PeriodB;
                                }
                        }

                        if (outn & 0x20)
                        {
                                if (ay.OutputC) volc += ay.CountC;
                                ay.CountC -= nextevent;
                                while (ay.CountC <= 0)
                                {
                                        ay.CountC += ay.PeriodC;
                                        if (ay.CountC > 0)
                                        {
                                                ay.OutputC ^= 1;
                                                if (ay.OutputC) volc += ay.PeriodC;
                                                break;
                                        }
                                        ay.CountC += ay.PeriodC;
                                        volc += ay.PeriodC;
                                }
                                if (ay.OutputC) volc -= ay.CountC;
                        }
                        else
                        {
                                ay.CountC -= nextevent;
                                while (ay.CountC <= 0)
                                {
                                        ay.CountC += ay.PeriodC;
                                        if (ay.CountC > 0)
                                        {
                                                ay.OutputC ^= 1;
                                                break;
                                        }
                                        ay.CountC += ay.PeriodC;
                                }
                        }

                        ay.CountN -= nextevent;
                        if (ay.CountN <= 0)
                        {
                                // Is noise output going to change?
                                if ((ay.RNG + 1) & 2)	// (bit0^bit1)?
                                {
                                        ay.OutputN = ~ay.OutputN;
                                        outn = (ay.OutputN | ay.Regs[AY_ENABLE]);
                                }

                                // The Random Number Generator of the 8910 is a 17-bit shift register.
                                // The input to the shift register is bit0 XOR bit2 (bit0 is the output).

                                // The following is a fast way to compute bit 17 = bit0^bit2.
		                // Instead of doing all the logic operations, we only check bit 0,
                                //  relying on the fact that after two shifts of the  register,
                                // what now is bit 2 will become bit 0, and will invert, if necessary, bit 16, which previously was bit 18.
                                if (ay.RNG & 1) ay.RNG ^= 0x24000; /* This version is called the "Galois configuration". */
                                ay.RNG >>= 1;
                                ay.CountN += ay.PeriodN;
                        }

                        left -= nextevent;
                } while (left > 0);

	        // update envelope
	        if (ay.Holding == 0)
                {
                        ay.CountE -= AYSTEP;
                        if (ay.CountE <= 0)
                        {
                                do {
                                        ay.CountEnv--;
                                        ay.CountE += ay.PeriodE;
                                } while (ay.CountE <= 0);

                                // check envelope current position
                                if (ay.CountEnv < 0)
                                {
                                        if (ay.Hold)
                                        {
                                                if (ay.Alternate)
		                                        ay.Attack ^= 0x1f;
                                                ay.Holding = 1;
                                                ay.CountEnv = 0;
                                        }
                                        else
                                        {
                                                // if CountEnv has looped an odd number of times (usually 1),
                                                // invert the output.
                                                if (ay.Alternate && (ay.CountEnv & 0x20))
                                                        ay.Attack ^= 0x1f;
                                                ay.CountEnv &= 0x1f;
                                        }
                                }
                                ay.VolE = ay.VolTable[ay.CountEnv ^ ay.Attack];

                                // reload volume
                                if (ay.EnvelopeA) ay.VolA = ay.VolE;
                                if (ay.EnvelopeB) ay.VolB = ay.VolE;
                                if (ay.EnvelopeC) ay.VolC = ay.VolE;
                        }
                }

                out = (vola*ay.VolA)+(volb*ay.VolB)+(volc*ay.VolC);
                if (out > AYMAX_OUTPUT*AYSTEP) out = AYMAX_OUTPUT*AYSTEP;
                val = (out>>16);
                out_l[index] = val;
        } // for (index = 0; index < length; index++)  {
}

// ----------------------------------------------------------------------------------------
void _AYWriteReg(int r, unsigned char v) {
    int old;

    if (r>=14)
        ay.Regs[r] = 0;

    ay.Regs[r] = v;

    // A note about the period of tones, noise and envelope: for speed reasons,
    // we count down from the period to 0, but careful studies of the chip
    // output prove that it instead counts up from 0 until the counter becomes
    // greater or equal to the period. This is an important difference when the
    // program is rapidly changing the period to modulate the sound.
    // To compensate for the difference, when the period is changed we adjust
    // our internal counter.
    // Also, note that period = 0 is the same as period = 1. This is mentioned
    // in the YM2203 data sheets. However, this does NOT apply to the Envelope
    // period. In that case, period = 0 is half as period = 1.
    switch( r ) {
        case AY_AFINE:
        case AY_ACOARSE:
            ay.Regs[AY_ACOARSE] &= 0x0f;
            old = ay.PeriodA;
            ay.PeriodA = (ay.Regs[AY_AFINE] + (ay.Regs[AY_ACOARSE]<<8)) * ay.UpdateStep;
            if (ay.PeriodA == 0) ay.PeriodA = ay.UpdateStep;
            ay.CountA += ay.PeriodA - old;
            if (ay.CountA <= 0) ay.CountA = 1;
            break;
        case AY_BFINE:
        case AY_BCOARSE:
            ay.Regs[AY_BCOARSE] &= 0x0f;
            old = ay.PeriodB;
            ay.PeriodB = (ay.Regs[AY_BFINE] + (ay.Regs[AY_BCOARSE]<<8)) * ay.UpdateStep;
            if (ay.PeriodB == 0) ay.PeriodB = ay.UpdateStep;
            ay.CountB += ay.PeriodB - old;
            if (ay.CountB <= 0) ay.CountB = 1;
            break;
        case AY_CFINE:
        case AY_CCOARSE:
            ay.Regs[AY_CCOARSE] &= 0x0f;
            old = ay.PeriodC;
            ay.PeriodC = (ay.Regs[AY_CFINE] + (ay.Regs[AY_CCOARSE]<<8)) * ay.UpdateStep;
            if (ay.PeriodC == 0) ay.PeriodC = ay.UpdateStep;
            ay.CountC += ay.PeriodC - old;
            if (ay.CountC <= 0) ay.CountC = 1;
            break;
        case AY_NOISEPER:
            ay.Regs[AY_NOISEPER] &= 0x1f;
            old = ay.PeriodN;
            ay.PeriodN = ay.Regs[AY_NOISEPER] * ay.UpdateStep;
            if (ay.PeriodN == 0) ay.PeriodN = ay.UpdateStep;
            ay.CountN += ay.PeriodN - old;
            if (ay.CountN <= 0) ay.CountN = 1;
            break;
/*
    case AY_ENABLE:
      if ((PSG->lastEnable == -1) || ((PSG->lastEnable & 0x40) != (PSG->Regs[AY_ENABLE] & 0x40))) {
        // write out 0xff if port set to input
        if (PSG->PortAwrite)
          (*PSG->PortAwrite)(0, (PSG->Regs[AY_ENABLE] & 0x40) ? PSG->Regs[AY_PORTA] : 0xff);
      }

      if ((PSG->lastEnable == -1) || ((PSG->lastEnable & 0x80) != (PSG->Regs[AY_ENABLE] & 0x80))) {
        // write out 0xff if port set to input
        if (PSG->PortBwrite)
          (*PSG->PortBwrite)(0, (PSG->Regs[AY_ENABLE] & 0x80) ? PSG->Regs[AY_PORTB] : 0xff);
      }
      PSG->lastEnable = PSG->Regs[AY_ENABLE];
      break;
*/
        case AY_AVOL:
            ay.Regs[AY_AVOL] &= 0x1f;
            ay.EnvelopeA = ay.Regs[AY_AVOL] & 0x10;
            ay.VolA = ay.EnvelopeA ? ay.VolE : ay.VolTable[ay.Regs[AY_AVOL] ? ay.Regs[AY_AVOL]*2+1 : 0];
            break;
        case AY_BVOL:
            ay.Regs[AY_BVOL] &= 0x1f;
            ay.EnvelopeB = ay.Regs[AY_BVOL] & 0x10;
            ay.VolB = ay.EnvelopeB ? ay.VolE : ay.VolTable[ay.Regs[AY_BVOL] ? ay.Regs[AY_BVOL]*2+1 : 0];
            break;
        case AY_CVOL:
            ay.Regs[AY_CVOL] &= 0x1f;
            ay.EnvelopeC = ay.Regs[AY_CVOL] & 0x10;
            ay.VolC = ay.EnvelopeC ? ay.VolE : ay.VolTable[ay.Regs[AY_CVOL] ? ay.Regs[AY_CVOL]*2+1 : 0];
            break;
        case AY_EFINE:
        case AY_ECOARSE:
            old = ay.PeriodE;
            ay.PeriodE = ((ay.Regs[AY_EFINE] + (ay.Regs[AY_ECOARSE]<<8))) * ay.UpdateStep;
            if (ay.PeriodE == 0) ay.PeriodE = ay.UpdateStep>>1;
            ay.CountE += ay.PeriodE - old;
            if (ay.CountE <= 0) ay.CountE = 1;
            break;
        case AY_ESHAPE:
      /* envelope shapes:
      C AtAlH
      0 0 x x  \___

      0 1 x x  /___

      1 0 0 0  \\\\

      1 0 0 1  \___

      1 0 1 0  \/\/
                ___
      1 0 1 1  \

      1 1 0 0  ////
                ___
      1 1 0 1  /

      1 1 1 0  /\/\

      1 1 1 1  /___

      The envelope counter on the AY-3-8910 has 16 steps. On the YM2149 it
      has twice the steps, happening twice as fast. Since the end result is
      just a smoother curve, we always use the YM2149 behaviour.
      */
            ay.Regs[AY_ESHAPE] &= 0x0f;
            ay.Attack = (ay.Regs[AY_ESHAPE] & 0x04) ? 0x1f : 0x00;
            if ((ay.Regs[AY_ESHAPE] & 0x08) == 0) {
                // if Continue = 0, map the shape to the equivalent one which has Continue = 1
                ay.Hold = 1;
                ay.Alternate = ay.Attack;
            }
            else {
                ay.Hold = ay.Regs[AY_ESHAPE] & 0x01;
                ay.Alternate = ay.Regs[AY_ESHAPE] & 0x02;
            }
            ay.CountE = ay.PeriodE;
            ay.CountEnv = 0x1f;
            ay.Holding = 0;
            ay.VolE = ay.VolTable[ay.CountEnv ^ ay.Attack];
            if (ay.EnvelopeA) ay.VolA = ay.VolE;
            if (ay.EnvelopeB) ay.VolB = ay.VolE;
            if (ay.EnvelopeC) ay.VolC = ay.VolE;
            break;

    case AY_PORTA:
            //if (ay.Regs[AY_ENABLE] & 0x40)
            //SP0256w(v);
            break;
    case AY_PORTB:
      //if ((PSG->Regs[AY_ENABLE] & 0x80) == 0)
      //if (PSG->Regs[AY_ENABLE] & 0x80)
        //if (PSG->PortBwrite)
          // (*PSG->PortBwrite)(0,v);
      break;
    }
}

// ----------------------------------------------------------------------------------------
void ay8910_write(int a ,int data)
{
    if (a & 1) {
        // Data port
        int r = ay.register_latch;
        /*if (r < 14) {
            if ( (r == AY_ESHAPE) || (ay.Regs[r] != data) ) {
              AY8910partupdate(chip);
            }
        } */
        _AYWriteReg(r,data);
    }
     else {
        // Register port
        ay.register_latch = data & 0x0f;
    }
}

// ----------------------------------------------------------------------------------------
unsigned short ay8910_read(void) {
    int r = ay.register_latch;
/*    switch (r) {
        case AY_PORTA:
            if (ay.PortAread) {
                ay.Regs[AY_PORTA] = (*PSG->PortAread)();
            }
            break;
        case AY_PORTB:
            if (ay.PortBread) {
                ay.Regs[AY_PORTB] = (*PSG->PortBread)();
            }
            break;
    }*/
    return ay.Regs[r];
}

// ----------------------------------------------------------------------------------------
void ay8910_buildmixertable(void)
{
    int i;
    double out;

    // calculate the volume->voltage conversion table
    // The AY-3-8910 has 16 levels, in a logarithmic scale (3dB per step)
    // The YM2149 still has 16 levels for the tone generators, but 32 for
    // the envelope generator (1.5dB per step).
    out = AYMAX_OUTPUT;
    for (i = 31;i > 0;i--) {
        ay.VolTable[i] = out + 0.5; // round to nearest
        out /= (1.188502227);  // = 10 ^ (1.5/20) = 1.5dB
    }
    ay.VolTable[0] = 0;
}

// ----------------------------------------------------------------------------------------
void ay8910_setclock(int clock)
{
    // the step clock for the tone and noise generators is the chip clock
    // divided by 8; for the envelope generator of the AY-3-8910, it is half
    // that much (clock/16), but the envelope of the YM2149 goes twice as
    // fast, therefore again clock/8.
    // Here we calculate the number of steps which happen during one sample
    // at the given sample rate. No. of events = sample rate / (clock/8).
    // STEP is a multiplier used to turn the fraction into a fixed point
    // number.
	//PSG->UpdateStep = ((double)STEP * PSG->SampleRate * 8) / clock;
    //ay.UpdateStep = ((double)AYSTEP * ay.SampleRate * 8 + clock/2) / clock;
    ay.UpdateStep = ((double)AYSTEP * ay.SampleRate * 8 ) / clock;
}

// ----------------------------------------------------------------------------------------
void ay8910_reset(void)
{
    int i;
    ay.register_latch = 0;
    ay.RNG = 1;
    ay.OutputA = 0;
    ay.OutputB = 0;
    ay.OutputC = 0;
    ay.OutputN = 0xff;
      for (i = 0;i < AY_PORTA;i++)
        _AYWriteReg(i,0);
}

// ----------------------------------------------------------------------------------------
void ay8910_init(int clock, int sample_rate)
{
    ay.SampleRate = sample_rate;
    ay8910_setclock(clock);
    ay8910_reset();
    ay8910_buildmixertable();
}

