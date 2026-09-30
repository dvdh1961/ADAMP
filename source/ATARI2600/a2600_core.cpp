#include "a2600_core.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>

namespace {
constexpr double CpuClockNtsc = 1193191.6666667;
constexpr double CpuClockPal = 1182298.0;
}

Atari6507::Atari6507(Atari2600Core& bus) : m_bus(bus) {}
uint8_t Atari6507::read(uint16_t a)
{
    if(m_bus.m_cpuStepping){m_bus.tick(1);++m_busCycles;}
    return m_bus.read(a);
}
void Atari6507::write(uint16_t a, uint8_t v)
{
    if(m_bus.m_cpuStepping){m_bus.tick(1);++m_busCycles;}
    m_bus.write(a,v);
}
uint8_t Atari6507::fetch() { return read(m_pc++); }
uint16_t Atari6507::fetch16() { const uint8_t l=fetch(), h=fetch(); return uint16_t(l)|(uint16_t(h)<<8); }
uint16_t Atari6507::read16(uint16_t a) { return uint16_t(read(a))|(uint16_t(read(a+1))<<8); }
uint16_t Atari6507::read16bug(uint16_t a) { return uint16_t(read(a))|(uint16_t(read((a&0xFF00)|uint8_t(a+1)))<<8); }
void Atari6507::push(uint8_t v) { write(0x100|m_sp--,v); }
uint8_t Atari6507::pull() { return read(0x100|++m_sp); }
bool Atari6507::flag(Flag f) const { return (m_p&f)!=0; }
void Atari6507::setFlag(Flag f,bool s) { if(s)m_p|=f;else m_p&=uint8_t(~f); }
void Atari6507::setNZ(uint8_t v) { setFlag(Z,v==0);setFlag(N,(v&0x80)!=0); }

void Atari6507::reset()
{
    m_a=m_x=m_y=0; m_sp=0xFD; m_p=U|I; m_pc=read16(0xFFFC);
}

void Atari6507::startAt(uint16_t address)
{
    m_a=0x9A; m_x=0xFF; m_y=0; m_sp=0xFF; m_p=U|I; m_pc=address;
}

void Atari6507::adc(uint8_t v)
{
    const uint16_t binary=uint16_t(m_a)+v+(flag(C)?1:0);
    setFlag(V,((~(m_a^v)&(m_a^uint8_t(binary)))&0x80)!=0);
    if(flag(D)) {
        int lo=(m_a&15)+(v&15)+(flag(C)?1:0), hi=(m_a>>4)+(v>>4);
        if(lo>9){lo+=6;hi++;} if(hi>9)hi+=6;
        setFlag(C,hi>15); m_a=uint8_t((hi<<4)|(lo&15));
    } else { setFlag(C,binary>0xFF); m_a=uint8_t(binary); }
    setNZ(m_a);
}

void Atari6507::sbc(uint8_t v)
{
    if(flag(D)) {
        int borrow=flag(C)?0:1, lo=(m_a&15)-(v&15)-borrow, hi=(m_a>>4)-(v>>4);
        const uint16_t binary=uint16_t(m_a)-v-borrow;
        setFlag(V,(((m_a^v)&(m_a^uint8_t(binary)))&0x80)!=0);
        if(lo<0){lo-=6;hi--;} if(hi<0)hi-=6;
        setFlag(C,binary<0x100); m_a=uint8_t((hi<<4)|(lo&15)); setNZ(m_a);
    } else adc(uint8_t(~v));
}

void Atari6507::compare(uint8_t r,uint8_t v){const uint8_t q=uint8_t(r-v);setFlag(C,r>=v);setNZ(q);}
uint8_t Atari6507::asl(uint8_t v){setFlag(C,v&0x80);v<<=1;setNZ(v);return v;}
uint8_t Atari6507::lsr(uint8_t v){setFlag(C,v&1);v>>=1;setNZ(v);return v;}
uint8_t Atari6507::rol(uint8_t v){const bool c=flag(C);setFlag(C,v&0x80);v=uint8_t((v<<1)|(c?1:0));setNZ(v);return v;}
uint8_t Atari6507::ror(uint8_t v){const bool c=flag(C);setFlag(C,v&1);v=uint8_t((v>>1)|(c?0x80:0));setNZ(v);return v;}
int Atari6507::branch(bool c){const int8_t d=int8_t(fetch());if(!c)return 2;const uint16_t old=m_pc;m_pc=uint16_t(m_pc+d);return 3+((old&0xFF00)!=(m_pc&0xFF00));}

