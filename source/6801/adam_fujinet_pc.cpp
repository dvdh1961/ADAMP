#include "adam_fujinet_pc.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>
#include <chrono>
#include <memory>
#include <algorithm>
#include <iterator>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
static constexpr Socket noSocket = INVALID_SOCKET;
static void closeSocket(Socket s) { if(s!=noSocket) closesocket(s); }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
using Socket = int;
static constexpr Socket noSocket = -1;
static void closeSocket(Socket s) { if(s!=noSocket) ::close(s); }
#endif

namespace AdamFujiNetPC {
using Clock = std::chrono::steady_clock;
struct Job {
    uint8_t device; int command; bool block; uint32_t sector;
    std::vector<uint8_t> data;
    unsigned generation;
    std::promise<Reply> promise;
};
class Service {
public:
    std::atomic<bool> running{false}, live{false};
    std::atomic<unsigned> generation{0};
    std::thread worker;
    std::mutex mutex;
    std::deque<std::shared_ptr<Job>> queue;
    std::string message = "Off";
    Socket peer = noSocket;
    uint8_t lastPacketReply=0;
    std::array<Reply,16> statusCache{};
    std::array<bool,16> statusValid{};
    std::array<Clock::time_point,16> statusTime{};
    ~Service() { stop(); }
    void setState(std::string s) { std::lock_guard<std::mutex> lock(mutex); message=std::move(s); }
    void stop() {
        running=false; live=false; ++generation;
        if(worker.joinable()) worker.join();
    }
    bool readable(Socket fd, int ms) {
        fd_set set; FD_ZERO(&set); FD_SET(fd,&set);
        timeval tv{ms/1000,(ms%1000)*1000};
        return select(int(fd)+1,&set,nullptr,nullptr,&tv)>0;
    }
    bool valid(unsigned gen) {return running && generation==gen;}
    bool sendBytes(const std::vector<uint8_t>& bytes, unsigned gen) {
        size_t done=0;
        while(done<bytes.size() && valid(gen)) {
#ifdef _WIN32
            int n=send(peer,reinterpret_cast<const char*>(bytes.data()+done),int(bytes.size()-done),0);
#else
            int n=send(peer,bytes.data()+done,bytes.size()-done,MSG_NOSIGNAL);
#endif
            if(n<=0) return false;
            done+=size_t(n);
        }
        return done==bytes.size();
    }
    bool recvBytes(uint8_t* bytes, size_t len, int timeout, unsigned gen) {
        auto until=Clock::now()+std::chrono::milliseconds(timeout);
        size_t done=0;
        while(done<len && valid(gen) && Clock::now()<until) {
            if(!readable(peer,std::min(50,timeout))) continue;
            int n=recv(peer,reinterpret_cast<char*>(bytes+done),int(len-done),0);
            if(n<=0) return false;
            done+=size_t(n);
        }
        return done==len;
    }
    bool control(int code, uint8_t dev, unsigned gen) {return sendBytes({uint8_t((code<<4)|dev)},gen);}
    bool packet(uint8_t dev, const std::vector<uint8_t>& data, unsigned gen) {
        std::vector<uint8_t> p{uint8_t(0x60|dev),uint8_t(data.size()>>8),uint8_t(data.size())};
        uint8_t xorByte=0;
        for(auto b:data) {p.push_back(b);xorByte^=b;}
        p.push_back(xorByte);
        lastPacketReply=0;
        return sendBytes(p,gen) && recvBytes(&lastPacketReply,1,3000,gen) && lastPacketReply==(0x90|dev);
    }
    Reply transaction(const Job& j) {
        Reply r; r.error="AdamNet transfer failed or timed out";
        const auto d=j.device; const auto gen=j.generation;
        if(peer==noSocket || !valid(gen)) return r;
        if(j.command==6) { // host-side boot probe, never a wire command
            for(unsigned sector=0;sector<3;++sector) {
                Job read;read.device=4;read.command=4;read.block=true;read.sector=sector;read.generation=gen;
                auto block=transaction(read);
                if(!block.ok) return block;
                r.data.insert(r.data.end(),block.data.begin(),block.data.end());
            }
            std::string header(r.data.begin(),r.data.end());
            for(char& c:header) if(c>='a' && c<='z') c=char(c-'a'+'A');
            r.bootProfile=header.find("CP/M")!=std::string::npos || header.find("CPM")!=std::string::npos || header.find("CP-M")!=std::string::npos ? 1
                : header.find("T-DOS")!=std::string::npos ? 2 : 0;
            // FujiNet wraps native .rom files in two fixed ADAM loader blocks.
            // Read the raw stream, then use ADAM+'s normal cartridge route.
            static const uint8_t loader[]={0x3a,0x6f,0xfd,0x01,0,0,0x11,0x01,0,0x21,0,0x20,0xcd,0xf3,0xfc,0xc3,0,0x20};
            if(std::equal(std::begin(loader),std::end(loader),r.data.begin())) {
                r.rom.assign(r.data.begin()+2048,r.data.end());
                bool ended=false;
                for(unsigned sector=3;sector<2+16*1024;++sector) {
                    if(!valid(gen)) {r.error="ROM download cancelled";return r;}
                    Job read;read.device=4;read.command=4;read.block=true;read.sector=sector;read.generation=gen;
                    auto block=transaction(read);
                    if(!block.ok) {
                        if(block.endOfMedia) {ended=true;break;}
                        return block;
                    }
                    r.rom.insert(r.rom.end(),block.data.begin(),block.data.end());
                }
                bool header=false;
                for(size_t i=0;i+1<r.rom.size();i+=16384)
                    if((r.rom[i]==0x55 && r.rom[i+1]==0xaa) || (r.rom[i]==0xaa && r.rom[i+1]==0x55)) header=true;
                if(!ended || r.rom.size()<8192 || !header) {r.error="Invalid or incomplete FujiNet cartridge ROM";return r;}
                r.bootProfile=3;
            }
            r.ok=true;r.error.clear();return r;
        }
        if(j.command==0 || j.command==2) {
            r.ok=control(0,d,gen); return r;
        }
        if(j.command==1) {
            uint8_t p[6]{};
            if(!control(1,d,gen)) return r;
            if(!recvBytes(p,1,100,gen)) {r.error="Device not present";return r;}
            if(!recvBytes(p+1,5,3000,gen)) return r;
            if(p[0]!=(0x80|d) || p[5]!=(p[1]^p[2]^p[3]^p[4])) {
                r.error="Invalid ADAM status packet"; return r;
            }
            std::copy(p+1,p+5,r.status.begin());r.ok=true;return r;
        }
        if(j.command!=3 && j.command!=4) {r.error="Unsupported DCB command";return r;}
        if(j.block) {
            if(!packet(d,{uint8_t(j.sector),uint8_t(j.sector>>8),uint8_t(j.sector>>16),uint8_t(j.sector>>24),0},gen)) {
                r.endOfMedia=lastPacketReply==(0xc0|d);
                if(r.endOfMedia) r.error="Block unavailable";
                return r;
            }
        }
        if(j.command==3) {
            if(d==15 && j.data.size()>=2 && j.data[0]==0xd9 && j.data[1]==0) {
                Job probe;probe.device=4;probe.command=6;probe.block=true;probe.generation=gen;
                auto boot=transaction(probe);
                // A native cartridge does not run the ADAM 32 KiB loader.
                // Its complete ROM is handed to the normal Coleco boot path.
                if(boot.ok && boot.bootProfile==3) return boot;
                if(!boot.ok && !boot.endOfMedia) return boot;
            }
            r.ok=packet(d,j.data,gen);
            if(r.ok && d==15 && j.data.size()>=2 && j.data[0]==0xd9 && j.data[1]==0) {
                std::string header;
                for(unsigned sector=0;sector<3;++sector) {
                    Job read;read.device=4;read.command=4;read.block=true;read.sector=sector;read.generation=gen;
                    auto block=transaction(read);
                    if(!block.ok) break;
                    header.append(block.data.begin(),block.data.end());
                }
                for(char& c:header) if(c>='a' && c<='z') c=char(c-'a'+'A');
                if(header.find("CP/M")!=std::string::npos || header.find("CPM")!=std::string::npos || header.find("CP-M")!=std::string::npos) r.bootProfile=1;
                else if(header.find("T-DOS")!=std::string::npos) r.bootProfile=2;
                else if(!header.empty()) r.bootProfile=0;
            }
            return r;
        }
        auto deadline=Clock::now()+std::chrono::seconds(20);
        uint8_t response=0;
        for(;;) {
            if(!control(4,d,gen)) return r;
            if(recvBytes(&response,1,5,gen)) {
                if(response==(0x90|d)) break;
                if(response==(0xc0|d)) {r.empty=!j.block;r.ok=r.empty;r.endOfMedia=j.block;if(j.block)r.error="Block unavailable";return r;}
                return r;
            }
            if(!valid(gen) || Clock::now()>=deadline) return r;
        }
        if(!control(3,d,gen)) return r;
        // Delayed ACKs from RECEIVE polls can precede the data packet.
        do {if(!recvBytes(&response,1,3000,gen)) return r;} while(response==(0x90|d));
        if(response==(0xc0|d)) {r.empty=!j.block;r.ok=r.empty;r.endOfMedia=j.block;if(j.block)r.error="Block unavailable";return r;}
        if(response!=(0xb0|d)) return r;
        uint8_t len[2];
        if(!recvBytes(len,2,3000,gen)) return r;
        size_t count=(size_t(len[0])<<8)|len[1];
        if(count>4096 || (j.block && count!=1024)) {r.error="Invalid ADAM data length";return r;}
        r.data.resize(count);
        uint8_t checksum=0, actual=0;
        if(!recvBytes(r.data.data(),count,20000,gen) || !recvBytes(&checksum,1,3000,gen)) return r;
        for(auto b:r.data) actual^=b;
        if(actual!=checksum) {r.error="AdamNet checksum mismatch";return r;}
        r.ok=true;r.error.clear();return r;
    }
    void cacheStatus(unsigned gen) {
        for(uint8_t d=3;d<=15 && valid(gen);++d) {
            if(d==8 || d==11) continue;
            Job job;job.device=d;job.command=1;job.block=false;job.generation=gen;
            auto result=transaction(job);
            std::lock_guard<std::mutex> lock(mutex);
            statusCache[d]=std::move(result);statusValid[d]=true;statusTime[d]=Clock::now();
        }
    }
    void run(Socket listener) {
        while(running) {
            if(peer==noSocket) {
                if(!readable(listener,50)) continue;
                peer=accept(listener,nullptr,nullptr);
                if(peer==noSocket) continue;
                int yes=1;setsockopt(peer,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&yes),sizeof(yes));
                Job probe;probe.device=15;probe.command=1;probe.block=false;probe.generation=generation;
                auto result=transaction(probe);
                if(!result.ok || result.status[2]!=0) {
                    closeSocket(peer);peer=noSocket;setState("Wrong peer or no ADAM response");continue;
                }
                cacheStatus(generation);
                live=true;setState("Connected");
            }
            std::shared_ptr<Job> job;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(!queue.empty()) {job=queue.front();queue.pop_front();}
            }
            if(job) {
                Reply result;
                if(job->generation==generation) result=transaction(*job);
                if(job->command==1) {
                    std::lock_guard<std::mutex> lock(mutex);
                    statusCache[job->device]=result;statusValid[job->device]=true;statusTime[job->device]=Clock::now();
                }
                if(result.ok && job->command==3 && job->device==15 && !job->data.empty()
                    && (job->data[0]==0xf8 || job->data[0]==0xe9 || job->data[0]==0xd9 || job->data[0]==0xd7))
                    cacheStatus(job->generation);
                if(!result.ok && !result.empty && !result.endOfMedia && result.error!="Device not present") {
                    live=false;closeSocket(peer);peer=noSocket;
                    setState("Waiting for ADAM FujiNet-PC");
                }
                job->promise.set_value(std::move(result));
            } else if(readable(peer,10)) {
                char b;
                if(recv(peer,&b,1,MSG_PEEK)<=0) {
                    closeSocket(peer);peer=noSocket;live=false;setState("Waiting for ADAM FujiNet-PC");
                } else {
                    // No unsolicited traffic is part of this master protocol.
                    closeSocket(peer);peer=noSocket;live=false;setState("Unexpected AdamNet traffic; reconnecting");
                }
            }
        }
        closeSocket(peer);peer=noSocket;closeSocket(listener);live=false;
        std::lock_guard<std::mutex> lock(mutex);
        for(auto& job:queue) job->promise.set_value(Reply{});
        queue.clear();message="Off";
    }
};
static Service service;
// CONFIG continues executing briefly after transport shutdown. Keep its narrow
// EOS sound filter active until a machine reset ends that execution session.
static std::atomic<bool> eosSoundSession{false};
bool start(uint16_t port,std::string& error) {
    service.stop();
#ifdef _WIN32
    static const bool initialized=[] {WSADATA w;return WSAStartup(MAKEWORD(2,2),&w)==0;}();
    if(!initialized) {error="Cannot initialize Windows TCP";return false;}
#endif
    Socket listener=socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
#ifndef _WIN32
    int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
#else
    int yes=1;setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&yes),sizeof(yes));
