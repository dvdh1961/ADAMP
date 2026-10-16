#ifndef ADAMP_ADAM_HARDDISK_H
#define ADAMP_ADAM_HARDDISK_H
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

// Raw, linear 1024-byte AdamNet blocks. No floppy sector interleave.
// File ownership belongs to the emulation thread; UI uses queued slots.
class AdamHardDiskImage {
    FILE* file_ = nullptr;
    uint32_t blocks_ = 0;
    std::string path_;
    static unsigned word(const uint8_t* p) { return p[0] | (unsigned(p[1]) << 8); }
    static void putWord(uint8_t* p, unsigned v) { p[0]=uint8_t(v);p[1]=uint8_t(v>>8); }
public:
    ~AdamHardDiskImage() { close(); }
    AdamHardDiskImage() = default;
    AdamHardDiskImage(const AdamHardDiskImage&) = delete;
    AdamHardDiskImage& operator=(const AdamHardDiskImage&) = delete;
    bool mounted() const { return file_ != nullptr; }
    uint32_t blocks() const { return blocks_; }
    const std::string& path() const { return path_; }
    bool flush() { return file_ && std::fflush(file_)==0; }
    bool close() {
        bool ok=true;
        if(file_) {ok=std::fflush(file_)==0; if(std::fclose(file_)!=0)ok=false;}
        file_=nullptr;blocks_=0;path_.clear();return ok;
    }
    bool open(const char* path, std::string& error) {
        FILE* next=std::fopen(path,"r+b");
        if(!next){error="Cannot open hard disk image for reading and writing.";return false;}
        if(std::fseek(next,0,SEEK_END)!=0){std::fclose(next);error="Cannot read image size.";return false;}
        long size=std::ftell(next);
        if(size<2048 || size>20L*1024*1024 || size%1024){std::fclose(next);error="Use a linear image of 2 KB to 20 MB, in whole 1024-byte blocks.";return false;}
        close();file_=next;blocks_=uint32_t(size/1024);path_=path;return true;
    }
    bool read(uint32_t block, uint8_t* data, size_t len=1024) {
        if(!file_ || block>=blocks_ || len>1024)return false;
        return std::fseek(file_,long(block)*1024,SEEK_SET)==0 && std::fread(data,1,len,file_)==len;
    }
    bool write(uint32_t block,const uint8_t* data,size_t len=1024) {
        if(!file_ || block>=blocks_ || len>1024)return false;
        return std::fseek(file_,long(block)*1024,SEEK_SET)==0 && std::fwrite(data,1,len,file_)==len && std::fflush(file_)==0;
    }
    std::string label() {
        uint8_t stamp[1024],directory[1024];
        if(!read(0,stamp) || std::memcmp(stamp+1008,"ACP3",4))return {};
        const uint32_t offset=word(stamp+1021);
        if(!read(offset,directory))return {};
        for(unsigned i=0;i<1024;i+=32)if(directory[i]==32){
            std::string name(reinterpret_cast<char*>(directory+i+1),11);
            while(!name.empty()&&name.back()==' ')name.pop_back();
            return name;
        }
        return {};
    }
    bool bootable() {
        uint8_t b[1024];
        if(!read(0,b)||b[0]!=0xf3||std::memcmp(b+1008,"ACP3",4))return false;
        const unsigned off=word(b+1021),drm=word(b+1017);
        if(off>=blocks_ || drm>1023)return false;
        bool sys=false,bdos=false,ccp=false;
        for(unsigned i=0;i<=drm;++i){
            if(i%32==0&&!read(off+i/32,b))return false;
            auto e=b+(i%32)*32;if(e[0]!=0)continue;
            char name[11];for(unsigned j=0;j<11;++j)name[j]=char(e[j+1]&127);
            sys|=!std::memcmp(name,"CPM3    SYS",11);
            bdos|=!std::memcmp(name,"BDOS3   SYS",11);
            ccp|=!std::memcmp(name,"CCP     COM",11);
        }
        return sys&&bdos&&ccp;
    }
    // Create a formatted 20 MB disk. Optional CP/M .dsk import repacks files
    // from its stamped geometry into 8 KB allocation blocks / 16-bit pointers.
    static bool create(const char* path,const char* source,std::string& error) {
        if(FILE* exists=std::fopen(path,"rb")){std::fclose(exists);error="The destination exists; choose a new filename.";return false;}
        const unsigned size=20*1024*1024, allocation=8192, dirEntries=1024;
        std::vector<uint8_t> image(size,0xe5);
        std::fill(image.begin(),image.begin()+1024,0);
        if(source && *source){
            FILE* f=std::fopen(source,"rb");if(!f){error="Cannot read CP/M source disk.";return false;}
            std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::rewind(f);
            if(n<4096 || n>2*1024*1024 || n%4096){std::fclose(f);error="Choose a stamped ADAM CP/M .dsk image.";return false;}
            std::vector<uint8_t> physical(size_t(n),0),logical(size_t(n),0);
            if(std::fread(physical.data(),1,size_t(n),f)!=size_t(n)){std::fclose(f);error="Source read failed.";return false;}std::fclose(f);
            const unsigned skew[8]={0,5,2,7,4,1,6,3};
            for(unsigned i=0;i<unsigned(n);++i){unsigned sec=i/512;logical[i]=physical[((sec&~7u)|skew[sec&7u])*512+i%512];}
            const uint8_t hdBootSignature[]={0x78,0xfe,0x0b,0x20,0x03,0x3e,0x04,0xc9};
            if(std::search(logical.begin(),logical.begin()+1008,std::begin(hdBootSignature),std::end(hdBootSignature))==logical.begin()+1008) {
                error="The source bootloader has no H: support. Use the newer CP/M 3 system disk (BIOS v72).";return false;
            }
            const auto st=logical.data()+1008;
            if(std::memcmp(st,"ACP3",4)||st[4]<3||st[4]>7){error="Source has no supported ACP3 geometry stamp.";return false;}
            unsigned bsh=st[4],exm=st[6],dsm=word(st+7),drm=word(st+9),off=word(st+13)*1024;
            unsigned sourceBlock=128u<<bsh,ptrBytes=dsm>255?2:1;
            if(drm>1023 || off+(drm+1)*32>logical.size()){error="Invalid source directory geometry.";return false;}
            std::copy(logical.begin(),logical.begin()+1024,image.begin());
            struct File {std::vector<uint8_t> data;uint8_t name[11]{};uint8_t user=0;};
            std::map<std::string,File> files;
            for(unsigned i=0;i<=drm;++i){
                const auto e=logical.data()+off+i*32;if(e[0]>15)continue;
                std::string key(1,char(e[0]));for(unsigned j=1;j<12;++j)key+=char(e[j]&127);
                auto& file=files[key];std::memcpy(file.name,e+1,11);file.user=e[0];
                const unsigned extent=((unsigned(e[14])&63)<<5)|(e[12]&31);
                const unsigned records=(extent&exm)*128+e[15],begin=(extent&~exm)*16384;
                if(e[15]>128 || records*128>sourceBlock*(16/ptrBytes)){error="Invalid source file extent.";return false;}
                file.data.resize(std::max(file.data.size(),size_t(begin+records*128)),0x1a);
                for(unsigned j=0;j<records*128;++j){unsigned p=j/sourceBlock;unsigned block=ptrBytes==1?e[16+p]:word(e+16+2*p);
                    size_t pos=size_t(off)+size_t(block)*sourceBlock+j%sourceBlock;
                    if(!block||block>dsm||pos>=logical.size()){error="Source file points outside the image.";return false;}
                    file.data[begin+j]=logical[pos];
                }
            }
            unsigned next=4,entry=1;
            std::fill(image.begin()+1024,image.begin()+1024+dirEntries*32,0xe5);
            for(const auto& pair:files){const auto& f=pair.second;unsigned total=unsigned(f.data.size()/128);
                for(unsigned rec=0;rec<total || (total==0&&rec==0);){
                    if(entry>=dirEntries){error="Hard disk directory full.";return false;}
                    unsigned count=std::min(512u,total-rec);auto e=image.data()+1024+(entry++)*32;std::memset(e,0,32);e[0]=f.user;std::memcpy(e+1,f.name,11);
                    unsigned ext=rec/128+(count?((count-1)/128):0);e[12]=uint8_t(ext&31);e[14]=uint8_t(ext>>5);e[15]=uint8_t(count?((count-1)%128)+1:0);
                    for(unsigned j=0;j<(count*128+allocation-1)/allocation;++j){
                        if(size_t(1024)+size_t(next+1)*allocation>image.size()){error="Hard disk is full.";return false;}
                        putWord(e+16+j*2,next);unsigned len=std::min(allocation,count*128-j*allocation);
                        std::copy(f.data.begin()+rec*128+j*allocation,f.data.begin()+rec*128+j*allocation+len,image.begin()+1024+next*allocation);++next;
                    }
                    if(!count)break;
                    rec+=count;
                }
            }
        }
        // ACP3 boot stamp: BSH/BLM/EXM, DSM/DRM, AL0/AL1, OFF, checksum.
        auto stamp=image.data()+1008;std::memcpy(stamp,"ACP3",4);
        stamp[4]=6;stamp[5]=63;stamp[6]=3;putWord(stamp+7,(size-1024)/allocation-1);
        putWord(stamp+9,dirEntries-1);stamp[11]=0xf0;stamp[12]=0;putWord(stamp+13,1);
        unsigned sum=0;for(unsigned i=4;i<15;++i)sum+=stamp[i];stamp[15]=uint8_t(-sum);
        auto volume=image.data()+1024;std::memset(volume,0,32);volume[0]=32;std::memcpy(volume+1,"ADAM HD    ",11);volume[12]=0x31;
        // Exclusive creation prevents races and accidental overwrites.
        FILE* out=std::fopen(path,"wbx");if(!out){error="Cannot create new hard disk image.";return false;}
        bool ok=std::fwrite(image.data(),1,image.size(),out)==image.size();if(std::fclose(out))ok=false;
        if(!ok){std::remove(path);error="Hard disk image write failed.";}return ok;
    }
};
#endif
