#ifdef NDEBUG
#undef NDEBUG
#endif
#include "machine/plugins/input/netplay/RemoteTransport.hpp"
#include <cassert>
#include <chrono>
#include <thread>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <source_location>
using namespace BMMQ::Netplay;
namespace {
struct Socket {
    int fd{-1};std::uint16_t port{};
    Socket() {
        fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);assert(fd>=0);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        assert(bind(fd,reinterpret_cast<const sockaddr*>(&address),sizeof(address))==0);
        socklen_t size=sizeof(address);assert(getsockname(fd,reinterpret_cast<sockaddr*>(&address),&size)==0);port=ntohs(address.sin_port);
    }
    ~Socket(){if(fd>=0)::close(fd);}
    void release(){::close(fd);fd=-1;}
    void send(std::uint16_t destination,std::span<const std::uint8_t> data) {
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(destination);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        assert(sendto(fd,data.data(),data.size(),0,reinterpret_cast<const sockaddr*>(&address),sizeof(address))==ssize_t(data.size()));
    }
};
template<class F> void await(F condition, std::source_location site=std::source_location::current()) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!condition()){if(std::chrono::steady_clock::now()>=until)std::cerr<<"transport wait timeout at line "<<site.line()<<std::endl;assert(std::chrono::steady_clock::now()<until);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
Packet packet() {
    Packet p;p.binding.core=Core::GameBoy;p.binding.generation=1;p.binding.session[0]=1;p.binding.rom[0]=2;p.binding.configuration[0]=3;p.before[0]=4;p.input=0x10;return p;
}
}
int main() {
    Socket one,two;const auto firstPort=one.port,secondPort=two.port;one.release();two.release();
    RemoteTransport first,second;
    assert(first.start({.localPort=firstPort,.peerPort=secondPort}));
    assert(second.start({.localPort=secondPort,.peerPort=firstPort}));
    assert(!first.hasTransmittedFrame(0));
    const auto original=packet();assert(first.send(original));
    std::optional<Packet> got;await([&]{got=second.receive();return got.has_value();});assert(*got==original);
    await([&]{return first.hasTransmittedFrame(0);});
    await([&]{return first.diagnostics().retransmitted>0;});
    assert(!second.receive() && second.fault()==Fault::None); // Duplicate coalescing before handoff.
    // Concurrent producer/worker/consumer handoff exercises more than one ring turn.
    for(unsigned n=1;n<130;++n) {
        auto p=original;p.frame=n;assert(first.send(p));
        await([&]{got=second.receive();return got.has_value();});assert(*got==p);
        await([&]{return first.hasTransmittedFrame(n);});
    }
    assert(!first.hasTransmittedFrame(130));
    first.stop();assert(!first.hasTransmittedFrame(0));second.stop();assert(first.fault()==Fault::Disconnected);
    // Reopen starts a new empty transport generation; no preceding packets survive.
    assert(first.start({.localPort=firstPort,.peerPort=secondPort}));assert(!first.receive());assert(!first.hasTransmittedFrame(0));first.stop();
    {Socket trusted,other;RemoteTransport target;assert(target.start({.peerPort=trusted.port}));
     auto wire=encode(original);other.send(target.localPort(),wire);
     await([&]{return target.diagnostics().rejectedEndpoints==1;});assert(target.fault()==Fault::None);
     wire[0]^=1;trusted.send(target.localPort(),wire);
     await([&]{return target.fault()==Fault::InvalidPacket;});assert(target.diagnostics().malformed==1);}
    {Socket trusted;RemoteTransport target;assert(target.start({.peerPort=trusted.port}));
     for(unsigned n=0;n<65;++n){auto p=original;p.frame=n;trusted.send(target.localPort(),encode(p));
       if(n<64)await([&]{return target.diagnostics().received>=n+1;});}
     await([&]{return target.fault()==Fault::HandoffOverflow;});assert(target.diagnostics().overflows==1);}
    {Socket trusted;RemoteTransport target;assert(target.start({.peerPort=trusted.port,.disconnectTimeout=std::chrono::milliseconds(100)}));
     await([&]{return target.fault()==Fault::Disconnected;});}
    {RemoteTransport bad;assert(!bad.start({.peerAddress="invalid",.peerPort=10}));assert(!bad.start({}));}
    std::cout<<"UDP owned handoff, duplicate coalescing, endpoint admission, corrupt input, overload, reopen and disconnect passed\n";
}
