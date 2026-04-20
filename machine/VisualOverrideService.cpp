#include "VisualOverrideService.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

#include <zlib.h>

#include "VisualPackLimits.hpp"

namespace BMMQ {
namespace {

[[nodiscard]] uint32_t readBigEndian32(std::span<const uint8_t> bytes, std::size_t offset) noexcept
{
    return (static_cast<uint32_t>(bytes[offset]) << 24u) |
           (static_cast<uint32_t>(bytes[offset + 1u]) << 16u) |
           (static_cast<uint32_t>(bytes[offset + 2u]) << 8u) |
           static_cast<uint32_t>(bytes[offset + 3u]);
}

[[nodiscard]] uint8_t paethPredictor(uint8_t left, uint8_t up, uint8_t upLeft) noexcept
{
    const int p = static_cast<int>(left) + static_cast<int>(up) - static_cast<int>(upLeft);
    const int pa = std::abs(p - static_cast<int>(left));
    const int pb = std::abs(p - static_cast<int>(up));
    const int pc = std::abs(p - static_cast<int>(upLeft));
    if (pa <= pb && pa <= pc) {
        return left;
    }
    if (pb <= pc) {
        return up;
    }
    return upLeft;
}

} // namespace

bool VisualOverrideService::enabled() const noexcept
{
    return enabled_;
}

void VisualOverrideService::setEnabled(bool enabled) noexcept
{
    enabled_ = enabled;
}

bool VisualOverrideService::hasActiveWork() const noexcept
{
    return enabled_ && (captureEnabled_ || !packs_.empty());
}

bool VisualOverrideService::hasLoadedPacks() const noexcept
{
    return !packs_.empty();
}

bool VisualOverrideService::capturing() const noexcept
{
    return captureEnabled_;
}

bool VisualOverrideService::loadPackManifest(const std::filesystem::path& manifestPath)
{
    auto loadResult = loadVisualPackManifest(manifestPath);
    if (!loadResult.manifest.has_value()) {
        lastError_ = std::move(loadResult.error);
        return false;
    }
    packs_.push_back(std::move(*loadResult.manifest));
    diagnostics_.invalidRulesSkipped += loadResult.invalidRulesSkipped;
    diagnostics_.missingReplacementImages += loadResult.missingReplacementImages;
    diagnostics_.rulesLoaded = 0;
    for (const auto& loadedPack : packs_) {
        diagnostics_.rulesLoaded += loadedPack.rules.size();
    }
    ++generation_;
    resolvedCache_.clear();
    imageCache_.clear();
    imageCacheBytes_ = 0;
    lastError_.clear();
    return true;
}

std::optional<ResolvedVisualOverride> VisualOverrideService::resolve(const VisualResourceDescriptor& descriptor)
{
    if (!enabled_ || descriptor.contentHash == 0u) {
        return std::nullopt;
    }

    const auto key = makeDescriptorKey(descriptor);
    if (const auto cached = resolvedCache_.find(key); cached != resolvedCache_.end()) {
        return loadResolved(cached->second);
    }

    const VisualOverrideRule* bestRule = nullptr;
    const VisualPackManifest* bestPack = nullptr;
    for (const auto& pack : packs_) {
        if (!pack.target.empty() && pack.target != descriptor.machineId) {
            continue;
        }
        for (const auto& rule : pack.rules) {
            if (!matches(rule, descriptor)) {
                continue;
            }
            if (bestRule != nullptr && rule.specificity == bestRule->specificity && rule.order != bestRule->order) {
                ++diagnostics_.ambiguousMatches;
            }
            if (bestRule == nullptr ||
                rule.specificity > bestRule->specificity ||
                (rule.specificity == bestRule->specificity && rule.order < bestRule->order)) {
                bestRule = &rule;
                bestPack = &pack;
            }
        }
    }

    if (bestRule == nullptr || bestPack == nullptr) {
        ++diagnostics_.resolveMisses;
        return std::nullopt;
    }
    ResolvedPath resolvedPath{bestPack->id, bestPack->root / bestRule->image};
    resolvedCache_.emplace(key, resolvedPath);
    auto resolved = loadResolved(resolvedPath);
    if (resolved.has_value()) {
        ++diagnostics_.resolveHits;
    } else {
        ++diagnostics_.replacementLoadFailures;
    }
    return resolved;
}

bool VisualOverrideService::beginCapture(const std::filesystem::path& directory, std::string machineId)
{
    captureDirectory_ = directory;
    captureMachineId_ = std::move(machineId);
    captureEnabled_ = true;
    captureSeen_.clear();
    captureEntries_.clear();
    captureStats_ = {};
    std::error_code ec;
    std::filesystem::create_directories(captureDirectory_, ec);
    if (ec) {
        lastError_ = "unable to create visual capture directory";
        captureEnabled_ = false;
        return false;
    }
    return writeCaptureManifest();
}

void VisualOverrideService::endCapture() noexcept
{
    if (captureManifestDirty_) {
        (void)writeCaptureManifest();
    }
    captureEnabled_ = false;
}

bool VisualOverrideService::observe(const DecodedVisualResource& resource)
{
    if (!captureEnabled_ || resource.descriptor.contentHash == 0u || resource.pixels.empty()) {
        return false;
    }
    const auto key = makeDescriptorKey(resource.descriptor);
    if (captureSeen_.find(key) != captureSeen_.end()) {
        ++captureStats_.duplicateResourcesSkipped;
        return false;
    }
    captureSeen_.insert(key);

    const auto kind = std::string(visualResourceKindName(resource.descriptor.kind));
    const auto fileName = kind + "_" + toHexVisualHash(resource.descriptor.contentHash) + "_" +
        std::to_string(resource.descriptor.width) + "x" + std::to_string(resource.descriptor.height) + ".png";
    const auto resourceDir = captureDirectory_ / kind;
    std::error_code ec;
    std::filesystem::create_directories(resourceDir, ec);
    if (ec) {
        lastError_ = "unable to create visual capture resource directory";
        return false;
    }
    const auto relativePath = kind + "/" + fileName;
    if (!VisualCaptureWriter::writeDecodedResourcePng(resourceDir / fileName, resource, lastError_)) {
        return false;
    }
    captureEntries_.push_back(VisualCaptureEntry{resource.descriptor, relativePath});
    captureManifestDirty_ = true;
    ++captureStats_.uniqueResourcesDumped;
    return true;
}

const VisualCaptureStats& VisualOverrideService::captureStats() const noexcept
{
    if (captureManifestDirty_) {
        (void)writeCaptureManifest();
    }
    return captureStats_;
}

const VisualOverrideDiagnostics& VisualOverrideService::diagnostics() const noexcept
{
    return diagnostics_;
}

std::string VisualOverrideService::lastError() const
{
    return lastError_;
}

uint64_t VisualOverrideService::generation() const noexcept
{
    return generation_;
}

std::string VisualOverrideService::makeDescriptorKey(const VisualResourceDescriptor& descriptor)
{
    return descriptor.machineId + "|" + visualResourceKindName(descriptor.kind) + "|" +
        std::to_string(descriptor.width) + "x" + std::to_string(descriptor.height) + "|" +
        toHexVisualHash(descriptor.contentHash) + "|" +
        toHexVisualHash(descriptor.paletteAwareHash);
}

bool VisualOverrideService::matches(const VisualOverrideRule& rule, const VisualResourceDescriptor& descriptor) noexcept
{
    if (rule.kind != descriptor.kind) {
        return false;
    }
    if (rule.paletteAwareHash != 0u && rule.paletteAwareHash != descriptor.paletteAwareHash) {
        return false;
    }
    if (rule.paletteHash != 0u && rule.paletteHash != descriptor.paletteHash) {
        return false;
    }
    if (rule.decodedHash != 0u && rule.decodedHash != descriptor.contentHash) {
        return false;
    }
    if (rule.paletteAwareHash == 0u && rule.decodedHash == 0u) {
        return false;
    }
    if (rule.width != 0u && rule.width != descriptor.width) {
        return false;
    }
    if (rule.height != 0u && rule.height != descriptor.height) {
        return false;
    }
    return true;
}

std::optional<ResolvedVisualOverride> VisualOverrideService::loadResolved(const ResolvedPath& resolvedPath)
{
    auto image = loadPng(resolvedPath.path);
    if (!image.has_value() || image->empty()) {
        return std::nullopt;
    }
    return ResolvedVisualOverride{
        .packId = resolvedPath.packId,
        .assetPath = resolvedPath.path.string(),
        .image = std::move(*image),
    };
}

std::optional<VisualReplacementImage> VisualOverrideService::loadPng(const std::filesystem::path& path)
{
    const auto key = path.lexically_normal().string();
    if (const auto cached = imageCache_.find(key); cached != imageCache_.end()) {
        return cached->second;
    }

    std::error_code sizeEc;
    const auto fileSize = std::filesystem::file_size(path, sizeEc);
    if (!sizeEc && fileSize > VisualPackLimits::kMaxPngBytes) {
        lastError_ = "replacement PNG too large: bytes=" + std::to_string(fileSize) +
            " max=" + std::to_string(VisualPackLimits::kMaxPngBytes);
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    constexpr std::array<uint8_t, 8> kPngSignature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < kPngSignature.size() ||
        !std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin())) {
        return std::nullopt;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t bitDepth = 0;
    uint8_t colorType = 0;
    std::vector<uint8_t> idat;
    std::size_t offset = kPngSignature.size();
    while (offset + 12u <= bytes.size()) {
        const auto length = readBigEndian32(bytes, offset);
        offset += 4u;
        if (offset + 4u + length + 4u > bytes.size()) {
            return std::nullopt;
        }
        const std::string type(reinterpret_cast<const char*>(bytes.data() + offset), 4u);
        offset += 4u;
        const auto dataOffset = offset;
        offset += length;
        offset += 4u;

        if (type == "IHDR") {
            if (length != 13u) {
                return std::nullopt;
            }
            width = readBigEndian32(bytes, dataOffset);
            height = readBigEndian32(bytes, dataOffset + 4u);
            bitDepth = bytes[dataOffset + 8u];
            colorType = bytes[dataOffset + 9u];
            const auto compression = bytes[dataOffset + 10u];
            const auto filter = bytes[dataOffset + 11u];
            const auto interlace = bytes[dataOffset + 12u];
            if (width > VisualPackLimits::kMaxReplacementImageDimension ||
                height > VisualPackLimits::kMaxReplacementImageDimension) {
                lastError_ = "replacement PNG dimensions too large: width=" + std::to_string(width) +
                    " height=" + std::to_string(height) +
                    " max=" + std::to_string(VisualPackLimits::kMaxReplacementImageDimension);
                return std::nullopt;
            }
            if (width == 0u || height == 0u || bitDepth != 8u ||
                (colorType != 2u && colorType != 6u) ||
                compression != 0u || filter != 0u || interlace != 0u) {
                return std::nullopt;
            }
        } else if (type == "IDAT") {
            idat.insert(idat.end(), bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                        bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + length));
        } else if (type == "IEND") {
            break;
        }
    }