int Atari6507::step()
{
    m_bus.m_cpuStepping=true;
    m_busCycles=0;
    const uint8_t op=fetch(); uint16_t a=0; uint8_t v=0; int cycles=2; bool page=false;
    auto idle=[&](){m_bus.tick(1);++m_busCycles;};
    auto zp=[&](){return uint16_t(fetch());};
    auto zpx=[&](){const uint8_t b=fetch();idle();return uint16_t(uint8_t(b+m_x));};
    auto zpy=[&](){const uint8_t b=fetch();idle();return uint16_t(uint8_t(b+m_y));};
    auto abs=[&](){return fetch16();};
    auto abx=[&](bool alwaysDummy=false){uint16_t b=fetch16(),q=uint16_t(b+m_x);page=(b&0xFF00)!=(q&0xFF00);if(page||alwaysDummy)idle();return q;};
    auto aby=[&](bool alwaysDummy=false){uint16_t b=fetch16(),q=uint16_t(b+m_y);page=(b&0xFF00)!=(q&0xFF00);if(page||alwaysDummy)idle();return q;};
    auto inx=[&](){uint8_t q=fetch();idle();q=uint8_t(q+m_x);return uint16_t(read(q))|(uint16_t(read(uint8_t(q+1)))<<8);};
    auto iny=[&](bool alwaysDummy=false){uint8_t q=fetch();uint16_t b=uint16_t(read(q))|(uint16_t(read(uint8_t(q+1)))<<8),r=uint16_t(b+m_y);page=(b&0xFF00)!=(r&0xFF00);if(page||alwaysDummy)idle();return r;};
    auto loadA=[&](uint8_t q){m_a=q;setNZ(m_a);}; auto loadX=[&](uint8_t q){m_x=q;setNZ(m_x);}; auto loadY=[&](uint8_t q){m_y=q;setNZ(m_y);};
    auto bit=[&](uint8_t q){setFlag(Z,(m_a&q)==0);setFlag(N,q&0x80);setFlag(V,q&0x40);};
    switch(op) {
    case 0x00: m_pc++;push(uint8_t(m_pc>>8));push(uint8_t(m_pc));push(m_p|B|U);setFlag(I,true);m_pc=read16(0xFFFE);cycles=7;break;
    case 0x01:a=inx();m_a|=read(a);setNZ(m_a);cycles=6;break; case 0x05:a=zp();m_a|=read(a);setNZ(m_a);cycles=3;break; case 0x09:m_a|=fetch();setNZ(m_a);cycles=2;break; case 0x0D:a=abs();m_a|=read(a);setNZ(m_a);cycles=4;break; case 0x11:a=iny();m_a|=read(a);setNZ(m_a);cycles=5+page;break; case 0x15:a=zpx();m_a|=read(a);setNZ(m_a);cycles=4;break; case 0x19:a=aby();m_a|=read(a);setNZ(m_a);cycles=4+page;break; case 0x1D:a=abx();m_a|=read(a);setNZ(m_a);cycles=4+page;break;
    case 0x06:a=zp();write(a,asl(read(a)));cycles=5;break;case 0x0A:m_a=asl(m_a);cycles=2;break;case 0x0E:a=abs();write(a,asl(read(a)));cycles=6;break;case 0x16:a=zpx();write(a,asl(read(a)));cycles=6;break;case 0x1E:a=abx();write(a,asl(read(a)));cycles=7;break;
    case 0x08:push(m_p|B|U);cycles=3;break;case 0x10:cycles=branch(!flag(N));break;case 0x18:setFlag(C,false);break;case 0x20:a=fetch16();{uint16_t r=uint16_t(m_pc-1);push(uint8_t(r>>8));push(uint8_t(r));m_pc=a;}cycles=6;break;
    case 0x21:a=inx();m_a&=read(a);setNZ(m_a);cycles=6;break;case 0x24:a=zp();bit(read(a));cycles=3;break;case 0x25:a=zp();m_a&=read(a);setNZ(m_a);cycles=3;break;case 0x29:m_a&=fetch();setNZ(m_a);break;case 0x2C:a=abs();bit(read(a));cycles=4;break;case 0x2D:a=abs();m_a&=read(a);setNZ(m_a);cycles=4;break;case 0x31:a=iny();m_a&=read(a);setNZ(m_a);cycles=5+page;break;case 0x35:a=zpx();m_a&=read(a);setNZ(m_a);cycles=4;break;case 0x39:a=aby();m_a&=read(a);setNZ(m_a);cycles=4+page;break;case 0x3D:a=abx();m_a&=read(a);setNZ(m_a);cycles=4+page;break;
    case 0x26:a=zp();write(a,rol(read(a)));cycles=5;break;case 0x2A:m_a=rol(m_a);break;case 0x2E:a=abs();write(a,rol(read(a)));cycles=6;break;case 0x36:a=zpx();write(a,rol(read(a)));cycles=6;break;case 0x3E:a=abx();write(a,rol(read(a)));cycles=7;break;
    case 0x28:m_p=uint8_t((pull()&~B)|U);cycles=4;break;case 0x30:cycles=branch(flag(N));break;case 0x38:setFlag(C,true);break;case 0x40:m_p=uint8_t((pull()&~B)|U);{uint8_t l=pull(),h=pull();m_pc=uint16_t(l)|(uint16_t(h)<<8);}cycles=6;break;
    case 0x41:a=inx();m_a^=read(a);setNZ(m_a);cycles=6;break;case 0x45:a=zp();m_a^=read(a);setNZ(m_a);cycles=3;break;case 0x49:m_a^=fetch();setNZ(m_a);break;case 0x4D:a=abs();m_a^=read(a);setNZ(m_a);cycles=4;break;case 0x51:a=iny();m_a^=read(a);setNZ(m_a);cycles=5+page;break;case 0x55:a=zpx();m_a^=read(a);setNZ(m_a);cycles=4;break;case 0x59:a=aby();m_a^=read(a);setNZ(m_a);cycles=4+page;break;case 0x5D:a=abx();m_a^=read(a);setNZ(m_a);cycles=4+page;break;
    case 0x46:a=zp();write(a,lsr(read(a)));cycles=5;break;case 0x4A:m_a=lsr(m_a);break;case 0x4E:a=abs();write(a,lsr(read(a)));cycles=6;break;case 0x56:a=zpx();write(a,lsr(read(a)));cycles=6;break;case 0x5E:a=abx();write(a,lsr(read(a)));cycles=7;break;
    case 0x48:push(m_a);cycles=3;break;case 0x4C:m_pc=fetch16();cycles=3;break;case 0x50:cycles=branch(!flag(V));break;case 0x58:setFlag(I,false);break;case 0x60:{uint8_t l=pull(),h=pull();m_pc=uint16_t((uint16_t(l)|(uint16_t(h)<<8))+1);}cycles=6;break;
    case 0x61:a=inx();adc(read(a));cycles=6;break;case 0x65:a=zp();adc(read(a));cycles=3;break;case 0x69:adc(fetch());break;case 0x6D:a=abs();adc(read(a));cycles=4;break;case 0x71:a=iny();adc(read(a));cycles=5+page;break;case 0x75:a=zpx();adc(read(a));cycles=4;break;case 0x79:a=aby();adc(read(a));cycles=4+page;break;case 0x7D:a=abx();adc(read(a));cycles=4+page;break;
    case 0x66:a=zp();write(a,ror(read(a)));cycles=5;break;case 0x6A:m_a=ror(m_a);break;case 0x6E:a=abs();write(a,ror(read(a)));cycles=6;break;case 0x76:a=zpx();write(a,ror(read(a)));cycles=6;break;case 0x7E:a=abx();write(a,ror(read(a)));cycles=7;break;
    case 0x68:m_a=pull();setNZ(m_a);cycles=4;break;case 0x6C:m_pc=read16bug(fetch16());cycles=5;break;case 0x70:cycles=branch(flag(V));break;case 0x78:setFlag(I,true);break;
    case 0x81:a=inx();write(a,m_a);cycles=6;break;case 0x84:a=zp();write(a,m_y);cycles=3;break;case 0x85:a=zp();write(a,m_a);cycles=3;break;case 0x86:a=zp();write(a,m_x);cycles=3;break;case 0x88:m_y--;setNZ(m_y);break;case 0x8A:m_a=m_x;setNZ(m_a);break;case 0x8C:a=abs();write(a,m_y);cycles=4;break;case 0x8D:a=abs();write(a,m_a);cycles=4;break;case 0x8E:a=abs();write(a,m_x);cycles=4;break;
    case 0x90:cycles=branch(!flag(C));break;case 0x91:a=iny(true);write(a,m_a);cycles=6;break;case 0x94:a=zpx();write(a,m_y);cycles=4;break;case 0x95:a=zpx();write(a,m_a);cycles=4;break;case 0x96:a=zpy();write(a,m_x);cycles=4;break;case 0x98:m_a=m_y;setNZ(m_a);break;case 0x99:a=aby(true);write(a,m_a);cycles=5;break;case 0x9A:m_sp=m_x;break;case 0x9D:a=abx(true);write(a,m_a);cycles=5;break;
    case 0xA0:loadY(fetch());break;case 0xA1:a=inx();loadA(read(a));cycles=6;break;case 0xA2:loadX(fetch());break;case 0xA4:a=zp();loadY(read(a));cycles=3;break;case 0xA5:a=zp();loadA(read(a));cycles=3;break;case 0xA6:a=zp();loadX(read(a));cycles=3;break;case 0xA8:m_y=m_a;setNZ(m_y);break;case 0xA9:loadA(fetch());break;case 0xAA:m_x=m_a;setNZ(m_x);break;case 0xAC:a=abs();loadY(read(a));cycles=4;break;case 0xAD:a=abs();loadA(read(a));cycles=4;break;case 0xAE:a=abs();loadX(read(a));cycles=4;break;
    case 0xB0:cycles=branch(flag(C));break;case 0xB1:a=iny();loadA(read(a));cycles=5+page;break;case 0xB4:a=zpx();loadY(read(a));cycles=4;break;case 0xB5:a=zpx();loadA(read(a));cycles=4;break;case 0xB6:a=zpy();loadX(read(a));cycles=4;break;case 0xB8:setFlag(V,false);break;case 0xB9:a=aby();loadA(read(a));cycles=4+page;break;case 0xBA:m_x=m_sp;setNZ(m_x);break;case 0xBC:a=abx();loadY(read(a));cycles=4+page;break;case 0xBD:a=abx();loadA(read(a));cycles=4+page;break;case 0xBE:a=aby();loadX(read(a));cycles=4+page;break;
    case 0xC0:compare(m_y,fetch());break;case 0xC1:a=inx();compare(m_a,read(a));cycles=6;break;case 0xC4:a=zp();compare(m_y,read(a));cycles=3;break;case 0xC5:a=zp();compare(m_a,read(a));cycles=3;break;case 0xC6:a=zp();v=uint8_t(read(a)-1);write(a,v);setNZ(v);cycles=5;break;case 0xC8:m_y++;setNZ(m_y);break;case 0xC9:compare(m_a,fetch());break;case 0xCA:m_x--;setNZ(m_x);break;case 0xCC:a=abs();compare(m_y,read(a));cycles=4;break;case 0xCD:a=abs();compare(m_a,read(a));cycles=4;break;case 0xCE:a=abs();v=uint8_t(read(a)-1);write(a,v);setNZ(v);cycles=6;break;
    case 0xD0:cycles=branch(!flag(Z));break;case 0xD1:a=iny();compare(m_a,read(a));cycles=5+page;break;case 0xD5:a=zpx();compare(m_a,read(a));cycles=4;break;case 0xD6:a=zpx();v=uint8_t(read(a)-1);write(a,v);setNZ(v);cycles=6;break;case 0xD8:setFlag(D,false);break;case 0xD9:a=aby();compare(m_a,read(a));cycles=4+page;break;case 0xDD:a=abx();compare(m_a,read(a));cycles=4+page;break;case 0xDE:a=abx();v=uint8_t(read(a)-1);write(a,v);setNZ(v);cycles=7;break;
    case 0xE0:compare(m_x,fetch());break;case 0xE1:a=inx();sbc(read(a));cycles=6;break;case 0xE4:a=zp();compare(m_x,read(a));cycles=3;break;case 0xE5:a=zp();sbc(read(a));cycles=3;break;case 0xE6:a=zp();v=uint8_t(read(a)+1);write(a,v);setNZ(v);cycles=5;break;case 0xE8:m_x++;setNZ(m_x);break;case 0xE9:case 0xEB:sbc(fetch());break;case 0xEA:break;case 0xEC:a=abs();compare(m_x,read(a));cycles=4;break;case 0xED:a=abs();sbc(read(a));cycles=4;break;case 0xEE:a=abs();v=uint8_t(read(a)+1);write(a,v);setNZ(v);cycles=6;break;
    case 0xF0:cycles=branch(flag(Z));break;case 0xF1:a=iny();sbc(read(a));cycles=5+page;break;case 0xF5:a=zpx();sbc(read(a));cycles=4;break;case 0xF6:a=zpx();v=uint8_t(read(a)+1);write(a,v);setNZ(v);cycles=6;break;case 0xF8:setFlag(D,true);break;case 0xF9:a=aby();sbc(read(a));cycles=4+page;break;case 0xFD:a=abx();sbc(read(a));cycles=4+page;break;case 0xFE:a=abx();v=uint8_t(read(a)+1);write(a,v);setNZ(v);cycles=7;break;
    default:
        // Veel 2600-programma's gebruiken de stabiele illegale NOP-vormen.
        if(op==0x04||op==0x44||op==0x64){fetch();cycles=3;}
        else if(op==0x0C){fetch16();cycles=4;}
        else if(op==0x14||op==0x34||op==0x54||op==0x74||op==0xD4||op==0xF4){fetch();cycles=4;}
        else if(op==0x1A||op==0x3A||op==0x5A||op==0x7A||op==0xDA||op==0xFA){cycles=2;}
        else if(op==0x80||op==0x82||op==0x89||op==0xC2||op==0xE2){fetch();cycles=2;}
        else if(op==0x1C||op==0x3C||op==0x5C||op==0x7C||op==0xDC||op==0xFC){abx();cycles=4+page;}
        else cycles=2;
        break;
    }
    m_p|=U;
    // Every real bus access above advances one 6507 cycle at the point where
    // it occurs.  Complete only the remaining internal/dummy cycles here.
    if(m_busCycles<cycles)m_bus.tick(cycles-m_busCycles);
    m_bus.m_cpuStepping=false;
    return cycles;
}

void Atari2600Core::Riot::reset(){ram.fill(0);swcha=0xFF;swchaOut=swchaDdr=0;swchb=0xCB;intim=0;instat=0;prescaler=divider=1;underflow=false;zeroReadWindow=0;}
// A TIMxT write primes the divider one clock before its normal interval.
// Keep the zero crossing observable for one longest polling iteration; this
// models the RIOT wrap read window needed by cycle-aligned INTIM loops.
void Atari2600Core::Riot::setTimer(uint8_t v,int scale){intim=v;prescaler=scale;divider=1;underflow=false;zeroReadWindow=0;instat=0;}
void Atari2600Core::Riot::tick(int c){while(c-->0){if(zeroReadWindow>0)--zeroReadWindow;if(--divider<=0){divider=underflow?1:prescaler;if(intim==0){intim=0xFF;underflow=true;zeroReadWindow=8;instat|=0x80;}else intim--;}}}

void Atari2600Core::Tia::reset(){reg.fill(0);for(int i=0x20;i<=0x24;++i)reg[i]=0x80;std::fill(std::begin(input),std::end(input),0x80);for(auto& write:delayedWrites)write=DelayedWrite{};colorClock=scanline=0;frameDone=wsync=vsync=false;vblank=true;vsyncStartLine=visibleStartLine=0;std::fill(std::begin(collision),std::end(collision),0);grpNew[0]=grpNew[1]=grpOld[0]=grpOld[1]=0;playerCounter[0]=playerCounter[1]=0;playerRenderCounter[0]=playerRenderCounter[1]=0;playerSampleCounter[0]=playerSampleCounter[1]=playerCopy[0]=playerCopy[1]=0;playerDivider[0]=playerDivider[1]=playerDividerPending[0]=playerDividerPending[1]=1;playerDividerChangeCounter[0]=playerDividerChangeCounter[1]=-1;playerMode[0]=playerMode[1]=0;playerRendering[0]=playerRendering[1]=playerOn[0]=playerOn[1]=false;missileCounter[0]=missileCounter[1]=0;missileRenderCounter[0]=missileRenderCounter[1]=0;missileRendering[0]=missileRendering[1]=missileOn[0]=missileOn[1]=false;ballCounter=0;ballRenderCounter=0;ballRendering=ballOn=false;std::fill(std::begin(objectMoving),std::end(objectMoving),false);movementClock=0;movementInProgress=extendedHblank=false;playfieldOn=playfieldReflected=false;ballEnableNew=ballEnableOld=false;p0x=20;p1x=100;m0x=20;m1x=100;ballx=80;audClockEnable[0]=audClockEnable[1]=audNoiseFeedback[0]=audNoiseFeedback[1]=audNoiseBit[0]=audNoiseBit[1]=audPulseHold[0]=audPulseHold[1]=false;audDivider[0]=audDivider[1]=audPulse[0]=audPulse[1]=audNoise[0]=audNoise[1]=0;sampleAccumulator=audioDcInput=audioDcOutput=0.0;audio=nullptr;}

