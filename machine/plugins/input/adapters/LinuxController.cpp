#include "LinuxController.hpp"
#include "machine/FixedSpscQueue.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace BMMQ {
namespace {
struct Packet {
    std::uint64_t generation{}, connection{};
    InputBoundarySnapshotV1 sample{};
};
bool valid(const LinuxControllerProfile& p) noexcept {
    for (const auto code : p.buttons) if (code > KEY_MAX) return false;
    for (const auto& a : p.axes)
        if (a.code > ABS_MAX || a.minimum >= a.maximum || a.deadzone < 0 ||
            std::int64_t(a.deadzone) * 2 >= std::int64_t(a.maximum) - a.minimum) return false;
    return p.horizontalHat <= ABS_MAX && p.verticalHat <= ABS_MAX && p.horizontalHat != p.verticalHat;
}
std::int16_t normalize(std::int32_t value, const LinuxControllerAxis& a) noexcept {
    const auto low = std::int64_t(a.minimum), high = std::int64_t(a.maximum);
    const auto mid = low + (high - low) / 2;
    const auto delta = std::clamp(std::int64_t(value), low, high) - mid;
    if (delta >= -a.deadzone && delta <= a.deadzone) return 0;
    if (delta < 0) return static_cast<std::int16_t>((delta + a.deadzone) * 32768 / (mid - low - a.deadzone));
    return static_cast<std::int16_t>((delta - a.deadzone) * 32767 / (high - mid - a.deadzone));
}
}
struct LinuxController::Impl {
    std::string path;
    LinuxControllerProfile configured, current;
    int source{-1}, wake{-1};
    bool stream{};
    std::thread worker;
    std::atomic<bool> stopping{false}, connected{false}, failed{false};
    std::atomic<std::uint64_t> requestedGeneration{0}, connection{0};
    std::atomic<LinuxControllerError> error{LinuxControllerError::None};
    std::atomic<std::uint64_t> reports{0}, events{0}, overflows{0}, connections{0}, drops{0}, stale{0};
    FixedSpscQueue<Packet, 32> queue;
    InputBoundarySnapshotV1 sampled{};
    bool sampledValid{};
    std::uint64_t sampledGeneration{std::numeric_limits<std::uint64_t>::max()}, sampledConnection{};
    InputButtonMask buttons{}, hatX{}, hatY{};
    InputAnalogState axes{};
    InputBoundarySnapshotV1 reported{};
    bool dropping{};

