#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include <cstdlib>

#include <unistd.h>

#include "CliBinaryTestSupport.h"
#include "support/TestAssertions.h"

#ifndef DASALL_GATEWAY_MISSING_BACKEND_BINARY_PATH
#error "DASALL_GATEWAY_MISSING_BACKEND_BINARY_PATH must be defined"
#endif

namespace {

namespace fs = std::filesystem;

constexpr char kRuntimeStateRootOverrideEnv[] =
    "DASALL_RUNTIME_STATE_ROOT_OVERRIDE";

using dasall::tests::integration::access_support::ProcessResult;
using dasall::tests::integration::access_support::run_process_capture_split;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class ScopedEnvironmentOverride {
 public:
  ScopedEnvironmentOverride(std::string name, std::string value)
      : name_(std::move(name)) {
    if (const char* previous = ::getenv(name_.c_str()); previous != nullptr) {
      previous_value_ = previous;
    }
    ::setenv(name_.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvironmentOverride() {
    if (previous_value_.has_value()) {
      ::setenv(name_.c_str(), previous_value_->c_str(), 1);
      return;
    }

    ::unsetenv(name_.c_str());
  }

 private:
  std::string name_;
  std::optional<std::string> previous_value_;
};

class ScopedTempDirectory {
 public:
  explicit ScopedTempDirectory(std::string_view stem)
      : path_(fs::temp_directory_path() /
              (std::string(stem) + "-" + std::to_string(::getpid()))) {
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

void gateway_binary_missing_backend_fails_closed_at_process_boundary() {
  ScopedTempDirectory temp_root("dasall-gateway-binary-missing-backend");
  const auto state_root = temp_root.path() / "state";
  fs::create_directories(state_root);
  ScopedEnvironmentOverride state_root_override(
      kRuntimeStateRootOverrideEnv,
      state_root.string());

  const ProcessResult result = run_process_capture_split(
      {DASALL_GATEWAY_MISSING_BACKEND_BINARY_PATH},
      temp_root.path());

  assert_equal(1,
               result.exit_code,
               "gateway binary missing-backend fixture should fail closed with exit code 1; stdout=" +
                   result.stdout_text + " stderr=" + result.stderr_text);
  assert_true(result.stdout_text.empty(),
              "gateway binary missing-backend fixture should not claim startup success on stdout; stdout=" +
                  result.stdout_text);
  assert_true(
      result.stderr_text.find(
          "[dasall_gateway] AccessGateway init failed: production submit pipeline unavailable") !=
          std::string::npos,
      "gateway binary missing-backend fixture should surface the fail-closed init message on stderr; stderr=" +
          result.stderr_text);
}

}  // namespace

int main() {
  try {
    gateway_binary_missing_backend_fails_closed_at_process_boundary();
  } catch (const std::exception& ex) {
    std::cerr << "[GatewayBinaryMissingBackendRegressionTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}