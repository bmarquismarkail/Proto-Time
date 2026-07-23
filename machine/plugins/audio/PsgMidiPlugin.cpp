#include "PsgMidiPlugin.hpp"

#include "machine/Machine.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace BMMQ {
namespace {

constexpr std::uint16_t kMidiPpq = 480u;
constexpr std::uint32_t kTempoMicrosPerQuarter = 500000u;

void appendBe16(std::vector<std::uint8_t>& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>(value >> 8u));
    output.push_back(static_cast<std::uint8_t>(value));
}

void appendBe32(std::vector<std::uint8_t>& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value >> 24u));
    output.push_back(static_cast<std::uint8_t>(value >> 16u));
    output.push_back(static_cast<std::uint8_t>(value >> 8u));
    output.push_back(static_cast<std::uint8_t>(value));
}

void appendVariableLength(std::vector<std::uint8_t>& output, std::uint64_t value)
{
    std::array<std::uint8_t, 10u> bytes{};
    std::size_t count = 0u;
    bytes[count++] = static_cast<std::uint8_t>(value & 0x7Fu);
    while ((value >>= 7u) != 0u) {
        bytes[count++] = static_cast<std::uint8_t>((value & 0x7Fu) | 0x80u);
    }
    while (count != 0u) output.push_back(bytes[--count]);
}

std::uint8_t velocityFromLevel(std::uint16_t levelQ15)
{
    return static_cast<std::uint8_t>(std::clamp<std::uint32_t>(
        (static_cast<std::uint32_t>(levelQ15) * 127u + 16383u) / 32767u, 1u, 127u));
}

std::uint8_t expressionFromLevel(std::uint16_t levelQ15, std::uint16_t attackLevelQ15)
{
    const auto attack = std::max<std::uint32_t>(attackLevelQ15, 1u);
    return static_cast<std::uint8_t>(std::min<std::uint32_t>(
        (static_cast<std::uint32_t>(levelQ15) * 127u + attack / 2u) / attack, 127u));
}

std::uint8_t melodicChannel(std::uint8_t voiceId)
{
    auto channel = static_cast<std::uint8_t>(voiceId % 15u);
    if (channel >= 9u) ++channel;
    return channel;
}

std::uint8_t noteFor(const PsgAudioEvent& event)
{
    if (event.voiceKind == PsgVoiceKind::Noise) {
        return static_cast<std::uint8_t>(35u + (event.timbre % 47u));
    }
    if (event.frequencyMilliHz == 0u) return 0u;
    const auto hz = static_cast<double>(event.frequencyMilliHz) / 1000.0;
    const auto note = 69.0 + 12.0 * std::log2(hz / 440.0);
    return static_cast<std::uint8_t>(std::clamp<long>(std::lround(note), 0l, 127l));
}

std::uint16_t pitchBendFor(const PsgAudioEvent& event, std::uint8_t note)
{
    if (event.voiceKind == PsgVoiceKind::Noise || event.frequencyMilliHz == 0u) return 8192u;
    const auto hz = static_cast<double>(event.frequencyMilliHz) / 1000.0;
    const auto exactNote = 69.0 + 12.0 * std::log2(hz / 440.0);
    const auto semitoneOffset = std::clamp(exactNote - static_cast<double>(note), -2.0, 2.0);
    return static_cast<std::uint16_t>(std::clamp<long>(
        std::lround(8192.0 + semitoneOffset * 4096.0), 0l, 16383l));
}

} // namespace

MidiFileSink::MidiFileSink(std::filesystem::path path) : path_(std::move(path))
{
    if (path_.empty()) throw std::invalid_argument("MIDI output path must not be empty");
}

void MidiFileSink::send(const MidiMessage& message)
{
    if (message.size < 2u || message.size > 3u || message.sampleRate == 0u) return;
    messages_.push_back(message);
    flushed_ = false;
}