    Impl(std::string device, LinuxControllerProfile p): path(std::move(device)), configured(p), current(p) {}
    Impl(int fd, LinuxControllerProfile p): configured(p), current(p), stream(true) {
        if (fd >= 0 && (fcntl(fd, F_GETFL) & O_NONBLOCK) != 0) source = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    }
    ~Impl() { if (source >= 0) ::close(source); }
    void fault(LinuxControllerError e) noexcept {
        error.store(e, std::memory_order_release);
        failed.store(true, std::memory_order_release);
        connected.store(false, std::memory_order_release);
    }
    void publish(bool commit = false) noexcept {
        if (failed.load(std::memory_order_relaxed)) return;
        Packet packet;
        packet.generation = requestedGeneration.load(std::memory_order_acquire);
        packet.connection = connection.load(std::memory_order_relaxed);
        if(commit) reported = {InputButtonMask(buttons | hatX | hatY), axes, false};
        packet.sample = reported;
        if (!queue.push(packet)) { ++overflows; fault(LinuxControllerError::Overflow); }
        else ++reports;
    }
    void event(const input_event& e) noexcept {
        if (dropping) return;
        if (e.type == EV_KEY) {
            if (e.value < 0 || e.value > 2) { fault(LinuxControllerError::MalformedStream); return; }
            for (std::size_t i = 0; i < current.buttons.size(); ++i)
                if (current.buttons[i] == e.code) {
                    const auto mask = InputButtonMask(1u << i);
                    if (e.value) buttons |= mask; else buttons &= InputButtonMask(~mask);
                }
        } else if (e.type == EV_ABS) {
            if (e.code == current.horizontalHat) hatX = e.value < 0 ? 2 : e.value > 0 ? 1 : 0;
            if (e.code == current.verticalHat) hatY = e.value < 0 ? 4 : e.value > 0 ? 8 : 0;
            for (std::size_t i = 0; i < current.axes.size(); ++i)
                if (e.code == current.axes[i].code) axes.channels[i] = normalize(e.value, current.axes[i]);
        }
    }
    bool resync(int fd) noexcept {
        // Runs only on the host worker, after SYN_REPORT when recovering loss.
        std::array<unsigned char, (KEY_MAX + 8) / 8> keys{};
        if (ioctl(fd, EVIOCGKEY(keys.size()), keys.data()) < 0) return false;
        buttons = hatX = hatY = 0; axes = {};
        for (std::size_t i = 0; i < current.buttons.size(); ++i) {
            const auto code = current.buttons[i];
            if ((keys[code / 8] & (1u << (code % 8))) != 0) buttons |= InputButtonMask(1u << i);
        }
        for (std::size_t i = 0; i < current.axes.size(); ++i) {
            input_absinfo info{};
            if (ioctl(fd, EVIOCGABS(current.axes[i].code), &info) < 0) continue; // Optional stick.
            LinuxControllerAxis axis{current.axes[i].code, info.minimum, info.maximum, info.flat};
            if (axis.minimum >= axis.maximum || axis.deadzone < 0 ||
                std::int64_t(axis.deadzone) * 2 >= std::int64_t(axis.maximum) - axis.minimum) return false;
            current.axes[i] = axis;
            axes.channels[i] = normalize(info.value, axis);
        }
        for (const auto code : {current.horizontalHat, current.verticalHat}) {
            input_absinfo info{};
            if (ioctl(fd, EVIOCGABS(code), &info) == 0) {
                input_event e{}; e.type = EV_ABS; e.code = code; e.value = info.value; event(e);
            }
        }
        return true;
    }
    bool wait(int fd, int timeout) noexcept {
        pollfd fds[2]{{wake, POLLIN, 0}, {fd, POLLIN, 0}};
        int result;
        do { result = ::poll(fds, fd < 0 ? 1 : 2, timeout); } while (result < 0 && errno == EINTR);
        if (result < 0) { fault(LinuxControllerError::WorkerFailure); return false; }
        return !stopping.load(std::memory_order_acquire);
    }
    void run() noexcept {
        int fd = -1;
        std::array<unsigned char, sizeof(input_event)> bytes{};
        std::size_t used = 0;
        std::uint64_t lastGeneration = requestedGeneration.load();
        while (!stopping.load(std::memory_order_acquire)) {
            if (failed.load(std::memory_order_acquire)) { if (!wait(-1, 100)) break; continue; }
            if (fd < 0) {
                fd = stream ? fcntl(source, F_DUPFD_CLOEXEC, 3) : ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd < 0) { error.store(LinuxControllerError::DeviceUnavailable); if (!wait(-1, 100)) break; continue; }
                current = configured; buttons = hatX = hatY = 0; axes = {}; dropping = false; used = 0;
                if (!stream) {
                    // Reject regular files and non-evdev devices before interpreting data.
                    struct stat st{}; int version{};
                    if (fstat(fd, &st) < 0 || !S_ISCHR(st.st_mode) || ioctl(fd, EVIOCGVERSION, &version) < 0 || !resync(fd)) {
                        ::close(fd); fd = -1; error.store(LinuxControllerError::DeviceUnavailable);
                        if (!wait(-1, 100)) break;
                        continue;
                    }
                }
                ++connection; ++connections;
                connected.store(true, std::memory_order_release); error.store(LinuxControllerError::None);
                publish(true);
            }
            if (!wait(fd, 100)) break;
            const auto gen = requestedGeneration.load(std::memory_order_acquire);
            if (gen != lastGeneration) { lastGeneration = gen; publish(); }
            bool lost = false;
            // A fixed read quantum keeps stop and generation changes responsive.
            for (std::size_t n = 0; n < 512 && !failed.load() && !stopping.load(); ++n) {
                const auto got = ::read(fd, bytes.data() + used, bytes.size() - used);
                if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                if (got < 0 && errno == EINTR) continue;
                if (got <= 0) {
                    if (used) fault(LinuxControllerError::MalformedStream);
                    else { connected.store(false, std::memory_order_release); error.store(LinuxControllerError::Disconnected); }
                    lost = true; break;
                }
                used += std::size_t(got);
                if (used != bytes.size()) continue;
                input_event e{}; std::memcpy(&e, bytes.data(), sizeof(e)); used = 0; ++events;
                if (e.type == EV_SYN && e.code == SYN_DROPPED) {
                    ++drops; dropping = true; connected.store(false, std::memory_order_release);
                } else if (e.type == EV_SYN && e.code == SYN_REPORT) {
                    if (dropping) {
                        dropping = false;
                        if (stream || !resync(fd)) { fault(LinuxControllerError::ResyncUnavailable); break; }
                        ++connection; connected.store(true, std::memory_order_release);
                    }
                    publish(true);
                } else event(e);
            }
            if (lost) {
                ::close(fd); fd = -1;
                if (stream) { fault(error.load()); continue; }
                if (!wait(-1, 100)) break;
            }
        }
        if (fd >= 0) ::close(fd);
        connected.store(false, std::memory_order_release);
    }
};
LinuxController::LinuxController(std::string path, LinuxControllerProfile profile): impl_(std::make_unique<Impl>(std::move(path), profile)) {}
LinuxController::LinuxController(int stream, LinuxControllerProfile profile): impl_(std::make_unique<Impl>(stream, profile)) {}
LinuxController::~LinuxController() { close(); }
InputPluginCapabilities LinuxController::capabilities() const noexcept {
    InputPluginCapabilities caps;
    caps.pollingSafe = caps.eventPumpSafe = caps.supportsDigital = caps.supportsAnalog = caps.fixedLogicalLayout = caps.hotSwapSafe = true;
    caps.deterministic = false; caps.headlessSafe = impl_->stream;
    return caps;
}
std::string_view LinuxController::name() const noexcept { return "Linux evdev controller / boundary snapshots v1"; }
bool LinuxController::open() {
    close();
    auto& p = *impl_;
    if (!valid(p.configured) || (p.stream ? p.source < 0 : p.path.empty() || p.path.front() != '/' || p.path.size() > 4096)) {
        p.error.store(LinuxControllerError::InvalidProfile); return false;
    }
    p.queue.resetQuiescent(); p.sampled = {}; p.sampledValid = false; p.sampledGeneration = std::numeric_limits<std::uint64_t>::max();
    p.stopping.store(false); p.failed.store(false); p.error.store(LinuxControllerError::None);
    p.wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (p.wake < 0) { p.error.store(LinuxControllerError::WorkerFailure); return false; }
    try { p.worker = std::thread([&p] { p.run(); }); }
    catch (...) { ::close(p.wake); p.wake = -1; p.error.store(LinuxControllerError::WorkerFailure); return false; }
    return true;
}
void LinuxController::close() noexcept {
    auto& p = *impl_;
    p.stopping.store(true, std::memory_order_release);
    if (p.wake >= 0) { const std::uint64_t one = 1; (void)::write(p.wake, &one, sizeof(one)); }
    if (p.worker.joinable()) p.worker.join();
    if (p.wake >= 0) { ::close(p.wake); p.wake = -1; }
    p.connected.store(false);
}
std::string_view LinuxController::lastError() const noexcept {
    switch (impl_->error.load(std::memory_order_acquire)) {
        case LinuxControllerError::None: return {};
        case LinuxControllerError::DeviceUnavailable: return "controller device unavailable; waiting for reconnect";
        case LinuxControllerError::Disconnected: return "controller disconnected; neutral input";
        case LinuxControllerError::Overflow: return "controller handoff overflow; neutral input until reopen";
        case LinuxControllerError::MalformedStream: return "malformed controller event stream; neutral input until reopen";
        case LinuxControllerError::ResyncUnavailable: return "controller SYN_DROPPED resynchronization unavailable; neutral input until reopen";
        case LinuxControllerError::InvalidProfile: return "invalid controller path, stream or mapping profile";
        case LinuxControllerError::WorkerFailure: return "controller worker failure";
    }
    return "unknown controller error";
}
InputBoundarySnapshotV1 LinuxController::sampleBoundaryV1(std::uint64_t generation) noexcept {
    auto& p = *impl_;
    p.requestedGeneration.store(generation, std::memory_order_release);
    const auto connection = p.connection.load(std::memory_order_acquire);
    if (p.sampledGeneration != generation || p.sampledConnection != connection) {
        p.sampled = {}; p.sampledValid = false; p.sampledGeneration = generation; p.sampledConnection = connection;
    }
    for (std::size_t n = 0; n < 32; ++n) {
        const auto packet = p.queue.pop(); if (!packet) break;
        if (packet->generation == generation && packet->connection == connection) { p.sampled = packet->sample; p.sampledValid = true; }
        else ++p.stale;
    }
    if (!p.sampledValid || p.connection.load(std::memory_order_acquire) != connection ||
        !p.connected.load(std::memory_order_acquire) || p.failed.load(std::memory_order_acquire)) return {0, {}, true};
    return p.sampled;
}
LinuxControllerDiagnostics LinuxController::diagnostics() const noexcept {
    const auto& p = *impl_;
    return {p.reports.load(), p.events.load(), p.overflows.load(), p.connections.load(), p.drops.load(), p.stale.load(), p.error.load(), p.connected.load()};
}
}
