#pragma once

#include "GameGearCartridge.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

class GameGearSaveManager {
public:
    void bindRomPath(const std::filesystem::path& romPath)
    {
        savePath_ = romPath;
        savePath_.replace_extension(".sav");
        bound_ = true;
    }

    void clearBinding() noexcept
    {
        bound_ = false;
        savePath_.clear();
    }

    [[nodiscard]] bool bound() const noexcept
    {
        return bound_;
    }

    [[nodiscard]] const std::filesystem::path& savePath() const noexcept
    {
        return savePath_;
    }

    void load(GameGearCartridge& cartridge) const
    {
        if (!bound_ || !cartridge.supportsSaveData()) {
            cartridge.markSaveClean();
            return;
        }

        if (std::filesystem::exists(savePath_)) {
            cartridge.importSaveData(readFile(savePath_));
        } else {
            cartridge.markSaveClean();
        }
    }

    [[nodiscard]] bool flush(GameGearCartridge& cartridge) const
    {
        if (!bound_ || !cartridge.supportsSaveData() || !cartridge.hasDirtySaveData()) {
            return false;
        }

        writeFileAtomic(savePath_, cartridge.exportSaveData());
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
            std::error_code rmEc;
            std::filesystem::remove(tempPath, rmEc);
            throw std::runtime_error("Unable to replace save file: " + path.string());
        }
    }

    bool bound_ = false;
    std::filesystem::path savePath_;
};
