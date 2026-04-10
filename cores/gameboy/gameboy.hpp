#ifndef DMG_CPU
#define DMG_CPU

#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "../../machine/CPU.hpp"
#include "../../common_microcode.hpp"
#include "../../inst_cycle/opcode.hpp"
#include "../../inst_cycle/execute/executionBlock.hpp"
#include "../../inst_cycle/fetch/fetchBlock.hpp"
#include "../../memory/MemoryPool.hpp"
#include "../../memory/MemorySnapshot/MemorySnapshot.hpp"
#include "../../memory/templ/reg_uint16.impl.hpp"
#include "register_id.hpp"

using AddressType = uint16_t;
using DataType = uint8_t;
using LR3592_Register = BMMQ::CPU_Register<AddressType>;
using LR3592_RegisterPair = BMMQ::CPU_RegisterPair<AddressType>;
using LR3592_RegisterFile = BMMQ::RegisterFile<AddressType>;

class LR3592_DMG : public BMMQ::CPU<AddressType, DataType, AddressType> {
  using Opcode = BMMQ::Opcode<AddressType, DataType, AddressType>;
  using OpcodeTable = std::array<std::optional<Opcode>, 256>;

  struct CachedRegisterRef {
    LR3592_Register *reg = nullptr;
    AddressType address = 0;
  };

  struct HardwareRegisterCache {
    CachedRegisterRef joyp{};
    CachedRegisterRef sb{};
    CachedRegisterRef sc{};
    CachedRegisterRef div{};
    CachedRegisterRef tima{};
    CachedRegisterRef tma{};
    CachedRegisterRef tac{};
    CachedRegisterRef interruptFlags{};
    CachedRegisterRef lcdc{};
    CachedRegisterRef stat{};
    CachedRegisterRef ly{};
    CachedRegisterRef lyc{};
    CachedRegisterRef dma{};
    CachedRegisterRef ie{};
  };

  static constexpr uint32_t kCpuClockHz = 4194304u;
  static constexpr uint32_t kApuSampleRate = 48000u;
  static constexpr std::size_t kApuHistorySamples = 4096u;
  static constexpr std::size_t kApuFrameChunkSamples = 256u;

  struct ApuPulseChannel {
    bool enabled = false;
    bool dacEnabled = false;
    bool lengthEnabled = false;
    uint8_t duty = 0;
    uint8_t dutyStep = 0;
    uint8_t lengthCounter = 0;
    uint8_t initialVolume = 0;
    uint8_t volume = 0;
    bool envelopeIncrease = false;
    uint8_t envelopePeriod = 0;
    uint8_t envelopeTimer = 0;
    uint16_t frequency = 0;
    uint16_t timer = 0;
    bool hasSweep = false;
    uint8_t sweepPeriod = 0;
    uint8_t sweepTimer = 0;
    bool sweepNegate = false;
    uint8_t sweepShift = 0;
    uint16_t shadowFrequency = 0;
    bool sweepEnabled = false;
  };

  struct ApuWaveChannel {
    bool enabled = false;
    bool dacEnabled = false;
    bool lengthEnabled = false;
    uint16_t lengthCounter = 0;
    uint16_t frequency = 0;
    uint16_t timer = 0;
    uint8_t sampleIndex = 0;
  };

  struct ApuNoiseChannel {
    bool enabled = false;
    bool dacEnabled = false;
    bool lengthEnabled = false;
    uint8_t lengthCounter = 0;
    uint8_t initialVolume = 0;
    uint8_t volume = 0;
    bool envelopeIncrease = false;
    uint8_t envelopePeriod = 0;
    uint8_t envelopeTimer = 0;
    uint8_t clockShift = 0;
    uint8_t divisorCode = 0;
    bool widthMode7 = false;
    uint16_t timer = 0;
    uint16_t lfsr = 0x7FFFu;
  };

  struct ApuState {
    bool masterEnabled = true;
    uint32_t frameSequencerCounter = 0;
    uint8_t frameSequencerStep = 0;
    uint32_t sampleAccumulator = 0;
    uint64_t sampleCounter = 0;
    uint64_t frameCounter = 0;
    std::array<int16_t, kApuHistorySamples> recentSamples{};
    std::size_t recentWriteCursor = 0;
    std::size_t recentSampleCount = 0;
    ApuPulseChannel pulse1{};
    ApuPulseChannel pulse2{};
    ApuWaveChannel wave{};
    ApuNoiseChannel noise{};
  };

