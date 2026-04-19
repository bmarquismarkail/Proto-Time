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

namespace BMMQ {
namespace {

struct JsonValue {
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    std::variant<std::nullptr_t, bool, double, std::string, Object, Array> value;

    [[nodiscard]] const Object* object() const noexcept
    {
        return std::get_if<Object>(&value);
    }

    [[nodiscard]] const Array* array() const noexcept
    {
        return std::get_if<Array>(&value);
    }

    [[nodiscard]] const std::string* string() const noexcept
    {
        return std::get_if<std::string>(&value);
    }

    [[nodiscard]] const double* number() const noexcept
    {
        return std::get_if<double>(&value);
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string text)
        : text_(std::move(text))
    {
    }

    [[nodiscard]] std::optional<JsonValue> parse()
    {
        skipWhitespace();
        auto value = parseValue();
        skipWhitespace();
        if (!value.has_value() || position_ != text_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    [[nodiscard]] std::optional<JsonValue> parseValue()
    {
        skipWhitespace();
        if (position_ >= text_.size()) {
            return std::nullopt;
        }
        const char c = text_[position_];
        if (c == '{') {
            return parseObject();
        }
        if (c == '[') {
            return parseArray();
        }
        if (c == '"') {
            auto value = parseString();
            if (!value.has_value()) {
                return std::nullopt;
            }
            return JsonValue{*value};
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
            return parseNumber();
        }
        if (consumeLiteral("true")) {
            return JsonValue{true};
        }
        if (consumeLiteral("false")) {
            return JsonValue{false};
        }
        if (consumeLiteral("null")) {
            return JsonValue{nullptr};
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<JsonValue> parseObject()
    {
        if (!consume('{')) {
            return std::nullopt;
        }
        JsonValue::Object object;
        skipWhitespace();
        if (consume('}')) {
            return JsonValue{object};
        }
        while (true) {
            auto key = parseString();
            if (!key.has_value()) {
                return std::nullopt;
            }
            skipWhitespace();
            if (!consume(':')) {
                return std::nullopt;
            }
            auto value = parseValue();
            if (!value.has_value()) {
                return std::nullopt;
            }
            object.emplace(std::move(*key), std::move(*value));
            skipWhitespace();
            if (consume('}')) {
                return JsonValue{object};
            }
            if (!consume(',')) {
                return std::nullopt;
            }
            skipWhitespace();
        }
    }

    [[nodiscard]] std::optional<JsonValue> parseArray()
    {
        if (!consume('[')) {
            return std::nullopt;
        }
        JsonValue::Array array;
        skipWhitespace();
        if (consume(']')) {
            return JsonValue{array};
        }
        while (true) {
            auto value = parseValue();
            if (!value.has_value()) {
                return std::nullopt;
            }
            array.push_back(std::move(*value));
            skipWhitespace();
            if (consume(']')) {
                return JsonValue{array};
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] std::optional<std::string> parseString()
    {
        if (!consume('"')) {
            return std::nullopt;
        }
        std::string value;
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '"') {
                return value;
            }
            if (c != '\\') {
                value.push_back(c);
                continue;
            }
            if (position_ >= text_.size()) {
                return std::nullopt;
            }
            const char escaped = text_[position_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escaped);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<JsonValue> parseNumber()
    {
        const auto start = position_;
        if (position_ < text_.size() && text_[position_] == '-') {
            ++position_;
        }
        while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
            ++position_;
        }
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
                ++position_;
            }
        }
        try {
            return JsonValue{std::stod(text_.substr(start, position_ - start))};
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    void skipWhitespace() noexcept
    {
        while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_])) != 0) {
            ++position_;
        }
    }

    [[nodiscard]] bool consume(char expected) noexcept
    {
        skipWhitespace();
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool consumeLiteral(std::string_view literal) noexcept
    {
        if (text_.compare(position_, literal.size(), literal) == 0) {
            position_ += literal.size();
            return true;
        }
        return false;
    }

    std::string text_;
    std::size_t position_ = 0;
};

[[nodiscard]] const JsonValue* findMember(const JsonValue::Object& object, const std::string& key)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return nullptr;
    }
    return &found->second;
}

[[nodiscard]] std::optional<std::string> jsonString(const JsonValue::Object& object, const std::string& key)
{
    const auto* value = findMember(object, key);
    if (value == nullptr || value->string() == nullptr) {
        return std::nullopt;
    }
    return *value->string();
}

[[nodiscard]] std::optional<uint32_t> jsonUInt32(const JsonValue::Object& object, const std::string& key)
{
    const auto* value = findMember(object, key);
    if (value == nullptr || value->number() == nullptr || *value->number() < 0.0) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(*value->number());
}

[[nodiscard]] std::optional<uint64_t> parseVisualHashString(const std::string& value)
{
    uint64_t parsed = 0;
    std::stringstream input{value};
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
        input >> std::hex >> parsed;
    } else {
        input >> parsed;
    }
    if (input.fail()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] std::string jsonEscaped(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (byte < 0x20u) {
                constexpr char kHex[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped.push_back(kHex[(byte >> 4u) & 0x0Fu]);
                escaped.push_back(kHex[byte & 0x0Fu]);
            } else {
                escaped.push_back(c);
            }
            break;
        }
    }
    return escaped;
}

[[nodiscard]] uint32_t readBigEndian32(std::span<const uint8_t> bytes, std::size_t offset) noexcept
{
    return (static_cast<uint32_t>(bytes[offset]) << 24u) |
           (static_cast<uint32_t>(bytes[offset + 1u]) << 16u) |
           (static_cast<uint32_t>(bytes[offset + 2u]) << 8u) |
           static_cast<uint32_t>(bytes[offset + 3u]);
}

void appendBigEndian32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
}

