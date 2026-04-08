#ifndef BMMQ_ROM_IMAGE_HPP
#define BMMQ_ROM_IMAGE_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace BMMQ {

class RomImage {
public:
    void load(const std::vector<uint8_t>& bytes) {
        bytes_ = bytes;
    }

    [[nodiscard]] bool empty() const noexcept {
        return bytes_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return bytes_.size();
    }

    [[nodiscard]] std::span<const uint8_t> bytes() const noexcept {
        return std::span<const uint8_t>(bytes_.data(), bytes_.size());
    }

private:
    std::vector<uint8_t> bytes_;
};

} // namespace BMMQ

#endif // BMMQ_ROM_IMAGE_HPP
