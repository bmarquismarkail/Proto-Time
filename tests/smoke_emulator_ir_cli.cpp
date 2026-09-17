#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace {

struct ProcessResult {
    int status = -1;
    std::string output;
};

std::vector<char*> makeArgv(const std::vector<std::string>& arguments)
{
    std::vector<char*> result;
    result.reserve(arguments.size() + 1u);
    for (const auto& argument : arguments) {
        result.push_back(const_cast<char*>(argument.c_str()));
    }
    result.push_back(nullptr);
    return result;
}

ProcessResult runProcess(const std::vector<std::string>& arguments)
{
    if (arguments.empty()) return {};
    int outputPipe[2] = {-1, -1};
    if (pipe(outputPipe) != 0) return {};

    const auto childArguments = makeArgv(arguments);
    const auto child = fork();
    if (child < 0) {
        close(outputPipe[0]);
        close(outputPipe[1]);
        return {};
    }
    if (child == 0) {
        close(outputPipe[0]);
        if (dup2(outputPipe[1], STDOUT_FILENO) < 0 ||
            dup2(outputPipe[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(outputPipe[1]);
        execv(childArguments[0], childArguments.data());
        _exit(127);
    }

    close(outputPipe[1]);
    ProcessResult result;
    std::array<char, 4096u> buffer{};
    for (;;) {
        const auto count = read(outputPipe[0], buffer.data(), buffer.size());
        if (count > 0) {
            result.output.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        break;
    }
    close(outputPipe[0]);

    int childStatus = 0;
    while (waitpid(child, &childStatus, 0) < 0) {
        if (errno != EINTR) return result;
    }
    if (WIFEXITED(childStatus)) result.status = WEXITSTATUS(childStatus);
    else if (WIFSIGNALED(childStatus)) result.status = 128 + WTERMSIG(childStatus);
    return result;
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto pattern = (base / "proto-time-emulator-ir-cli-XXXXXX").string();
        std::vector<char> writablePattern(pattern.begin(), pattern.end());
        writablePattern.push_back('\0');
        const auto* created = mkdtemp(writablePattern.data());
        if (created == nullptr) {
            throw std::runtime_error(std::string("unable to create temp directory: ") +
                                     std::strerror(errno));
        }
        path_ = created;
        if (path_.parent_path() != base ||
            path_.filename().string().find("proto-time-emulator-ir-cli-") != 0u) {
            throw std::runtime_error("temp directory resolved outside the expected scope");
        }
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        if (!path_.empty()) std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void writeRom(const std::filesystem::path& path)
{
    const std::vector<std::uint8_t> bytes(0x8000u, 0x00u);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("unable to create fixture ROM");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("unable to write fixture ROM");
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[noreturn]] void failCase(std::string_view name,
                           const ProcessResult& result,
                           std::string_view reason)
{
    std::fprintf(stderr, "%.*s: %.*s (status=%d)\n%s\n",
                 static_cast<int>(name.size()), name.data(),
                 static_cast<int>(reason.size()), reason.data(),
                 result.status, result.output.c_str());
    std::exit(EXIT_FAILURE);
}

void expectCase(std::string_view name,
                const ProcessResult& result,
                int expectedStatus,
                std::initializer_list<std::string_view> diagnostics)
{
    if (result.status != expectedStatus) {
        failCase(name, result, "unexpected exit status");
    }
    for (const auto diagnostic : diagnostics) {
        if (result.output.find(diagnostic) == std::string::npos) {
            failCase(name, result, "missing diagnostic");
        }
    }
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 6) {
        std::fprintf(stderr,
            "expected emulator, valid, malformed, incompatible, and wrong-architecture plugins\n");
        return EXIT_FAILURE;
    }

    const std::filesystem::path emulator = argv[1];
    const std::filesystem::path validPlugin = argv[2];
    const std::filesystem::path malformedPlugin = argv[3];
    const std::filesystem::path incompatiblePlugin = argv[4];
    const std::filesystem::path wrongArchitecturePlugin = argv[5];
    TemporaryDirectory temporary;
    const auto rom = temporary.path() / "minimal.gg";
    writeRom(rom);

    const std::vector<std::string> base{
        emulator.string(), "--core", "gamegear", "--rom", rom.string(),
        "--headless", "--steps", "1", "--unthrottled",
    };
    const auto with = [&](std::initializer_list<std::string_view> additions) {
        auto arguments = base;
        for (const auto addition : additions) arguments.emplace_back(addition);
        return arguments;
    };

    expectCase("built-in IR", runProcess(with({"--cpu-mode", "ir"})), EXIT_SUCCESS,
               {"Core: gamegear",
                "Executor policy: bmmq.executor.policy.portable-ir",
                "Execution backend: portable-ir",
                "Stopped after 1 instruction steps"});

    const auto diagnostics = temporary.path() / "gamegear-ir.jsonl";
    expectCase("Game Gear detailed diagnostics",
        runProcess(with({"--cpu-mode", "ir", "--cpu-detailed-timing",
                         "--diagnostics-report", diagnostics.string(),
                         "--diagnostics-interval-ms", "1"})),
        EXIT_SUCCESS, {"Diagnostics report:", "Stopped after 1 instruction steps"});
    const ProcessResult diagnosticsFile{EXIT_SUCCESS, readText(diagnostics)};
    expectCase("Game Gear diagnostics JSON", diagnosticsFile, EXIT_SUCCESS,
               {"\"gamegear_ir\":{\"supported\":true",
                "\"detailed_timing_enabled\":true",
                "\"dispatch_attempts\":1",
                "\"executions\":1",
                "\"lowering_ns\":"});

    expectCase("dynamic IR",
        runProcess(with({"--cpu-mode", "ir",
                         "--ir-adapter-plugin", validPlugin.string(),
                         "--ir-adapter-id", "test.ir-adapter.gamegear-nop",
                         "--ir-backend-plugin", validPlugin.string(),
                         "--ir-backend-id", "test.ir-backend.host-callback"})),
        EXIT_SUCCESS,
        {"Executor policy: bmmq.executor.policy.portable-ir",
         "Execution backend: portable-ir",
         "Stopped after 1 instruction steps",
         "IR attempts=1 translations=1 executions=1",
         "fallbacks=0", "fixture-ir: adapter-lowered", "fixture-ir: backend-executed"});

    expectCase("dynamic adapter only",
        runProcess(with({"--cpu-mode", "ir",
                         "--ir-adapter-plugin", validPlugin.string(),
                         "--ir-adapter-id", "test.ir-adapter.gamegear-nop"})),
        EXIT_SUCCESS,
        {"IR attempts=1 translations=1 executions=1", "fallbacks=0",
         "fixture-ir: adapter-lowered"});

    expectCase("dynamic backend only",
        runProcess(with({"--cpu-mode", "ir",
                         "--ir-backend-plugin", validPlugin.string(),
                         "--ir-backend-id", "test.ir-backend.host-callback"})),
        EXIT_SUCCESS,
        {"IR attempts=1 translations=1 executions=1", "fallbacks=0",
         "fixture-ir: backend-executed"});

    expectCase("dynamic outside IR",
        runProcess(with({"--cpu-mode", "baseline",
                         "--ir-adapter-plugin", validPlugin.string(),
                         "--ir-adapter-id", "test.ir-adapter.gamegear-nop"})),
        EXIT_FAILURE, {"dynamic IR components selected", "require --cpu-mode ir"});

    expectCase("unknown adapter ID",
        runProcess(with({"--cpu-mode", "ir",
                         "--ir-adapter-plugin", validPlugin.string(),
                         "--ir-adapter-id", "does-not-exist"})),
        EXIT_FAILURE, {"IR core adapter not found in module: does-not-exist"});

    expectCase("wrong architecture",
        runProcess(with({"--cpu-mode", "ir",
                         "--ir-adapter-plugin", wrongArchitecturePlugin.string(),
                         "--ir-adapter-id", "test.ir-adapter.wrong-arch"})),
        EXIT_FAILURE, {"selected IR core adapter does not match the active machine"});

    expectCase("incompatible ABI",
        runProcess(with({"--cpu-mode", "ir",
                         "--ir-adapter-plugin", incompatiblePlugin.string(),
                         "--ir-adapter-id", "test.ir-adapter.gamegear-nop"})),
        EXIT_FAILURE, {"incomplete ir-core-adapter API"});

    expectCase("malformed function table",
        runProcess(with({"--cpu-mode", "ir",
                         "--ir-adapter-plugin", malformedPlugin.string(),
                         "--ir-adapter-id", "test.ir-adapter.bad"})),
        EXIT_FAILURE, {"incomplete ir-core-adapter API"});

    expectCase("adapter path without ID",
        runProcess(with({"--cpu-mode", "ir",
                         "--ir-adapter-plugin", validPlugin.string()})),
        EXIT_FAILURE, {"--ir-adapter-plugin (ir_adapter_plugin) and --ir-adapter-id"});

    expectCase("adapter ID without path",
        runProcess(with({"--cpu-mode", "ir",
                         "--ir-adapter-id", "test.ir-adapter.gamegear-nop"})),
        EXIT_FAILURE, {"--ir-adapter-plugin (ir_adapter_plugin) and --ir-adapter-id"});
    return EXIT_SUCCESS;
}