Atari2600Core::Atari2600Core() : m_cpu(*this){updatePalette();reset();}

const char* Atari2600Core::coreRevision()
{
    // Defined in the core implementation rather than the public header so
    // the log reports the revision of the object code actually linked into
    // the executable.  A stale duplicate header can no longer fake it.
    return "A2600-TIA-20260815-R84";
}

bool Atari2600Core::loadRom(const std::string& path,std::string* error)
{
    std::ifstream f(path,std::ios::binary);if(!f){if(error)*error="ROM file could not be opened";return false;}
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});
    if(!loadRomData(data,Mapper::Auto,error))return false;
    // ROM collections contain several verified Pac-Man revisions.  Stella's
    // database enables phosphor for them, while a single CRC only covered one
    // dump here.  Keep CRC matching for unnamed data and recognize the normal
    // cartridge filename as a fallback for the other revisions.
    std::string name=path;
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(name.find("pac-man")!=std::string::npos||name.find("pacman")!=std::string::npos||
       name.find("pac man")!=std::string::npos)m_phosphorEnabled=true;
    return true;
}

Atari2600Core::Mapper Atari2600Core::detectMapper(const std::vector<uint8_t>& data) const
{
    const std::size_t s=data.size();
    if(s>=8448 && (s%8448)==0)return Mapper::AR;
    if(s<=2048)return Mapper::Cart2K;
    if(s<=4096)return Mapper::Cart4K;
    if(s==8192) {
        auto occurrences=[&](const uint8_t* signature,std::size_t length){
            int count=0;
            for(std::size_t i=0;i+length<=s;++i)
                if(std::equal(signature,signature+length,data.begin()+i))++count;
            return count;
        };
        // Use Stella's instruction signatures.  Looking for the operand bytes
        // FE 01 anywhere in an 8K image classified ordinary game data as the
        // FE mapper (Crazy Climber [a1]) and disabled its F8 hotspots.
        static constexpr uint8_t f8Signatures[][3]={{0x8D,0xF9,0x1F},{0x8D,0xF9,0xFF}};
        bool probableF8=false;
        for(const auto& signature:f8Signatures)probableF8|=occurrences(signature,3)>=2;

        // Tigervision 3F cartridges switch their lower 2K ROM window by
        // writing the desired bank number to TIA mirror $003F.  Multiple
        // STA $3F instructions are a strong signature in an 8K image and
        // distinguish Espial from ordinary Atari F8 cartridges.
        static constexpr uint8_t threeFSignature[]={0x85,0x3F};
        if(occurrences(threeFSignature,2)>=2)return Mapper::ThreeF;

        static constexpr uint8_t bnjE7Signatures[][3]={
            {0xAD,0xE4,0xFF},{0xAD,0xE5,0xFF},{0xAD,0xE6,0xFF}};
        int bnjE7Matches=0;
        for(const auto& signature:bnjE7Signatures)
            bnjE7Matches+=occurrences(signature,3);
        if(bnjE7Matches>=3)return Mapper::E7;

        static constexpr uint8_t e0Signatures[][3]={
            {0x8D,0xE0,0x1F},{0x8D,0xE0,0x5F},{0x8D,0xE9,0xFF},{0x0C,0xE0,0x1F},
            {0xAD,0xE0,0x1F},{0xAD,0xE9,0xFF},{0xAD,0xED,0xFF},{0xAD,0xF3,0xBF}};
        for(const auto& signature:e0Signatures)
            if(occurrences(signature,3)>0)return Mapper::E0;

        static constexpr uint8_t feSignatures[][5]={
            {0x20,0x00,0xD0,0xC6,0xC5},{0x20,0xC3,0xF8,0xA5,0x82},
            {0xD0,0xFB,0x20,0x73,0xFE},{0xD0,0xFB,0x20,0x68,0xFE},
            {0x20,0x00,0xF0,0x84,0xD6}};
        if(!probableF8)
            for(const auto& signature:feSignatures)
                if(occurrences(signature,5)>0)return Mapper::FE;
        return Mapper::F8;
    }
    if(s==16384){
        // Crack'ed prototype, CRC32 1B5E52A7.  Keep an exact database-style
        // override in addition to the generic repeated-RAM-window signature;
        // this cartridge is unusable (black display) when treated as F6.
        uint32_t crc=0xFFFFFFFFu;
        for(const uint8_t byte:data){
            crc^=byte;
            for(int bit=0;bit<8;++bit)
                crc=(crc>>1)^((crc&1)?0xEDB88320u:0u);
        }
        crc^=0xFFFFFFFFu;
        if(crc==0x1B5E52A7u)return Mapper::F6SC;

        bool superchip=true;
        for(std::size_t bank=1;bank<4 && superchip;++bank)
            for(std::size_t i=0;i<128;++i)
                if(data[i]!=data[bank*4096+i]){superchip=false;break;}
        if(superchip)return Mapper::F6SC;
    }
    if(s<=16384)return Mapper::F6;
    if(s<=32768)return Mapper::F4;
    return Mapper::Cart4K;
}

bool Atari2600Core::loadRomData(const std::vector<uint8_t>& data,Mapper mapper,std::string* error)
{
    if(data.empty()||data.size()>1024*1024){if(error)*error="Unsupported or empty Atari 2600 ROM";return false;}
    // Een nieuw geplaatste cartridge begint met de twee momentary console-
    // schakelaars los. Zo kan een oude, nog gequeue-de Start/Select-status
    // uit de vorige machine of cartridge het spel niet vanzelf starten.
    m_resetPressed=false;
    m_selectPressed=false;
    // Stella's ROM properties enable phosphor for games which deliberately
    // alternate objects between frames. Pac-Man PAL uses this for its four
    // power pellets. Keep this as a ROM property, not as altered TIA timing.
    uint32_t crc=0xFFFFFFFFu;
    for(const uint8_t byte:data){
        crc^=byte;
        for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xEDB88320u:0u);
    }
    crc^=0xFFFFFFFFu;
    // These kernels deliberately multiplex objects over alternating frames.
    // A CRT's phosphor hides that multiplexing; retain the previous frame in
    // the same way Stella's per-ROM phosphor property does.
    m_phosphorEnabled=(crc==0x4947EF99u || // Pac-Man PAL, MD5 fc2233fc...
                       crc==0xE9B05565u);  // Amidar PAL/NTSC50
    // Amidar's PAL release is an NTSC50 conversion: its kernel generates a
    // 311-line/50 Hz frame, but its colour constants still address the NTSC
    // hue wheel.  Coupling the palette directly to PAL timing turns its
    // purple maze red and its ochre enemies green.
    m_forceNtscPalette=(crc==0xE9B05565u);
    m_mindLinkEnabled=(crc==0x347B9516u);
    m_mindLinkPosition=0x2A00;
    m_mindLinkBit[0]=m_mindLinkBit[1]=0;
    m_rom=data;m_mapper=(mapper==Mapper::Auto)?detectMapper(data):mapper;updatePalette();reset();return true;
}
void Atari2600Core::eject(){m_rom.clear();reset();}
void Atari2600Core::reset()
{
    m_riot.reset();m_tia.reset();
    m_lastFrameScanlines=0;m_busAccessCount=0;m_arWritePending=false;
    m_bank=(m_mapper==Mapper::F8?1:(m_mapper==Mapper::F6||m_mapper==Mapper::F6SC)?3:m_mapper==Mapper::F4?7:0);
    m_superchipRam.fill(0);
    m_e0Banks={{0,1,2,7}};
    m_frame.fill(0xFF000000u);m_previousDisplayFrame.fill(0xFF000000u);
    m_havePreviousDisplayFrame=false;m_debugTraceHead=m_debugTraceCount=0;
    updateInputPorts();
    if(m_rom.empty())return;
    if(m_mapper==Mapper::AR && arActivateBlock(0)){
        return;
    }
    m_cpu.reset();
}
void Atari2600Core::setNtsc(bool n){m_ntsc=n;m_totalScanlines=n?262:312;updatePalette();}
void Atari2600Core::setPhosphorEnabled(bool enabled)
{
    m_phosphorEnabled=enabled;
    m_previousDisplayFrame.fill(0xFF000000u);
    m_havePreviousDisplayFrame=false;
}

const char* Atari2600Core::mapperName() const{switch(m_mapper){case Mapper::Cart2K:return"2K";case Mapper::Cart4K:return"4K";case Mapper::F8:return"F8";case Mapper::F6:return"F6";case Mapper::F6SC:return"F6SC";case Mapper::F4:return"F4";case Mapper::FE:return"FE";case Mapper::E0:return"E0";case Mapper::E7:return"E7/BNJ";case Mapper::ThreeF:return"3F";case Mapper::AR:return"AR/Supercharger";default:return"Auto";}}

void Atari2600Core::setJoystick(int port,bool up,bool down,bool left,bool right,bool fire)
{
    if(port==0&&m_mindLinkEnabled){
        m_mindLinkLeft=left||up;
        m_mindLinkRight=right||down;
        m_mindLinkFire=fire;
    }
    uint8_t shift=port?0:4,mask=uint8_t(0x0F<<shift),bits=0x0F;
    if(up) bits&=~0x1;
    if(down) bits&=~0x2;
    if(left) bits&=~0x4;
    if(right) bits&=~0x8;
    m_riot.swcha=uint8_t((m_riot.swcha&~mask)|((bits<<shift)&mask));
    const int input=port?5:4;m_tia.input[input]=fire?0x00:0x80;
}

uint8_t Atari2600Core::riotReadSwcha() const
{
    uint8_t input=m_riot.swcha;
    if(m_mindLinkEnabled){
        // MindLink identifies itself by holding pin 3 low; pin 4 carries the
        // serial position bit.  Bionic Breakthrough waits on this presence
        // signal before it ever enters the calibration/transfer routine.
        input&=uint8_t(~0x44);
        const uint16_t serialValue=uint16_t(m_mindLinkPosition|(m_mindLinkFire?0x8000:0));
        const bool rightData=((serialValue>>m_mindLinkBit[0])&1u)!=0;
        const bool leftData=((serialValue>>m_mindLinkBit[1])&1u)!=0;
        if(rightData)input|=0x08;else input&=uint8_t(~0x08);
        if(leftData)input|=0x80;else input&=uint8_t(~0x80);
    }
    return uint8_t((m_riot.swchaOut&m_riot.swchaDdr)|(input&uint8_t(~m_riot.swchaDdr)));
}

