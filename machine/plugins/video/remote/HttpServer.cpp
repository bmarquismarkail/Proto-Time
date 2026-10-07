#include "HttpServer.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <charconv>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>
#include <map>
namespace BMMQ::RemoteVideo {
namespace {
constexpr std::size_t maxHeader=8192,maxBody=4096,maxResponse=4*1024*1024;
std::string lowercase(std::string text) {for(char& c:text)if(c>='A'&&c<='Z')c+=32;return text;}
}
HttpServer::HttpServer(std::uint16_t port,std::string token,std::string page,std::string css,std::string script,std::string postprocess):token_(std::move(token)),page_(std::move(page)),css_(std::move(css)),script_(std::move(script)),postprocess_(std::move(postprocess)) {
    if(token_.size()!=64 || token_.find_first_not_of("0123456789abcdef")!=std::string::npos || page_.size()>262144 || css_.size()>262144 || script_.size()>262144 || postprocess_.size()>262144)
        throw std::invalid_argument("HTTP static resource budget");
    listener_=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(listener_<0)throw std::runtime_error("HTTP socket failed");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);
    if(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address)) || listen(listener_,8)) {
        ::close(listener_);listener_=-1;throw std::runtime_error("HTTP loopback bind failed");}
    socklen_t length=sizeof(address);
    if(getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&length)) {::close(listener_);listener_=-1;throw std::runtime_error("HTTP port query failed");}
    port_=ntohs(address.sin_port);host_="127.0.0.1:"+std::to_string(port_);
}
HttpServer::~HttpServer(){for(auto& c:clients_)close(c);if(listener_>=0)::close(listener_);}
void HttpServer::close(Client& c) noexcept {if(c.fd>=0) {::close(c.fd);++disconnects_;}c=Client{};}
void HttpServer::respond(Client& c,int status,std::string body,std::string_view type) {
    if(body.size()>maxResponse) {status=503;body="{\"error\":\"response budget exceeded\"}";}
    const auto reason=status==200?"OK":status==403?"Forbidden":status==404?"Not Found":status==413?"Content Too Large":status==503?"Unavailable":"Bad Request";
    c.output="HTTP/1.1 "+std::to_string(status)+" "+reason+"\r\nContent-Type: "+std::string(type)+
        "\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"+
        "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'\r\n\r\n"+body;
    c.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
}
void HttpServer::parse(Client& c,std::string_view state,std::string_view metadata,const std::function<bool(const Debug::Command&)>& request) {
    const auto end=c.input.find("\r\n\r\n");
    if(end==std::string::npos) {if(c.input.size()>maxHeader){++rejected_;respond(c,413,"{}");}return;}
    auto bad=[&](int status=400){++rejected_;respond(c,status,"{\"error\":\"request rejected\"}");};
    if(end>maxHeader){bad(413);return;}
    const auto line=c.input.find("\r\n");
    if(line==std::string::npos){bad();return;}
    auto first=c.input.substr(0,line);const auto space=first.find(' '),last=first.rfind(' ');
    if(space==std::string::npos || space==last || first.substr(last+1)!="HTTP/1.1"){bad();return;}
    const auto method=first.substr(0,space),path=first.substr(space+1,last-space-1);
    std::map<std::string,std::string> headers;
    for(auto at=line+2;at<end;) {
        auto next=c.input.find("\r\n",at);auto colon=c.input.find(':',at);
        if(next==std::string::npos || colon==std::string::npos || colon>=next){bad();return;}
        auto key=lowercase(c.input.substr(at,colon-at));auto value=c.input.substr(colon+1,next-colon-1);
        while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.erase(0,1);
        if(key.empty() || key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=key.npos ||
           !headers.emplace(key,value).second){bad();return;}
        at=next+2;
    }
    if(!headers.contains("host") || headers.at("host")!=host_ || headers.contains("transfer-encoding") ||
       (headers.contains("origin") && headers.at("origin")!="http://"+host_)) {bad(403);return;}
    std::size_t length=0;
    if(headers.contains("content-length")) {const auto& t=headers.at("content-length");auto result=std::from_chars(t.data(),t.data()+t.size(),length);
        if(result.ec!=std::errc{} || result.ptr!=t.data()+t.size()){bad();return;}}
    if(length>maxBody){bad(413);return;}
    if(c.input.size()<end+4+length)return;
    if(c.input.size()!=end+4+length){bad();return;}
    if(method=="GET" && length==0) {
        if(path=="/")respond(c,200,page_,"text/html; charset=utf-8");
        else if(path=="/inspector.css")respond(c,200,css_,"text/css; charset=utf-8");
        else if(path=="/inspector.js")respond(c,200,script_,"text/javascript; charset=utf-8");
        else if(path=="/postprocess.js")respond(c,200,postprocess_,"text/javascript; charset=utf-8");
        else if(path=="/api/state")respond(c,200,std::string(state));
        else if(path=="/api/meta")respond(c,200,std::string(metadata));
        else {bad(404);}return;
    }
    if(method!="POST" || path!="/api/command" || !headers.contains("x-time-token") || headers.at("x-time-token")!=token_ ||
        !headers.contains("content-type") || headers.at("content-type")!="application/json") {bad(403);return;}
    try {
        if(nextId_==0)throw std::invalid_argument("command identity exhausted");
        const auto command=Inspector::command(nlohmann::json::parse(c.input.substr(end+4,length)),nextId_++);
        if(!request(command)){++rejected_;respond(c,503,"{\"error\":\"command queue full\"}");return;}
        c.pending=command.id;c.input.clear();
    }catch(const std::exception&){bad();}
}
void HttpServer::deliver(const Debug::Reply& reply) {
    if(!reply.id)return;
    for(auto& client:clients_)if(client.fd>=0 && client.pending==reply.id) {
        client.pending=0;respond(client,200,Inspector::reply(reply).dump());return;}
}
void HttpServer::tick(std::string_view state,std::string_view metadata,const std::function<bool(const Debug::Command&)>& request) {
    std::array<pollfd,9> fds{};fds[0]={listener_,POLLIN,0};
    for(unsigned i=0;i<8;++i)fds[i+1]={clients_[i].fd,short(clients_[i].output.empty()?POLLIN:POLLOUT),0};
    if(poll(fds.data(),fds.size(),5)<0 && errno!=EINTR)throw std::runtime_error("HTTP poll failed");
    for(unsigned i=0;i<8;++i) {
        auto& c=clients_[i];if(c.fd<0)continue;
        if(std::chrono::steady_clock::now()>c.deadline || (fds[i+1].revents&(POLLERR|POLLNVAL))) {close(c);continue;}
        if(!c.output.empty()) {
            auto sent=send(c.fd,c.output.data()+c.sent,std::min<std::size_t>(65536,c.output.size()-c.sent),MSG_NOSIGNAL);
            if(sent>0)c.sent+=sent;
            else if(sent<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR){close(c);continue;}
            if(c.sent==c.output.size())close(c);
        } else if(fds[i+1].revents&(POLLIN|POLLHUP)) {
            char buffer[2048];const auto got=recv(c.fd,buffer,sizeof(buffer),0);
            if(got>0) {if(c.pending){++rejected_;close(c);continue;}c.input.append(buffer,std::size_t(got));parse(c,state,metadata,request);}
            else if(!got || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR))close(c);
        }
    }
    // Reclaim EOF/disconnected slots before admitting the next connection.
    if(fds[0].revents&POLLIN)for(unsigned attempt=0;attempt<8;++attempt) {
        const auto fd=accept4(listener_,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);
        if(fd<0)break;
        bool accepted=false;for(auto& c:clients_)if(c.fd<0) {c.fd=fd;c.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);accepted=true;break;}
        if(!accepted){++rejected_;::close(fd);}
    }

}
}