  BMMQ::MemoryPool<AddressType, DataType, AddressType> mem;
  OpcodeTable opcodeTable;
  uint16_t flagset;
  BMMQ::CpuFeedback feedback;
  DataType cip;
  bool ime = false;
  bool imeEnablePending = false;
  DataType imeEnableDelay = 0;
  LR3592_Register *pcRegister_ = nullptr;
  LR3592_Register *spRegister_ = nullptr;
  HardwareRegisterCache hardwareRegisters_{};
  bool stopFlag = false, haltFlag = false;
  bool haltBugActive = false;
  bool haltBugPcAdjustPending = false;
  uint16_t dividerCounter = 0;
  bool dmaActive = false;
  AddressType dmaSourceBase = 0;
  uint16_t dmaCycleProgress = 0;
  std::size_t pendingCycleCharge_ = 0;
  bool serialTransferActive = false;
  uint16_t serialCycleProgress = 0;
  ApuState apu_{};
  DataType joypSelect = 0x30;
  DataType joypadPressedMask = 0;
  uint32_t ppuDotCounter = 0;
  bool lcdEnabledLastTick = false;
  bool statInterruptLatched = false;

  // Gameboy-specific Decode Helper Functions
  LR3592_Register &GetRegister(BMMQ::RegisterInfo<AddressType> &Reg,
                               LR3592_RegisterFile *file);
  bool checkJumpCond(DataType opcode, LR3592_RegisterFile *file);
  AddressType *ld_R16_I16_GetRegister(DataType opcode,
                                      LR3592_RegisterFile *File);
  AddressType *add_HL_r16_GetRegister(DataType opcode,
                                      LR3592_RegisterFile *file);
  AddressType *ld_r16_8_GetRegister(DataType opcode, LR3592_RegisterFile *file);
  std::pair<DataType *, DataType *>
  ld_r16_8_GetOperands(DataType opcode, LR3592_RegisterFile *file);
  DataType *ld_r8_i8_GetRegister(DataType opcode, LR3592_RegisterFile *file);
  void rotateAccumulator(DataType opcode, LR3592_RegisterFile *file);
  void manipulateAccumulator(DataType opcode, LR3592_RegisterFile *file);
  void manipulateCarry(DataType opcode, LR3592_RegisterFile *file);
  DataType *ld_r8_r8_GetRegister(DataType regcode, LR3592_RegisterFile *file);
  std::pair<DataType *, DataType *>
  ld_r8_r8_GetOperands(DataType opcode, LR3592_RegisterFile *file);
  void math_r8(DataType opcode, LR3592_RegisterFile *file);
  void math_i8(DataType opcode, LR3592_RegisterFile *file);
  void ret(LR3592_RegisterFile *file);
  void ret_cc(DataType opcode, LR3592_RegisterFile *file);
  AddressType *push_pop_GetRegister(DataType opcode, LR3592_RegisterFile *file);
  void pop(DataType opcode, LR3592_RegisterFile *file);
  void push(DataType opcode, LR3592_RegisterFile *file);
  void call(LR3592_RegisterFile *file);
  void call_cc(DataType opcode, LR3592_RegisterFile *file);
  void rst(DataType opcode, LR3592_RegisterFile *file);
  void ldh(DataType opcode, LR3592_RegisterFile *file);
  void ld_ir16_r8(DataType opcode, LR3592_RegisterFile *file);
  void ei_di(DataType opcode, LR3592_RegisterFile *file);
  void ld_hl_sp(DataType opcode, LR3592_RegisterFile *file);
  void cb_execute(DataType opcode, LR3592_RegisterFile *file);
  void calculateflags(uint16_t calculationFlags, LR3592_RegisterFile *file);
  void nop();
  void stop();
  void populateOpcodes();
  void requestInterrupt(DataType mask);
  bool serviceInterruptIfPending();
  void initializeRegisterCache();
  void cacheRegisterRef(CachedRegisterRef& slot, std::string_view name, AddressType address);
  [[nodiscard]] static DataType readCachedRegister(const CachedRegisterRef& slot);
  [[nodiscard]] const CachedRegisterRef* cachedIoRegisterForAddress(AddressType address) const;
  void writeCachedRegister(const CachedRegisterRef& slot, DataType value);
  DataType readIoRegister(std::string_view name) const;
  void writeIoRegister(std::string_view name, DataType value);
  void initializeApu();
  void handleApuRegisterWrite(AddressType address, DataType value);
  void stepApuOneCycle();
  void stepApuFrameSequencer();
  void tickApuLengthCounters();
  void tickApuSweep();
  void tickApuEnvelope(ApuPulseChannel& channel);
  void tickApuEnvelope(ApuNoiseChannel& channel);
  void triggerApuPulse(ApuPulseChannel& channel, DataType control, DataType envelope, bool withSweep);
  void triggerApuWave();
  void triggerApuNoise();
  [[nodiscard]] uint16_t pulseTimerPeriod(uint16_t frequency) const;
  [[nodiscard]] uint16_t waveTimerPeriod(uint16_t frequency) const;
  [[nodiscard]] uint16_t noiseTimerPeriod() const;
  [[nodiscard]] int currentPulseSample(const ApuPulseChannel& channel) const;
  [[nodiscard]] int currentWaveSample() const;
  [[nodiscard]] int currentNoiseSample() const;
  void pushApuSample();
  void updateApuStatusRegister();
  void retireInstruction(std::size_t executedByteCount);
  [[nodiscard]] bool lcdEnabled() const;
  [[nodiscard]] DataType currentPpuMode() const;
  [[nodiscard]] DataType joypadLowNibble() const;
  static AddressType normalizeAccessAddress(AddressType address);

public:
  BMMQ::RegisterInfo<AddressType> AF{GB::RegisterId::AF};
  BMMQ::RegisterInfo<AddressType> BC{GB::RegisterId::BC};
  BMMQ::RegisterInfo<AddressType> DE{GB::RegisterId::DE};
  BMMQ::RegisterInfo<AddressType> HL{GB::RegisterId::HL};
  BMMQ::RegisterInfo<AddressType> SP{GB::RegisterId::SP};
  BMMQ::RegisterInfo<AddressType> PC{GB::RegisterId::PC};

