#include <array>
#include <cerrno>
#include <csignal>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "support/TestAssertions.h"

#ifndef DASALL_DAEMON_BINARY_PATH
#error "DASALL_DAEMON_BINARY_PATH must be defined"
#endif

#ifndef DASALL_CLI_BINARY_PATH
#error "DASALL_CLI_BINARY_PATH must be defined"
#endif

#ifndef DASALL_REPOSITORY_ROOT
#error "DASALL_REPOSITORY_ROOT must be defined"
#endif

namespace {

namespace fs = std::filesystem;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class ScopedTempDirectory {
 public:
  explicit ScopedTempDirectory(std::string stem)
      : path_(fs::temp_directory_path() /
              (std::move(stem) + "-" + std::to_string(::getpid()) + "-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    fs::create_directories(path_);
    fs::permissions(path_, static_cast<fs::perms>(0700), fs::perm_options::replace);
  }

  ~ScopedTempDirectory() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

struct ProcessResult {
  int exit_code = -1;
  std::string output;
};

[[nodiscard]] fs::path repository_root() {
  return fs::path(DASALL_REPOSITORY_ROOT);
}

[[nodiscard]] ProcessResult run_process_capture(
    const std::vector<std::string>& args,
    const fs::path& working_directory) {
  int output_pipe[2];
  if (::pipe(output_pipe) != 0) {
    throw std::runtime_error("failed to create output pipe");
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(output_pipe[0]);
    ::close(output_pipe[1]);
    throw std::runtime_error("failed to fork child process");
  }

  if (pid == 0) {
    ::dup2(output_pipe[1], STDOUT_FILENO);
    ::dup2(output_pipe[1], STDERR_FILENO);
    ::close(output_pipe[0]);
    ::close(output_pipe[1]);

    if (::chdir(working_directory.c_str()) != 0) {
      std::perror("chdir");
      std::_Exit(127);
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    ::execv(argv.front(), argv.data());
    std::perror("execv");
    std::_Exit(127);
  }

  ::close(output_pipe[1]);

  std::string output;
  std::array<char, 4096> buffer {};
  ssize_t read_count = 0;
  while ((read_count = ::read(output_pipe[0], buffer.data(), buffer.size())) > 0) {
    output.append(buffer.data(), static_cast<std::size_t>(read_count));
  }
  ::close(output_pipe[0]);

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    throw std::runtime_error("failed to wait for child process");
  }

  ProcessResult result;
  result.output = std::move(output);
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }
  return result;
}

class ScopedDaemonProcess {
 public:
  ScopedDaemonProcess(std::string binary_path,
                      fs::path working_directory,
                      fs::path socket_path,
                      fs::path log_path)
      : log_path_(std::move(log_path)) {
    pid_ = start(std::move(binary_path),
                 std::move(working_directory),
                 std::move(socket_path),
                 log_path_);
  }

  ScopedDaemonProcess(const ScopedDaemonProcess&) = delete;
  ScopedDaemonProcess& operator=(const ScopedDaemonProcess&) = delete;

  ~ScopedDaemonProcess() {
    if (pid_ > 0) {
      (void)stop();
    }
  }

  [[nodiscard]] int stop() {
    if (pid_ <= 0) {
      return exit_code_;
    }

    if (::kill(pid_, SIGTERM) != 0 && errno != ESRCH) {
      throw std::runtime_error("failed to terminate daemon process");
    }

    int status = 0;
    if (::waitpid(pid_, &status, 0) < 0) {
      throw std::runtime_error("failed to wait for daemon process");
    }

    pid_ = -1;
    if (WIFEXITED(status)) {
      exit_code_ = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      exit_code_ = 128 + WTERMSIG(status);
    }
    return exit_code_;
  }

  [[nodiscard]] std::string read_log() const {
    std::ifstream stream(log_path_);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
  }

 private:
  [[nodiscard]] static pid_t start(std::string binary_path,
                                   fs::path working_directory,
                                   fs::path socket_path,
                                   const fs::path& log_path) {
    const pid_t pid = ::fork();
    if (pid < 0) {
      throw std::runtime_error("failed to fork daemon process");
    }

    if (pid == 0) {
      const int log_fd = ::open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
      if (log_fd < 0) {
        std::perror("open");
        std::_Exit(127);
      }

      ::dup2(log_fd, STDOUT_FILENO);
      ::dup2(log_fd, STDERR_FILENO);
      ::close(log_fd);

      if (::chdir(working_directory.c_str()) != 0) {
        std::perror("chdir");
        std::_Exit(127);
      }

      const std::string socket_arg = socket_path.string();
      std::vector<std::string> args = {
          std::move(binary_path),
          "--socket-path",
          socket_arg,
      };
      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (auto& arg : args) {
        argv.push_back(arg.data());
      }
      argv.push_back(nullptr);

      ::execv(argv.front(), argv.data());
      std::perror("execv");
      std::_Exit(127);
    }

    return pid;
  }

  pid_t pid_ = -1;
  int exit_code_ = -1;
  fs::path log_path_;
};

[[nodiscard]] bool wait_for_ping_ready(const fs::path& socket_path) {
  const auto root = repository_root();
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto ping = run_process_capture(
        {
            DASALL_CLI_BINARY_PATH,
            "--socket-path",
            socket_path.string(),
            "ping",
        },
        root);
    if (ping.exit_code == 0) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

void daemon_binary_unary_smoke_completes_with_real_main_init() {
  ScopedTempDirectory temp_root("dasall-daemon-binary-unary");
  const auto socket_path = temp_root.path() / "control.sock";
  const auto log_path = temp_root.path() / "daemon.log";
  const auto root = repository_root();

  ScopedDaemonProcess daemon(
      DASALL_DAEMON_BINARY_PATH,
      root,
      socket_path,
      log_path);

  assert_true(wait_for_ping_ready(socket_path),
              "binary unary smoke should observe daemon ping readiness before issuing cli run; daemon log=" +
                  daemon.read_log());

  const auto run = run_process_capture(
      {
          DASALL_CLI_BINARY_PATH,
          "--socket-path",
          socket_path.string(),
          "run",
          "{\"prompt\":\"binary smoke\"}",
      },
      root);

  assert_equal(0, run.exit_code,
               "binary unary smoke should complete cli run through the built daemon main path; output=" +
                   run.output + " daemon_log=" + daemon.read_log());
  assert_true(run.output.find("[dasall_cli] submit: completed") != std::string::npos,
              "binary unary smoke should surface completed cli output; output=" + run.output);
  assert_true(run.output.find("runtime orchestrator skeleton completed") != std::string::npos,
              "binary unary smoke should surface the runtime skeleton response text; output=" + run.output);

  assert_equal(0, daemon.stop(),
               "binary unary smoke should stop the daemon cleanly; daemon_log=" + daemon.read_log());
}

}  // namespace

int main() {
  try {
    daemon_binary_unary_smoke_completes_with_real_main_init();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonBinaryUnarySmokeTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}