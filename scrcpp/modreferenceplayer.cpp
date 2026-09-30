#include "modreferenceplayer.h"
#include <QAudioFormat>
#include <QtMath>
#include <cstring>
#include <algorithm>
#include <cmath>

quint16 ModReferencePlayer::be16(const QByteArray& d,int o){return (quint16(quint8(d[o]))<<8)|quint8(d[o+1]);}
double ModReferencePlayer::semitonePeriod(double p,int s){return p/std::pow(2.0,double(s)/12.0);} 

ModReferencePlayer::ModReferencePlayer(QObject* p):QObject(p){}
ModReferencePlayer::~ModReferencePlayer(){stop();}

bool ModReferencePlayer::load(const QByteArray& d, QString* err){
    stop(); m_loaded=false; m_samples.clear(); m_patterns.clear(); m_order.clear();
    if(d.size()<1084){if(err)*err="MOD too small";return false;}
    QByteArray sig=d.mid(1080,4); if(!(sig=="M.K."||sig=="M!K!"||sig=="4CHN"||sig=="FLT4"||sig=="TDZ4")){if(err)*err="Only classic 4-channel MOD supported";return false;}
    m_title=QString::fromLatin1(d.mid(0,20)).split(QChar('\0')).value(0).trimmed(); m_samples.resize(31);
    int sampleBytes=0; for(int i=0;i<31;i++){int o=20+i*30; auto &s=m_samples[i]; int len=be16(d,o+22)*2; int ft=quint8(d[o+24])&15; if(ft>=8)ft-=16; s.finetune=ft; s.volume=std::clamp(int(quint8(d[o+25])),0,64); s.loopStart=be16(d,o+26)*2; s.loopLen=be16(d,o+28)*2; s.pcm.resize(len); sampleBytes+=len;}
    int songLen=std::clamp(int(quint8(d[950])),1,128), maxPat=0; for(int i=0;i<songLen;i++){int p=quint8(d[952+i]);m_order<<p;maxPat=std::max(maxPat,p);} int pc=maxPat+1; int pos=1084; if(pos+pc*1024>d.size()){if(err)*err="MOD pattern data truncated";return false;}
    m_patterns.resize(pc); for(int p=0;p<pc;p++){m_patterns[p].resize(256);for(int i=0;i<256;i++){int b0=quint8(d[pos]),b1=quint8(d[pos+1]),b2=quint8(d[pos+2]),b3=quint8(d[pos+3]);pos+=4; Cell c; c.sample=(b0&0xF0)|((b2>>4)&15); c.period=((b0&15)<<8)|b1; c.effect=b2&15;c.param=b3;m_patterns[p][i]=c;}}
    for (int i = 0; i < 31; ++i) {
        const qsizetype sampleSize = m_samples[i].pcm.size();
        qsizetype n = sampleSize;
        if (static_cast<qsizetype>(pos) + n > d.size())
            n = qMax<qsizetype>(0, d.size() - static_cast<qsizetype>(pos));

        if (n > 0)
            memcpy(m_samples[i].pcm.data(), d.constData() + pos, static_cast<size_t>(n));

        pos += static_cast<int>(sampleSize);
    }
    m_loaded=true; return true;
}

void ModReferencePlayer::play(bool loop){if(!m_loaded)return;stop();m_loop=loop;m_orderPos=m_row=m_tick=0;m_speed=6;m_bpm=125;m_v[0]=Voice();m_v[1]=Voice();m_v[2]=Voice();m_v[3]=Voice();startRow();
    QAudioFormat f;f.setSampleRate(m_rate);f.setChannelCount(2);f.setSampleFormat(QAudioFormat::Int16);m_sink=new QAudioSink(QMediaDevices::defaultAudioOutput(),f,this);m_sink->setBufferSize(8192);m_dev=new Device(this);m_dev->open(QIODevice::ReadOnly);m_playing=true;m_samplesToTick=m_rate*(2.5/double(m_bpm));m_sink->start(m_dev);}
void ModReferencePlayer::stop(){m_playing=false;if(m_sink){m_sink->stop();delete m_sink;m_sink=nullptr;}if(m_dev){m_dev->close();delete m_dev;m_dev=nullptr;}}