#endif
    if(listener==noSocket || bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0 || listen(listener,1)!=0) {
        closeSocket(listener);error="Cannot listen on 127.0.0.1:"+std::to_string(port)+" (port already in use?)";return false;
    }
    service.statusValid.fill(false);
    eosSoundSession=true;
    service.running=true;service.setState("Waiting for ADAM FujiNet-PC");
    service.worker=std::thread([listener]{service.run(listener);});return true;
}
void stop() {service.stop();}
bool enabled() {return service.running;}
bool eosSoundSessionActive() {return eosSoundSession;}
void resetEosSoundSession() {eosSoundSession=enabled();}
bool connected() {return service.live;}
std::string state() {std::lock_guard<std::mutex> lock(service.mutex);return service.message;}
void cancel() {++service.generation;}
unsigned session() {return service.generation;}
bool forwards(uint8_t dev) {return enabled() && (dev==3 || (dev>=4 && dev<=7) || (dev>=9 && dev<=15 && dev!=11));}
std::future<Reply> request(uint8_t device,int command,bool block,uint32_t sector,std::vector<uint8_t> data) {
    auto job=std::make_shared<Job>();job->device=device;job->command=command;job->block=block;
    job->sector=sector;job->data=std::move(data);job->generation=service.generation;
    auto future=job->promise.get_future();
    std::lock_guard<std::mutex> lock(service.mutex);
    if(!service.running || !service.live || service.queue.size()>=16) job->promise.set_value(Reply{});
    else if(command==1 && service.statusValid[device] && Clock::now()-service.statusTime[device]<std::chrono::seconds(1)) job->promise.set_value(service.statusCache[device]);
    else service.queue.push_back(job);
    return future;
}
}
