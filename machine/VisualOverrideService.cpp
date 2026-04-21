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

[[nodiscard]] bool targetsMachine(const VisualPackManifest& pack, const std::string& machineId) noexcept
{
    if (pack.targets.empty()) {
        return pack.target.empty() || pack.target == machineId;
    }
    return std::find(pack.targets.begin(), pack.targets.end(), machineId) != pack.targets.end();
}

[[nodiscard]] std::string describeObservedResource(const VisualObservedResourceStat& resource)
{
    std::ostringstream out;
    out << "observations=" << resource.observations
        << " kind=" << visualResourceKindName(resource.descriptor.kind)
        << " size=" << resource.descriptor.width << "x" << resource.descriptor.height
        << " decodedHash=" << toHexVisualHash(resource.descriptor.contentHash);
    if (resource.descriptor.sourceHash != 0u) {
        out << " sourceHash=" << toHexVisualHash(resource.descriptor.sourceHash);
    }
    if (!resource.descriptor.source.label.empty()) {
        out << " label=" << resource.descriptor.source.label;
    }
    if (resource.descriptor.source.address != 0u) {
        out << " address=0x" << std::hex << std::nouppercase << resource.descriptor.source.address << std::dec;
    }
    if (!resource.descriptor.source.paletteRegister.empty()) {
        out << " paletteRegister=" << resource.descriptor.source.paletteRegister;
    }
    if (!resource.imagePath.empty()) {
        out << " image=" << resource.imagePath;
    }
    return out.str();
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
    packs_.push_back(makeLoadedPack(manifestPath, std::move(*loadResult.manifest)));
    diagnostics_.invalidRulesSkipped += loadResult.invalidRulesSkipped;
    diagnostics_.missingReplacementImages += loadResult.missingReplacementImages;
    diagnostics_.rulesLoaded = countLoadedRules(packs_);
    ++generation_;
    clearResolutionCaches();
    lastError_.clear();
    return true;
}

bool VisualOverrideService::reloadChangedPacks()
{
    ++diagnostics_.packReloadChecks;
    if (packs_.empty()) {
        ++diagnostics_.packReloadsSkipped;
        return false;
    }

    std::vector<LoadedPack> reloadedPacks;
    std::size_t localInvalidRulesSkipped = 0;
    std::size_t localMissingReplacementImages = 0;
    bool changed = false;
    bool failed = false;
    for (const auto& loadedPack : packs_) {
        const auto manifestTime = fileWriteTime(loadedPack.manifestPath);
        const bool manifestChanged = manifestTime != loadedPack.manifestWriteTime;
        const bool assetChanged = watchedAssetChanged(loadedPack);
        if (!manifestChanged && !assetChanged) {
            reloadedPacks.push_back(loadedPack);
            continue;
        }

        auto loadResult = loadVisualPackManifest(loadedPack.manifestPath);
        if (!loadResult.manifest.has_value()) {
            ++diagnostics_.packReloadsFailed;
            lastError_ = "visual pack reload failed: " + loadedPack.manifestPath.string() + ": " + loadResult.error;
            failed = true;
            reloadedPacks.push_back(loadedPack);
            continue;
        }

        localInvalidRulesSkipped += loadResult.invalidRulesSkipped;
        localMissingReplacementImages += loadResult.missingReplacementImages;
        reloadedPacks.push_back(makeLoadedPack(loadedPack.manifestPath, std::move(*loadResult.manifest)));
        changed = true;
    }

    if (failed) {
        return false;
    }
    if (!changed) {
        ++diagnostics_.packReloadsSkipped;
        return false;
    }

    packs_ = std::move(reloadedPacks);
    diagnostics_.invalidRulesSkipped += localInvalidRulesSkipped;
    diagnostics_.missingReplacementImages += localMissingReplacementImages;
    diagnostics_.rulesLoaded = countLoadedRules(packs_);
    ++diagnostics_.packReloadsSucceeded;
    ++generation_;
    clearResolutionCaches();
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
        auto resolved = loadResolved(cached->second);
        if (resolved.has_value()) {
            emitVisualEvent(MachineEventType::VisualOverrideResolved, 0u, "visual-override-resolved");
        }
        return resolved;
    }

    const VisualOverrideRule* bestRule = nullptr;
    const LoadedPack* bestPack = nullptr;
    for (const auto& loadedPack : packs_) {
        const auto& pack = loadedPack.manifest;
        if (!targetsMachine(pack, descriptor.machineId)) {
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
                (rule.specificity == bestRule->specificity && pack.priority > bestPack->manifest.priority) ||
                (rule.specificity == bestRule->specificity && pack.priority == bestPack->manifest.priority &&
                 rule.order < bestRule->order)) {
                bestRule = &rule;
                bestPack = &loadedPack;
            }
        }
    }

    if (bestRule == nullptr || bestPack == nullptr) {
        ++diagnostics_.resolveMisses;
        emitVisualEvent(MachineEventType::VisualPackMiss, 0u, "visual-pack-miss");
        return std::nullopt;
    }
    ResolvedPath resolvedPath{bestPack->manifest.id,
                              bestPack->manifest.root / bestRule->image,
                              bestRule->scalePolicy,
                              bestRule->filterPolicy,
                              bestRule->anchor};
    resolvedCache_.emplace(key, resolvedPath);
    auto resolved = loadResolved(resolvedPath);
    if (resolved.has_value()) {
        ++diagnostics_.resolveHits;
        emitVisualEvent(MachineEventType::VisualOverrideResolved, 0u, "visual-override-resolved");
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
    return writeCaptureManifest() && writeAuthorReport();
}

