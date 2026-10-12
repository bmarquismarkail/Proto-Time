#pragma once
#include "machine/plugins/script/ScriptEngine.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <span>
#include <vector>

namespace BMMQ::VisualAuthoring {
using Json = nlohmann::json;
Json parse(std::string_view, std::size_t maxBytes);
struct Binding {
    std::string core, romSha256, videoSequence, generation;
    bool operator==(const Binding&) const = default;
};
// Owned observations, not a machine handle or a claim of physical completeness.
struct Capture {
    Binding binding;
    Script::Snapshot cpu;
    Json resources;
    static Capture read(const Json&);
};
class Prepared {
    friend class Engine;
    Binding binding_;
    Json manifest_, annotations_;
    bool valid_{};
    struct Asset { std::string name; std::vector<std::uint8_t> bytes; };
    std::vector<Asset> assets_;
public:
    const Binding& binding() const noexcept { return binding_; }
    const Json& manifest() const noexcept { return manifest_; }
    const Json& annotations() const noexcept { return annotations_; }
};
// Offline/authoring lane only. Preparing validates every rule and decodes every
// owned image before any artifact is published. Old schema-1 packs are emitted.
class Engine {
public:
    static Prepared prepare(const Capture&, const Json&, const std::filesystem::path& assetRoot);
    static Prepared script(const Capture&, Script::Language, std::string_view,
                           const std::filesystem::path& assetRoot, Script::Limits = {});
    static void publish(const Prepared&, const Binding& expected, const std::filesystem::path& output);
};
}
