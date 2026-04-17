#ifndef GB_CARTRIDGE_SAVE_MANAGER_HPP
#define GB_CARTRIDGE_SAVE_MANAGER_HPP

#include "GameBoyCartridge.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <vector>

namespace GB {

class CartridgeSaveManager {
public:
    void bindRomPath(const std::filesystem::path& romPath)
    {
        savePath_ = romPath;
        savePath_.replace_extension(".sav");
        rtcPath_ = romPath;
        rtcPath_.replace_extension(".rtc");
        bound_ = true;
    }

    void clearBinding() noexcept
    {
        bound_ = false;
        savePath_.clear();
        rtcPath_.clear();
    }

    [[nodiscard]] bool bound() const noexcept
    {
        return bound_;
    }

    [[nodiscard]] const std::filesystem::path& savePath() const noexcept
    {
        return savePath_;
    }

    [[nodiscard]] const std::filesystem::path& rtcPath() const noexcept
    {
        return rtcPath_;
    }

    void load(GameBoyCartridge& cartridge) const
    {
        if (!bound_ || !cartridge.supportsBatterySave()) {
            cartridge.markSaveClean();
            return;
        }

        CartridgeSaveData save = cartridge.exportSaveData();
        if (std::filesystem::exists(savePath_)) {
            save.externalRam = readFile(savePath_);
        }
        if (cartridge.supportsRtc() && std::filesystem::exists(rtcPath_)) {
            if (const auto rtc = readRtcSidecar(rtcPath_); rtc.has_value()) {
                save.rtc = *rtc;
            }
        }
        cartridge.importSaveData(save);
    }

    [[nodiscard]] bool flush(GameBoyCartridge& cartridge) const
    {
        if (!bound_ || !cartridge.supportsBatterySave() || !cartridge.hasDirtySaveData()) {
            return false;
        }

        const auto save = cartridge.exportSaveData();
        writeFileAtomic(savePath_, save.externalRam);
        if (cartridge.supportsRtc() && save.rtc.has_value()) {
            writeFileAtomic(rtcPath_, encodeRtcSidecar(*save.rtc));
        }
        cartridge.markSaveClean();
        return true;
    }

private:
    [[nodiscard]] static std::vector<uint8_t> readFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Unable to open save file: " + path.string());
        }
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    }

    static void writeFileAtomic(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
    {
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        auto tempPath = path;
        tempPath += ".tmp";

        {
            std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("Unable to open temporary save file: " + tempPath.string());
            }
            if (!bytes.empty()) {
                output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!output) {
                    throw std::runtime_error("Unable to write temporary save file: " + tempPath.string());
                }
            }
            output.flush();
            if (output.fail() || output.bad()) {
                throw std::runtime_error("Unable to write temporary save file: " + tempPath.string());
            }
        }

        std::error_code ec;
        std::filesystem::rename(tempPath, path, ec);
        if (ec) {
            // Attempt to remove the orphaned temp file, ignore any error
            std::error_code rm_ec;
            std::filesystem::remove(tempPath, rm_ec);
            throw std::runtime_error("Unable to replace save file: " + path.string());
        }
    }

    [[nodiscard]] static std::vector<uint8_t> encodeRtcSidecar(const RtcSaveData& rtc)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(12);
        bytes.push_back('P');
        bytes.push_back('T');
        bytes.push_back('R');
        bytes.push_back('T');
        bytes.push_back('C');
        bytes.push_back(1);
        for (const auto value : rtc.registers) {
            bytes.push_back(value);
        }
        bytes.push_back(rtc.latched ? 1u : 0u);
        return bytes;
    }

    [[nodiscard]] static std::optional<RtcSaveData> readRtcSidecar(const std::filesystem::path& path)
    {
        std::vector<uint8_t> bytes;
        try {
            bytes = readFile(path);
        } catch (const std::exception&) {
            return std::nullopt;
        }

        static constexpr std::array<uint8_t, 6> kHeader{{'P', 'T', 'R', 'T', 'C', 1}};
        if (bytes.size() < kHeader.size() + 6u) {
            return std::nullopt;
        }
        if (!std::equal(kHeader.begin(), kHeader.end(), bytes.begin())) {
            return std::nullopt;
        }

        RtcSaveData rtc{};
        const auto offset = kHeader.size();
        for (std::size_t i = 0; i < rtc.registers.size(); ++i) {
            rtc.registers[i] = bytes[offset + i];
        }
        rtc.latched = bytes[offset + rtc.registers.size()] != 0u;
        return rtc;
    }

    bool bound_ = false;
    std::filesystem::path savePath_;
    std::filesystem::path rtcPath_;
};

} // namespace GB

#endif // GB_CARTRIDGE_SAVE_MANAGER_HPP
