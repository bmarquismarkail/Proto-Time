#include "VisualPackManifest.hpp"

#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "VisualPackLimits.hpp"

namespace BMMQ {
namespace {

constexpr uint32_t kSupportedVisualPackSchemaVersion = 1u;

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
            case 'u': {
                auto codePoint = parseUnicodeEscape();
                if (!codePoint.has_value() || !appendUtf8(value, *codePoint)) {
                    return std::nullopt;
                }
                break;
            }
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

    [[nodiscard]] static std::optional<uint32_t> hexValue(char value) noexcept
    {
        if (value >= '0' && value <= '9') {
            return static_cast<uint32_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<uint32_t>(value - 'a' + 10);
        }
        if (value >= 'A' && value <= 'F') {
            return static_cast<uint32_t>(value - 'A' + 10);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<uint32_t> parseUnicodeHexQuad()
    {
        if (position_ + 4u > text_.size()) {
            return std::nullopt;
        }
        uint32_t codeUnit = 0;
        for (std::size_t i = 0; i < 4u; ++i) {
            const auto digit = hexValue(text_[position_ + i]);
            if (!digit.has_value()) {
                return std::nullopt;
            }
            codeUnit = static_cast<uint32_t>((codeUnit << 4u) | *digit);
        }
        position_ += 4u;
        return codeUnit;
    }

    [[nodiscard]] std::optional<uint32_t> parseUnicodeEscape()
    {
        auto first = parseUnicodeHexQuad();
        if (!first.has_value()) {
            return std::nullopt;
        }

        if (*first >= 0xD800u && *first <= 0xDBFFu) {
            if (position_ + 6u > text_.size() || text_[position_] != '\\' || text_[position_ + 1u] != 'u') {
                return std::nullopt;
            }
            position_ += 2u;
            auto second = parseUnicodeHexQuad();
            if (!second.has_value() || *second < 0xDC00u || *second > 0xDFFFu) {
                return std::nullopt;
            }
            return 0x10000u + (((*first - 0xD800u) << 10u) | (*second - 0xDC00u));
        }

        if (*first >= 0xDC00u && *first <= 0xDFFFu) {
            return std::nullopt;
        }
        return first;
    }

    [[nodiscard]] static bool appendUtf8(std::string& output, uint32_t codePoint)
    {
        if (codePoint <= 0x7Fu) {
            output.push_back(static_cast<char>(codePoint));
            return true;
        }
        if (codePoint <= 0x7FFu) {
            output.push_back(static_cast<char>(0xC0u | ((codePoint >> 6u) & 0x1Fu)));
            output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
            return true;
        }
        if (codePoint <= 0xFFFFu) {
            output.push_back(static_cast<char>(0xE0u | ((codePoint >> 12u) & 0x0Fu)));
            output.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
            return true;
        }
        if (codePoint <= 0x10FFFFu) {
            output.push_back(static_cast<char>(0xF0u | ((codePoint >> 18u) & 0x07u)));
            output.push_back(static_cast<char>(0x80u | ((codePoint >> 12u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
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

[[nodiscard]] std::optional<uint64_t> parseVisualHashString(const std::string& value);

[[nodiscard]] std::optional<uint32_t> jsonUInt32(const JsonValue::Object& object, const std::string& key)
{
    const auto* value = findMember(object, key);
    if (value == nullptr || value->number() == nullptr || *value->number() < 0.0) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(*value->number());
}

[[nodiscard]] std::optional<uint32_t> jsonFlexibleUInt32(const JsonValue::Object& object, const std::string& key)
{
    const auto* value = findMember(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->number() != nullptr && *value->number() >= 0.0 &&
        *value->number() <= static_cast<double>(std::numeric_limits<uint32_t>::max())) {
        return static_cast<uint32_t>(*value->number());
    }
    if (value->string() != nullptr) {
        const auto parsed = parseVisualHashString(*value->string());
        if (parsed.has_value() && *parsed <= std::numeric_limits<uint32_t>::max()) {
            return static_cast<uint32_t>(*parsed);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<VisualReplacementPalette> jsonPalette(const JsonValue::Object& object, const std::string& key)
{
    const auto* value = findMember(object, key);
    if (value == nullptr || value->array() == nullptr || value->array()->size() != 4u) {
        return std::nullopt;
    }

    VisualReplacementPalette palette{};
    for (std::size_t i = 0; i < 4u; ++i) {
        const auto& entry = (*value->array())[i];
        std::optional<uint32_t> parsed;
        if (entry.number() != nullptr && *entry.number() >= 0.0 &&
            *entry.number() <= static_cast<double>(std::numeric_limits<uint32_t>::max())) {
            parsed = static_cast<uint32_t>(*entry.number());
        } else if (entry.string() != nullptr) {
            const auto hash = parseVisualHashString(*entry.string());
            if (hash.has_value() && *hash <= std::numeric_limits<uint32_t>::max()) {
                parsed = static_cast<uint32_t>(*hash);
            }
        }
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        palette[i] = *parsed;
    }
    return palette;
}

[[nodiscard]] std::optional<VisualSliceRect> jsonSlicing(const JsonValue::Object& object, const std::string& key)
{
    const auto* value = findMember(object, key);
    if (value == nullptr || value->object() == nullptr) {
        return std::nullopt;
    }
    VisualSliceRect slice{
        .x = jsonUInt32(*value->object(), "x").value_or(0u),
        .y = jsonUInt32(*value->object(), "y").value_or(0u),
        .width = jsonUInt32(*value->object(), "width").value_or(0u),
        .height = jsonUInt32(*value->object(), "height").value_or(0u),
    };
    if (slice.width == 0u || slice.height == 0u) {
        return std::nullopt;
    }
    return slice;
}

[[nodiscard]] bool jsonUInt32Equals(const JsonValue::Object& object, const std::string& key, uint32_t expected)
{
    const auto* value = findMember(object, key);
    return value != nullptr && value->number() != nullptr && *value->number() == static_cast<double>(expected);
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
    input >> std::ws;
    if (!input.eof()) {
        return std::nullopt;
    }
    return parsed;
}

} // namespace

VisualPackManifestLoadResult loadVisualPackManifest(const std::filesystem::path& manifestPath)
{
    std::error_code sizeEc;
    const auto manifestSize = std::filesystem::file_size(manifestPath, sizeEc);
    if (!sizeEc && manifestSize > VisualPackLimits::kMaxManifestBytes) {
        VisualPackManifestLoadResult result;
        result.error = "visual pack manifest too large: bytes=" + std::to_string(manifestSize) +
            " max=" + std::to_string(VisualPackLimits::kMaxManifestBytes);
        return result;
    }

    std::ifstream input(manifestPath);
    if (!input) {
        VisualPackManifestLoadResult result;
        result.error = "unable to open visual pack manifest";
        return result;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();

    auto rootValue = JsonParser(buffer.str()).parse();
    if (!rootValue.has_value() || rootValue->object() == nullptr) {
        VisualPackManifestLoadResult result;
        result.error = "invalid visual pack JSON";
        return result;
    }

    VisualPackManifestLoadResult result;
    const auto& root = *rootValue->object();
    if (!jsonUInt32Equals(root, "schemaVersion", kSupportedVisualPackSchemaVersion)) {
        result.error = "unsupported visual pack schemaVersion: expected=" +
            std::to_string(kSupportedVisualPackSchemaVersion);
        return result;
    }

    VisualPackManifest pack;
    pack.root = manifestPath.parent_path();
    pack.id = jsonString(root, "id").value_or("");
    pack.name = jsonString(root, "name").value_or("");
    pack.priority = jsonUInt32(root, "priority").value_or(0u);
    if (const auto target = jsonString(root, "target"); target.has_value() && !target->empty()) {
        pack.targets.push_back(*target);
    }
    if (const auto* targetsValue = findMember(root, "targets");
        targetsValue != nullptr && targetsValue->array() != nullptr) {
        for (const auto& targetValue : *targetsValue->array()) {
            if (targetValue.string() != nullptr && !targetValue.string()->empty()) {
                pack.targets.push_back(*targetValue.string());
            }
        }
    }
    if (!pack.targets.empty()) {
        pack.target = pack.targets.front();
    }

    const auto* rulesValue = findMember(root, "rules");
    if (rulesValue != nullptr && rulesValue->array() != nullptr) {
        if (rulesValue->array()->size() > VisualPackLimits::kMaxManifestRules) {
            result.error = "too many visual pack rules: count=" + std::to_string(rulesValue->array()->size()) +
                " max=" + std::to_string(VisualPackLimits::kMaxManifestRules);
            return result;
        }

        std::size_t order = 0;
        for (const auto& ruleValue : *rulesValue->array()) {
            if (ruleValue.object() == nullptr) {
                ++result.invalidRulesSkipped;
                continue;
            }
            const auto* matchValue = findMember(*ruleValue.object(), "match");
            const auto* replaceValue = findMember(*ruleValue.object(), "replace");
            if (matchValue == nullptr || replaceValue == nullptr ||
                matchValue->object() == nullptr || replaceValue->object() == nullptr) {
                ++result.invalidRulesSkipped;
                continue;
            }

            VisualOverrideRule rule;
            rule.order = order++;
            rule.machineId = jsonString(*matchValue->object(), "machineId").value_or("");
            if (rule.machineId.empty()) {
                rule.machineId = jsonString(*matchValue->object(), "machine").value_or("");
            }
            rule.semanticLabel = jsonString(*matchValue->object(), "semanticLabel").value_or("");
            rule.kind = visualResourceKindFromString(jsonString(*matchValue->object(), "kind").value_or(""));
            rule.decodedFormat =
                visualPixelFormatFromString(jsonString(*matchValue->object(), "decodedFormat").value_or(""));
            if (const auto hash = jsonString(*matchValue->object(), "sourceHash"); hash.has_value()) {
                rule.sourceHash = parseVisualHashString(*hash).value_or(0u);
            }
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
            rule.sourceBank = jsonFlexibleUInt32(*matchValue->object(), "sourceBank");
            rule.sourceAddress = jsonFlexibleUInt32(*matchValue->object(), "sourceAddress");
            rule.sourceIndex = jsonFlexibleUInt32(*matchValue->object(), "tileIndex");
            if (!rule.sourceIndex.has_value()) {
                rule.sourceIndex = jsonFlexibleUInt32(*matchValue->object(), "sourceIndex");
            }
            rule.paletteRegister = jsonString(*matchValue->object(), "paletteRegister").value_or("");
            rule.paletteValue = jsonFlexibleUInt32(*matchValue->object(), "paletteValue");
            rule.image = jsonString(*replaceValue->object(), "image").value_or("");
            rule.palette = jsonPalette(*replaceValue->object(), "palette");
            rule.slicing = jsonSlicing(*replaceValue->object(), "slicing");
            rule.scalePolicy = jsonString(*replaceValue->object(), "scalePolicy").value_or("");
            rule.filterPolicy = jsonString(*replaceValue->object(), "filterPolicy").value_or("");
            rule.anchor = jsonString(*replaceValue->object(), "anchor").value_or("");
            if (rule.kind == VisualResourceKind::Unknown ||
                (rule.sourceHash == 0u && rule.decodedHash == 0u && rule.paletteAwareHash == 0u) ||
                (rule.image.empty() && !rule.palette.has_value()) ||
                (findMember(*replaceValue->object(), "slicing") != nullptr && !rule.slicing.has_value())) {
                ++result.invalidRulesSkipped;
                continue;
            }
            std::error_code existsEc;
            if (!rule.image.empty() && !std::filesystem::exists(pack.root / rule.image, existsEc)) {
                ++result.missingReplacementImages;
            }
            rule.specificity = 1u +
                (!rule.machineId.empty() ? 4u : 0u) +
                (rule.decodedFormat != VisualPixelFormat::Unknown ? 2u : 0u) +
                (rule.sourceHash != 0u ? 2u : 0u) +
                (rule.decodedHash != 0u ? 2u : 0u) +
                (rule.paletteHash != 0u ? 3u : 0u) +
                (rule.paletteAwareHash != 0u ? 4u : 0u) +
                (!rule.semanticLabel.empty() ? 3u : 0u) +
                (rule.width != 0u ? 1u : 0u) +
                (rule.height != 0u ? 1u : 0u) +
                (rule.sourceBank.has_value() ? 1u : 0u) +
                (rule.sourceAddress.has_value() ? 2u : 0u) +
                (rule.sourceIndex.has_value() ? 2u : 0u) +
                (!rule.paletteRegister.empty() ? 2u : 0u) +
                (rule.paletteValue.has_value() ? 2u : 0u);
            pack.rules.push_back(std::move(rule));
        }
    }

    if (pack.id.empty() || pack.targets.empty()) {
        result.error = "visual pack manifest missing id or target";
        return result;
    }

    result.manifest = std::move(pack);
    return result;
}

} // namespace BMMQ
