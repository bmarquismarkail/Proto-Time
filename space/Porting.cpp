#include "Porting.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <set>
namespace BMMQ::Space {
namespace {
void need(bool ok, const char *reason) {
  if (!ok)
    throw std::invalid_argument(reason);
}
void hash(const Json &v) {
  auto s = v.get<std::string>();
  need(s.size() == 64 &&
           s.find_first_not_of("0123456789abcdef") == std::string::npos,
       "invalid port digest");
}
unsigned natural(const Json &value, unsigned maximum) {
  need(value.is_number_integer() && value >= 0 && value <= maximum,
       "invalid port numeric range");
  return value.get<unsigned>();
}
using Range = std::pair<uint64_t, uint64_t>;
Range range(const Json &r, uint64_t size) {
  auto a = counter(r.at("offset")), n = counter(r.at("length"));
  need(n > 0 && a < size && n <= size - a, "invalid port byte range");
  return {a, a + n};
}
std::set<std::string> ids(const Json &a) {
  need(a.is_array(), "port collection must be an array");
  std::set<std::string> s;
  for (auto &r : a) {
    auto id = r.at("id").get<std::string>();
    need(!id.empty() && id.size() <= 256 && s.insert(id).second,
         "duplicate/invalid port ID");
  }
  return s;
}
void refs(const Json &a, const std::set<std::string> &known) {
  need(a.is_array(), "port references must be an array");
  std::set<std::string> seen;
  for (auto &v : a) {
    auto s = v.get<std::string>();
    need(known.contains(s) && seen.insert(s).second,
         "unknown/duplicate port reference");
  }
}
std::vector<uint8_t> bytes(const std::filesystem::path &p) {
  need(std::filesystem::file_size(p) <= 64u * 1024u * 1024u,
       "port ROM exceeds 64 MiB");
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("cannot read port ROM");
  return {(std::istreambuf_iterator<char>(f)), {}};
}
bool reviewed(const Json &v) {
  return v.at("reviewed").get<bool>() && !v.at("evidence").empty();
}
} // namespace
std::string portBinding(const Json &p) {
  Json b = p;
  b.erase("revision");
  b.erase("verification");
  auto s = b.dump();
  return digest(
      std::span(reinterpret_cast<const uint8_t *>(s.data()), s.size()));
}
void validatePorting(const Json &j) {
  if (!j.contains("porting"))
    return;
  auto &p = j.at("porting");
  need(p.at("version") == 1, "unsupported porting version");
  counter(p.at("revision"));
  hash(p.at("buildSha256"));
  hash(p.at("sourceSha256"));
  hash(p.at("assetSha256"));
  auto &source = p.at("source");
  auto &target = p.at("target");
  hash(source.at("sha256"));
  hash(target.at("sha256"));
  need(source.at("sha256") == j.at("romSha256") &&
           source.at("core") == "gameboy" && target.at("core") == "gamegear",
       "port ROM/core mismatch");
  auto size = counter(source.at("size")), ts = counter(target.at("size"));
  need(size > 0 && size <= 64u * 1024u * 1024u && ts > 0 &&
           ts <= 64u * 1024u * 1024u,
       "invalid port ROM size");
  auto inv = ids(p.at("inventory")), contracts = ids(p.at("contracts"));
  need(p.at("requiredScenarios").is_array() && !p["requiredScenarios"].empty(),
       "missing required verification scenarios");
  std::set<std::string> scenarios;
  for (auto &s : p["requiredScenarios"]) {
    auto name = s.get<std::string>();
    need(!name.empty() && scenarios.insert(name).second,
         "duplicate/invalid scenario");
  }
  auto mappings = ids(p.at("mappings"));
  (void)mappings;
  ids(p.at("hardware"));
  ids(p.at("controlFlow"));
  ids(p.at("verification"));
  need(p.at("supplementalEvidence").is_array() &&
           p.at("assumptions").is_array(),
       "invalid port evidence/assumptions");
  for (auto &e : p["supplementalEvidence"]) {
    hash(e.at("sha256"));
    e.at("kind").get<std::string>();
    e.at("path").get<std::string>();
    need(e.at("romSha256") == source.at("sha256"),
         "unmatched supplemental evidence");
  }
  for (auto &c : p["contracts"]) {
    c.at("text").get<std::string>();
    hash(c.at("sha256"));
    auto text = c.at("text").get<std::string>();
    need(c["sha256"] ==
             digest(std::span(reinterpret_cast<const uint8_t *>(text.data()),
                              text.size())),
         "contract digest mismatch");
  }
  auto &model = p.at("model");
  need(model.at("banks").is_array() && model.at("entryPoints").is_array() &&
           model.at("evidence").is_array(),
       "invalid bounded control-flow model");
  model.at("reviewed").get<bool>();
  model.at("ramExecution").get<bool>();
  natural(model.at("stackBytes"), 8192);
  std::set<unsigned> banks;
  for (auto &bank : model["banks"]) {
    auto b = natural(bank, 4095);
    need(b * uint64_t(16384) < size && banks.insert(b).second,
         "invalid legal bank");
  }
  std::vector<Range> ranges;
  std::set<std::string> staticInstructions;
  std::set<uint64_t> entryLocations;
  for (auto &r : p["inventory"]) {
    auto [a, b] = range(r, size);
    ranges.push_back({a, b});
    hash(r.at("sha256"));
    auto kind = r.at("kind").get<std::string>();
    need(kind == "code" || kind == "data" || kind == "header" ||
             kind == "padding" || kind == "unknown" || kind == "ambiguous",
         "invalid inventory classification");
    r.at("reviewed").get<bool>();
    need(r.at("evidence").is_array(), "inventory evidence must be an array");
    auto bank = natural(r.at("bank"), 4095),
         address = natural(r.at("address"), 65535);
    need(a / 16384 == bank && (b - 1) / 16384 == bank &&
             address == (bank ? 16384 : 0) + a % 16384,
         "invalid physical bank/address mapping");
    need(r.at("instructions").is_array(), "invalid instruction inventory");
    uint64_t cursor = a;
    for (auto &i : r["instructions"]) {
      need(kind == "code", "non-code instruction inventory");
      auto [x, y] = range(i, size);
      need(x == cursor && y <= b && y - x <= 3,
           "invalid inventory instruction boundary");
      cursor = y;
      auto id = i.at("id").get<std::string>();
      hash(i.at("id"));
      need(staticInstructions.insert(id).second,
           "duplicate static instruction");
      auto hex = i.at("bytes").get<std::string>();
      need(hex.size() == (y - x) * 2 &&
               hex.find_first_not_of("0123456789abcdef") == std::string::npos,
           "invalid inventory instruction bytes");
      auto location = counter(i.at("location"));
      entryLocations.insert(location);
      need(location ==
               ((uint64_t(1) << 32) | ((x / 16384) << 16) | (x % 16384)),
           "invalid inventory instruction location");
      auto identity = decimal(location) + ":" + hex;
      need(id == digest(std::span(
                     reinterpret_cast<const uint8_t *>(identity.data()),
                     identity.size())),
           "invalid stable port instruction identity");
      if (j["instructions"].contains(id))
        need(j["instructions"][id]["bytes"] == hex &&
                 counter(j["instructions"][id]["location"]) == location,
             "captured/static instruction mismatch");
    }
    need(kind != "code" || cursor == b,
         "code inventory needs all instruction boundaries");
  }
  std::sort(ranges.begin(), ranges.end());
  uint64_t end = 0;
  for (auto [a, b] : ranges) {
    need(a == end,
         "inventory must partition every source ROM byte without overlap");
    end = b;
  }
  need(end == size, "incomplete physical inventory");
  std::set<uint64_t> entries;
  for (auto &e : model["entryPoints"]) {
    auto text = e.get<std::string>();
    auto colon = text.find(':');
    need(colon != std::string::npos, "invalid model entry");
    unsigned bank = 0, address = 0;
    auto b = std::from_chars(text.data(), text.data() + colon, bank);
    auto a = std::from_chars(text.data() + colon + 1, text.data() + text.size(),
                             address, 16);
    need(b.ec == std::errc{} && b.ptr == text.data() + colon &&
             a.ec == std::errc{} && a.ptr == text.data() + text.size() &&
             banks.contains(bank) && address < 32768 &&
             (bank ? address >= 16384 : address < 16384),
         "invalid model bank/entry");
    auto location =
        (uint64_t(1) << 32) | (uint64_t(bank) << 16) | (address & 16383);
    need(entryLocations.contains(location) && entries.insert(location).second,
         "unknown/duplicate model instruction entry");
  }
  for (auto &m : p["mappings"]) {
    auto kind = m.at("kind").get<std::string>();
    need(kind == "translation" || kind == "replacement",
         "invalid conversion record kind");
    refs(m.at("sourceRegions"), inv);
    need(!m["sourceRegions"].empty(), "mapping needs source region");
    refs(m.at("instructions"), staticInstructions);
    std::set<std::string> owned;
    for (auto &r : p["inventory"])
      if (std::find(m["sourceRegions"].begin(), m["sourceRegions"].end(),
                    r["id"]) != m["sourceRegions"].end())
        for (auto &i : r["instructions"])
          owned.insert(i["id"].get<std::string>());
    refs(m["instructions"], owned);
    refs(m.at("contracts"), contracts);
    need(!m.at("targets").empty(), "mapping needs target bytes");
    m.at("implemented").get<bool>();
    m.at("reviewed").get<bool>();
    need(m.at("evidence").is_array(), "invalid conversion evidence");
    for (auto &r : m["targets"]) {
      auto [a, b] = range(r, ts);
      need(natural(r.at("bank"), 4095) == a / 16384 &&
               (b - 1) / 16384 == a / 16384 &&
               natural(r.at("address"), 65535) ==
                   (a / 16384 ? 16384 : 0) + a % 16384,
           "invalid target bank/address mapping");
      hash(r.at("sha256"));
    }
    need(kind != "replacement" || !m["contracts"].empty(),
         "replacement needs a contract");
  }
  for (auto key : {"hardware", "controlFlow"})
    for (auto &o : p[key]) {
      refs(o.at("contracts"), contracts);
      need(!o["contracts"].empty(), "obligation needs contracts");
      o.at("implemented").get<bool>();
      o.at("reviewed").get<bool>();
      need(o.at("evidence").is_array(), "invalid obligation evidence");
    }
  std::map<std::string, std::string> results;
  for (auto &v : p["verification"]) {
    hash(v.at("binding"));
    refs(v.at("contracts"), contracts);
    auto type = v.at("type").get<std::string>(),
         status = v.at("status").get<std::string>();
    need(type == "behavioral" || type == "independent" || type == "live" ||
             type == "viewer",
         "invalid verification type");
    need(status == "passed" || status == "failed" || status == "not run",
         "invalid verification status");
    need(v.at("scenarios").is_array() && !v["scenarios"].empty() &&
             v.at("artifacts").is_array() && !v["artifacts"].empty(),
         "verification needs scenarios/artifacts");
    refs(v["scenarios"], scenarios);
    for (auto &a : v["artifacts"]) {
      hash(a.at("sha256"));
      a.at("path").get<std::string>();
    }
    need(v.at("tools").is_array() && !v["tools"].empty(),
         "verification needs tool identities");
    for (auto &t : v["tools"]) {
      t.at("name").get<std::string>();
      t.at("version").get<std::string>();
      hash(t.at("sha256"));
    }
    need(v.at("evidence").is_array() && !v["evidence"].empty(),
         "verification needs evidence");
    Json scope;
    for (auto key :
         {"binding", "type", "contracts", "scenarios", "artifacts", "tools"})
      scope[key] = v[key];
    auto key = scope.dump();
    if (results.contains(key))
      need(results[key] == status, "conflicting verification outcomes");
    results[key] = status;
  }
}
Json checkPorting(const Json &j) {
  validatePorting(j);
  need(j.contains("porting"), "project has no port ledger");
  auto &p = j["porting"];
  Json problems = Json::array();
  std::set<std::string> covered, instructionCoverage;
  for (auto &m : p["mappings"])
    if (m["implemented"].get<bool>() && reviewed(m)) {
      for (auto &r : m["sourceRegions"])
        covered.insert(r.get<std::string>());
      for (auto &i : m["instructions"])
        instructionCoverage.insert(i.get<std::string>());
    }
  for (auto &r : p["inventory"]) {
    auto kind = r["kind"].get<std::string>();
    auto id = r["id"].get<std::string>();
    if (!reviewed(r) || kind == "unknown" || kind == "ambiguous")
      problems.push_back("unreviewed/unknown region: " + id);
    if ((kind == "code" || kind == "data" || kind == "header") &&
        !covered.contains(id))
      problems.push_back("unconverted region: " + id);
    for (auto &i : r["instructions"])
      if (!instructionCoverage.contains(i["id"].get<std::string>()))
        problems.push_back("unconverted instruction: " +
                           i["id"].get<std::string>());
  }
  for (auto key : {"hardware", "controlFlow"}) {
    if (p[key].empty())
      problems.push_back(std::string("missing ") + key + " obligations");
    for (auto &o : p[key])
      if (!o["implemented"].get<bool>() || !reviewed(o))
        problems.push_back(std::string("open ") + key + ": " +
                           o["id"].get<std::string>());
  }
  auto &model = p["model"];
  if (!reviewed(model) || model["ramExecution"].get<bool>() ||
      model["banks"].empty() || model["entryPoints"].empty() ||
      model["stackBytes"].get<unsigned>() == 0)
    problems.push_back("bounded control-flow model is not closed");
  Json verification = Json::array();
  std::set<std::string> passed;
  auto binding = portBinding(p);
  for (auto v : p["verification"]) {
    if (v["binding"] != binding)
      v["status"] = "stale";
    else if (v["status"] == "passed") {
      // A single partial scenario record cannot establish the acceptance gate.
      bool scoped = true;
      for (auto &required : p["requiredScenarios"])
        scoped =
            scoped && std::find(v["scenarios"].begin(), v["scenarios"].end(),
                                required) != v["scenarios"].end();
      for (auto &c : p["contracts"])
        scoped =
            scoped && std::find(v["contracts"].begin(), v["contracts"].end(),
                                c["id"]) != v["contracts"].end();
      if (scoped)
        passed.insert(v["type"].get<std::string>());
    }
    verification.push_back(v);
  }
  bool complete = problems.empty();
  return {{"complete", complete},
          {"problems", problems},
          {"assumptions", p["assumptions"]},
          {"binding", binding},
          {"revision", p["revision"]},
          {"verification", verification},
          {"stage5Gate", complete && passed.contains("behavioral") &&
                             passed.contains("independent") &&
                             passed.contains("live")},
          {"liveAcceptance", passed.contains("live") ? "passed" : "not run"}};
}
Json mergePorting(const Json &a, const Json &b) {
  need(a.is_object() && b.is_object() && a.contains("verification") &&
           b.contains("verification") && a.contains("revision") && b.contains("revision"),
       "invalid port ledger for merge");
  auto records = ids(a.at("verification"));
  ids(b.at("verification"));
  const auto revision = std::max(counter(a.at("revision")), counter(b.at("revision")));
  auto x = a, y = b;
  x.erase("verification");
  y.erase("verification");
  x.erase("revision");
  y.erase("revision");
  need(x == y,
       "conflicting port ledger; explicitly initialize a revised ledger");
  Json out = a;
  for (auto &v : b.at("verification")) {
    auto id = v.at("id").get<std::string>();
    if (records.contains(id)) {
      auto found =
          std::find_if(out["verification"].begin(), out["verification"].end(),
                       [&](const auto &r) { return r["id"] == id; });
      need(*found == v, "conflicting verification record");
    } else {
      records.insert(id);
      out["verification"].push_back(v);
    }
  }
  out["revision"] =
      decimal(revision);
  return out;
}
void attachPorting(Json &j, const Json &p) {
  Json next = j;
  next["porting"] = p;
  Project::validate(next);
  if (next != j)
    next["revision"] = decimal(counter(j["revision"]) + 1);
  j = std::move(next);
}
void recordVerification(Json &j, const Json &v) {
  need(j.contains("porting"), "project has no port ledger");
  auto next = j;
  auto &p = next["porting"];
  auto record = v;
  need(record.at("binding") == portBinding(p),
       "verification artifact/contract binding mismatch");
  for (auto &old : p["verification"])
    if (old["id"] == record.at("id")) {
      need(old == record, "conflicting verification record");
      return;
    }
  p["verification"].push_back(record);
  p["revision"] = decimal(counter(p["revision"]) + 1);
  Project::validate(next);
  if (next != j)
    next["revision"] = decimal(counter(j["revision"]) + 1);
  j = std::move(next);
}
void verifyPortArtifacts(const Json &j, const std::filesystem::path &source,
                         const std::filesystem::path &target) {
  validatePorting(j);
  auto &p = j.at("porting");
  auto s = bytes(source), t = bytes(target);
  need(s.size() == counter(p["source"]["size"]) &&
           p["source"]["sha256"] == digest(s) &&
           t.size() == counter(p["target"]["size"]) &&
           p["target"]["sha256"] == digest(t),
       "port artifact ROM mismatch");
  auto build = bytes(source.parent_path() / "build.json");
  need(p["buildSha256"] == digest(build), "build manifest mismatch");
  auto manifest = Json::parse(build.begin(), build.end());
  need(manifest.at("roms").at("gb").at("sha256") == p["source"]["sha256"] &&
           manifest.at("roms").at("gg").at("sha256") == p["target"]["sha256"],
       "build ROM identity mismatch");
  auto sources = manifest.at("sources").dump();
  need(p["sourceSha256"] ==
           digest(std::span(reinterpret_cast<const uint8_t *>(sources.data()),
                            sources.size())),
       "build source digest mismatch");
  for (auto it = manifest["sources"].begin(); it != manifest["sources"].end();
       ++it) {
    auto data = bytes(source.parent_path() / it.key());
    need(it.value() == digest(data), "build source file mismatch");
  }
  auto art = bytes(source.parent_path() / "canonical-art.json");
  need(p["assetSha256"] == digest(art), "canonical asset digest mismatch");

  for (auto &v : p["verification"])
    if (v["binding"] == portBinding(p))
      for (auto &a : v["artifacts"]) {
        auto data =
            bytes(source.parent_path() / a.at("path").get<std::string>());
        need(a["sha256"] == digest(data),
             "verification artifact file mismatch");
      }
  for (auto &e : p["supplementalEvidence"]) {
    auto path = source.parent_path() / e.at("path").get<std::string>();
    auto data = bytes(path);
    need(e["sha256"] == digest(data), "supplemental evidence file mismatch");
  }
  for (auto &r : p["inventory"]) {
    auto [a, b] = range(r, s.size());
    need(r["sha256"] == digest(std::span(s).subspan(a, b - a)),
         "inventory bytes mismatch");
    for (auto &i : r["instructions"]) {
      auto [x, y] = range(i, s.size());
      std::string hex;
      constexpr char h[] = "0123456789abcdef";
      for (auto n : std::span(s).subspan(x, y - x)) {
        hex += h[n >> 4];
        hex += h[n & 15];
      }
      need(i["bytes"] == hex, "inventory instruction ROM mismatch");
      auto op = s[x];
      size_t length = 1;
      if (op == 0xCB || op == 0x10 || (op < 0x40 && (op & 7) == 6) ||
          op == 0x18 || op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38 ||
          (op >= 0xC0 && (op & 7) == 6) || op == 0xE0 || op == 0xF0 ||
          op == 0xE8 || op == 0xF8)
        length = 2;
      if ((op < 0x40 && (op & 15) == 1) || op == 8 || op == 0xC3 ||
          op == 0xCD || op == 0xC2 || op == 0xCA || op == 0xD2 || op == 0xDA ||
          op == 0xC4 || op == 0xCC || op == 0xD4 || op == 0xDC || op == 0xEA ||
          op == 0xFA)
        length = 3;
      need(length == y - x, "inventory opcode boundary mismatch");
    }
    if (r["kind"] == "padding")
      need(std::all_of(s.begin() + a, s.begin() + b,
                       [](auto n) { return n == 255; }),
           "padding is not assembler fill");
  }
  for (auto &m : p["mappings"])
    for (auto &r : m["targets"]) {
      auto [a, b] = range(r, t.size());
      need(r["sha256"] == digest(std::span(t).subspan(a, b - a)),
           "target mapping bytes mismatch");
    }
}
Json queryPorting(const Json &j, const Json &r) {
  Project::validate(j);
  auto check = checkPorting(j);
  auto kind = r.value("type", std::string("port"));
  Json list = Json::array();
  auto &p = j.at("porting");
  if (kind == "port")
    list.push_back(check);
  else if (kind == "inventory")
    list = p["inventory"];
  else if (kind == "conversions")
    list = p["mappings"];
  else if (kind == "obligations") {
    list = p["hardware"];
    for (auto &o : p["controlFlow"])
      list.push_back(o);
  } else if (kind == "verification")
    list = check["verification"];
  else
    throw std::invalid_argument("unknown port query");
  auto limit = r.value("limit", Json(256)), offset = r.value("offset", Json(0));
  need(limit.is_number_integer() && limit >= 1 && limit <= 1024 &&
           offset.is_number_integer() && offset >= 0,
       "invalid port query pagination");
  auto start = offset.get<size_t>(), n = limit.get<size_t>();
  Json page = Json::array();
  for (size_t i = start; i < list.size() && page.size() < n; ++i)
    page.push_back(list[i]);
  auto next = start + page.size();
  return {{"portIdentity", check["binding"]},
          {"revision", p["revision"]},
          {"items", page},
          {"total", decimal(list.size())},
          {"nextOffset", next < list.size() ? Json(next) : Json(nullptr)},
          {"truncated", next < list.size()},
          {"incomplete", !check["complete"].get<bool>()}};
}
} // namespace BMMQ::Space
