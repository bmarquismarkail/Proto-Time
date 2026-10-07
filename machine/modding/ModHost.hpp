#ifndef BMMQ_MOD_HOST_HPP
#define BMMQ_MOD_HOST_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace BMMQ::Modding {

struct Symbol {
    std::uint8_t bank = 0;
    std::uint16_t address = 0;
};

struct Region {
    std::string name;
    std::vector<std::uint8_t> bytes;
    std::uint16_t guestBase = 0;
    std::uint8_t bank = 0;
};

struct HookContext {
    std::uint64_t programCounter = 0;
    std::uint64_t userData = 0;
};

class ModHost final {
public:
    ModHost() = default;
    ModHost(const ModHost&);
    ModHost(ModHost&&);
    ModHost& operator=(const ModHost&);
    ModHost& operator=(ModHost&&) noexcept;
    [[nodiscard]] std::weak_ptr<const void> lifetime() const noexcept { return lifetime_; }
    using PatchVerifier = std::function<bool(std::uint8_t bank, std::uint16_t address,
                                             std::span<const std::uint8_t> expected)>;
    using PatchWriter = std::function<bool(std::uint8_t bank, std::uint16_t address,
                                           std::span<const std::uint8_t> bytes)>;
    using WindowMapper = std::function<bool(std::uint16_t base, std::span<const std::uint8_t> bytes,
                                            std::uint8_t bank)>;
    using Hook = std::function<void(HookContext&)>;

    [[nodiscard]] std::uint32_t createRegion(std::string name, std::size_t size,
                                              std::uint16_t guestBase = 0,
                                              std::uint8_t bank = 0);
    [[nodiscard]] std::span<std::uint8_t> region(std::uint32_t id) noexcept;
    [[nodiscard]] std::span<const std::uint8_t> region(std::uint32_t id) const noexcept;
    [[nodiscard]] const Region* regionInfo(std::uint32_t id) const noexcept;
    [[nodiscard]] bool mapRegion(std::uint32_t id, const WindowMapper& mapper) const;

    [[nodiscard]] bool addSymbol(std::string name, Symbol symbol);
    [[nodiscard]] const Symbol* resolveSymbol(std::string_view name) const;

    [[nodiscard]] bool applyPatch(const Symbol& target,
                                  std::span<const std::uint8_t> expected,
                                  std::span<const std::uint8_t> replacement,
                                  const PatchVerifier& verifier,
                                  const PatchWriter& writer);

    [[nodiscard]] std::uint32_t registerHook(std::string symbolName, Hook hook);
    [[nodiscard]] bool invokeHook(std::string_view symbolName, HookContext& context) const;

    // Prepared bytes are owned by the caller; publication keeps region addresses stable.
    class PreparedState {
        friend class ModHost;
        struct Binding { std::string name; std::uint16_t base; std::uint8_t bank; };
        std::vector<Binding> bindings_;
        std::vector<std::vector<std::uint8_t>> bytes_;
    };
    [[nodiscard]] PreparedState prepareState(std::span<const std::uint8_t> state) const;
    void commitState(PreparedState&& state);
    [[nodiscard]] PreparedState prepareCheckpoint(std::optional<std::span<const std::uint8_t>> state) const;
    [[nodiscard]] bool hasRegions() const noexcept { return !regions_.empty(); }
    void reset() noexcept;
    [[nodiscard]] std::vector<std::uint8_t> exportState() const;
    [[nodiscard]] bool importState(std::span<const std::uint8_t> state) noexcept;

private:
    struct PatchKey {
        std::uint16_t address = 0;
        std::uint8_t bank = 0;
        bool operator==(const PatchKey&) const noexcept = default;
    };
    struct PatchKeyHash {
        std::size_t operator()(PatchKey key) const noexcept
        {
            return (static_cast<std::size_t>(key.bank) << 16u) | key.address;
        }
    };

    std::shared_ptr<const void> lifetime_ = std::make_shared<int>(0);
    std::vector<Region> regions_;
    std::unordered_map<std::string, Symbol> symbols_;
    std::unordered_map<std::string, Hook> hooks_;
    std::unordered_map<PatchKey, std::size_t, PatchKeyHash> patches_;
};

} // namespace BMMQ::Modding

#endif
