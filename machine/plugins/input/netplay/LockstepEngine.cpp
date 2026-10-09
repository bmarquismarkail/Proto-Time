#include "LockstepEngine.hpp"
#include <algorithm>
#include <limits>
#include <openssl/sha.h>
#include <stdexcept>

namespace BMMQ::Netplay {
namespace {
bool nonzero(const Digest& d) noexcept { return std::any_of(d.begin(), d.end(), [](auto v) { return v != 0; }); }
void integer(WirePacket& bytes, std::size_t at, std::uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) bytes[at+i] = std::uint8_t(value >> (8*(size-1-i)));
}
std::uint64_t integer(std::span<const std::uint8_t> bytes, std::size_t at, unsigned size) {
    std::uint64_t value{}; for (unsigned i = 0; i < size; ++i) value = (value << 8) | bytes[at+i]; return value;
}
void put(WirePacket& bytes, std::size_t at, const Digest& value) { std::copy(value.begin(), value.end(), bytes.begin()+at); }
Digest get(std::span<const std::uint8_t> bytes, std::size_t at) { Digest value; std::copy_n(bytes.begin()+at, 32, value.begin()); return value; }
}
bool valid(const Binding& b) noexcept {
    return (b.core == Core::GameBoy || b.core == Core::GameGear) && b.generation && nonzero(b.session) &&
        nonzero(b.rom) && nonzero(b.configuration) && !(b.ownership[0] & b.ownership[1]) &&
        (b.ownership[0] | b.ownership[1]) == 0xff;
}
std::uint64_t cyclesPerFrame(Core core) noexcept {
    // Protocol frames are emulated clock intervals starting at session attach,
    // including LCD-off operation. Instruction retirement is the input boundary.
    return core == Core::GameBoy ? 456u * 154u : core == Core::GameGear ? 228u * 262u : 0;
}
Digest digestFromHex(std::string_view text) {
    if (text.size() != 64) throw std::invalid_argument("netplay digest requires 64 hexadecimal characters");
    auto nibble = [](char c)->unsigned {
        if (c >= '0' && c <= '9') return unsigned(c-'0');
        if (c >= 'a' && c <= 'f') return unsigned(c-'a'+10);
        if (c >= 'A' && c <= 'F') return unsigned(c-'A'+10);
        throw std::invalid_argument("invalid netplay digest");
    };
    Digest result; for (std::size_t i=0;i<32;++i) result[i] = std::uint8_t(nibble(text[i*2])*16+nibble(text[i*2+1])); return result;
}
Digest stateDigest(std::string_view fingerprint) {
    if(fingerprint.size()!=16 && fingerprint.size()!=64) throw std::invalid_argument("unsupported core fingerprint identity");
    Digest result{};SHA256(reinterpret_cast<const unsigned char*>(fingerprint.data()),fingerprint.size(),result.data());return result;
}
WirePacket encode(const Packet& p) {
    if (!valid(p.binding) || p.peer > 1 || (p.input & ~p.binding.ownership[p.peer])) throw std::invalid_argument("invalid netplay packet");
    WirePacket bytes{};
    bytes[0]='T'; bytes[1]='N'; bytes[2]='P'; bytes[3]='1'; integer(bytes,4,1,2);
    bytes[6]=std::uint8_t(p.binding.core); bytes[7]=p.peer;
    integer(bytes,8,p.binding.generation,8); integer(bytes,16,p.frame,8);
    put(bytes,24,p.binding.session); put(bytes,56,p.binding.rom); put(bytes,88,p.binding.configuration); put(bytes,120,p.before);
    bytes[152]=p.input; bytes[153]=p.binding.ownership[0]; bytes[154]=p.binding.ownership[1];
    SHA256(bytes.data(),164,bytes.data()+164);
    return bytes;
}
std::optional<Packet> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size()!=196 || bytes[0]!='T' || bytes[1]!='N' || bytes[2]!='P' || bytes[3]!='1' || integer(bytes,4,2)!=1 || bytes[7]>1) return {};
    for (std::size_t n=155;n<164;++n) if(bytes[n]) return {};
    Digest trailer{}; SHA256(bytes.data(),164,trailer.data());
    if (!std::equal(trailer.begin(),trailer.end(),bytes.begin()+164)) return {};
    Packet p; p.binding.core=Core(bytes[6]); p.peer=bytes[7]; p.binding.generation=integer(bytes,8,8); p.frame=integer(bytes,16,8);
    p.binding.session=get(bytes,24); p.binding.rom=get(bytes,56); p.binding.configuration=get(bytes,88); p.before=get(bytes,120);
    p.input=bytes[152]; p.binding.ownership={bytes[153],bytes[154]};
    if (!valid(p.binding) || (p.input & ~p.binding.ownership[p.peer])) return {};
    return p;
}
LockstepEngine::LockstepEngine(Binding binding):binding_(binding) {
    if (!valid(binding)) throw std::invalid_argument("invalid lockstep session binding");
}
bool LockstepEngine::accept(const Packet& p) noexcept {
    if (fault_!=Fault::None) return false;
    if (!valid(p.binding) || p.peer>1 || (p.input & ~p.binding.ownership[p.peer])) { fail(Fault::InvalidPacket); return false; }
    if (p.binding!=binding_) { fail(Fault::BindingMismatch); return false; }
    if (p.frame>=next_ && p.frame-next_>=frames_.size()) { fail(Fault::WindowExhausted); return false; }
    auto& frame=p.frame<next_?history_[p.frame%history_.size()]:frames_[p.frame%frames_.size()];
    if (p.frame<next_ && (!frame.occupied || frame.number!=p.frame)) { ++stale_; return true; }
    if (!frame.occupied || frame.number!=p.frame) frame={.number=p.frame,.occupied=true};
    const auto bit=std::uint8_t(1u<<p.peer);
    if(frame.received&bit) {
        if (frame.packets[p.peer]!=p) { fail(Fault::ConflictingInput); return false; }
        ++duplicates_; return true;
    }
    if(p.frame<next_) { ++stale_; return true; }
    frame.packets[p.peer]=p; frame.received|=bit; return true;
}
std::optional<InputButtonMask> LockstepEngine::ready(const Digest& before) noexcept {
    if (fault_!=Fault::None) return {};
    const auto& frame=frames_[next_%frames_.size()];
    if(!frame.occupied || frame.number!=next_ || frame.received!=3) return {};
    if(frame.packets[0].before!=before || frame.packets[1].before!=before) { fail(Fault::FingerprintMismatch); return {}; }
    ready_=true;
    return InputButtonMask(frame.packets[0].input|frame.packets[1].input);
}
bool LockstepEngine::completeFrame() noexcept {
    const auto& frame=frames_[next_%frames_.size()];
    if(!ready_ || fault_!=Fault::None || !frame.occupied || frame.number!=next_ || frame.received!=3) return false;
    if(next_==std::numeric_limits<std::uint64_t>::max()) { fail(Fault::WindowExhausted); return false; }
    history_[next_%history_.size()]=frame;
    ++next_; ready_=false; return true;
}
}
