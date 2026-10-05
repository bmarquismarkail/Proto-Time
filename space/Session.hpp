#pragma once
#include "Project.hpp"
#include "Execution.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include <thread>
#include <mutex>
namespace BMMQ::Space {
class Session {
public:
    Session(GB::GameBoyMachine& machine,std::span<const uint8_t> rom,const std::filesystem::path& projectPath={});
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    void flush(); // only at a producer boundary; no further records until it returns
    void save(const std::filesystem::path& path);
    Json document();
    void input(uint8_t mask);
    void executionMode(const std::string&);
    Json executionStatus() const;
    void step();
    void checkpoint(const std::filesystem::path& directory);
    void restore(const std::filesystem::path& directory);
private:
    GB::GameBoyMachine& machine_;
    std::vector<uint8_t> rom_;
    std::unique_ptr<Capture> capture_;
    Project project_;
    std::unique_ptr<Execution> execution_;
    std::mutex mutex_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
    uint8_t inputMask_=0;
    uint64_t inputPosition_=0;
    void consume();
};
}