void appendPngChunk(std::vector<uint8_t>& png, const std::array<char, 4>& type, const std::vector<uint8_t>& data)
{
    appendBigEndian32(png, static_cast<uint32_t>(data.size()));
    const auto typeOffset = png.size();
    for (const auto c : type) {
        png.push_back(static_cast<uint8_t>(c));
    }
    png.insert(png.end(), data.begin(), data.end());
    const auto crc = crc32(0u, png.data() + typeOffset, static_cast<uInt>(type.size() + data.size()));
    appendBigEndian32(png, static_cast<uint32_t>(crc));
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
    std::ifstream input(manifestPath);
    if (!input) {
        lastError_ = "unable to open visual pack manifest";
        return false;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();

    auto rootValue = JsonParser(buffer.str()).parse();
    if (!rootValue.has_value() || rootValue->object() == nullptr) {
        lastError_ = "invalid visual pack JSON";
        return false;
    }

    const auto& root = *rootValue->object();
    VisualPack pack;
    pack.root = manifestPath.parent_path();
    pack.id = jsonString(root, "id").value_or("");
    pack.name = jsonString(root, "name").value_or("");
    if (const auto* targetsValue = findMember(root, "targets");
        targetsValue != nullptr && targetsValue->array() != nullptr && !targetsValue->array()->empty()) {
        const auto& firstTarget = targetsValue->array()->front();
        if (firstTarget.string() != nullptr) {
            pack.target = *firstTarget.string();
        }
    }
    if (pack.target.empty()) {
        pack.target = jsonString(root, "target").value_or("");
    }

    const auto* rulesValue = findMember(root, "rules");
    std::size_t invalidRulesSkipped = 0;
    std::size_t missingReplacementImages = 0;
    if (rulesValue != nullptr && rulesValue->array() != nullptr) {
        std::size_t order = 0;
        for (const auto& ruleValue : *rulesValue->array()) {
            if (ruleValue.object() == nullptr) {
                ++invalidRulesSkipped;
                continue;
            }
            const auto* matchValue = findMember(*ruleValue.object(), "match");
            const auto* replaceValue = findMember(*ruleValue.object(), "replace");
            if (matchValue == nullptr || replaceValue == nullptr ||
                matchValue->object() == nullptr || replaceValue->object() == nullptr) {
                ++invalidRulesSkipped;
                continue;
            }

            Rule rule;
            rule.order = order++;
            rule.kind = visualResourceKindFromString(jsonString(*matchValue->object(), "kind").value_or(""));
            if (const auto hash = jsonString(*matchValue->object(), "decodedHash"); hash.has_value()) {
                rule.decodedHash = parseVisualHashString(*hash).value_or(0u);
            }
            if (const auto hash = jsonString(*matchValue->object(), "paletteHash"); hash.has_value()) {
                rule.paletteHash = parseVisualHashString(*hash).value_or(0u);
            }
            if (const auto hash = jsonString(*matchValue->object(), "paletteAwareHash"); hash.has_value()) {
                rule.paletteAwareHash = parseVisualHashString(*hash).value_or(0u);
            }
            rule.width = jsonUInt32(*matchValue->object(), "width").value_or(0u);
            rule.height = jsonUInt32(*matchValue->object(), "height").value_or(0u);
            rule.image = jsonString(*replaceValue->object(), "image").value_or("");
            if (rule.kind == VisualResourceKind::Unknown ||
                (rule.decodedHash == 0u && rule.paletteAwareHash == 0u) ||
                rule.image.empty()) {
                ++invalidRulesSkipped;
                continue;
            }
            std::error_code existsEc;
            if (!std::filesystem::exists(pack.root / rule.image, existsEc)) {
                ++missingReplacementImages;
            }
            rule.specificity = 1u +
                (rule.decodedHash != 0u ? 2u : 0u) +
                (rule.paletteHash != 0u ? 3u : 0u) +
                (rule.paletteAwareHash != 0u ? 4u : 0u) +
                (rule.width != 0u ? 1u : 0u) +
                (rule.height != 0u ? 1u : 0u);
            pack.rules.push_back(std::move(rule));
        }
    }

    if (pack.id.empty() || pack.target.empty()) {
        lastError_ = "visual pack manifest missing id or target";
        return false;
    }
    packs_.push_back(std::move(pack));
    diagnostics_.invalidRulesSkipped += invalidRulesSkipped;
    diagnostics_.missingReplacementImages += missingReplacementImages;
    diagnostics_.rulesLoaded = 0;
    for (const auto& loadedPack : packs_) {
        diagnostics_.rulesLoaded += loadedPack.rules.size();
    }
    ++generation_;
    resolvedCache_.clear();
    imageCache_.clear();
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

    const Rule* bestRule = nullptr;
    const VisualPack* bestPack = nullptr;
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
    if (!writeDecodedResourcePng(resourceDir / fileName, resource)) {
        return false;
    }
    captureEntries_.push_back(CaptureEntry{resource.descriptor, relativePath});
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

bool VisualOverrideService::matches(const Rule& rule, const VisualResourceDescriptor& descriptor) noexcept
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
    image.argbPixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); ++i) {
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
    return image;
}