qint64 ModReferencePlayer::Device::readData(char* d,qint64 n){if(!m_p||!m_p->m_playing){memset(d,0,size_t(n));return n;}int frames=int(n/4);m_p->render(reinterpret_cast<qint16*>(d),frames);return frames*4;}

void ModReferencePlayer::startRow(){if(m_orderPos>=m_order.size()){if(m_loop){m_orderPos=0;m_row=0;}else{m_playing=false;return;}}int pat=m_order[m_orderPos]; if(pat<0||pat>=m_patterns.size())return;auto &cells=m_patterns[pat];
    for(int ch=0;ch<4;ch++){Cell c=cells[m_row*4+ch];Voice &v=m_v[ch]; if(c.sample>0&&c.sample<=31){v.sample=c.sample;v.volume=m_samples[c.sample-1].volume;} v.effect=c.effect;v.param=c.param;
        if(c.effect==0x0C)v.volume=std::clamp(c.param,0,64); if(c.effect==0x0F&&c.param){if(c.param<=31)m_speed=c.param;else m_bpm=c.param;}
        if(c.period>0){ if(c.effect==0x03){v.targetPeriod=c.period;} else {v.period=v.basePeriod=c.period;v.pos=0.0;} }
        if(c.effect==0x09 && v.sample>0) v.pos=double(c.param*256);
    } m_tick=0; m_samplesToTick=m_rate*(2.5/double(m_bpm));}

void ModReferencePlayer::processTick(){for(int ch=0;ch<4;ch++){Voice &v=m_v[ch];int x=(v.param>>4)&15,y=v.param&15;switch(v.effect){case 0x01:if(m_tick>0)v.period=std::max(57,v.period-v.param);break;case 0x02:if(m_tick>0)v.period=std::min(1712,v.period+v.param);break;case 0x03:if(m_tick>0&&v.targetPeriod>0){if(v.period<v.targetPeriod)v.period=std::min(v.targetPeriod,v.period+v.param);else if(v.period>v.targetPeriod)v.period=std::max(v.targetPeriod,v.period-v.param);}break;case 0x0A:if(m_tick>0)v.volume=std::clamp(v.volume+x-y,0,64);break;default:break;}}}
void ModReferencePlayer::advanceRow(){m_tick++; if(m_tick<m_speed){processTick();m_samplesToTick+=m_rate*(2.5/double(m_bpm));return;}m_row++;if(m_row>=64){m_row=0;m_orderPos++;}startRow();}

void ModReferencePlayer::render(qint16* out,int frames){for(int i=0;i<frames;i++){while(m_samplesToTick<=0&&m_playing)advanceRow();double mix=0.0;for(int ch=0;ch<4;ch++){Voice &v=m_v[ch];if(v.sample<=0||v.sample>m_samples.size()||v.period<=0)continue;Sample &s=m_samples[v.sample-1];if(s.pcm.isEmpty())continue;double period=v.period; if(v.effect==0x00&&v.param){int step=(m_tick%3==1)?((v.param>>4)&15):(m_tick%3==2?(v.param&15):0);period=semitonePeriod(v.basePeriod>0?v.basePeriod:v.period,step);} if(v.effect==0x04){int dep=v.param&15,spd=(v.param>>4)&15;v.vibPhase+=spd*0.12;period*=std::pow(2.0,(std::sin(v.vibPhase)*dep*0.08)/12.0);} double hz=7093789.2/(2.0*period);double adv=hz/double(m_rate);int idx=int(v.pos);if(idx>=s.pcm.size()){if(s.loopLen>2&&s.loopStart<s.pcm.size()){double rel=v.pos-s.loopStart;v.pos=s.loopStart+std::fmod(std::max(0.0,rel),double(s.loopLen));idx=int(v.pos);}else continue;}qint8 smp=qint8(s.pcm[idx]);mix+=(double(smp)/128.0)*(double(v.volume)/64.0);v.pos+=adv;}mix*=0.23;mix=std::clamp(mix,-1.0,1.0);qint16 q=qint16(mix*32767.0);out[i*2]=q;out[i*2+1]=q;m_samplesToTick-=1.0;}}