void VisualOverrideService::endCapture() noexcept
{
    if (captureManifestDirty_) {
        (void)writeCaptureManifest();
        (void)writeAuthorReport();
    }
    captureEnabled_ = false;
}

bool VisualOverrideService::observe(const DecodedVisualResource& resource)
{
    if (enabled_ && resource.descriptor.contentHash != 0u) {
        recordObservation(resource.descriptor);
        emitVisualEvent(MachineEventType::VisualResourceObserved, 0u, "visual-resource-observed");
    }
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
    if (const auto it = observedResourceIndices_.find(key); it != observedResourceIndices_.end()) {
        observedResources_[it->second].imagePath = relativePath;
    }
    captureManifestDirty_ = true;
    ++captureStats_.uniqueResourcesDumped;
    return true;
}

void VisualOverrideService::notifyResourceDecoded(const VisualResourceDescriptor& descriptor) const
{
    if (enabled_ && descriptor.contentHash != 0u) {
        emitVisualEvent(MachineEventType::VisualResourceDecoded, 0u, "visual-resource-decoded");
    }
}

void VisualOverrideService::notifyFrameCompositionStarted(uint64_t generation) const
{
    emitVisualEvent(MachineEventType::FrameCompositionStarted, generation, "frame-composition-started");
}

void VisualOverrideService::notifyFrameCompositionCompleted(uint64_t generation) const
{
    emitVisualEvent(MachineEventType::FrameCompositionCompleted, generation, "frame-composition-completed");
}

void VisualOverrideService::setEventSink(EventSink sink)
{
    eventSink_ = std::move(sink);
}

void VisualOverrideService::clearEventSink() noexcept
{
    eventSink_ = nullptr;
}

const VisualCaptureStats& VisualOverrideService::captureStats() const noexcept
{
    if (captureManifestDirty_) {
        (void)writeCaptureManifest();
        (void)writeAuthorReport();
    }
    return captureStats_;
}

const VisualOverrideDiagnostics& VisualOverrideService::diagnostics() const noexcept
{
    return diagnostics_;
}