bool VisualOverrideService::writeCaptureManifest() const
{
    const auto writeRules = [this](const std::filesystem::path& path, bool includeMetadata) {
        std::ofstream out(path);
        if (!out) {
            return false;
        }
        out << "{\n";
        out << "  \"schemaVersion\": 1,\n";
        out << "  \"id\": \"capture." << jsonEscaped(captureMachineId_) << "\",\n";
        out << "  \"name\": \"Captured " << jsonEscaped(captureMachineId_) << " visual resources\",\n";
        out << "  \"targets\": [\"" << jsonEscaped(captureMachineId_) << "\"],\n";
        out << "  \"rules\": [\n";
        for (std::size_t i = 0; i < captureEntries_.size(); ++i) {
            const auto& entry = captureEntries_[i];
            out << "    {\n";
            out << "      \"match\": {\n";
            out << "        \"kind\": \"" << visualResourceKindName(entry.descriptor.kind) << "\",\n";
            out << "        \"decodedHash\": \"" << toHexVisualHash(entry.descriptor.contentHash) << "\",\n";
            out << "        \"paletteHash\": \"" << toHexVisualHash(entry.descriptor.paletteHash) << "\",\n";
            out << "        \"paletteAwareHash\": \"" << toHexVisualHash(entry.descriptor.paletteAwareHash) << "\",\n";
            out << "        \"width\": " << entry.descriptor.width << ",\n";
            out << "        \"height\": " << entry.descriptor.height << "\n";
            out << "      },\n";
            if (includeMetadata) {
                out << "      \"metadata\": {\n";
                out << "        \"resourceKind\": \"" << visualResourceKindName(entry.descriptor.kind) << "\",\n";
                out << "        \"sourceAddress\": \"0x" << std::hex << entry.descriptor.source.address << std::dec << "\",\n";
                out << "        \"tileIndex\": " << entry.descriptor.source.index << ",\n";
                out << "        \"paletteRegister\": \"" << jsonEscaped(entry.descriptor.source.paletteRegister) << "\",\n";
                out << "        \"paletteValue\": \"0x" << std::hex << entry.descriptor.source.paletteValue << std::dec << "\"\n";
                out << "      },\n";
            }
            out << "      \"replace\": {\n";
            out << "        \"image\": \"" << jsonEscaped(entry.imagePath) << "\"\n";
            out << "      }\n";
            out << "    }" << (i + 1u < captureEntries_.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return true;
    };

    const auto writeMetadata = [this](const std::filesystem::path& path) {
        std::ofstream out(path);
        if (!out) {
            return false;
        }
        out << "{\n";
        out << "  \"schemaVersion\": 1,\n";
        out << "  \"machine\": \"" << jsonEscaped(captureMachineId_) << "\",\n";
        out << "  \"entries\": [\n";
        for (std::size_t i = 0; i < captureEntries_.size(); ++i) {
            const auto& entry = captureEntries_[i];
            out << "    {\n";
            out << "      \"image\": \"" << jsonEscaped(entry.imagePath) << "\",\n";
            out << "      \"resourceKind\": \"" << visualResourceKindName(entry.descriptor.kind) << "\",\n";
            out << "      \"decodedHash\": \"" << toHexVisualHash(entry.descriptor.contentHash) << "\",\n";
            out << "      \"paletteHash\": \"" << toHexVisualHash(entry.descriptor.paletteHash) << "\",\n";
            out << "      \"paletteAwareHash\": \"" << toHexVisualHash(entry.descriptor.paletteAwareHash) << "\",\n";
            out << "      \"sourceAddress\": \"0x" << std::hex << entry.descriptor.source.address << std::dec << "\",\n";
            out << "      \"tileIndex\": " << entry.descriptor.source.index << ",\n";
            out << "      \"paletteRegister\": \"" << jsonEscaped(entry.descriptor.source.paletteRegister) << "\",\n";
            out << "      \"paletteValue\": \"0x" << std::hex << entry.descriptor.source.paletteValue << std::dec << "\"\n";
            out << "    }" << (i + 1u < captureEntries_.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return true;
    };

    if (!writeRules(captureDirectory_ / "pack.json", false) ||
        !writeRules(captureDirectory_ / "manifest.stub.json", true) ||
        !writeMetadata(captureDirectory_ / "capture_metadata.json")) {
        lastError_ = "unable to write visual capture manifest";
        return false;
    }
    captureManifestDirty_ = false;
    return true;
}

bool VisualOverrideService::writeDecodedResourcePng(const std::filesystem::path& path,
                                                    const DecodedVisualResource& resource)
{
    constexpr std::array<uint8_t, 4> kIndexedArgbAlpha{
        0xFFu, 0xFFu, 0xFFu, 0xFFu,
    };
    constexpr std::array<uint8_t, 4> kIndexedArgbRed{
        0xE0u, 0x88u, 0x34u, 0x08u,
    };
    constexpr std::array<uint8_t, 4> kIndexedArgbGreen{
        0xF8u, 0xC0u, 0x68u, 0x18u,
    };
    constexpr std::array<uint8_t, 4> kIndexedArgbBlue{
        0xD0u, 0x70u, 0x56u, 0x20u,
    };

    const auto width = resource.descriptor.width;
    const auto height = resource.descriptor.height;
    if (width == 0u || height == 0u || resource.stride == 0u) {
        return false;
    }

    std::vector<uint8_t> raw;
    raw.reserve((static_cast<std::size_t>(width) * 4u + 1u) * static_cast<std::size_t>(height));
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0u);
        for (uint32_t x = 0; x < width; ++x) {
            const auto sourceIndex = static_cast<std::size_t>(y) * resource.stride + x;
            const auto color = sourceIndex < resource.pixels.size() ? resource.pixels[sourceIndex] & 0x03u : 0u;
            raw.push_back(kIndexedArgbRed[color]);
            raw.push_back(kIndexedArgbGreen[color]);
            raw.push_back(kIndexedArgbBlue[color]);
            raw.push_back(kIndexedArgbAlpha[color]);
        }
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK) {
        lastError_ = "unable to compress visual capture PNG";
        return false;
    }
    compressed.resize(compressedSize);

    std::vector<uint8_t> png{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    appendBigEndian32(ihdr, width);
    appendBigEndian32(ihdr, height);
    ihdr.push_back(8u);
    ihdr.push_back(6u);
    ihdr.push_back(0u);
    ihdr.push_back(0u);
    ihdr.push_back(0u);
    appendPngChunk(png, {'I', 'H', 'D', 'R'}, ihdr);
    appendPngChunk(png, {'I', 'D', 'A', 'T'}, compressed);
    appendPngChunk(png, {'I', 'E', 'N', 'D'}, {});

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        lastError_ = "unable to write visual capture image";
        return false;
    }
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return true;
}

} // namespace BMMQ