void Atari2600Core::riotWriteSwcha(uint8_t value)
{
    const uint8_t previous=m_riot.swchaOut;
    m_riot.swchaOut=value;
    if(!m_mindLinkEnabled)return;
    // Pin 1 of each jack clocks the next serial bit.  This prototype declares
    // MindLink devices on both controller ports.
    if(!(previous&0x01)&&(value&0x01))m_mindLinkBit[0]=uint8_t((m_mindLinkBit[0]+1)&15);
    if(!(previous&0x10)&&(value&0x10))m_mindLinkBit[1]=uint8_t((m_mindLinkBit[1]+1)&15);
}
void Atari2600Core::setPaddle(int paddle,int position,bool fire)
{
    if(paddle<0||paddle>3)return;
    position=std::max(-32768,std::min(32767,position));
    if(m_mindLinkEnabled&&paddle==0){
        // The UI's paddle axis is also our mouse-like MindLink input.  Map
        // its complete signed range linearly to the controller's real 16-bit
        // calibration range instead of presenting a normal TIA potentiometer.
        constexpr int minPos=0x0B00,maxPos=0x6500;
        m_mindLinkPosition=uint16_t(minPos+
            (int64_t(position+32768)*(maxPos-minPos))/65535);
        m_mindLinkFire=fire;
        return;
    }
    m_tia.paddleEnabled[paddle]=true;
    // Real hardware measures approximately 380 scanlines at the far-left
    // (1 MOhm) position and almost zero at the far-right position.
    const int normalized=32767-position;
    m_tia.paddleThreshold[paddle]=int((int64_t(normalized)*380*228)/65535);
    if(!(m_tia.reg[0x01]&0x80))
        m_tia.input[paddle]=m_tia.paddleChargeClocks>=m_tia.paddleThreshold[paddle]?0x80:0x00;

    static constexpr uint8_t buttonBits[4]={0x80,0x40,0x08,0x04};
    if(fire)m_riot.swcha&=uint8_t(~buttonBits[paddle]);
    else m_riot.swcha|=buttonBits[paddle];
}
void Atari2600Core::setConsoleSwitches(bool r,bool s,bool c,bool la,bool ra){m_resetPressed=r;m_selectPressed=s;m_color=c;m_leftDifficultyA=la;m_rightDifficultyA=ra;updateInputPorts();}
void Atari2600Core::setResetSwitch(bool pressed){m_resetPressed=pressed;updateInputPorts();}
void Atari2600Core::updateInputPorts(){uint8_t v=0xCB;if(m_resetPressed)v&=~0x01;if(m_selectPressed)v&=~0x02;if(!m_color)v&=~0x08;if(m_leftDifficultyA)v&=~0x40;else v|=0x40;if(m_rightDifficultyA)v&=~0x80;else v|=0x80;m_riot.swchb=v;}

bool Atari2600Core::arLoadBlock(std::size_t block)
{
    constexpr std::size_t LoadSize=8448,PageData=8192,RamSize=6144;
    const std::size_t base=block*LoadSize;
    if(base+LoadSize>m_rom.size())return false;
    m_arMemory.fill(0);
    std::copy_n(m_rom.begin()+base+RamSize,2048,m_arMemory.begin()+RamSize);
    const std::size_t header=base+PageData;
    const std::size_t pages=std::min<std::size_t>(m_rom[header+3],24);
    for(std::size_t pageIndex=0;pageIndex<pages;++pageIndex){
        const uint8_t map=m_rom[header+16+pageIndex];
        const std::size_t bank=map&0x03u;
        const std::size_t page=(map&0x1Cu)>>2;
        if(bank<3)
            std::copy_n(m_rom.begin()+base+pageIndex*256,256,
                        m_arMemory.begin()+bank*2048+page*256);
    }
    m_arCurrentBlock=block;
    return true;
}

bool Atari2600Core::arActivateBlock(std::size_t block)
{
    constexpr std::size_t LoadSize=8448,PageData=8192;
    if(!arLoadBlock(block))return false;
    const std::size_t header=block*LoadSize+PageData;
    const uint16_t start=uint16_t(m_rom[header]) |
                         (uint16_t(m_rom[header+1])<<8);
    arBankConfiguration(m_rom[header+2]);
    // Match the state left behind by the Supercharger cassette loader.
    m_riot.ram[0x7E]=m_rom[header];
    m_riot.ram[0x7F]=m_rom[header+1];
    m_riot.ram[0x00]=m_rom[header+2];
    m_arWritePending=false;
    m_cpu.startAt(start);
    return true;
}

void Atari2600Core::arBankConfiguration(uint8_t configuration)
{
    static constexpr std::size_t lower[8]={4096,0,4096,0,4096,2048,4096,2048};
    static constexpr std::size_t upper[8]={6144,6144,0,4096,6144,6144,2048,4096};
    const unsigned mode=(configuration&0x1Cu)>>2;
    m_arConfiguration=configuration&0x1Fu;
    m_arWriteEnabled=(configuration&0x02u)!=0;
    m_arOffsets={{lower[mode],upper[mode]}};
}

std::size_t Atari2600Core::arImageIndex(uint16_t address) const
{
    return (address&0x07FFu)+m_arOffsets[(address&0x0800u)?1:0];
}

void Atari2600Core::arHandleAccess(uint16_t address)
{
    address&=0x1FFF;
    if(m_arWritePending && m_busAccessCount>m_arDataHoldAccess+5)
        m_arWritePending=false;
    if(!(address&0x0F00u) && (!m_arWriteEnabled || !m_arWritePending)){
        m_arDataHold=uint8_t(address);
        m_arDataHoldAccess=m_busAccessCount;
        m_arWritePending=true;
    }else if(address==0x1FF8){
        m_arWritePending=false;
        arBankConfiguration(m_arDataHold);
    }else if(m_arWriteEnabled && m_arWritePending &&
             m_busAccessCount==m_arDataHoldAccess+5){
        const std::size_t offset=arImageIndex(address);
        if(offset<6144)m_arMemory[offset]=m_arDataHold;
        m_arWritePending=false;
    }
}

void Atari2600Core::cartridgeHotspot(uint16_t a)
{
    a&=0x1FFF;
    if(m_mapper==Mapper::F8&&a>=0x1FF8&&a<=0x1FF9)m_bank=a-0x1FF8;
    else if((m_mapper==Mapper::F6||m_mapper==Mapper::F6SC)&&a>=0x1FF6&&a<=0x1FF9)m_bank=a-0x1FF6;
    else if(m_mapper==Mapper::F4&&a>=0x1FF4&&a<=0x1FFB)m_bank=a-0x1FF4;
    else if(m_mapper==Mapper::FE&&(a==0x01FE||a==0x01FF))m_bank=a&1;
    else if(m_mapper==Mapper::E7&&a>=0x1FE4&&a<=0x1FE6)m_bank=a-0x1FE4;
    else if(m_mapper==Mapper::E0){if(a>=0x1FE0&&a<=0x1FE7)m_e0Banks[0]=a&7;else if(a>=0x1FE8&&a<=0x1FEF)m_e0Banks[1]=a&7;else if(a>=0x1FF0&&a<=0x1FF7)m_e0Banks[2]=a&7;}
}

uint8_t Atari2600Core::readCartridge(uint16_t a)
{
    if(m_mapper==Mapper::AR){
        arHandleAccess(a);
        return m_arMemory[arImageIndex(a)];
    }
    cartridgeHotspot(a);if(m_rom.empty())return 0xFF;const uint16_t off=a&0x0FFF;std::size_t index=0;
    if(m_mapper==Mapper::F6SC && off>=0x0080 && off<0x0100)
        return m_superchipRam[off&0x7F];
    if(m_mapper==Mapper::Cart2K)index=off&0x07FF;else if(m_mapper==Mapper::Cart4K)index=off;
    else if(m_mapper==Mapper::E0){int slot=off>>10;index=std::size_t(m_e0Banks[slot])*1024+(off&1023);}
    else if(m_mapper==Mapper::E7){index=off<0x0800?std::size_t(m_bank)*0x0800+off:0x1800+(off-0x0800);}
    else if(m_mapper==Mapper::ThreeF){index=off<0x0800?std::size_t(m_bank)*0x0800+off:(m_rom.size()-0x0800)+(off-0x0800);}
    else index=std::size_t(m_bank)*4096+off;
    return m_rom[index%m_rom.size()];
}

uint8_t Atari2600Core::read(uint16_t a)
{
    ++m_busAccessCount;
    a&=0x1FFF;
    if(m_mapper==Mapper::FE&&(a==0x01FE||a==0x01FF))cartridgeHotspot(a);
    if(a&0x1000)return readCartridge(a);
    if((a&0x1080)==0)return tiaRead(uint8_t(a&0x0F));
    if((a&0x1280)==0x0080)return m_riot.ram[a&0x7F];
    if((a&0x1280)==0x0280){switch(a&0x1F){case 0:return riotReadSwcha();case 1:return m_riot.swchaDdr;case 2:return m_riot.swchb;case 4:return m_riot.zeroReadWindow>0?0:m_riot.intim;case 5:{uint8_t v=m_riot.instat;m_riot.instat&=0x7F;return v;}default:return 0xFF;}}
    return 0xFF;
}

void Atari2600Core::write(uint16_t a,uint8_t v)
{
    ++m_busAccessCount;
    a&=0x1FFF;
    if(m_mapper==Mapper::ThreeF&&(a&0x1080)==0&&(a&0x003F)==0x003F){
        const std::size_t bankCount=m_rom.size()/0x0800;
        m_bank=bankCount?int(v%bankCount):0;
    }
    if(m_mapper==Mapper::FE&&(a==0x01FE||a==0x01FF))cartridgeHotspot(a);
    if(a&0x1000){
        if(m_mapper==Mapper::AR)arHandleAccess(a);
        else{
            if(m_mapper==Mapper::F6SC && (a&0x0FFF)<0x0080)
                m_superchipRam[a&0x7F]=v;
            cartridgeHotspot(a);
        }
        return;
    }
    if((a&0x1080)==0){
        tiaWrite(uint8_t(a&0x3F),v);
        return;
    }
    if((a&0x1280)==0x0080){m_riot.ram[a&0x7F]=v;return;}
    if((a&0x1280)==0x0280){switch(a&0x1F){case 0x00:riotWriteSwcha(v);break;case 0x01:m_riot.swchaDdr=v;break;case 0x14:m_riot.setTimer(v,1);break;case 0x15:m_riot.setTimer(v,8);break;case 0x16:m_riot.setTimer(v,64);break;case 0x17:m_riot.setTimer(v,1024);break;default:break;}}
}