void MidiFileSink::flush()
{
    if (flushed_) return;
    std::stable_sort(messages_.begin(), messages_.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.sampleFrame < rhs.sampleFrame;
    });

    std::vector<std::uint8_t> track;
    appendVariableLength(track, 0u);
    track.insert(track.end(), {0xFFu, 0x51u, 0x03u,
        static_cast<std::uint8_t>(kTempoMicrosPerQuarter >> 16u),
        static_cast<std::uint8_t>(kTempoMicrosPerQuarter >> 8u),
        static_cast<std::uint8_t>(kTempoMicrosPerQuarter)});

    std::uint64_t previousTick = 0u;
    for (const auto& message : messages_) {
        const auto tick = (message.sampleFrame * 2u * kMidiPpq) / message.sampleRate;
        appendVariableLength(track, tick >= previousTick ? tick - previousTick : 0u);
        track.push_back(message.status);
        track.push_back(message.data1);
        if (message.size == 3u) track.push_back(message.data2);
        previousTick = std::max(previousTick, tick);
    }
    appendVariableLength(track, 0u);
    track.insert(track.end(), {0xFFu, 0x2Fu, 0x00u});

    std::vector<std::uint8_t> file;
    file.insert(file.end(), {'M', 'T', 'h', 'd'});
    appendBe32(file, 6u);
    appendBe16(file, 0u);
    appendBe16(file, 1u);
    appendBe16(file, kMidiPpq);
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    appendBe32(file, static_cast<std::uint32_t>(track.size()));
    file.insert(file.end(), track.begin(), track.end());

    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Unable to open MIDI output: " + path_.string());
    output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!output) throw std::runtime_error("Unable to write MIDI output: " + path_.string());
    flushed_ = true;
}

AsyncMidiSink::AsyncMidiSink(std::unique_ptr<IMidiMessageSink> sink) : sink_(std::move(sink))
{
    if (sink_ == nullptr) throw std::invalid_argument("Async MIDI sink must not be null");
    worker_ = std::thread([this]() { run(); });
}