std::string VisualOverrideService::authorDiagnosticsReport(std::size_t maxObservedResources) const
{
    std::ostringstream out;
    out << "Visual override summary\n"
        << "rules loaded: " << diagnostics_.rulesLoaded << '\n'
        << "invalid rules skipped: " << diagnostics_.invalidRulesSkipped << '\n'
        << "missing replacement images: " << diagnostics_.missingReplacementImages << '\n'
        << "resolve hits: " << diagnostics_.resolveHits << '\n'
        << "resolve misses: " << diagnostics_.resolveMisses << '\n'
        << "replacement load failures: " << diagnostics_.replacementLoadFailures << '\n'
        << "ambiguous matches: " << diagnostics_.ambiguousMatches << '\n'
        << "reload checks: " << diagnostics_.packReloadChecks << '\n'
        << "reloads succeeded: " << diagnostics_.packReloadsSucceeded << '\n'
        << "reload failures: " << diagnostics_.packReloadsFailed << '\n'
        << '\n'
        << "Visual capture summary\n"
        << "unique resources dumped: " << captureStats_.uniqueResourcesDumped << '\n'
        << "duplicate resources skipped: " << captureStats_.duplicateResourcesSkipped << '\n';

    std::vector<const VisualObservedResourceStat*> sorted;
    sorted.reserve(observedResources_.size());
    for (const auto& resource : observedResources_) {
        sorted.push_back(&resource);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto* lhs, const auto* rhs) {
        if (lhs->observations != rhs->observations) {
            return lhs->observations > rhs->observations;
        }
        if (lhs->descriptor.kind != rhs->descriptor.kind) {
            return lhs->descriptor.kind < rhs->descriptor.kind;
        }
        return lhs->descriptor.contentHash < rhs->descriptor.contentHash;
    });

    out << '\n' << "Top observed resources\n";
    const auto count = std::min(maxObservedResources, sorted.size());
    if (count == 0u) {
        out << "- none\n";
        return out.str();
    }
    for (std::size_t i = 0; i < count; ++i) {
        out << "- " << describeObservedResource(*sorted[i]) << '\n';
    }
    return out.str();
}

std::string VisualOverrideService::lastError() const
{
    return lastError_;
}

uint64_t VisualOverrideService::generation() const noexcept
{
    return generation_;
}

std::filesystem::file_time_type VisualOverrideService::fileWriteTime(const std::filesystem::path& path) noexcept
{
    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::filesystem::file_time_type::min();
    }
    return writeTime;
}

std::vector<VisualOverrideService::WatchedPathStamp> VisualOverrideService::collectAssetStamps(
    const VisualPackManifest& manifest)
{
    std::vector<WatchedPathStamp> stamps;
    std::set<std::string> seen;
    for (const auto& rule : manifest.rules) {
        if (rule.image.empty()) {
            continue;
        }
        const auto assetPath = (manifest.root / rule.image).lexically_normal();
        const auto key = assetPath.string();
        if (!seen.insert(key).second) {
            continue;
        }
        stamps.push_back(WatchedPathStamp{
            .path = assetPath,
            .writeTime = fileWriteTime(assetPath),
        });
    }
    return stamps;
}

bool VisualOverrideService::watchedAssetChanged(const LoadedPack& pack) noexcept
{
    for (const auto& stamp : pack.assetStamps) {
        if (fileWriteTime(stamp.path) != stamp.writeTime) {
            return true;
        }
    }
    return false;
}

std::size_t VisualOverrideService::countLoadedRules(const std::vector<LoadedPack>& packs) noexcept
{
    std::size_t rulesLoaded = 0;
    for (const auto& loadedPack : packs) {
        rulesLoaded += loadedPack.manifest.rules.size();
    }
    return rulesLoaded;
}

VisualOverrideService::LoadedPack VisualOverrideService::makeLoadedPack(std::filesystem::path manifestPath,
                                                                        VisualPackManifest manifest) const
{
    auto assetStamps = collectAssetStamps(manifest);
    const auto manifestWriteTime = fileWriteTime(manifestPath);
    return LoadedPack{
        .manifest = std::move(manifest),
        .manifestPath = std::move(manifestPath),
        .manifestWriteTime = manifestWriteTime,
        .assetStamps = std::move(assetStamps),
    };
}