uint8_t Atari2600Core::tiaRead(uint8_t a){if(a<=7)return m_tia.collision[a];if(a>=8&&a<=13)return m_tia.input[a-8];return 0;}
uint8_t Atari2600Core::tiaResxCounter() const
{
    const int hblankEnd=m_tia.extendedHblank?76:68;
    if(m_tia.colorClock>=hblankEnd)return 157;
    return m_tia.colorClock>=73?158:159;
}
void Atari2600Core::tiaWrite(uint8_t a,uint8_t v)
{
    // Internal delayed events used by the TIA graphics latch pipeline.
    // They deliberately share the GRP write's delay and insertion order.
    constexpr uint8_t ShuffleP0=0xF0,ShuffleP1=0xF1,ShuffleBall=0xF2;
    int delay=0;
    if(a>=0x0D&&a<=0x0F)delay=2;       // PF0..PF2
    else if(a==0x1B||a==0x1C)delay=1;  // GRP0/GRP1
    else if(a>=0x20&&a<=0x24)delay=2;  // horizontal motion registers
    else if(a==0x0B||a==0x0C)delay=1;  // REFP0/REFP1
    else if(a>=0x1D&&a<=0x1F)delay=1;  // ENAM0/1, ENABL
    else if(a==0x01)delay=1;           // VBLANK
    else if(a==0x2A)delay=6;           // HMOVE
    else if(a==0x2B)delay=2;           // HMCLR

    auto queueWrite=[&](uint8_t address,uint8_t value,int clocks){
        for(auto& write:m_tia.delayedWrites){
            // Stella's queue puts a delay-N write in slot current+N.  Since
            // this compact queue counts down at the start of every following
            // color clock, it needs N+1 here to execute on the same slot.
            if(!write.active){write.address=address;write.value=value;write.clocks=clocks+1;write.active=true;return true;}
        }
        return false;
    };
    if(delay>0){
        if(!queueWrite(a,v,delay))applyTiaWrite(a,v);
        if(a==0x1B)queueWrite(ShuffleP1,0,delay);
        else if(a==0x1C){queueWrite(ShuffleP0,0,delay);queueWrite(ShuffleBall,0,delay);}
        return;
    }
    applyTiaWrite(a,v);
}

void Atari2600Core::applyTiaWrite(uint8_t a,uint8_t v)
{
    constexpr uint8_t ShuffleP0=0xF0,ShuffleP1=0xF1,ShuffleBall=0xF2;
    if(a==ShuffleP0){m_tia.grpOld[0]=m_tia.grpNew[0];return;}
    if(a==ShuffleP1){m_tia.grpOld[1]=m_tia.grpNew[1];return;}
    if(a==ShuffleBall){m_tia.ballEnableOld=m_tia.ballEnableNew;return;}
    m_tia.reg[a]=v;
    switch(a){
    case 0x00:{
        const bool newVsync=(v&0x02)!=0;
        if(newVsync&&!m_tia.vsync){
            m_tia.vsync=true;
            m_tia.vsyncStartLine=m_tia.scanline;
        }else if(!newVsync&&m_tia.vsync){
            const int vsyncLines=m_tia.scanline-m_tia.vsyncStartLine;
            m_tia.vsync=false;
            if(vsyncLines>=2){
                // The cartridge program defines the television standard by
                // the frame it generates.  Preserve that measured length so
                // the host can select NTSC/PAL automatically instead of
                // forcing an NTSC kernel through PAL timing (or vice versa).
                m_lastFrameScanlines=m_tia.scanline;
                // VSYNC defines the frame boundary for both standards.  The
                // old PAL-only deferred boundary allowed the remainder of
                // this scanline to enter the next host buffer.  Kernels that
                // update score graphics close to VSYNC then lost alternating
                // character rows.  Use the same proven boundary as NTSC;
                // PAL timing remains determined by its generated scanlines.
                m_tia.frameDone=true;
                m_tia.scanline=0;
            }
        }
        break;
    }
    case 0x01:{
        if(v&0x80){
            m_tia.paddleChargeClocks=0;
            for(int i=0;i<4;++i)if(m_tia.paddleEnabled[i])m_tia.input[i]=0x00;
        }
        const bool newVblank=(v&0x02)!=0;
        if(m_tia.vblank&&!newVblank){
            m_tia.visibleStartLine=m_tia.scanline;
        }
        m_tia.vblank=newVblank;
        break;
    }
    case 0x02:m_tia.wsync=true;break;
    case 0x04:setPlayerNusiz(0,v);break;
    case 0x05:setPlayerNusiz(1,v);break;
    case 0x10: {
        const uint8_t counter=tiaResxCounter();
        m_tia.p0x=m_tia.colorClock<68?3:(m_tia.colorClock-68+5)%160;
        m_tia.playerCounter[0]=counter;
        if(m_tia.playerRendering[0]&&m_tia.playerRenderCounter[0]+5<4)
            m_tia.playerRenderCounter[0]=int8_t(-5+(counter-157));
        break;
    }
    case 0x11: {
        const uint8_t counter=tiaResxCounter();
        m_tia.p1x=m_tia.colorClock<68?3:(m_tia.colorClock-68+5)%160;
        m_tia.playerCounter[1]=counter;
        if(m_tia.playerRendering[1]&&m_tia.playerRenderCounter[1]+5<4)
            m_tia.playerRenderCounter[1]=int8_t(-5+(counter-157));
        break;
    }
    case 0x12:m_tia.m0x=m_tia.colorClock<68?2:(m_tia.colorClock-68+4)%160;m_tia.missileCounter[0]=tiaResxCounter();break;
    case 0x13:m_tia.m1x=m_tia.colorClock<68?2:(m_tia.colorClock-68+4)%160;m_tia.missileCounter[1]=tiaResxCounter();break;
    case 0x14:{
        const uint8_t counter=tiaResxCounter();
        m_tia.ballx=m_tia.colorClock<68?2:(m_tia.colorClock-68+4)%160;
        m_tia.ballCounter=counter;
        m_tia.ballRendering=true;
        m_tia.ballRenderCounter=int8_t(-4+(counter-157));
        break;
    }
    case 0x1B:
        m_tia.grpNew[0]=v;
        break;
    case 0x1C:
        m_tia.grpNew[1]=v;
        break;
    case 0x1F:m_tia.ballEnableNew=(v&0x02)!=0;break;
    case 0x2A: {
        m_tia.movementClock=0;
        m_tia.movementInProgress=true;
        m_tia.extendedHblank=true;
        std::fill(std::begin(m_tia.objectMoving),std::end(m_tia.objectMoving),true);
        break;
    }
    case 0x2B:for(int i=0x20;i<=0x24;i++)m_tia.reg[i]=0;break; // HMCLR
    case 0x2C:std::fill(std::begin(m_tia.collision),std::end(m_tia.collision),0);break; // CXCLR
    default:break;
    }
}

uint8_t Atari2600Core::playfieldPixel(int x) const
{
    int p=x/4;if(p>=20){if(m_tia.reg[0x0A]&1)p=39-p;else p-=20;}
    if(p<4) return (m_tia.reg[0x0D]>>(4+p))&1;
    if(p<12) return (m_tia.reg[0x0E]>>(11-p))&1;
    return (m_tia.reg[0x0F]>>(p-12))&1;
}
void Atari2600Core::tickPlayfield(int x)
{
    // The TIA only samples a new playfield bit every four colour clocks.
    // Reflection is likewise latched at the two half-screen boundaries.
    if(x==0||x==79)m_tia.playfieldReflected=(m_tia.reg[0x0A]&1)!=0;
    if((x&3)!=0)return;
    int bit=x>>2;
    if(bit>=20)bit=m_tia.playfieldReflected?39-bit:bit-20;
    if(bit<4)m_tia.playfieldOn=((m_tia.reg[0x0D]>>(4+bit))&1)!=0;
    else if(bit<12)m_tia.playfieldOn=((m_tia.reg[0x0E]>>(11-bit))&1)!=0;
    else m_tia.playfieldOn=((m_tia.reg[0x0F]>>(bit-12))&1)!=0;
}
bool Atari2600Core::playerPixel(int p,int x) const
{
    (void)x;
    return m_tia.playerOn[p];
}

void Atari2600Core::setPlayerNusiz(int p,uint8_t value)
{
    static constexpr uint8_t decodeMasks[8]={0x01,0x03,0x05,0x07,0x09,0x01,0x0D,0x01};
    const uint8_t oldMode=m_tia.playerMode[p];
    const uint8_t newMode=value&7;
    const uint8_t pending=newMode==5?2:(newMode==7?4:1);
    m_tia.playerMode[p]=newMode;
    m_tia.playerDividerPending[p]=pending;

    const uint8_t previousCounter=uint8_t((m_tia.playerCounter[p]+159)%160);
    const int previousCopy=previousCounter==156?0:previousCounter==12?1:previousCounter==28?2:previousCounter==60?3:-1;
    if(!m_tia.playerRendering[p]&&previousCopy>=0&&(decodeMasks[newMode]&(1u<<previousCopy))){
        m_tia.playerRendering[p]=true;
        m_tia.playerSampleCounter[p]=0;
        m_tia.playerRenderCounter[p]=-5;
        m_tia.playerCopy[p]=uint8_t(previousCopy);
    }

    if(oldMode!=newMode&&m_tia.playerRendering[p]&&m_tia.playerRenderCounter[p]+5<2){
        const int source=(int(m_tia.playerCounter[p])-int(m_tia.playerRenderCounter[p])-6+160)%160;
        const int copy=source==156?0:source==12?1:source==28?2:source==60?3:-1;
        if(copy<0||(decodeMasks[newMode]&(1u<<copy))==0)m_tia.playerRendering[p]=false;
    }

    const int oldDivider=m_tia.playerDivider[p];
    if(oldDivider==pending)return;
    auto applyDivider=[&](){m_tia.playerDivider[p]=pending;m_tia.playerDividerChangeCounter[p]=-1;};
    if(!m_tia.playerRendering[p]){applyDivider();return;}

    const bool hblank=m_tia.colorClock<(m_tia.extendedHblank?76:68);
    const int delta=m_tia.playerRenderCounter[p]+5;
    const int transition=(oldDivider<<4)|pending;
    switch(transition){
    case 0x12:case 0x14:
        if(hblank){if(delta<4)applyDivider();else m_tia.playerDividerChangeCounter[p]=delta<5?1:0;}
        else {if(delta<3)applyDivider();else m_tia.playerDividerChangeCounter[p]=1;}
        break;
    case 0x21:case 0x41:
        if(delta<(hblank?4:3))applyDivider();
        else if(delta<(hblank?6:5)){applyDivider();--m_tia.playerRenderCounter[p];}
        else m_tia.playerDividerChangeCounter[p]=hblank?0:1;
        break;
    case 0x42:case 0x24:
        if(m_tia.playerRenderCounter[p]<1||(hblank&&m_tia.playerRenderCounter[p]%oldDivider==1))applyDivider();
        else m_tia.playerDividerChangeCounter[p]=int8_t(oldDivider-(m_tia.playerRenderCounter[p]-1)%oldDivider);
        break;
    default:applyDivider();break;
    }
}

