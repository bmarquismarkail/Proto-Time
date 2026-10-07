#pragma once
#include "Inspector.hpp"
#include <chrono>
#include <functional>
namespace BMMQ::RemoteVideo {
// Transport/presentation lane only. Eight bounded nonblocking clients, one
// queued mutation per client, no socket waits or network callbacks on Machine.
class HttpServer {
    struct Client {
        int fd{-1};std::string input,output;std::size_t sent{};
        std::uint32_t pending{};
        std::chrono::steady_clock::time_point deadline;
    };
    int listener_{-1};std::uint16_t port_{};std::string token_,page_,css_,script_,host_;
    std::array<Client,8> clients_{};
    std::uint32_t nextId_{1};
    std::uint64_t rejected_{},disconnects_{};
    void close(Client&) noexcept;
    void respond(Client&,int,std::string,std::string_view="application/json");
    void parse(Client&,std::string_view,std::string_view,const std::function<bool(const Debug::Command&)>&);
public:
    HttpServer(std::uint16_t port,std::string token,std::string page,std::string css,std::string script);
    ~HttpServer();
    HttpServer(const HttpServer&)=delete;
    std::uint16_t port()const noexcept {return port_;}
    void tick(std::string_view state,std::string_view metadata,const std::function<bool(const Debug::Command&)>& request);
    void deliver(const Debug::Reply&);
    std::uint64_t rejected()const noexcept{return rejected_;}
    std::uint64_t disconnected()const noexcept{return disconnects_;}
};
}
