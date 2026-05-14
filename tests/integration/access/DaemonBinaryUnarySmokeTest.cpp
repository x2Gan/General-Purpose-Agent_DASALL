#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
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

#include "CliBinaryTestSupport.h"
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

constexpr char kRuntimeStateRootOverrideEnv[] =
  "DASALL_RUNTIME_STATE_ROOT_OVERRIDE";
constexpr char kRuntimeCognitionFirstEnv[] =
  "DASALL_RUNTIME_COGNITION_FIRST";

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

class ScopedEnvVar {
 public:
  ScopedEnvVar(const char* name, const std::string& value) : name_(name) {
    if (const char* current = std::getenv(name); current != nullptr) {
      had_previous_ = true;
      previous_value_ = current;
    }
    ::setenv(name, value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if (had_previous_) {
      ::setenv(name_, previous_value_.c_str(), 1);
    } else {
      ::unsetenv(name_);
    }
  }

 private:
  const char* name_;
  bool had_previous_ = false;
  std::string previous_value_;
};

[[nodiscard]] fs::path repository_root() {
  return fs::path(DASALL_REPOSITORY_ROOT);
}

class ScopedDaemonProcess {
 public:
  ScopedDaemonProcess(std::string binary_path,
                      fs::path working_directory,
                      fs::path state_root,
                      fs::path socket_path,
                      fs::path log_path)
      : log_path_(std::move(log_path)) {
    pid_ = start(std::move(binary_path),
                 std::move(working_directory),
                 std::move(state_root),
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
                                   fs::path state_root,
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

      const std::string state_root_env = state_root.string();
      if (::setenv(kRuntimeStateRootOverrideEnv, state_root_env.c_str(), 1) != 0) {
        std::perror("setenv");
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
    const auto ping = dasall::tests::integration::access_support::run_process_capture_split(
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
  const auto state_root = temp_root.path() / "state";
  const auto socket_path = temp_root.path() / "control.sock";
  const auto log_path = temp_root.path() / "daemon.log";
  const auto root = repository_root();
  fs::create_directories(state_root);
  ScopedEnvVar cognition_first_override(kRuntimeCognitionFirstEnv, "1");

  ScopedDaemonProcess daemon(
      DASALL_DAEMON_BINARY_PATH,
      temp_root.path(),
      state_root,
      socket_path,
      log_path);

  if (!wait_for_ping_ready(socket_path)) {
    const auto daemon_exit_code = daemon.stop();
    assert_true(false,
                "binary unary smoke should observe daemon ping readiness before issuing cli run; socket_path=" +
                    socket_path.string() +
                    " socket_path_length=" +
                    std::to_string(socket_path.string().size()) +
                    " artifact_path=" + log_path.string() +
                    " daemon_exit_code=" + std::to_string(daemon_exit_code) +
                    " daemon_log=" + daemon.read_log());
  }

  const auto readiness =
      dasall::tests::integration::access_support::run_process_capture_split(
          {
              DASALL_CLI_BINARY_PATH,
              "--socket-path",
              socket_path.string(),
              "readiness",
              "--json",
          },
          root);
  assert_equal(0,
               readiness.exit_code,
               "binary unary smoke should keep daemon readiness query successful while exposing the real helper readiness state; stdout=" +
                   readiness.stdout_text + " stderr=" + readiness.stderr_text);
    assert_true(readiness.stdout_text.find("\\\"state\\\":\\\"READY\\\"") !=
            std::string::npos,
              "binary unary smoke should surface daemon ready state in JSON payload after helper composition succeeds; stdout=" +
                  readiness.stdout_text);
    assert_true(readiness.stdout_text.find("\\\"runtime_readiness\\\":\\\"default-ready\\\"") !=
            std::string::npos,
              "binary unary smoke should surface runtime default-ready label after helper composition succeeds; stdout=" +
                  readiness.stdout_text);
  assert_true(readiness.stdout_text.find("stub-ready") == std::string::npos,
              "binary unary smoke should reject stub-ready readiness projection; stdout=" +
                  readiness.stdout_text);

  const auto run = dasall::tests::integration::access_support::run_process_capture_split(
      {
          DASALL_CLI_BINARY_PATH,
          "--socket-path",
          socket_path.string(),
          "run",
          "{\"prompt\":\"binary smoke\"}",
      },
      root);

  const std::string run_output = run.stdout_text + run.stderr_text;
      assert_equal(0,
             run.exit_code,
             "binary unary smoke should complete through the cognition-first daemon path; output=" +
               run_output + " artifact_path=" + log_path.string() +
               " daemon_log=" + daemon.read_log());
  assert_true(run_output.find("[dasall-cli] run: completed") != std::string::npos,
              "binary unary smoke should surface completed daemon envelope; output=" +
                  run_output);
  assert_true(run_output.find("response=") != std::string::npos,
              "binary unary smoke should surface a non-empty runtime response; output=" +
                  run_output);
      assert_true(run_output.find("runtime unary integration completed:") != std::string::npos,
            "binary unary smoke should surface response-builder completion text when cognition-first is forced; output=" +
              run_output);
  assert_true(run_output.find("stub-ready") == std::string::npos &&
              run_output.find("stub_runtime_path") == std::string::npos &&
              run_output.find("llm.origin=") == std::string::npos,
              "binary unary smoke should not fall back to stub runtime path; output=" +
                  run_output);
      assert_true(run.stderr_text.empty(),
            "binary unary smoke should keep successful human output off stderr; stderr=" +
              run.stderr_text);

  const auto json_run =
    dasall::tests::integration::access_support::run_process_capture_split(
      {
        DASALL_CLI_BINARY_PATH,
        "--socket-path",
        socket_path.string(),
        "run",
        "{\"prompt\":\"binary smoke json\"}",
        "--json",
        "--request-id",
        "cli-013-sync",
        "--trace-id",
        "cli-013-sync-trace",
      },
      root);

  assert_equal(0,
               json_run.exit_code,
               "binary unary smoke should keep JSON cognition-first run completed; stdout=" +
                   json_run.stdout_text + " stderr=" + json_run.stderr_text);
  assert_true(json_run.stderr_text.empty(),
        "binary unary smoke should keep JSON output off stderr; stderr=" +
          json_run.stderr_text);
  assert_true(json_run.stdout_text.find("\"disposition\":\"completed\"") !=
          std::string::npos,
        "binary unary smoke should keep completed disposition in JSON output; stdout=" +
          json_run.stdout_text);
  assert_true(json_run.stdout_text.find("\"request_id\":\"cli-013-sync\"") !=
          std::string::npos,
        "binary unary smoke should preserve explicit request_id in JSON output; stdout=" +
          json_run.stdout_text);
  assert_true(json_run.stdout_text.find("\"trace_id\":\"cli-013-sync-trace\"") !=
          std::string::npos,
        "binary unary smoke should preserve explicit trace_id in JSON output; stdout=" +
          json_run.stdout_text);
  assert_true(json_run.stdout_text.find("runtime unary integration completed:") !=
                  std::string::npos,
              "binary unary smoke JSON should retain the cognition response-builder text; stdout=" +
                  json_run.stdout_text);
  assert_true(json_run.stdout_text.find("stub-ready") == std::string::npos &&
                  json_run.stdout_text.find("stub_runtime_path") == std::string::npos &&
                  json_run.stdout_text.find("llm.origin=") == std::string::npos,
              "binary unary smoke JSON should not expose stub runtime path; stdout=" +
                  json_run.stdout_text);

  const auto daemon_exit_code = daemon.stop();
  const auto daemon_log = daemon.read_log();
  assert_equal(0, daemon_exit_code,
           "binary unary smoke should stop the daemon cleanly; artifact_path=" + log_path.string() +
             " daemon_log=" + daemon_log);
  assert_true(daemon_log.find("[dasall-daemon] runtime readiness=") != std::string::npos,
              "binary unary smoke should pass through the real daemon main init path; daemon_log=" +
                  daemon_log);
  assert_true(daemon_log.find("[dasall-daemon] runtime readiness=default-ready") !=
          std::string::npos,
        "binary unary smoke should log default-ready instead of accepted-only readiness; daemon_log=" +
          daemon_log);
  assert_true(daemon_log.find("runtime readiness=stub-ready") == std::string::npos,
        "binary unary smoke should fail if daemon main falls back to stub-ready; daemon_log=" +
          daemon_log);
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