void Atari2600Core::tickPlayer(int p)
{
    static constexpr uint8_t decodeMasks[8]={0x01,0x03,0x05,0x07,0x09,0x01,0x0D,0x01};
    static constexpr uint8_t decodeCounter[4]={156,12,28,60};
    const int mode=m_tia.playerMode[p];
    const int divider=m_tia.playerDivider[p];

    bool on=false;
    const int tripPoint=divider==1?0:1;
    if(m_tia.playerRendering[p]&&m_tia.playerRenderCounter[p]>=tripPoint&&m_tia.playerSampleCounter[p]<8){
        const uint8_t graphics=(m_tia.reg[0x25+p]&1)?m_tia.grpOld[p]:m_tia.grpNew[p];
        const int sample=m_tia.playerSampleCounter[p];
        const int bit=(m_tia.reg[0x0B+p]&0x08)?sample:(7-sample);
        on=((graphics>>bit)&1)!=0;
    }
    m_tia.playerOn[p]=on;

    bool decoded=false;
    for(int copy=0;copy<4;++copy){
        if((decodeMasks[mode]&(1<<copy))&&m_tia.playerCounter[p]==decodeCounter[copy]){
            m_tia.playerRendering[p]=true;
            m_tia.playerRenderCounter[p]=-5;
            m_tia.playerSampleCounter[p]=0;
            m_tia.playerCopy[p]=uint8_t(copy);
            decoded=true;
            break;
        }
    }
    if(!decoded&&m_tia.playerRendering[p]){
        ++m_tia.playerRenderCounter[p];
        if(divider==1){
            if(m_tia.playerRenderCounter[p]>0)++m_tia.playerSampleCounter[p];
            if(m_tia.playerRenderCounter[p]>=0&&m_tia.playerDividerChangeCounter[p]>=0&&
               m_tia.playerDividerChangeCounter[p]--==0){
                m_tia.playerDivider[p]=m_tia.playerDividerPending[p];
                m_tia.playerDividerChangeCounter[p]=-1;
            }
        }else if(m_tia.playerRenderCounter[p]>1&&
                 ((m_tia.playerRenderCounter[p]-1)&(divider-1))==0){
            ++m_tia.playerSampleCounter[p];
        }
        if(divider!=1&&m_tia.playerRenderCounter[p]>0&&m_tia.playerDividerChangeCounter[p]>=0&&
           m_tia.playerDividerChangeCounter[p]--==0){
            m_tia.playerDivider[p]=m_tia.playerDividerPending[p];
            m_tia.playerDividerChangeCounter[p]=-1;
        }
        if(m_tia.playerSampleCounter[p]>7)m_tia.playerRendering[p]=false;
    }
    if(++m_tia.playerCounter[p]>=160)m_tia.playerCounter[p]=0;
}
void Atari2600Core::tickMissile(int p)
{
    static constexpr uint8_t decodeMasks[8]={0x01,0x03,0x05,0x07,0x09,0x01,0x0D,0x01};
    static constexpr uint8_t decodeCounter[4]={156,12,28,60};
    const int mode=m_tia.reg[0x04+p]&7;
    const int width=1<<((m_tia.reg[0x04+p]>>4)&3);

    m_tia.missileOn[p]=m_tia.missileRendering[p]&&
        m_tia.missileRenderCounter[p]>=0&&m_tia.missileRenderCounter[p]<width&&
        (m_tia.reg[0x1D+p]&0x02)!=0&&(m_tia.reg[0x28+p]&0x02)==0;

    bool decoded=false;
    for(int copy=0;copy<4;++copy){
        if((decodeMasks[mode]&(1<<copy))&&m_tia.missileCounter[p]==decodeCounter[copy]){
            m_tia.missileRendering[p]=true;
            m_tia.missileRenderCounter[p]=-4;
            decoded=true;
            break;
        }
    }
    if(!decoded&&m_tia.missileRendering[p]){
        ++m_tia.missileRenderCounter[p];
        if(m_tia.missileRenderCounter[p]>=width)m_tia.missileRendering[p]=false;
    }
    if(++m_tia.missileCounter[p]>=160)m_tia.missileCounter[p]=0;

    // RESMP does not continuously copy the player counter.  The real TIA
    // samples it only at FSTOB: pixel 4 of the player's main copy.  This
    // sampled position remains in the missile counter after RESMP unlocks.
    if((m_tia.reg[0x28+p]&0x02)&&m_tia.playerRendering[p]&&
       m_tia.playerCopy[p]==0&&m_tia.playerSampleCounter[p]==4){
        const int divider=m_tia.playerDivider[p];
        const int shift=divider==1?5:(divider==2?8:12);
        m_tia.missileCounter[p]=uint8_t((int(m_tia.playerCounter[p])+160-shift)%160);
    }
}
bool Atari2600Core::missilePixel(int p,int x) const
{
    (void)x;
    return m_tia.missileOn[p];
}
void Atari2600Core::tickBall()
{
    const int width=1<<((m_tia.reg[0x0A]>>4)&3);
    const bool enabled=(m_tia.reg[0x27]&1)?m_tia.ballEnableOld:m_tia.ballEnableNew;
    m_tia.ballOn=m_tia.ballRendering&&m_tia.ballRenderCounter>=0&&
        m_tia.ballRenderCounter<width&&enabled;
    if(m_tia.ballCounter==156){
        m_tia.ballRendering=true;
        m_tia.ballRenderCounter=-4;
    }else if(m_tia.ballRendering&&++m_tia.ballRenderCounter>=width){
        m_tia.ballRendering=false;
    }
    if(++m_tia.ballCounter>=160)m_tia.ballCounter=0;
}
bool Atari2600Core::ballPixel(int x) const
{
    (void)x;
    return m_tia.ballOn;
}
void Atari2600Core::renderPixel()
{
    // Capture a centred 240-line window one source scanline at a time.  The
    // previous PAL path compressed all 312 raster lines into NTSC's 262-line
    // geometry while rendering.  Several PAL lines consequently landed on
    // the same output row and overwrote each other, visibly removing thin
    // rows from characters and score text.  Cropping the blanking interval
    // instead preserves every displayed PAL scanline without changing TIA
    // timing; the host still presents the resulting 160x240 image as 320x240.
    const int displayStartLine=(m_totalScanlines-Height)/2;
    const int x=m_tia.colorClock-68,y=m_tia.scanline-displayStartLine;
    if(x<0||x>=Width||m_tia.vblank)return;
    if(m_tia.extendedHblank&&m_tia.colorClock<76){
        if(y>=0&&y<Height)m_frame[y*Width+x]=0xFF000000u;
        return;
    }
    const bool pf=m_tia.playfieldOn,p0=playerPixel(0,x),p1=playerPixel(1,x);
    const bool m0=missilePixel(0,x),m1=missilePixel(1,x),bl=ballPixel(x);

    // TIA collision latches.  Bits remain set until CXCLR is strobed.
    if(m0&&p1)m_tia.collision[0]|=0x80; // CXM0P: M0-P1
    if(m0&&p0)m_tia.collision[0]|=0x40; // CXM0P: M0-P0
    if(m1&&p0)m_tia.collision[1]|=0x80; // CXM1P: M1-P0
    if(m1&&p1)m_tia.collision[1]|=0x40; // CXM1P: M1-P1
    if(p0&&pf)m_tia.collision[2]|=0x80; // CXP0FB: P0-PF
    if(p0&&bl)m_tia.collision[2]|=0x40; // CXP0FB: P0-BL
    if(p1&&pf)m_tia.collision[3]|=0x80; // CXP1FB: P1-PF
    if(p1&&bl)m_tia.collision[3]|=0x40; // CXP1FB: P1-BL
    if(m0&&pf)m_tia.collision[4]|=0x80; // CXM0FB: M0-PF
    if(m0&&bl)m_tia.collision[4]|=0x40; // CXM0FB: M0-BL
    if(m1&&pf)m_tia.collision[5]|=0x80; // CXM1FB: M1-PF
    if(m1&&bl)m_tia.collision[5]|=0x40; // CXM1FB: M1-BL
    if(bl&&pf)m_tia.collision[6]|=0x80; // CXBLPF: BL-PF
    if(p0&&p1)m_tia.collision[7]|=0x80; // CXPPMM: P0-P1
    if(m0&&m1)m_tia.collision[7]|=0x40; // CXPPMM: M0-M1

    // Collisions are generated over the complete active TIA line, including
    // lines outside the host's selected display crop.
    if(y<0||y>=Height)return;

    const bool player0=p0||m0,player1=p1||m1;
    uint8_t pfColor=(m_tia.reg[0x0A]&2)?(x<80?m_tia.reg[0x06]:m_tia.reg[0x07]):m_tia.reg[0x08];
    uint8_t color=m_tia.reg[0x09];
    if(m_tia.reg[0x0A]&4){
        // CTRLPF priority: PF/BL, P0/M0, P1/M1, background.
        if(player1)color=m_tia.reg[0x07];
        if(player0)color=m_tia.reg[0x06];
        if(bl)color=m_tia.reg[0x08];
        if(pf)color=pfColor;
    }else if(m_tia.reg[0x0A]&2){
        // Score mode: P0/M0, PF, P1/M1, BL, background.
        if(bl)color=m_tia.reg[0x08];
        if(player1)color=m_tia.reg[0x07];
        if(pf)color=pfColor;
        if(player0)color=m_tia.reg[0x06];
    }else{
        // Normal: P0/M0, P1/M1, PF/BL, background.
        if(bl)color=m_tia.reg[0x08];
        if(pf)color=pfColor;
        if(player1)color=m_tia.reg[0x07];
        if(player0)color=m_tia.reg[0x06];
    }
    m_frame[y*Width+x]=m_palette[(color>>1)&0x7F];
}
void Atari2600Core::clockAudioPhase0(int ch)
{
    const uint8_t audc=m_tia.reg[0x15+ch]&0x0F;
    const uint8_t audf=m_tia.reg[0x17+ch]&0x1F;
    if(m_tia.audClockEnable[ch]){
        m_tia.audNoiseBit[ch]=(m_tia.audNoise[ch]&1)!=0;
        switch(audc&3){
        case 0:case 1:m_tia.audPulseHold[ch]=false;break;
        case 2:m_tia.audPulseHold[ch]=(m_tia.audNoise[ch]&0x1E)!=0x02;break;
        case 3:m_tia.audPulseHold[ch]=!m_tia.audNoiseBit[ch];break;
        }
        if((audc&3)==0)
            m_tia.audNoiseFeedback[ch]=(((m_tia.audPulse[ch]^m_tia.audNoise[ch])&1)!=0)||
                !(m_tia.audNoise[ch]||(m_tia.audPulse[ch]!=0x0A))||!(audc&0x0C);
        else
            m_tia.audNoiseFeedback[ch]=((((m_tia.audNoise[ch]&4)?1:0)^(m_tia.audNoise[ch]&1))!=0)||m_tia.audNoise[ch]==0;
    }
    m_tia.audClockEnable[ch]=m_tia.audDivider[ch]==audf;
    m_tia.audDivider[ch]=(m_tia.audDivider[ch]==audf||m_tia.audDivider[ch]==0x1F)?0:uint8_t(m_tia.audDivider[ch]+1);
}

