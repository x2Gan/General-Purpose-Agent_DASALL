#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "CliBinaryTestSupport.h"
#include "support/TestAssertions.h"

#ifndef DASALL_DAEMON_BINARY_PATH
#error "DASALL_DAEMON_BINARY_PATH must be defined"
#endif

#ifndef DASALL_REPOSITORY_ROOT
#error "DASALL_REPOSITORY_ROOT must be defined"
#endif

namespace {

namespace fs = std::filesystem;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tests::integration::access_support::ProcessResult;

constexpr char kDaemonStartupDiagnosticsForceStageEnv[] =
  "DASALL_DAEMON_STARTUP_DIAGNOSTICS_FORCE_STAGE";

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
    if (const char* current = std::getenv(name)) {
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

class ScopedUnixSocketListener {
 public:
  explicit ScopedUnixSocketListener(const fs::path& socket_path)
      : socket_path_(socket_path) {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
      throw std::runtime_error("failed to create occupied unix socket");
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string raw_path = socket_path_.string();
    if (raw_path.size() >= sizeof(address.sun_path)) {
      throw std::runtime_error("occupied unix socket path is too long");
    }
    std::strncpy(address.sun_path, raw_path.c_str(), sizeof(address.sun_path) - 1U);

    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      throw std::runtime_error("failed to bind occupied unix socket");
    }
    (void)::chmod(socket_path_.c_str(), 0600);
    if (::listen(fd_, 1) != 0) {
      throw std::runtime_error("failed to listen on occupied unix socket");
    }
  }

  ~ScopedUnixSocketListener() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    std::error_code error;
    fs::remove(socket_path_, error);
  }

 private:
  fs::path socket_path_;
  int fd_ = -1;
};

void write_artifact(const fs::path& artifact_path, const std::string& contents) {
  std::ofstream stream(artifact_path);
  stream << contents;
}

void assert_daemon_startup_failure(const ProcessResult& result,
                   const fs::path& artifact_path,
                   const std::string& artifact_text,
                   const std::string& stage,
                   const std::string& error_code) {
  assert_equal(1,
         result.exit_code,
         "daemon startup diagnostics should fail-closed; artifact_path=" +
           artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("[dasall-daemon] startup failure") != std::string::npos,
        "daemon startup diagnostics should emit the unified startup failure prefix; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("stage=" + stage) != std::string::npos,
        "daemon startup diagnostics should report expected stage; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("error_code=" + error_code) != std::string::npos,
        "daemon startup diagnostics should report expected error code; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("trace_id=startup:daemon:" + stage) != std::string::npos,
        "daemon startup diagnostics should report deterministic startup trace id; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("assets_root=") != std::string::npos,
        "daemon startup diagnostics should report assets_root; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("profiles_root=") != std::string::npos,
        "daemon startup diagnostics should report profiles_root; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
}

void daemon_startup_diagnostics_report_stage_error_code_and_artifact_path() {
  ScopedTempDirectory temp_root("dasall-daemon-startup-diag");
  const auto artifact_path = temp_root.path() / "daemon-startup.log";
  const auto socket_path =
      fs::path("/tmp") / ("dasall-startup-diag-" + std::to_string(::getpid()) + ".sock");

  const auto result = dasall::tests::integration::access_support::run_process_capture_split(
      {
          DASALL_DAEMON_BINARY_PATH,
          "--socket-path",
          socket_path.string(),
      },
      temp_root.path());
  const std::string artifact_text = result.stdout_text + result.stderr_text;
  write_artifact(artifact_path, artifact_text);

    assert_daemon_startup_failure(result,
                  artifact_path,
                  artifact_text,
                  "config-validation",
                  "DAEMON_E_CONFIG_VALIDATION_FAILED");
  assert_true(result.stderr_text.find("socket_path=" + socket_path.string()) != std::string::npos,
              "daemon startup diagnostics should report the failing socket path; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find(
                  "detail=socket parent directory owner/group does not match daemon socket policy: /tmp") !=
                  std::string::npos,
              "daemon startup diagnostics should preserve the config validation detail; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
}

  void daemon_startup_diagnostics_cover_forced_runtime_stages() {
    const std::vector<std::pair<std::string, std::string>> cases = {
      {"runtime-dependency-composition", "DAEMON_E_RUNTIME_COMPOSITION_FAILED"},
      {"runtime-init", "DAEMON_E_RUNTIME_INIT_FAILED"},
      {"access-gateway-init", "DAEMON_E_ACCESS_GATEWAY_INIT_FAILED"},
    };

    for (const auto& [stage, error_code] : cases) {
    ScopedTempDirectory temp_root("dasall-daemon-startup-diag-forced");
    const auto artifact_path = temp_root.path() / (stage + ".log");
    const auto socket_path = temp_root.path() / "daemon.sock";
    ScopedEnvVar forced_stage(kDaemonStartupDiagnosticsForceStageEnv, stage);

    const auto result = dasall::tests::integration::access_support::run_process_capture_split(
      {
        DASALL_DAEMON_BINARY_PATH,
        "--socket-path",
        socket_path.string(),
      },
      temp_root.path());
    const std::string artifact_text = result.stdout_text + result.stderr_text;
    write_artifact(artifact_path, artifact_text);

    assert_daemon_startup_failure(result, artifact_path, artifact_text, stage, error_code);
    assert_true(result.stderr_text.find("detail=forced startup diagnostics failure") !=
            std::string::npos,
          "daemon forced startup diagnostics should preserve forced detail; artifact_path=" +
            artifact_path.string() + " artifact=" + artifact_text);
    }
  }

  void daemon_startup_diagnostics_report_listener_bind_stage() {
    ScopedTempDirectory temp_root("dasall-daemon-startup-diag-listener-bind");
    const auto artifact_path = temp_root.path() / "listener-bind.log";
    const auto socket_path = temp_root.path() / "daemon.sock";
    ScopedUnixSocketListener occupied_socket(socket_path);

    const auto result = dasall::tests::integration::access_support::run_process_capture_split(
      {
        DASALL_DAEMON_BINARY_PATH,
        "--socket-path",
        socket_path.string(),
      },
      temp_root.path());
    const std::string artifact_text = result.stdout_text + result.stderr_text;
    write_artifact(artifact_path, artifact_text);

    assert_daemon_startup_failure(result,
                  artifact_path,
                  artifact_text,
                  "listener-bind",
                  "DAEMON_E_LISTENER_BIND_FAILED");
    assert_true(result.stderr_text.find("active unix socket cannot be removed during bind preflight") !=
            std::string::npos,
          "daemon listener-bind diagnostics should preserve active socket detail; artifact_path=" +
            artifact_path.string() + " artifact=" + artifact_text);
  }

}  // namespace

int main() {
  try {
    daemon_startup_diagnostics_report_stage_error_code_and_artifact_path();
    daemon_startup_diagnostics_cover_forced_runtime_stages();
    daemon_startup_diagnostics_report_listener_bind_stage();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonStartupDiagnosticsTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
