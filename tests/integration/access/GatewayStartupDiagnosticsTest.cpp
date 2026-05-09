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

void gateway_startup_diagnostics_report_stage_error_code_and_artifact_path() {
  ScopedTempDirectory temp_root("dasall-gateway-startup-diag");
  const auto artifact_path = temp_root.path() / "gateway-startup.log";

  const auto result = dasall::tests::integration::access_support::run_process_capture_split(
      {
          DASALL_GATEWAY_BINARY_PATH,
          "--profile-id=",
      },
      temp_root.path());
  const std::string artifact_text = result.stdout_text + result.stderr_text;
  write_artifact(artifact_path, artifact_text);

  assert_equal(1,
               result.exit_code,
               "gateway startup diagnostics should fail-closed on invalid profile input; artifact_path=" +
                   artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("[dasall_gateway] startup failure") != std::string::npos,
              "gateway startup diagnostics should emit the unified startup failure prefix; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("stage=runtime-policy-load") != std::string::npos,
              "gateway startup diagnostics should report runtime-policy-load stage; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("error_code=PRF_E_SCHEMA_INVALID") != std::string::npos,
              "gateway startup diagnostics should report the profile error code; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("trace_id=startup:gateway:runtime-policy-load") !=
                  std::string::npos,
              "gateway startup diagnostics should report a deterministic startup trace id; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("requested_profile=") != std::string::npos,
              "gateway startup diagnostics should report the requested profile slot; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("listen_port=8080") != std::string::npos,
              "gateway startup diagnostics should report the requested listen port; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("assets_root=") != std::string::npos,
              "gateway startup diagnostics should report assets_root; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("profiles_root=") != std::string::npos,
              "gateway startup diagnostics should report profiles_root; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
  assert_true(result.stderr_text.find("detail=runtime policy snapshot load failed for requested profile") !=
                  std::string::npos,
              "gateway startup diagnostics should preserve the profile load detail; artifact_path=" +
                  artifact_path.string() + " artifact=" + artifact_text);
}

}  // namespace

int main() {
  try {
    gateway_startup_diagnostics_report_stage_error_code_and_artifact_path();
  } catch (const std::exception& ex) {
    std::cerr << "[GatewayStartupDiagnosticsTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