    const std::size_t channels = colorType == 6u ? 4u : 3u;
    const std::size_t stride = static_cast<std::size_t>(width) * channels;
    const std::size_t inflatedSize = (stride + 1u) * static_cast<std::size_t>(height);
    if (inflatedSize > VisualPackLimits::kMaxReplacementInflatedBytes) {
        lastError_ = "replacement PNG inflated data too large: bytes=" + std::to_string(inflatedSize) +
            " max=" + std::to_string(VisualPackLimits::kMaxReplacementInflatedBytes);
        return std::nullopt;
    }
    std::vector<uint8_t> inflated(inflatedSize);
    auto destinationSize = static_cast<uLongf>(inflated.size());
    if (uncompress(inflated.data(), &destinationSize, idat.data(), static_cast<uLong>(idat.size())) != Z_OK ||
        destinationSize != inflated.size()) {
        return std::nullopt;
    }

    std::vector<uint8_t> decoded(stride * static_cast<std::size_t>(height));
    for (std::size_t y = 0; y < height; ++y) {
        const auto filter = inflated[y * (stride + 1u)];
        const auto* source = inflated.data() + y * (stride + 1u) + 1u;
        auto* output = decoded.data() + y * stride;
        const auto* previous = y == 0u ? nullptr : decoded.data() + (y - 1u) * stride;
        for (std::size_t x = 0; x < stride; ++x) {
            const auto left = x >= channels ? output[x - channels] : 0u;
            const auto up = previous != nullptr ? previous[x] : 0u;
            const auto upLeft = previous != nullptr && x >= channels ? previous[x - channels] : 0u;
            uint8_t predictor = 0;
            switch (filter) {
            case 0:
                predictor = 0;
                break;
            case 1:
                predictor = left;
                break;
            case 2:
                predictor = up;
                break;
            case 3:
                predictor = static_cast<uint8_t>((static_cast<uint16_t>(left) + static_cast<uint16_t>(up)) / 2u);
                break;
            case 4:
                predictor = paethPredictor(left, up, upLeft);
                break;
            default:
                return std::nullopt;
            }
            output[x] = static_cast<uint8_t>(source[x] + predictor);
        }
    }

