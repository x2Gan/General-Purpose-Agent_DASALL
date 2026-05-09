#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include <unistd.h>

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

void write_artifact(const fs::path& artifact_path, const std::string& contents) {
  std::ofstream stream(artifact_path);
  stream << contents;
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

  assert_equal(1,
               result.exit_code,
               "daemon startup diagnostics should fail-closed on invalid socket parent; artifact_path=" +
                   artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("[dasall-daemon] startup failure") != std::string::npos,
              "daemon startup diagnostics should emit the unified startup failure prefix; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("stage=config-validation") != std::string::npos,
              "daemon startup diagnostics should report config-validation stage; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("error_code=DAEMON_E_CONFIG_VALIDATION_FAILED") !=
                  std::string::npos,
              "daemon startup diagnostics should report config validation error code; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("trace_id=startup:daemon:config-validation") !=
                  std::string::npos,
              "daemon startup diagnostics should report a deterministic startup trace id; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("socket_path=" + socket_path.string()) != std::string::npos,
              "daemon startup diagnostics should report the failing socket path; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("assets_root=") != std::string::npos,
              "daemon startup diagnostics should report assets_root; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("profiles_root=") != std::string::npos,
              "daemon startup diagnostics should report profiles_root; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find(
                  "detail=socket parent directory owner/group does not match daemon socket policy: /tmp") !=
                  std::string::npos,
              "daemon startup diagnostics should preserve the config validation detail; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
}

}  // namespace

int main() {
  try {
    daemon_startup_diagnostics_report_stage_error_code_and_artifact_path();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonStartupDiagnosticsTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
