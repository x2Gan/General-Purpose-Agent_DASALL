#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#include "data/DaemonTuiDataSource.h"
#include "ipc/TuiIpcController.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_INTEGRATION_LOCAL_CMAKE
#define DASALL_TUI_INTEGRATION_LOCAL_CMAKE \
  "/home/gangan/DASALL/tests/integration/tui/CMakeLists.txt"
#endif

#ifndef DASALL_APPS_TUI_MAIN
#define DASALL_APPS_TUI_MAIN "/home/gangan/DASALL/apps/tui/src/main.cpp"
#endif

namespace {

class ScopedEnvironmentValue {
 public:
  ScopedEnvironmentValue(const char* name, std::optional<std::string> value)
      : name_(name) {
    const char* existing = std::getenv(name_);
    if (existing != nullptr) {
      original_value_ = existing;
    }

    if (value.has_value()) {
      ::setenv(name_, value->c_str(), 1);
    } else {
      ::unsetenv(name_);
    }
  }

  ScopedEnvironmentValue(const ScopedEnvironmentValue&) = delete;
  ScopedEnvironmentValue& operator=(const ScopedEnvironmentValue&) = delete;

  ~ScopedEnvironmentValue() {
    if (original_value_.has_value()) {
      ::setenv(name_, original_value_->c_str(), 1);
    } else {
      ::unsetenv(name_);
    }
  }

 private:
  const char* name_;
  std::optional<std::string> original_value_;
};

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void test_socket_override_helper_defaults_and_overrides() {
  using dasall::tests::support::assert_equal;

  {
    ScopedEnvironmentValue cleared(
        dasall::tui::data::kTuiDaemonSocketOverrideEnv.data(), std::nullopt);
    const auto options =
        dasall::tui::data::resolve_daemon_tui_controller_options_from_environment();
    assert_equal(std::string(dasall::tui::ipc::kTuiDefaultDaemonSocketPath),
                 options.socket_path,
                 "missing socket override env should preserve the production default socket path");
  }

  {
    ScopedEnvironmentValue overridden(
        dasall::tui::data::kTuiDaemonSocketOverrideEnv.data(),
        std::string("/tmp/dasall-tui-039.sock"));
    const auto options =
        dasall::tui::data::resolve_daemon_tui_controller_options_from_environment();
    assert_equal(std::string("/tmp/dasall-tui-039.sock"),
                 options.socket_path,
                 "socket override env should replace the production default socket path");
  }

  {
    ScopedEnvironmentValue blank(
        dasall::tui::data::kTuiDaemonSocketOverrideEnv.data(), std::string());
    const auto options =
        dasall::tui::data::resolve_daemon_tui_controller_options_from_environment();
    assert_equal(std::string(dasall::tui::ipc::kTuiDefaultDaemonSocketPath),
                 options.socket_path,
                 "blank socket override env should fail closed back to the production default socket path");
  }
}

void test_formal_entrypoint_uses_socket_override_helper() {
  using dasall::tests::support::assert_true;

  const std::string main_text =
      read_text_file(std::filesystem::path{DASALL_APPS_TUI_MAIN});

  assert_true(
      main_text.find("resolve_daemon_tui_controller_options_from_environment()") !=
          std::string::npos,
      "formal main.cpp should derive daemon controller options from the socket override helper");
  assert_true(main_text.find("std::make_unique<dasall::tui::data::DaemonTuiDataSource>()") ==
                  std::string::npos,
              "formal main.cpp should not hard-code a default-constructed daemon data source without socket override seam");
}

void test_integration_cmake_registers_socket_override_test() {
  using dasall::tests::support::assert_true;

  const std::string local_cmake =
      read_text_file(std::filesystem::path{DASALL_TUI_INTEGRATION_LOCAL_CMAKE});

  assert_true(local_cmake.find("dasall_tui_socket_override_integration_test") !=
                  std::string::npos,
              "tui integration CMake should declare the socket override integration target");
  assert_true(local_cmake.find("DasallTuiSocketOverrideTest") != std::string::npos,
              "tui integration CMake should register the socket override integration test name");
}

}  // namespace

int main() {
  try {
    test_socket_override_helper_defaults_and_overrides();
    test_formal_entrypoint_uses_socket_override_helper();
    test_integration_cmake_registers_socket_override_test();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}