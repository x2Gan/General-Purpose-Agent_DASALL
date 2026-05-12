#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "CliBinaryTestSupport.h"
#include "support/TestAssertions.h"

#ifndef DASALL_GATEWAY_BINARY_PATH
#error "DASALL_GATEWAY_BINARY_PATH must be defined"
#endif

#ifndef DASALL_REPOSITORY_ROOT
#error "DASALL_REPOSITORY_ROOT must be defined"
#endif

namespace {

namespace fs = std::filesystem;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tests::integration::access_support::ProcessResult;

constexpr char kGatewayStartupDiagnosticsForceStageEnv[] =
  "DASALL_GATEWAY_STARTUP_DIAGNOSTICS_FORCE_STAGE";
constexpr char kStateRootEnv[] = "DASALL_STATE_ROOT";

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

class ScopedTcpListener {
 public:
  ScopedTcpListener() {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
      throw std::runtime_error("failed to create occupied tcp listener");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = 0;

    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      throw std::runtime_error("failed to bind occupied tcp listener");
    }
    if (::listen(fd_, 1) != 0) {
      throw std::runtime_error("failed to listen on occupied tcp listener");
    }

    socklen_t address_length = sizeof(address);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
      throw std::runtime_error("failed to inspect occupied tcp listener port");
    }
    port_ = ntohs(address.sin_port);
  }

  ~ScopedTcpListener() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  [[nodiscard]] int port() const {
    return port_;
  }

 private:
  int fd_ = -1;
  int port_ = 0;
};

void write_artifact(const fs::path& artifact_path, const std::string& contents) {
  std::ofstream stream(artifact_path);
  stream << contents;
}

void assert_gateway_startup_failure(const ProcessResult& result,
                  const fs::path& artifact_path,
                  const std::string& artifact_text,
                  const std::string& stage,
                  const std::string& error_code) {
  assert_equal(1,
         result.exit_code,
         "gateway startup diagnostics should fail-closed; artifact_path=" +
           artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("[dasall_gateway] startup failure") != std::string::npos,
        "gateway startup diagnostics should emit the unified startup failure prefix; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("stage=" + stage) != std::string::npos,
        "gateway startup diagnostics should report expected stage; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("error_code=" + error_code) != std::string::npos,
        "gateway startup diagnostics should report expected error code; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("trace_id=startup:gateway:" + stage) !=
          std::string::npos,
        "gateway startup diagnostics should report deterministic startup trace id; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("assets_root=") != std::string::npos,
        "gateway startup diagnostics should report assets_root; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("profiles_root=") != std::string::npos,
        "gateway startup diagnostics should report profiles_root; artifact_path=" +
          artifact_path.string() + " artifact=" + artifact_text);
}

void gateway_startup_diagnostics_report_stage_error_code_and_artifact_path() {
  ScopedTempDirectory temp_root("dasall-gateway-startup-diag");
  const auto artifact_path = temp_root.path() / "gateway-startup.log";
  const auto state_root = temp_root.path() / "state";
  fs::create_directories(state_root);
  ScopedEnvVar state_root_override(kStateRootEnv, state_root.string());

  const auto result = dasall::tests::integration::access_support::run_process_capture_split(
      {
          DASALL_GATEWAY_BINARY_PATH,
          "--profile-id=",
      },
      temp_root.path());
  const std::string artifact_text = result.stdout_text + result.stderr_text;
  write_artifact(artifact_path, artifact_text);

    assert_gateway_startup_failure(result,
                   artifact_path,
                   artifact_text,
                   "runtime-policy-load",
                   "PRF_E_SCHEMA_INVALID");
  assert_true(result.stderr_text.find("requested_profile=") != std::string::npos,
              "gateway startup diagnostics should report the requested profile slot; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("listen_port=8080") != std::string::npos,
              "gateway startup diagnostics should report the requested listen port; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("detail=runtime policy snapshot load failed for requested profile") !=
                  std::string::npos,
              "gateway startup diagnostics should preserve the profile load detail; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
}

  void gateway_startup_diagnostics_cover_forced_runtime_stages() {
    const std::vector<std::pair<std::string, std::string>> cases = {
      {"runtime-dependency-composition", "GATEWAY_E_RUNTIME_COMPOSITION_FAILED"},
      {"runtime-init", "GATEWAY_E_RUNTIME_INIT_FAILED"},
      {"access-gateway-init", "GATEWAY_E_ACCESS_GATEWAY_INIT_FAILED"},
    };

    for (const auto& [stage, error_code] : cases) {
    ScopedTempDirectory temp_root("dasall-gateway-startup-diag-forced");
    const auto artifact_path = temp_root.path() / (stage + ".log");
    const auto state_root = temp_root.path() / "state";
    fs::create_directories(state_root);
    ScopedEnvVar forced_stage(kGatewayStartupDiagnosticsForceStageEnv, stage);
    ScopedEnvVar state_root_override(kStateRootEnv, state_root.string());

    const auto result = dasall::tests::integration::access_support::run_process_capture_split(
      {
        DASALL_GATEWAY_BINARY_PATH,
      },
      temp_root.path());
    const std::string artifact_text = result.stdout_text + result.stderr_text;
    write_artifact(artifact_path, artifact_text);

    assert_gateway_startup_failure(result, artifact_path, artifact_text, stage, error_code);
    assert_true(result.stderr_text.find("detail=forced startup diagnostics failure") !=
            std::string::npos,
          "gateway forced startup diagnostics should preserve forced detail; artifact_path=" +
            artifact_path.string() + " artifact=" + artifact_text);
    }
  }

  void gateway_startup_diagnostics_report_listen_stage() {
    ScopedTempDirectory temp_root("dasall-gateway-startup-diag-listen");
    const auto artifact_path = temp_root.path() / "listen.log";
    const auto state_root = temp_root.path() / "state";
    fs::create_directories(state_root);
    ScopedEnvVar state_root_override(kStateRootEnv, state_root.string());
    ScopedTcpListener occupied_port;

    const auto result = dasall::tests::integration::access_support::run_process_capture_split(
      {
        DASALL_GATEWAY_BINARY_PATH,
        "--port",
        std::to_string(occupied_port.port()),
      },
      temp_root.path());
    const std::string artifact_text = result.stdout_text + result.stderr_text;
    write_artifact(artifact_path, artifact_text);

    assert_gateway_startup_failure(result,
                   artifact_path,
                   artifact_text,
                   "listen",
                   "GATEWAY_E_LISTEN_FAILED");
    assert_true(result.stderr_text.find("listen_port=" + std::to_string(occupied_port.port())) !=
            std::string::npos,
          "gateway listen diagnostics should report occupied port; artifact_path=" +
            artifact_path.string() + " artifact=" + artifact_text);
    assert_true(result.stderr_text.find("detail=listen failed on 0.0.0.0:") !=
            std::string::npos,
          "gateway listen diagnostics should preserve bind failure detail; artifact_path=" +
            artifact_path.string() + " artifact=" + artifact_text);
  }

}  // namespace

int main() {
  try {
    gateway_startup_diagnostics_report_stage_error_code_and_artifact_path();
    gateway_startup_diagnostics_cover_forced_runtime_stages();
    gateway_startup_diagnostics_report_listen_stage();
  } catch (const std::exception& ex) {
    std::cerr << "[GatewayStartupDiagnosticsTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