void VisualOverrideService::clearResolutionCaches()
{
    resolvedCache_.clear();
    imageCache_.clear();
    imageCacheBytes_ = 0;
}

std::string VisualOverrideService::makeDescriptorKey(const VisualResourceDescriptor& descriptor)
{
    return descriptor.machineId + "|" + visualResourceKindName(descriptor.kind) + "|" +
        std::to_string(descriptor.width) + "x" + std::to_string(descriptor.height) + "|" +
        visualPixelFormatName(descriptor.decodedFormat) + "|" +
        descriptor.source.label + "|" +
        std::to_string(descriptor.source.bank) + "|" +
        std::to_string(descriptor.source.address) + "|" +
        std::to_string(descriptor.source.index) + "|" +
        descriptor.source.paletteRegister + "|" +
        std::to_string(descriptor.source.paletteValue) + "|" +
        toHexVisualHash(descriptor.sourceHash) + "|" +
        toHexVisualHash(descriptor.contentHash) + "|" +
        toHexVisualHash(descriptor.paletteHash) + "|" +
        toHexVisualHash(descriptor.paletteAwareHash);
}

bool VisualOverrideService::matches(const VisualOverrideRule& rule, const VisualResourceDescriptor& descriptor) noexcept
{
    if (rule.kind != descriptor.kind) {
        return false;
    }
    if (!rule.machineId.empty() && rule.machineId != descriptor.machineId) {
        return false;
    }
    if (rule.decodedFormat != VisualPixelFormat::Unknown && rule.decodedFormat != descriptor.decodedFormat) {
        return false;
    }
    if (!rule.semanticLabel.empty() && rule.semanticLabel != descriptor.source.label) {
        return false;
    }
    if (rule.sourceBank.has_value() && *rule.sourceBank != descriptor.source.bank) {
        return false;
    }
    if (rule.sourceAddress.has_value() && *rule.sourceAddress != descriptor.source.address) {
        return false;
    }
    if (rule.sourceIndex.has_value() && *rule.sourceIndex != descriptor.source.index) {
        return false;
    }
    if (!rule.paletteRegister.empty() && rule.paletteRegister != descriptor.source.paletteRegister) {
        return false;
    }
    if (rule.paletteValue.has_value() && *rule.paletteValue != descriptor.source.paletteValue) {
        return false;
    }
    if (rule.sourceHash != 0u && rule.sourceHash != descriptor.sourceHash) {
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
    if (rule.sourceHash == 0u && rule.paletteAwareHash == 0u && rule.decodedHash == 0u) {
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
        .scalePolicy = resolvedPath.scalePolicy,
        .filterPolicy = resolvedPath.filterPolicy,
        .anchor = resolvedPath.anchor,
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

bool VisualOverrideService::writeAuthorReport() const
{
    if (captureDirectory_.empty()) {
        return true;
    }
    std::ofstream out(captureDirectory_ / "author_report.txt");
    if (!out) {
        lastError_ = "unable to write visual author report";
        return false;
    }
    out << authorDiagnosticsReport();
    return true;
}

void VisualOverrideService::emitVisualEvent(MachineEventType type, uint64_t tick, std::string_view detail) const
{
    if (!eventSink_) {
        return;
    }
    eventSink_(MachineEvent{
        .type = type,
        .category = PluginCategory::Video,
        .tick = tick,
        .detail = detail,
    });
}

void VisualOverrideService::recordObservation(const VisualResourceDescriptor& descriptor)
{
    const auto key = makeDescriptorKey(descriptor);
    if (const auto it = observedResourceIndices_.find(key); it != observedResourceIndices_.end()) {
        ++observedResources_[it->second].observations;
        return;
    }
    observedResourceIndices_.emplace(key, observedResources_.size());
    observedResources_.push_back(VisualObservedResourceStat{
        .descriptor = descriptor,
        .observations = 1u,
        .imagePath = {},
    });
}

} // namespace BMMQ