  // CTOR

  LR3592_DMG();
  BMMQ::MemoryStorage<AddressType, DataType> buildMemoryStore();
  LR3592_RegisterFile buildRegisterfile();
  void attachMemory(BMMQ::MemoryStorage<AddressType, DataType>& store);
  BMMQ::fetchBlock<AddressType, DataType> fetch();
  void fetchInto(BMMQ::fetchBlock<AddressType, DataType>& out);
  void loadProgram(const std::vector<DataType>& program,
                   AddressType startAddress = 0);

  BMMQ::executionBlock<AddressType, DataType, AddressType>
  decode(BMMQ::fetchBlock<AddressType, DataType> &fetchData) override;
  void decodeInto(BMMQ::fetchBlock<AddressType, DataType>& fetchData,
                  BMMQ::executionBlock<AddressType, DataType, AddressType>& block);
  bool tryFastExecute(BMMQ::fetchBlock<AddressType, DataType>& fetchData);

  void
  execute(const BMMQ::executionBlock<AddressType, DataType, AddressType> &block,
          BMMQ::fetchBlock<AddressType, DataType> &fb) override;
  const BMMQ::CpuFeedback &getLastFeedback() const override;
  uint32_t clockHz() const override { return kCpuClockHz; }

  BMMQ::MemoryPool<AddressType, DataType, AddressType> &getMemory();
  const BMMQ::MemoryPool<AddressType, DataType, AddressType> &getMemory() const;
  void setIme(bool enabled);
  void scheduleImeEnable();
  void resetDivider();
  void resetApu() { initializeApu(); }
  [[nodiscard]] uint32_t audioSampleRate() const noexcept { return kApuSampleRate; }
  [[nodiscard]] uint64_t audioFrameCounter() const noexcept { return apu_.frameCounter; }
  [[nodiscard]] std::vector<int16_t> copyRecentAudioSamples() const;
  void setJoypadState(DataType pressedMask);
  bool handleMemoryRead(AddressType address, std::span<DataType> value) const;
  bool handleMemoryWrite(AddressType address, std::span<const DataType> value);
  void setStopFlag(bool f);
  void setHaltFlag(bool f);
  void clearHaltFlag();
};
#endif // DMG_CPU
