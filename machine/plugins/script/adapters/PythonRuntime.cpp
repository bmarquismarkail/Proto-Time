#include "../ScriptEngine.hpp"
#ifdef TIME_SCRIPT_PYTHON
#include "space/CoreModel.hpp"
#include "space/Project.hpp"
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace BMMQ::Script {
namespace {
struct Socket {
  int ends[2]{-1, -1};
  ~Socket() {
    for (int fd : ends)
      if (fd >= 0)
        ::close(fd);
  }
  bool open() {
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ends) != 0)
      return false;
    for (int &fd : ends)
      if (fd < 3) {
        const int moved = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (moved < 0)
          return false;
        ::close(fd);
        fd = moved;
      }
    return true;
  }
};
} // namespace
Result evaluatePython(std::string_view source, Invocation &call) {
  using Space::Json;
  const auto &model = Space::coreModel(call.snapshot.core);
  Json request{
      {"source", source},
      {"core", call.snapshot.core},
      {"memoryBase", call.snapshot.memoryBase},
      {"memory", std::vector<std::uint8_t>(call.snapshot.state.memory.begin(),
                                           call.snapshot.state.memory.begin() +
                                               call.snapshot.state.length)},
      {"permissions",
       {{"registers", call.permissions.registers},
        {"ram", call.permissions.ram},
        {"reports", call.permissions.reports}}},
      {"heapBytes", call.limits.heapBytes},
      {"instructions", call.limits.instructions},
      {"timeoutMs", call.limits.timeout.count()},
      {"registers", Json::object()},
      {"maximums", Json::object()}};
  for (std::size_t i = 0; i < model.pairs.size(); ++i) {
    request["registers"][model.pairs[i]] = call.staged.registers[i];
    request["maximums"][model.pairs[i]] = model.registerMaximum(i);
  }
  const auto input = request.dump();
  Socket in, out, err;
  if (!in.open() || !out.open() || !err.open())
    return {.error = "Python worker channels unavailable"};
  const char *arguments[] = {TIME_SCRIPT_PYTHON_EXECUTABLE, "-I", "-S",
                             TIME_SCRIPT_PYTHON_WORKER, nullptr};
  // All allocation and argument preparation precede fork. The child invokes
  // only async-signal-safe setup calls before exec; it never touches a machine,
  // C++ container, interpreter, host plugin or inherited lock.
  const pid_t pid = ::fork();
  if (pid < 0)
    return {.error = "Python worker process unavailable"};
  if (pid == 0) {
    if (::setpgid(0, 0) < 0 || ::dup2(in.ends[1], STDIN_FILENO) < 0 ||
        ::dup2(out.ends[1], STDOUT_FILENO) < 0 ||
        ::dup2(err.ends[1], STDERR_FILENO) < 0)
      ::_exit(127);
    for (int fd : in.ends)
      ::close(fd);
    for (int fd : out.ends)
      ::close(fd);
    for (int fd : err.ends)
      ::close(fd);
    ::execve(arguments[0], const_cast<char *const *>(arguments), environ);
    ::_exit(127);
  }
  struct Child {
    pid_t pid;
    bool reaped{};
    ~Child() {
      ::kill(-pid, SIGKILL);
      if (!reaped) {
        // Also terminate the child directly: setup may not have established
        // its process group yet, or trusted host code may have left it.
        ::kill(pid, SIGKILL);
        int status;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
      }
    }
  } child{pid};
  for (auto *channel : {&in, &out, &err}) {
    ::close(channel->ends[1]);
    channel->ends[1] = -1;
    ::fcntl(channel->ends[0], F_SETFL, O_NONBLOCK);
  }
  std::size_t sent = 0;
  std::string output, errors;
  bool outputDone = false, errorDone = false, inputDone = false;
  int status = 0;
  while (!child.reaped || !outputDone || !errorDone) {
    if (!call.budget(0))
      return {.error = "Python worker timeout"};
    pollfd descriptors[3] = {{in.ends[0], short(inputDone ? 0 : POLLOUT), 0},
                             {out.ends[0], short(outputDone ? 0 : POLLIN), 0},
                             {err.ends[0], short(errorDone ? 0 : POLLIN), 0}};
    if (::poll(descriptors, 3, 5) < 0 && errno != EINTR)
      return {.error = "Python worker channel failed"};
    if (!inputDone &&
        (descriptors[0].revents & (POLLOUT | POLLERR | POLLHUP))) {
      auto count = ::send(in.ends[0], input.data() + sent, input.size() - sent,
                          MSG_NOSIGNAL);
      if (count > 0)
        sent += count;
      else if (count < 0 && errno != EINTR && errno != EAGAIN)
        return {.error = "Python worker input disconnected"};
      if (sent == input.size()) {
        ::shutdown(in.ends[0], SHUT_WR);
        inputDone = true;
      }
    }
    for (int i = 1; i < 3; ++i) {
      bool &done = i == 1 ? outputDone : errorDone;
      auto &buffer = i == 1 ? output : errors;
      if (done)
        continue;
      char chunk[4096];
      auto count = ::read(descriptors[i].fd, chunk, sizeof(chunk));
      if (count > 0) {
        if (buffer.size() + count > 1024 * 1024)
          return {.error = "Python worker output budget exhausted"};
        buffer.append(chunk, count);
      } else if (count == 0)
        done = true;
      else if (errno != EINTR && errno != EAGAIN)
        return {.error = "Python worker output disconnected"};
    }
    if (!child.reaped) {
      auto result = ::waitpid(pid, &status, WNOHANG);
      if (result == pid)
        child.reaped = true;
      else if (result < 0 && errno != EINTR)
        return {.error = "Python worker wait failed"};
    }
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    return {.error = "Python worker failed or exhausted its resources"};
  const auto result = Json::parse(output);
  if (!result.at("success").get<bool>())
    return {.permissionDenied = result.value("permissionDenied", false),
            .error = result.value("error", "Python invocation rejected")};
  for (auto it = result.at("registers").begin();
       it != result.at("registers").end(); ++it) {
    if (!it.value().is_number_integer() || it.value() < 0 ||
        it.value() > 65535 ||
        !call.setRegister(it.key(), it.value().get<std::uint16_t>()))
      return {.error = "Python register recipe rejected"};
  }
  for (const auto &edit : result.at("writes")) {
    if (!edit.at("address").is_number_integer() || edit["address"] < 0 ||
        edit["address"] > 65535 || !edit.at("value").is_number_integer() ||
        edit["value"] < 0 || edit["value"] > 255 ||
        !call.setByte(edit["address"].get<std::uint16_t>(),
                      edit["value"].get<std::uint8_t>()))
      return {.error = "Python RAM recipe rejected"};
  }
  if (!result.at("report").get_ref<const std::string &>().empty() &&
      !call.output(result.at("report").get<std::string>()))
    return {.error = "Python report recipe rejected"};
  return {.success = true};
}
} // namespace BMMQ::Script
#else
namespace BMMQ::Script {
Result evaluatePython(std::string_view, Invocation &) {
  return {.error = "Python runtime unavailable"};
}
} // namespace BMMQ::Script
#endif
