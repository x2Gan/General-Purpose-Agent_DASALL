#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

#include "RuntimeInstalledProofRunner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr char kDefaultProfileId[] = "desktop_full";

[[nodiscard]] std::filesystem::path source_root() {
  return std::filesystem::path(__FILE__)
      .parent_path()
      .parent_path()
      .parent_path()
      .parent_path()
      .parent_path();
}

class TempDir {
 public:
  explicit TempDir(const std::string& stem)
      : path_(std::filesystem::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                                              .count()))) {
    std::filesystem::create_directories(path_);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

  class ScopedEnvVar {
   public:
    ScopedEnvVar(const char* name, std::string value)
        : name_(name), had_old_value_(false) {
      if (const char* old_value = std::getenv(name_); old_value != nullptr) {
        had_old_value_ = true;
        old_value_ = old_value;
      }

      if (setenv(name_, value.c_str(), 1) != 0) {
        throw std::runtime_error(std::string("failed to set env var: ") + name_);
      }
    }

    ~ScopedEnvVar() {
      if (had_old_value_) {
        (void)setenv(name_, old_value_.c_str(), 1);
        return;
      }

      (void)unsetenv(name_);
    }

   private:
    const char* name_;
    bool had_old_value_;
    std::string old_value_;
  };

void copy_installed_runtime_assets(const std::filesystem::path& assets_root) {
  const auto root = source_root();
  std::filesystem::create_directories(assets_root / "sql");
  std::filesystem::copy(root / "sql" / "memory",
                        assets_root / "sql" / "memory",
                        std::filesystem::copy_options::recursive);
  std::filesystem::copy(root / "profiles",
                        assets_root / "profiles",
                        std::filesystem::copy_options::recursive);
  std::filesystem::create_directories(assets_root / "llm");
  std::filesystem::copy(root / "llm" / "assets" / "providers",
                        assets_root / "llm" / "providers",
                        std::filesystem::copy_options::recursive);
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void runtime_installed_proof_runner_collects_tool_and_recovery_evidence() {
  const TempDir assets_root("dasall-runtime-installed-proof-assets");
  const TempDir state_root("dasall-runtime-installed-proof-state");
  copy_installed_runtime_assets(assets_root.path());

  const auto result = dasall::apps::daemon::collect_runtime_installed_proof(
      dasall::apps::daemon::RuntimeInstalledProofOptions{
          .requested_profile_id = kDefaultProfileId,
          .deployment_config_path = std::nullopt,
          .readonly_assets_root_override = assets_root.path(),
          .state_root_override = state_root.path(),
      });

    assert_true(result.ok(),
          "runtime installed proof runner should succeed on copied assets: " +
            result.error);
  assert_true(result.agent_dataset_visible,
              "runtime installed proof runner should keep agent.dataset visible");
  assert_true(result.agent_terminal_visible,
              "runtime installed proof runner should keep agent.terminal visible");
  assert_equal(std::string("Completed"),
               result.tool_status,
               "runtime installed proof runner should complete the tool-positive probe");
  assert_true(result.tool_task_completed,
              "runtime installed proof runner should mark the tool-positive probe complete");
  assert_equal(std::string("runtime_path:tool_positive"),
               result.tool_runtime_path,
               "runtime installed proof runner should classify the tool probe as tool_positive");
  assert_true(!result.tool_checkpoint_ref.empty(),
              "runtime installed proof runner should materialize a tool-positive checkpoint");
  assert_equal(std::string("PartiallyCompleted"),
               result.waiting_status,
               "runtime installed proof runner should first produce a waiting checkpoint for recovery");
  assert_true(!result.waiting_checkpoint_ref.empty(),
              "runtime installed proof runner should expose a waiting checkpoint anchor");
  assert_equal(std::string("Completed"),
               result.recovery_positive_status,
               "runtime installed proof runner should complete the positive recovery probe");
  assert_true(result.recovery_positive_task_completed,
              "runtime installed proof runner should mark the positive recovery probe complete");
  assert_equal(std::string("runtime_path:recovery_positive"),
               result.recovery_positive_runtime_path,
               "runtime installed proof runner should classify the recovery probe as recovery_positive");
  assert_true(result.recovery_positive_checkpoint_persisted,
              "runtime installed proof runner should persist a terminal recovery checkpoint");
  assert_equal(std::string("Failed"),
               result.recovery_negative_status,
               "runtime installed proof runner should fail a mismatched resume token");
  assert_true(!result.recovery_negative_task_completed,
              "runtime installed proof runner should keep the negative recovery probe incomplete");
  assert_true(result.recovery_negative_binding_rejected,
              "runtime installed proof runner should surface the binding rejection detail");
}

void runtime_installed_proof_runner_defaults_to_install_layout_state_root() {
  const TempDir assets_root("dasall-runtime-installed-proof-default-assets");
  const TempDir state_root("dasall-runtime-installed-proof-default-state");
  copy_installed_runtime_assets(assets_root.path());
  const ScopedEnvVar scoped_state_root("DASALL_STATE_ROOT",
                                       state_root.path().string());

  const auto result = dasall::apps::daemon::collect_runtime_installed_proof(
      dasall::apps::daemon::RuntimeInstalledProofOptions{
          .requested_profile_id = kDefaultProfileId,
          .deployment_config_path = std::nullopt,
          .readonly_assets_root_override = assets_root.path(),
          .state_root_override = std::nullopt,
      });

  assert_true(result.ok(),
              "runtime installed proof runner should use install layout state root by default: " +
                  result.error);
  assert_true(std::filesystem::exists(state_root.path() / "tool-positive" / "logging" /
                                      "runtime.log"),
              "runtime installed proof runner should write tool-positive logs under the install layout state root");
  assert_true(std::filesystem::exists(state_root.path() / "recovery-positive" /
                                      "logging" / "runtime.log"),
              "runtime installed proof runner should write recovery-positive logs under the install layout state root");
  assert_true(std::filesystem::exists(state_root.path() / "recovery-negative" /
                                      "logging" / "runtime.log"),
              "runtime installed proof runner should write recovery-negative logs under the install layout state root");
  const auto tool_positive_log =
      read_text_file(state_root.path() / "tool-positive" / "logging" /
                     "runtime.log");
  assert_true(tool_positive_log.find("\"module\":\"cognition\"") !=
                  std::string::npos,
              "runtime installed proof runner should preserve cognition logging in the tool-positive runtime log");
  assert_true(tool_positive_log.find("runtime.transition") != std::string::npos,
              "runtime installed proof runner should emit runtime logging in the tool-positive runtime log");
}

}  // namespace

int main() {
  try {
    runtime_installed_proof_runner_collects_tool_and_recovery_evidence();
    runtime_installed_proof_runner_defaults_to_install_layout_state_root();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}