void Atari2600Core::clockAudioPhase1(int ch)
{
    if(!m_tia.audClockEnable[ch])return;
    const uint8_t audc=m_tia.reg[0x15+ch]&0x0F;
    bool pulseFeedback=false;
    switch(audc>>2){
    case 0:pulseFeedback=((((m_tia.audPulse[ch]&2)?1:0)^(m_tia.audPulse[ch]&1))!=0)&&m_tia.audPulse[ch]!=0x0A&&(audc&3);break;
    case 1:pulseFeedback=(m_tia.audPulse[ch]&8)==0;break;
    case 2:pulseFeedback=!m_tia.audNoiseBit[ch];break;
    case 3:pulseFeedback=!((m_tia.audPulse[ch]&2)||!(m_tia.audPulse[ch]&0x0E));break;
    }
    m_tia.audNoise[ch]>>=1;
    if(m_tia.audNoiseFeedback[ch])m_tia.audNoise[ch]|=0x10;
    if(!m_tia.audPulseHold[ch]){
        m_tia.audPulse[ch]=uint8_t(~(m_tia.audPulse[ch]>>1))&0x07;
        if(pulseFeedback)m_tia.audPulse[ch]|=0x08;
    }
}

void Atari2600Core::generateAudioCycle()
{
    const double clock=m_ntsc?CpuClockNtsc:CpuClockPal;m_tia.sampleAccumulator+=SampleRate/clock;
    if(m_tia.audio&&m_tia.sampleAccumulator>=1.0){
        m_tia.sampleAccumulator-=1.0;
        int raw=0;
        for(int ch=0;ch<2;ch++)
            if(m_tia.audPulse[ch]&1)raw+=(m_tia.reg[0x19+ch]&15)*900;

        // De echte console-uitgang is AC-gekoppeld.  Deze DC-blocker voorkomt
        // de zware brom/klik die een vaste digitale offset veroorzaakt.
        const double filtered=double(raw)-m_tia.audioDcInput+0.995*m_tia.audioDcOutput;
        m_tia.audioDcInput=double(raw);
        m_tia.audioDcOutput=filtered;
        m_tia.audio->push_back(int16_t(std::clamp(int(filtered),-32768,32767)));
    }
}
void Atari2600Core::tickMovement()
{
    if(!m_tia.movementInProgress||(m_tia.colorClock&3)!=0)return;
    const uint8_t clock=m_tia.movementClock>15?0:m_tia.movementClock;
    const int hblankEnd=m_tia.extendedHblank?76:68;
    const bool hblank=m_tia.colorClock<hblankEnd;
    const uint8_t registers[5]={0x20,0x21,0x22,0x23,0x24};
    for(int object=0;object<5;++object){
        if(!m_tia.objectMoving[object])continue;
        const uint8_t stopClock=uint8_t((m_tia.reg[registers[object]]>>4)^0x08);
        if(clock==stopClock){m_tia.objectMoving[object]=false;continue;}
        if(!hblank)continue;
        if(object<2)tickPlayer(object);
        else if(object<4)tickMissile(object-2);
        else tickBall();
    }
    m_tia.movementInProgress=false;
    for(bool moving:m_tia.objectMoving)m_tia.movementInProgress|=moving;
    ++m_tia.movementClock;
}
void Atari2600Core::tickTiaColorClock(){
    if(m_tia.reg[0x01]&0x80){
        m_tia.paddleChargeClocks=0;
    }else{
        ++m_tia.paddleChargeClocks;
        for(int i=0;i<4;++i)
            if(m_tia.paddleEnabled[i]&&m_tia.paddleChargeClocks>=m_tia.paddleThreshold[i])
                m_tia.input[i]=0x80;
    }
    // Stella verwerkt de gepipeline-de TIA-registers vóór de objecttick van
    // de color-clock waarop hun delay verstrijkt. Na renderPixel toepassen
    // maakte iedere PF/GRP/ENA/REFP/HMOVE-write effectief één klok te laat.
    for(auto& write:m_tia.delayedWrites){
        if(write.active&&--write.clocks<=0){
            const uint8_t address=write.address,value=write.value;
            write.active=false;
            applyTiaWrite(address,value);
        }
    }
    if((m_tia.colorClock==9)||(m_tia.colorClock==81))
        for(int ch=0;ch<2;++ch)clockAudioPhase0(ch);
    if((m_tia.colorClock==37)||(m_tia.colorClock==149))
        for(int ch=0;ch<2;++ch)clockAudioPhase1(ch);
    tickMovement();
    const int playfieldX=m_tia.colorClock-68;
    if(playfieldX>=0&&playfieldX<Width)tickPlayfield(playfieldX);
    const int hblankEnd=m_tia.extendedHblank?76:68;
    if(m_tia.colorClock>=hblankEnd){tickPlayer(0);tickPlayer(1);tickMissile(0);tickMissile(1);tickBall();}
    else {m_tia.playerOn[0]=m_tia.playerOn[1]=false;m_tia.missileOn[0]=m_tia.missileOn[1]=false;m_tia.ballOn=false;}
    renderPixel();
    if(++m_tia.colorClock>=228){
        m_tia.colorClock=0;
        m_tia.extendedHblank=false;
        ++m_tia.scanline;
        m_tia.wsync=false;
        // Safety net for malformed ROMs. Normal frames end on a valid VSYNC pulse.
        if(m_tia.scanline>=m_totalScanlines+64){
            m_tia.scanline=0;
            m_tia.frameDone=true;
            m_tia.vsync=false;
        }
    }
}
void Atari2600Core::tick(int c){m_riot.tick(c);while(c-->0){generateAudioCycle();tickTiaColorClock();tickTiaColorClock();tickTiaColorClock();}}

uint8_t Atari2600Core::debugPeek(uint16_t a) const
{
    a&=0x1FFF;
    if(a&0x1000){
        if(m_rom.empty())return 0xFF;
        if(m_mapper==Mapper::AR)return m_arMemory[arImageIndex(a)];
        const uint16_t off=a&0x0FFF;
        if(m_mapper==Mapper::F6SC && off>=0x0080 && off<0x0100)
            return m_superchipRam[off&0x7F];
        std::size_t index=0;
        if(m_mapper==Mapper::Cart2K)index=off&0x07FF;
        else if(m_mapper==Mapper::Cart4K)index=off;
        else if(m_mapper==Mapper::E0){const int slot=off>>10;index=std::size_t(m_e0Banks[slot])*1024+(off&1023);}
        else if(m_mapper==Mapper::E7){index=off<0x0800?std::size_t(m_bank)*0x0800+off:0x1800+(off-0x0800);}
        else if(m_mapper==Mapper::ThreeF){index=off<0x0800?std::size_t(m_bank)*0x0800+off:(m_rom.size()-0x0800)+(off-0x0800);}
        else index=std::size_t(m_bank)*4096+off;
        return m_rom[index%m_rom.size()];
    }
    if((a&0x1080)==0){const uint8_t r=uint8_t(a&0x0F);return r<=7?m_tia.collision[r]:(r>=8&&r<=13?m_tia.input[r-8]:0);}
    if((a&0x1280)==0x0080)return m_riot.ram[a&0x7F];
    if((a&0x1280)==0x0280){switch(a&0x1F){case 0:return m_riot.swcha;case 2:return m_riot.swchb;case 4:return m_riot.intim;case 5:return m_riot.instat;default:return 0xFF;}}
    return 0xFF;
}

void Atari2600Core::debugStepInstruction()
{
    if(m_rom.empty())return;
    m_tia.audio=nullptr;
    recordDebugTrace();
    m_cpu.step();
    if(m_tia.wsync){
        const int colorClocks=(228-m_tia.colorClock)%228;
        const int cpuCycles=(colorClocks+2)/3;
        m_riot.tick(cpuCycles);
        for(int cycle=0;cycle<cpuCycles;++cycle)generateAudioCycle();
        for(int clock=0;clock<colorClocks;++clock)tickTiaColorClock();
        if(colorClocks==0)m_tia.wsync=false;
    }
}

void Atari2600Core::recordDebugTrace()
{
    if(!m_debugTraceEnabled)return;
    const uint16_t pc=m_cpu.pc();
    m_debugTrace[m_debugTraceHead]={pc,debugPeek(pc),debugPeek(uint16_t(pc+1)),debugPeek(uint16_t(pc+2)),
                                    m_cpu.a(),m_cpu.x(),m_cpu.y(),m_cpu.sp(),m_cpu.status()};
    m_debugTraceHead=(m_debugTraceHead+1)%m_debugTrace.size();
    if(m_debugTraceCount<m_debugTrace.size())++m_debugTraceCount;
}

void Atari2600Core::setDebugTraceEnabled(bool enabled)
{
    m_debugTraceEnabled=enabled;
    m_debugTraceHead=m_debugTraceCount=0;
}

std::vector<Atari2600Core::DebugTraceEntry> Atari2600Core::debugTrace() const
{
    std::vector<DebugTraceEntry> trace;trace.reserve(m_debugTraceCount);
    const std::size_t first=(m_debugTraceHead+m_debugTrace.size()-m_debugTraceCount)%m_debugTrace.size();
    for(std::size_t i=0;i<m_debugTraceCount;++i)trace.push_back(m_debugTrace[(first+i)%m_debugTrace.size()]);
    return trace;
}

