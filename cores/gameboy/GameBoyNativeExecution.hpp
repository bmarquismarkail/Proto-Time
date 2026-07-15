#ifndef BMMQ_GAMEBOY_NATIVE_EXECUTION_HPP
#define BMMQ_GAMEBOY_NATIVE_EXECUTION_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "GameBoyIrExecution.hpp"

namespace GB::NativeExecution {

class BlockArtifact final : public BMMQ::BlockBackendArtifact {
public:
    ~BlockArtifact() override;
    BlockArtifact(const BlockArtifact&) = delete;
    BlockArtifact& operator=(const BlockArtifact&) = delete;

    [[nodiscard]] IRExecution::InstructionResult execute(
        std::size_t instructionIndex,
        const IRExecution::ExecutionAbiV1& abi) const;
    [[nodiscard]] std::size_t instructionCount() const noexcept;
    [[nodiscard]] std::size_t codeSize() const noexcept;
    [[nodiscard]] const void* codeAddress() const noexcept;
    [[nodiscard]] bool sealedExecutable() const noexcept;

private:
    friend std::shared_ptr<const BlockArtifact> compile(
        const BMMQ::IR::Block&, std::string*);
    struct Impl;
    explicit BlockArtifact(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool supported() noexcept;
[[nodiscard]] std::shared_ptr<const BlockArtifact> compile(
    const BMMQ::IR::Block& block,
    std::string* error = nullptr);

} // namespace GB::NativeExecution

#endif
