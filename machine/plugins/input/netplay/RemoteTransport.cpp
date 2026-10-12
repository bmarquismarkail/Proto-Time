#include "RemoteTransport.hpp"
#include "machine/FixedSpscQueue.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <thread>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace BMMQ::Netplay {
struct RemoteTransport::Impl {
    FixedSpscQueue<Packet,64> incoming, outgoing;
    int socket{-1}, wake{-1};
    sockaddr_in peer{};
    std::uint16_t port{};
    std::chrono::milliseconds timeout{};
    std::thread worker;
    std::atomic<bool> stopping{true};
    std::atomic<Fault> failure{Fault::Disconnected};
    std::atomic<std::uint64_t> received{0}, sent{0}, retransmitted{0}, endpoints{0}, malformed{0}, overflows{0};
    std::atomic<std::uint64_t> publishedFrame{0};
    std::atomic<bool> publishedAny{false};
    void fail(Fault f) noexcept {
        Fault expected=Fault::None; failure.compare_exchange_strong(expected,f);
    }
    void transmit(const WirePacket& wire, std::uint64_t frame, bool retry) noexcept {
        const auto result=sendto(socket,wire.data(),wire.size(),MSG_DONTWAIT|MSG_NOSIGNAL,reinterpret_cast<const sockaddr*>(&peer),sizeof(peer));
        if(result==ssize_t(wire.size())) {
            ++sent; if(retry) ++retransmitted;
            const auto previous=publishedFrame.load(std::memory_order_relaxed);
            if(!publishedAny.load(std::memory_order_relaxed) || frame>previous) publishedFrame.store(frame,std::memory_order_release);
            publishedAny.store(true,std::memory_order_release);
        }
        else if(result<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) fail(Fault::Disconnected);
    }
    void run() noexcept {
        struct Cached { WirePacket wire{}; std::uint64_t frame{}; bool valid{}; };
        std::array<Cached,128> cache{};
        std::array<std::optional<Packet>,128> receivedCache{};
        auto lastContact=std::chrono::steady_clock::now();
        auto retryAt=lastContact+std::chrono::milliseconds(100);
        while(!stopping.load(std::memory_order_acquire) && failure.load()==Fault::None) {
            for(unsigned i=0;i<64;++i) {
                const auto packet=outgoing.pop(); if(!packet) break;
                try {
                    const auto wire=encode(*packet);
                    auto& entry=cache[packet->frame%cache.size()];
                    if(entry.valid && packet->frame<entry.frame) continue;
                    entry={wire,packet->frame,true}; transmit(wire,packet->frame,false);
                } catch(...) { fail(Fault::InvalidPacket); break; }
            }
            pollfd fds[2]{{wake,POLLIN,0},{socket,POLLIN,0}};
            int ready; do { ready=poll(fds,2,10); } while(ready<0 && errno==EINTR);
            if(ready<0 || (fds[1].revents&POLLNVAL)) { fail(Fault::Disconnected); break; }
            if(fds[1].revents&POLLERR) {
                int socketError{};socklen_t length=sizeof(socketError);
                (void)getsockopt(socket,SOL_SOCKET,SO_ERROR,&socketError,&length);
            }
            if(stopping.load(std::memory_order_acquire)) break;
            if(fds[1].revents&POLLIN) for(unsigned n=0;n<64;++n) {
                std::array<std::uint8_t,196> bytes{};
                sockaddr_in sender{}; iovec io{bytes.data(),bytes.size()};
                msghdr message{}; message.msg_name=&sender; message.msg_namelen=sizeof(sender); message.msg_iov=&io; message.msg_iovlen=1;
                const auto count=recvmsg(socket,&message,MSG_DONTWAIT);
                if(count<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) break;
                if(count<0 && errno==EINTR) continue;
                if(count<0 && errno==ECONNREFUSED) break;
                if(count<0) { fail(Fault::Disconnected); break; }
                if(sender.sin_family!=AF_INET || sender.sin_port!=peer.sin_port || sender.sin_addr.s_addr!=peer.sin_addr.s_addr) { ++endpoints; continue; }
                const auto packet=(message.msg_flags&MSG_TRUNC)?std::optional<Packet>{}:decode(std::span(bytes.data(),std::size_t(count)));
                if(!packet) { ++malformed; fail(Fault::InvalidPacket); break; }
                lastContact=std::chrono::steady_clock::now();
                auto& seen=receivedCache[packet->frame%receivedCache.size()];
                if(seen && (*seen==*packet || packet->frame<seen->frame)) continue;
                if(!incoming.push(*packet)) { ++overflows; fail(Fault::HandoffOverflow); break; }
                seen=*packet; ++received;
            }
            const auto now=std::chrono::steady_clock::now();
            if(now-lastContact>=timeout) { fail(Fault::Disconnected); break; }
            if(now>=retryAt) {
                // Bounded replay of unacknowledged/history packets tolerates loss
                // and reordering. The engine validates duplicate identity.
                for(const auto& entry:cache) if(entry.valid) transmit(entry.wire,entry.frame,true);
                retryAt=now+std::chrono::milliseconds(100);
            }
        }
    }
};
RemoteTransport::RemoteTransport():impl_(std::make_unique<Impl>()) {}
RemoteTransport::~RemoteTransport() { stop(); }
bool RemoteTransport::start(const RemoteTransportConfig& config) {
    stop(); auto& p=*impl_;
    if(!config.peerPort || config.disconnectTimeout<std::chrono::milliseconds(100) || config.disconnectTimeout>std::chrono::seconds(60)) return false;
    sockaddr_in local{}; local.sin_family=AF_INET; local.sin_port=htons(config.localPort);
    p.peer={}; p.peer.sin_family=AF_INET; p.peer.sin_port=htons(config.peerPort);
    if(inet_pton(AF_INET,config.localAddress.c_str(),&local.sin_addr)!=1 || inet_pton(AF_INET,config.peerAddress.c_str(),&p.peer.sin_addr)!=1) return false;
    p.socket=::socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(p.socket<0) return false;
    if(bind(p.socket,reinterpret_cast<const sockaddr*>(&local),sizeof(local))<0) { ::close(p.socket);p.socket=-1;return false; }
    socklen_t length=sizeof(local);
    if(getsockname(p.socket,reinterpret_cast<sockaddr*>(&local),&length)<0) { ::close(p.socket);p.socket=-1;return false; }
    p.port=ntohs(local.sin_port);
    p.wake=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);
    if(p.wake<0) { ::close(p.socket);p.socket=-1;p.port=0;return false; }
    p.incoming.resetQuiescent();p.outgoing.resetQuiescent();p.timeout=config.disconnectTimeout;
    p.publishedFrame.store(0,std::memory_order_relaxed);
    p.publishedAny.store(false,std::memory_order_relaxed);
    p.stopping.store(false);p.failure.store(Fault::None);
    try { p.worker=std::thread([&p]{p.run();}); }
    catch(...) { stop();return false; }
    return true;
}
void RemoteTransport::stop() noexcept {
    auto& p=*impl_;p.stopping.store(true,std::memory_order_release);
    if(p.wake>=0) { const std::uint64_t one=1;(void)::write(p.wake,&one,sizeof(one)); }
    if(p.worker.joinable()) p.worker.join();
    if(p.wake>=0) {::close(p.wake);p.wake=-1;}
    if(p.socket>=0) {::close(p.socket);p.socket=-1;}
    p.port=0;p.failure.store(Fault::Disconnected);
}
std::uint16_t RemoteTransport::localPort()const noexcept{return impl_->port;}
bool RemoteTransport::send(const Packet& packet)noexcept {
    if(impl_->failure.load()!=Fault::None) return false;
    if(!impl_->outgoing.push(packet)) { ++impl_->overflows;impl_->fail(Fault::HandoffOverflow);return false; }
    return true;
}
std::optional<Packet> RemoteTransport::receive()noexcept{return impl_->incoming.pop();}
bool RemoteTransport::hasTransmittedFrame(std::uint64_t frame)const noexcept {
    if(impl_->failure.load()!=Fault::None || !impl_->publishedAny.load(std::memory_order_acquire)) return false;
    return impl_->publishedFrame.load(std::memory_order_acquire)>=frame;
}
Fault RemoteTransport::fault()const noexcept{return impl_->failure.load(std::memory_order_acquire);}
RemoteTransportDiagnostics RemoteTransport::diagnostics()const noexcept {
    const auto& p=*impl_;return {p.received.load(),p.sent.load(),p.retransmitted.load(),p.endpoints.load(),p.malformed.load(),p.overflows.load(),p.failure.load()};
}
}