bool Atari2600Core::runFrame(std::vector<int16_t>& audio,const std::vector<uint16_t>& breakpoints)
{
    audio.clear();
    if(m_rom.empty()){
        // An empty cartridge slot is electrically idle.  Do not continue
        // executing the previous 6507 state against $FF bus reads: that
        // creates random TIA writes, garbage at the top of the display and
        // crackling audio while no ROM is inserted.
        m_frame.fill(0xFF000000u);
        m_previousDisplayFrame.fill(0xFF000000u);
        m_havePreviousDisplayFrame=false;
        audio.resize(m_ntsc?735:882,0);
        m_tia.audio=nullptr;
        return false;
    }
    if(m_mindLinkEnabled){
        // Stella refreshes the controller once per video frame and starts a
        // new transfer at the least-significant bit.
        m_mindLinkBit[0]=m_mindLinkBit[1]=0;
        // Cursor-controlled MindLink fallback: keep its travel speed close to
        // the gradual keyboard paddle instead of jumping between extremes.
        constexpr int step=0x0040;
        if(m_mindLinkLeft&&!m_mindLinkRight)m_mindLinkPosition=uint16_t(std::max(0x0B00,int(m_mindLinkPosition)-step));
        else if(m_mindLinkRight&&!m_mindLinkLeft)m_mindLinkPosition=uint16_t(std::min(0x6500,int(m_mindLinkPosition)+step));
    }
    // A cartridge may intentionally open the display more than once in a
    // single TV frame (Beat 'Em & Eat 'Em draws its game area and status
    // strip in separate visible intervals).  Clearing on every VBLANK-off
    // transition erased everything drawn by the earlier interval.  Clear
    // exactly once, before generating the new frame instead.
    m_frame.fill(0xFF000000u);
    m_tia.audio=&audio;m_tia.frameDone=false;int guard=40000;
    while(!m_tia.frameDone&&guard-->0){
        // Multi-load Supercharger images concatenate 8448-byte cassette
        // loads.  Games return to the cartridge's fixed loader ROM when the
        // next cassette segment is required.  Dump files contain no audio
        // loader ROM there (the fixed 2K window is blank), so recognize that
        // entry and directly perform the next cassette load.
        if(m_mapper==Mapper::AR && m_rom.size()>8448 &&
           arImageIndex(m_cpu.pc())>=6144){
            const std::size_t loadCount=m_rom.size()/8448;
            if(m_arCurrentBlock+1<loadCount){
                arActivateBlock(m_arCurrentBlock+1);
                continue;
            }
        }
        if(std::find(breakpoints.begin(),breakpoints.end(),m_cpu.pc())!=breakpoints.end()){
            m_tia.audio=nullptr;
            return true;
        }
        recordDebugTrace();
        m_cpu.step();
        if(m_tia.wsync){
            // WSYNC halts the 6507 until the exact end of the current
            // scanline.  Rounding this up to complete CPU cycles advanced
            // the TIA one or two colour clocks into the next line, so a
            // kernel with a conditional drawing path could alternate its
            // horizontal phase from line to line.
            // Stella/TIA: WSYNC waits to the next horizontal boundary.
            // At clock 0 that boundary has already been reached, so the
            // modulo result is zero.  Treating it as 228 inserted a complete
            // extra scanline (Q*bert's upward jump and Pac-Man's top row).
            const int colorClocks=(228-m_tia.colorClock)%228;
            const int cpuCycles=(colorClocks+2)/3;
            m_riot.tick(cpuCycles);
            for(int cycle=0;cycle<cpuCycles;++cycle)generateAudioCycle();
            for(int clock=0;clock<colorClocks;++clock)tickTiaColorClock();
            if(colorClocks==0)m_tia.wsync=false;
        }
    }
    if(m_phosphorEnabled){
        if(m_havePreviousDisplayFrame){
            // Retain 85% of the previous displayed frame.  Pac-Man's ghosts
            // and power pills are multiplexed between frames; 60% still
            // produced a very visible brightness pulse on modern LCDs.
            // channel unless the current frame is brighter.
            for(std::size_t i=0;i<m_frame.size();++i){
                const uint32_t current=m_frame[i],previous=m_previousDisplayFrame[i];
                uint32_t mixed=0xFF000000u;
                for(int shift=0;shift<=16;shift+=8){
                    const uint32_t c=(current>>shift)&0xFFu;
                    const uint32_t p=(((previous>>shift)&0xFFu)*85u)/100u;
                    mixed|=std::max(c,p)<<shift;
                }
                m_frame[i]=mixed;
            }
        }else m_havePreviousDisplayFrame=true;
        m_previousDisplayFrame=m_frame;
    }
    m_tia.audio=nullptr;const std::size_t wanted=m_ntsc?735:882;if(audio.size()<wanted)audio.resize(wanted,0);else if(audio.size()>wanted)audio.resize(wanted);
    return false;
}

void Atari2600Core::updatePalette()
{
    // Stella's Standard palettes, ordered as 16 hues x 8 luminance levels.
    static constexpr uint32_t ntsc[128]={
        0x000000,0x4a4a4a,0x6f6f6f,0x8e8e8e,0xaaaaaa,0xc0c0c0,0xd6d6d6,0xececec,
        0x484800,0x69690f,0x86861d,0xa2a22a,0xbbbb35,0xd2d240,0xe8e84a,0xfcfc54,
        0x7c2c00,0x904811,0xa26221,0xb47a30,0xc3903d,0xd2a44a,0xdfb755,0xecc860,
        0x901c00,0xa33915,0xb55328,0xc66c3a,0xd5824a,0xe39759,0xf0aa67,0xfcbc74,
        0x940000,0xa71a1a,0xb83232,0xc84848,0xd65c5c,0xe46f6f,0xf08080,0xfc9090,
        0x840064,0x97197a,0xa8308f,0xb846a2,0xc659b3,0xd46cc3,0xe07cd2,0xec8ce0,
        0x500084,0x68199a,0x7d30ad,0x9246c0,0xa459d0,0xb56ce0,0xc57cee,0xd48cfc,
        0x140090,0x331aa3,0x4e32b5,0x6848c6,0x7f5cd5,0x956fe3,0xa980f0,0xbc90fc,
        0x000094,0x181aa7,0x2d32b8,0x4248c8,0x545cd6,0x656fe4,0x7580f0,0x8490fc,
        0x001c88,0x183b9d,0x2d57b0,0x4272c2,0x548ad2,0x65a0e1,0x75b5ef,0x84c8fc,
        0x003064,0x185080,0x2d6d98,0x4288b0,0x54a0c5,0x65b7d9,0x75cceb,0x84e0fc,
        0x004030,0x18624e,0x2d8169,0x429e82,0x54b899,0x65d1ae,0x75e7c2,0x84fcd4,
        0x004400,0x1a661a,0x328432,0x48a048,0x5cba5c,0x6fd26f,0x80e880,0x90fc90,
        0x143c00,0x355f18,0x527e2d,0x6e9c42,0x87b754,0x9ed065,0xb4e775,0xc8fc84,
        0x303800,0x505916,0x6d762b,0x88923e,0xa0ab4f,0xb7c25f,0xccd86e,0xe0ec7c,
        0x482c00,0x694d14,0x866a26,0xa28638,0xbb9f47,0xd2b656,0xe8cc63,0xfce070
    };
    static constexpr uint32_t pal[128]={
        0x0b0b0b,0x333333,0x595959,0x7b7b7b,0x999999,0xb6b6b6,0xcfcfcf,0xe6e6e6,
        0x0b0b0b,0x333333,0x595959,0x7b7b7b,0x999999,0xb6b6b6,0xcfcfcf,0xe6e6e6,
        0x3b2400,0x664700,0x8b7000,0xac9200,0xc5ae36,0xdec85e,0xf7e27f,0xfff19e,
        0x004500,0x006f00,0x3b9200,0x65b009,0x85ca3d,0xa3e364,0xbffc84,0xd5ffa5,
        0x590000,0x802700,0xa15700,0xbc7937,0xd6985f,0xeeb381,0xffce9e,0xffdcbd,
        0x004900,0x007200,0x169216,0x45af45,0x6bc96b,0x8be38b,0xa9fba9,0xc5ffc5,
        0x640012,0x890821,0xa73d4d,0xc26472,0xdc8491,0xf4a3ae,0xffbeca,0xffdae0,
        0x003d29,0x006a48,0x048e63,0x3caa84,0x62c5a2,0x83dfbe,0xa1f8d9,0xbeffe9,
        0x550046,0x88006e,0xa5318d,0xc159aa,0xda7cc5,0xf39adf,0xffb9f3,0xffd4f6,
        0x003651,0x005a7d,0x117e9c,0x429cb8,0x68b7d2,0x88d2eb,0xa6ebff,0xc3ffff,
        0x4c007c,0x75009d,0x932eb8,0xaf57d2,0xca7aeb,0xe499ff,0xecb7ff,0xf3d4ff,
        0x002d83,0x003ea4,0x2d65bf,0x5685da,0x79a2f2,0x99bfff,0xb7dbff,0xd3f5ff,
        0x220096,0x5200b6,0x7538cf,0x945fe8,0xb181ff,0xc5a0ff,0xd6bdff,0xe8daff,
        0x00009a,0x241db6,0x504ad0,0x746fe9,0x928eff,0xb1adff,0xcecaff,0xe9e5ff,
        0x0b0b0b,0x333333,0x595959,0x7b7b7b,0x999999,0xb6b6b6,0xcfcfcf,0xe6e6e6,
        0x0b0b0b,0x333333,0x595959,0x7b7b7b,0x999999,0xb6b6b6,0xcfcfcf,0xe6e6e6
    };
    // Stella applies its default PC-to-TV gamma correction even when all
    // palette sliders are neutral.  Previously PAL used the unadjusted table
    // and NTSC used an unrelated approximation, so neither matched Stella.
    const uint32_t* source=(m_ntsc||m_forceNtscPalette)?ntsc:pal;
    auto adjusted=[](uint32_t channel){
        const double value=std::pow(double(channel)/255.0,1.1333)*256.0+0.5;
        return uint32_t(std::clamp(int(value),0,255));
    };
    for(int i=0;i<128;++i){
        const uint32_t raw=source[i];
        const uint32_t r=adjusted((raw>>16)&0xFF),g=adjusted((raw>>8)&0xFF),b=adjusted(raw&0xFF);
        m_palette[i]=0xFF000000u|(r<<16)|(g<<8)|b;
    }
}