    VisualReplacementImage image;
    image.width = width;
    image.height = height;
    const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto imageBytes = pixelCount * sizeof(uint32_t);
    if (imageBytes > VisualPackLimits::kMaxReplacementCacheBytes ||
        imageCacheBytes_ > VisualPackLimits::kMaxReplacementCacheBytes - imageBytes) {
        lastError_ = "replacement image cache budget exceeded: requested=" + std::to_string(imageBytes) +
            " current=" + std::to_string(imageCacheBytes_) +
            " max=" + std::to_string(VisualPackLimits::kMaxReplacementCacheBytes);
        return std::nullopt;
    }
    image.argbPixels.reserve(pixelCount);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const auto base = i * channels;
        const auto r = decoded[base];
        const auto g = decoded[base + 1u];
        const auto b = decoded[base + 2u];
        const auto a = channels == 4u ? decoded[base + 3u] : 0xFFu;
        image.argbPixels.push_back((static_cast<uint32_t>(a) << 24u) |
                                   (static_cast<uint32_t>(r) << 16u) |
                                   (static_cast<uint32_t>(g) << 8u) |
                                   static_cast<uint32_t>(b));
    }
    imageCache_.emplace(key, image);
    imageCacheBytes_ += imageBytes;
    return image;
}

bool VisualOverrideService::writeCaptureManifest() const
{
    if (!VisualCaptureWriter::writeManifests(captureDirectory_, captureMachineId_, captureEntries_, lastError_)) {
        return false;
    }
    captureManifestDirty_ = false;
    return true;
}

} // namespace BMMQ
