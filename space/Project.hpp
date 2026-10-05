#pragma once
#include "Capture.hpp"
#include "Snapshots.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <vector>
#include "memory/MemorySnapshot/MemorySnapshot.hpp"
namespace BMMQ::Space {
using Json = nlohmann::json;
class Project {
public:
    static constexpr size_t defaultBudget=64u*1024u*1024u;
    explicit Project(std::string romHash, size_t budget=defaultBudget);
    void ingest(const Record& record);
    void gap(std::string reason);
    Json document() const;
    Json history() const;
    void restoreHistory(const Json& history);
    void merge(const Json& document);
    void save(const std::filesystem::path& path) const;
    static Project load(const std::filesystem::path& path, const std::string& expectedHash="");
    static Json read(const std::filesystem::path& path);
    static void write(const std::filesystem::path& path,const Json& data);
    static void validate(const Json& data);
    void annotate(const std::string&,const Json&);
    void startHistory() { branch_=instance_+":"+std::to_string(++branchCounter_);if(std::find(state_["sessions"].begin(),state_["sessions"].end(),instance_)==state_["sessions"].end())state_["sessions"].push_back(instance_);writers_=Json::object(); values_=Json::object(); views_=Json::object(); inputPosition_=0; previous_.clear(); previousTransfer_.clear(); active_=false; }
    void setBudget(std::shared_ptr<StateBudget> budget,bool chargeExisting=true) {if(budget&&chargeExisting&&!budget->reserve(used_))throw std::invalid_argument("analysis budget exhausted");sharedBudget_=std::move(budget);}
    size_t chargedBytes()const noexcept {return used_;}
    bool exhausted() const noexcept {return exhausted_;}
private:
    Json state_, writers_=Json::object(), values_=Json::object(), views_=Json::object();
    Record begin_{};
    std::vector<Record> accesses_;
    std::string branch_,instance_,previous_,previousTransfer_;
    uint64_t visit_=0, revision_=0, branchCounter_=0, inputPosition_=0;
    size_t budget_,used_=0;
    std::shared_ptr<StateBudget> sharedBudget_;
    bool active_=false,exhausted_=false;
    mutable std::map<std::string,std::shared_ptr<const BlockSnapshot>> blockSnapshots_;
    void finish(const Record& end);
};
std::string decimal(uint64_t v);
uint64_t counter(const Json& v);
void exportHtml(const Project& project,const std::filesystem::path& path);
}