AsyncMidiSink::~AsyncMidiSink()
{
    try {
        flush();
    } catch (...) {
        errors_.fetch_add(1u, std::memory_order_relaxed);
    }
    running_.store(false, std::memory_order_release);
    wakeCv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void AsyncMidiSink::send(const MidiMessage& message)
{
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = (head + 1u) % kQueueSlots;
    if (next == tail_.load(std::memory_order_acquire)) {
        dropped_.fetch_add(1u, std::memory_order_relaxed);
        return;
    }
    queue_[head] = message;
    head_.store(next, std::memory_order_release);
    enqueued_.fetch_add(1u, std::memory_order_relaxed);
    wakeCv_.notify_one();
}

void AsyncMidiSink::flush()
{
    std::unique_lock<std::mutex> lock(wakeMutex_);
    drainedCv_.wait(lock, [this]() {
        return empty() && !inFlight_.load(std::memory_order_acquire);
    });
    lock.unlock();
    sink_->flush();
}

AsyncMidiSinkStats AsyncMidiSink::stats() const noexcept
{
    return {
        .enqueued = enqueued_.load(std::memory_order_relaxed),
        .sent = sent_.load(std::memory_order_relaxed),
        .dropped = dropped_.load(std::memory_order_relaxed),
        .errors = errors_.load(std::memory_order_relaxed),
    };
}

bool AsyncMidiSink::empty() const noexcept
{
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
}

void AsyncMidiSink::run() noexcept
{
    while (running_.load(std::memory_order_acquire) || !empty()) {
        {
            std::unique_lock<std::mutex> lock(wakeMutex_);
            wakeCv_.wait(lock, [this]() {
                return !running_.load(std::memory_order_acquire) || !empty();
            });
        }
        while (!empty()) {
            const auto tail = tail_.load(std::memory_order_relaxed);
            const auto message = queue_[tail];
            inFlight_.store(true, std::memory_order_release);
            tail_.store((tail + 1u) % kQueueSlots, std::memory_order_release);
            try {
                sink_->send(message);
                sent_.fetch_add(1u, std::memory_order_relaxed);
            } catch (...) {
                errors_.fetch_add(1u, std::memory_order_relaxed);
            }
            inFlight_.store(false, std::memory_order_release);
            drainedCv_.notify_all();
        }
    }
    drainedCv_.notify_all();
}

PsgMidiPlugin::PsgMidiPlugin(std::unique_ptr<IMidiMessageSink> sink) : sink_(std::move(sink))
{
    if (sink_ == nullptr) throw std::invalid_argument("PSG MIDI sink must not be null");
}

void PsgMidiPlugin::onAttach(MutableMachineView&)
{
    voices_.fill({});
    lastSampleFrame_ = 0u;
    lastSampleRate_ = 48000u;
}

void PsgMidiPlugin::onDetach(MutableMachineView&)
{
    for (auto& voice : voices_) noteOff(voice, lastSampleFrame_, lastSampleRate_);
    sink_->flush();
}

void PsgMidiPlugin::onAudioEvent(const MachineEvent& event, const MachineView& view)
{
    if (event.type != MachineEventType::AudioFrameReady) return;
    const auto packet = view.realtimeAudioPacket();
    if (!packet.has_value() || packet->contractVersion != RealtimeAudioPacket::kContractVersion) return;
    lastSampleRate_ = std::max(packet->sampleRate, 1u);
    for (const auto& psgEvent : packet->events) {
        lastSampleFrame_ = packet->firstSampleFrame + psgEvent.sampleFrameOffset;
        processEvent(psgEvent, lastSampleFrame_, lastSampleRate_);
    }
}

void PsgMidiPlugin::processEvent(const PsgAudioEvent& event, std::uint64_t sampleFrame,
                                 std::uint32_t sampleRate)
{
    if (event.kind == PsgEventKind::RawWrite) return;
    auto& voice = voices_[event.voiceId];
    voice.channel = event.voiceKind == PsgVoiceKind::Noise ? 9u : melodicChannel(event.voiceId);
    voice.gate = event.gate;
    voice.timbre = event.timbre;

    switch (event.kind) {
    case PsgEventKind::StateSnapshot:
    case PsgEventKind::GateOn:
        if (event.gate) noteOn(voice, event, sampleFrame, sampleRate);
        else noteOff(voice, sampleFrame, sampleRate);
        break;
    case PsgEventKind::GateOff:
        noteOff(voice, sampleFrame, sampleRate);
        break;
    case PsgEventKind::Retrigger:
        noteOff(voice, sampleFrame, sampleRate);
        if (event.gate) noteOn(voice, event, sampleFrame, sampleRate);
        break;
    case PsgEventKind::PitchChange:
        if (voice.active) noteOff(voice, sampleFrame, sampleRate);
        if (event.gate) noteOn(voice, event, sampleFrame, sampleRate);
        break;
    case PsgEventKind::LevelChange:
        if (voice.active) {
            emit(sampleFrame, sampleRate, static_cast<std::uint8_t>(0xB0u | voice.channel),
                 11u, expressionFromLevel(event.levelQ15, voice.attackLevelQ15));
        }
        if (!event.gate) noteOff(voice, sampleFrame, sampleRate);
        break;
    case PsgEventKind::RoutingChange: {
        const auto pan = event.routingMask == 1u ? 0u : event.routingMask == 2u ? 127u : 64u;
        emit(sampleFrame, sampleRate, static_cast<std::uint8_t>(0xB0u | voice.channel), 10u, pan);
        break;
    }
    case PsgEventKind::TimbreChange:
        emit(sampleFrame, sampleRate, static_cast<std::uint8_t>(0xC0u | voice.channel),
             static_cast<std::uint8_t>(event.timbre & 0x7Fu), 0u, 2u);
        break;
    case PsgEventKind::RawWrite:
        break;
    }
}

void PsgMidiPlugin::emit(std::uint64_t sampleFrame, std::uint32_t sampleRate,
                         std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
                         std::uint8_t size)
{
    sink_->send({sampleFrame, sampleRate, status, data1, data2, size});
}

void PsgMidiPlugin::noteOn(VoiceState& voice, const PsgAudioEvent& event,
                           std::uint64_t sampleFrame, std::uint32_t sampleRate)
{
    const auto note = noteFor(event);
    if (voice.active) noteOff(voice, sampleFrame, sampleRate);
    voice.note = note;
    voice.attackLevelQ15 = std::max<std::uint16_t>(event.levelQ15, 1u);
    voice.velocity = velocityFromLevel(event.levelQ15);
    const auto bend = pitchBendFor(event, note);
    emit(sampleFrame, sampleRate, static_cast<std::uint8_t>(0xB0u | voice.channel),
         11u, 127u);
    emit(sampleFrame, sampleRate, static_cast<std::uint8_t>(0xE0u | voice.channel),
         static_cast<std::uint8_t>(bend & 0x7Fu), static_cast<std::uint8_t>((bend >> 7u) & 0x7Fu));
    emit(sampleFrame, sampleRate, static_cast<std::uint8_t>(0x90u | voice.channel),
         voice.note, voice.velocity);
    voice.active = true;
}

void PsgMidiPlugin::noteOff(VoiceState& voice, std::uint64_t sampleFrame,
                            std::uint32_t sampleRate)
{
    if (!voice.active) return;
    emit(sampleFrame, sampleRate, static_cast<std::uint8_t>(0x80u | voice.channel),
         voice.note, 0u);
    voice.active = false;
}

} // namespace BMMQ
