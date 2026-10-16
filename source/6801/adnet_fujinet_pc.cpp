#include "adam_fujinet_pc.h"
#include "adnet_core.h"
#include "CORE/cv.h"
#include <chrono>
#include <cstdio>

namespace {
struct Pending {
    bool active=false;
    unsigned session=0;
    unsigned char device=0;
    unsigned short buffer=0,length=0;
    int command=0;
    bool block=false;
    unsigned sector=0;
    std::future<AdamFujiNetPC::Reply> future;
} pending;
int completedBootProfile=-1;
std::vector<uint8_t> completedRom;
unsigned lastCpmSector=~0u;
}
void fujinet_pc_reset() {
    // The worker never owns an emulator memory pointer. Discard the pending
    // result before resetting PCB/RAM, including replies from an earlier boot.
    pending.active=false;
    completedBootProfile=-1;
    completedRom.clear();
    lastCpmSector=~0u;
    AdamFujiNetPC::cancel();
}
int fujinet_pc_take_boot_profile() {int p=completedBootProfile;completedBootProfile=-1;return p;}
std::vector<uint8_t> fujinet_pc_take_rom() {auto rom=std::move(completedRom);completedRom.clear();return rom;}
void UpdateFUJINET_PC(unsigned char dev,int command,int forcedDevice) {
    if(pending.active && pending.session!=AdamFujiNetPC::session()) pending.active=false;
    if(command<0) {
        if(!pending.active || pending.device!=dev) return;
        if(pending.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready) {
            SetDCB(dev,DCB_CMD_STAT,0);return;
        }
        auto reply=pending.future.get();pending.active=false;
        if(!reply.ok) {
            SetDCB(dev,DCB_CMD_STAT,RSP_TIMEOUT);
            if(!reply.error.empty() && reply.error!="Device not present") std::fprintf(stderr,"[FujiNet-PC] %s\n",reply.error.c_str());
            return;
        }
        if(pending.command==CMD_STATUS) {
            for(unsigned i=0;i<3;++i) SetDCB(dev,DCB_MAXL_LO+i,reply.status[i]);
            SetDCB(dev,DCB_NODE_TYPE,(GetDCB(dev,DCB_NODE_TYPE)&0xf0)|(reply.status[3]&0x0f));
        } else if(pending.command==CMD_READ) {
            unsigned n=std::min<unsigned>(pending.length,reply.data.size());
            const unsigned pcbLength=PCB_SIZE+(GetMaxDCB()+1)*DCB_SIZE;
            for(unsigned i=0;i<n;++i) {
                word a=word(pending.buffer+i);
                // PCBTable[0] also carries the Data-In Full notification.
                // It is not a protected PCB byte when the PCB lives at FEC0.
                if(unsigned(word(a-PCBAddr))>=pcbLength) RAM_Memory[a]=reply.data[i];
            }
            // Block length belongs to the caller; character replies report
            // the actual received length in the DCB buffer length fields.
            if(!pending.block) {
                SetDCB(dev,DCB_BUF_LEN_LO,byte(n));SetDCB(dev,DCB_BUF_LEN_HI,byte(n>>8));
            }
        }
        SetDCB(dev,DCB_CMD_STAT,RSP_STATUS);
        if(reply.bootProfile>=0) completedBootProfile=reply.bootProfile;
        if(!reply.rom.empty()) completedRom=std::move(reply.rom);
        return;
    }
    if(pending.active) {SetDCB(dev,DCB_CMD_STAT,0x03);return;}
    const byte id=forcedDevice>=0?byte(forcedDevice):byte(GetDCB(dev,DCB_ADD_CODE)&15);
    const bool block=id>=4 && id<=7;
    const auto len=GetDCBLen(dev);
    if((command==CMD_READ || command==CMD_WRITE) && (!len || len>4096 || (block && len>1024))) {
        SetDCB(dev,DCB_CMD_STAT,RSP_TIMEOUT);return;
    }
    if(command!=CMD_STATUS && command!=CMD_READ && command!=CMD_WRITE && command!=CMD_SOFT_RESET && command!=CMD_RESET) {
        SetDCB(dev,DCB_CMD_STAT,RSP_TIMEOUT);return;
    }
    std::vector<uint8_t> bytes;
    if(command==CMD_WRITE) {
        bytes.resize(block?1024:len,0);
        for(unsigned i=0;i<len;++i) bytes[i]=coleco_readbyte(word(GetDCBBase(dev)+i));
    }
    pending.block=block;pending.sector=GetDCBSector(dev);
    pending.session=AdamFujiNetPC::session();
    pending.device=dev;pending.buffer=GetDCBBase(dev);pending.length=len;pending.command=command;
    pending.future=AdamFujiNetPC::request(id,command,block,GetDCBSector(dev),std::move(bytes));
    pending.active=true;SetDCB(dev,DCB_CMD_STAT,0);
    if(block && (command==CMD_READ || command==CMD_WRITE)) {
        // EOS loaders may issue STATUS before polling the preceding READ,
        // and then execute its buffer immediately (read-ahead). Deliver the
        // complete block before the Z80 can reuse that DCB or buffer.
        // The socket worker remains independent; Disconnect cancels the wait.
        pending.future.wait();
        UpdateFUJINET_PC(dev,-1);
    }
}

void UpdateFUJINET_PC_CPM_D5(unsigned char dev,int command) {
    if(pending.active && pending.session!=AdamFujiNetPC::session()) pending.active=false;
    if(command<0) {
        if(pending.active && pending.device!=dev) {
            if(pending.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready) {
                SetDCB(dev,DCB_CMD_STAT,0);return;
            }
            const bool same=pending.sector==GetDCBSector(dev) && pending.buffer==GetDCBBase(dev) && pending.length==GetDCBLen(dev);
            const auto previousDevice=pending.device;
            UpdateFUJINET_PC(previousDevice,-1);
            if(same) {SetDCB(dev,DCB_CMD_STAT,GetDCB(previousDevice,DCB_CMD_STAT));return;}
            UpdateFUJINET_PC(dev,CMD_READ,4);return;
        }
        UpdateFUJINET_PC(dev,command,4);return;
    }
    if(command!=CMD_READ) {SetDCB(dev,DCB_CMD_STAT,RSP_TIMEOUT);return;}
    if(lastCpmSector!=GetDCBSector(dev)) {
        lastCpmSector=GetDCBSector(dev);SetDCB(dev,DCB_CMD_STAT,RSP_TIMEOUT);return;
    }
    if(pending.active) {SetDCB(dev,DCB_CMD_STAT,0);return;}
    UpdateFUJINET_PC(dev,CMD_READ,4);
}
