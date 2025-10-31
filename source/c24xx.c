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
 * c24xx.c
 *
 * Based on   C24XX emulation by Marat Fayzullin in 2017-2019
*/

#include "c24xx.h"

c24xx c24;

void c24xx_reset(unsigned char *Data,unsigned int Flags)
{
    c24.Pins  = C24XX_SDA|C24XX_SCL;
    c24.Out   = C24XX_SDA|C24XX_SCL;
    c24.State = RECV_CMD;
    c24.Bits  = 0x0001;
    c24.Cmd   = 0x00;
    c24.Addr  = 0;
    c24.Data  = Data;
    c24.Flags = Flags;
    c24.Rsrvd = 0;
}

unsigned int c24xx_getsize(void)
{
    unsigned int Chip = c24.Flags&C24XX_CHIP;
    return(Chip<=C24XX_24C1024? (0x80<<Chip):0);
}

unsigned char c24xx_read(void)
{
//  if(c24.Flags&C24XX_DEBUG)
//    printf("EEPROM SDA=%d, SCL=%d\n",c24.Pins&C24XX_SDA? 1:0,c24.Pins&C24XX_SCL? 1:0);

    return(c24.Out);
}

unsigned char c24xx_write(unsigned char value)
{
    static const unsigned short PageSize[16] =
    { 8,8,16,16,16,32,32,64,128,256,256,256,256,256,256,256 };

    unsigned short pagevalue;

//  if((c24.Flags&C24XX_DEBUG)&&((c24.Pins^V)&(C24XX_SDA|C24XX_SCL)))
//    printf("EEPROM SDA: %d=>%d, SCL: %d=>%d\n",
//      c24.Pins&C24XX_SDA? 1:0,V&C24XX_SDA? 1:0,
//      c24.Pins&C24XX_SCL? 1:0,V&C24XX_SCL? 1:0
//    );

    // When SDA line changes while SCL=1...
    if(((c24.Pins^value)&C24XX_SDA) && (c24.Pins&value&C24XX_SCL))
    {
        if(value&C24XX_SDA)
        {
            // SDA=1: STOP condition
            // if(c24.Flags&C24XX_DEBUG) printf("EEPROM STOP\n");
            c24.State = RECV_CMD;
            c24.Bits  = 0x0001;
        }
        else
        {
            // SDA=0: START condition
            // if(c24.Flags&C24XX_DEBUG) printf("EEPROM START\n");
            c24.State = RECV_CMD;
            c24.Bits  = 0x0001;
        }
    }
    // When SCL line goes up...
    else if((c24.Pins^value)&value&C24XX_SCL)
    {
        if((c24.State==SEND_DATA) && (c24.Bits!=0x8000))
        {
            // Report current output bit
            c24.Out = (c24.Out&~C24XX_SDA)|(c24.Bits&0x8000? C24XX_SDA:0);
            // Shift output bits
            c24.Bits<<=1;
        }
        else if((c24.State!=SEND_DATA) && (c24.Bits<0x0100))
        {
            // Shift input bits
            c24.Bits = (c24.Bits<<1)|(value&C24XX_SDA? 1:0);
        }
        else
        {
            // Depending on the state...
            switch(c24.State)
            {
                case RECV_CMD:
                    c24.Cmd   = c24.Bits&0x00FF;
                    c24.State = (c24.Cmd&0xF0)!=0xA0? RECV_CMD:c24.Cmd&0x01? SEND_DATA:RECV_ADDR;
                    // if(c24.Flags&C24XX_DEBUG) printf("EEPROM CMD=%02Xh(, ADDR=%Xh)\n",c24.Cmd,c24.Addr);
                    break;
                case RECV_ADDR:
                    c24.Addr  = ((unsigned int)(c24.Cmd&0x0E)<<7)+(c24.Bits&0x00FF);
                    c24.Addr &= (0x80<<(c24.Flags&C24XX_CHIP))-1;
                    c24.State = (c24.Flags&C24XX_CHIP)>=C24XX_24C32? RECV_ADR2:RECV_DATA;
                    //if(c24.Flags&C24XX_DEBUG) printf("EEPROM CMD=%02Xh, ADDR=%Xh\n",c24.Cmd,c24.Addr);
                    break;
                case RECV_ADR2:
                    c24.Addr  = (c24.Addr<<8)+(c24.Bits&0x00FF);
                    c24.Addr &= (0x80<<(c24.Flags&C24XX_CHIP))-1;
                    c24.State = RECV_DATA;
                    //if(c24.Flags&C24XX_DEBUG) printf("EEPROM CMD=%02Xh, ADDR=%Xh\n",c24.Cmd,c24.Addr);
                    break;
                case RECV_DATA:
                    //if(c24.Flags&C24XX_DEBUG) printf("EEPROM WRITE[%Xh] <= %02Xh\n",c24.Addr,c24.Bits&0xFF);
                    // Write byte into EEPROM
                    c24.Data[c24.Addr] = c24.Bits&0x00FF;
                    // Go to the next address inside N-byte page
                    pagevalue = PageSize[c24.Flags&C24XX_CHIP]-1;
                    c24.Addr = ((c24.Addr+1)&pagevalue)|(c24.Addr&~pagevalue);
                    break;
                case SEND_DATA:
                    // See below
                    break;
                default:
                    c24.State = RECV_CMD;
                break;
            }
            // Acknowledge received byte, clear bit buffer
            c24.Bits = 0x0001;
            c24.Out  = C24XX_SCL;
            // If sending the next byte...
            if(c24.State==SEND_DATA)
            {
                // Read byte from EEPROM
                c24.Bits = ((unsigned short)c24.Data[c24.Addr]<<8)|0x0080;
                // if(c24.Flags&C24XX_DEBUG) printf("EEPROM READ[%Xh] => %02Xh\n",c24.Addr,c24.Bits>>8);
                // Go to the next address inside N-byte page
                pagevalue = PageSize[c24.Flags&C24XX_CHIP]-1;
                c24.Addr = ((c24.Addr+1)&pagevalue)|(c24.Addr&~pagevalue);
            }
        }
    } // else When SCL line goes up...

    // New pin values
    c24.Pins = value;
    return(c24.Out);
}
