#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

#include "ToolsInstalledProofRunner.h"
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

void tools_installed_proof_runner_collects_live_dataset_evidence() {
  const TempDir assets_root("dasall-tools-installed-proof-assets");
  const TempDir state_root("dasall-tools-installed-proof-state");
  copy_installed_runtime_assets(assets_root.path());

  const auto result = dasall::apps::daemon::collect_tools_installed_proof(
      dasall::apps::daemon::ToolsInstalledProofOptions{
          .requested_profile_id = kDefaultProfileId,
          .deployment_config_path = std::nullopt,
          .readonly_assets_root_override = assets_root.path(),
          .state_root_override = state_root.path(),
      });

  assert_true(result.ok(), "tools installed proof runner should succeed on copied assets");
  assert_true(result.agent_dataset_visible,
              "tools installed proof runner should keep agent.dataset visible");
  assert_true(result.agent_terminal_visible,
              "tools installed proof runner should keep agent.terminal visible");
  assert_true(result.tool_invocation_succeeded,
              "tools installed proof runner should keep the agent.dataset invocation successful");
  assert_true(result.terminal_confirmation_denied,
              "tools installed proof runner should keep the agent.terminal confirmation gate visible in installed proof");
  assert_true(result.terminal_invocation_succeeded,
              "tools installed proof runner should keep the confirmed agent.terminal invocation successful");
  assert_equal(std::string("builtin"),
               result.route_kind,
               "tools installed proof runner should stay on the builtin lane");
  assert_equal(std::string("builtin"),
               result.terminal_route_kind,
               "tools installed proof runner should keep agent.terminal on the builtin lane");
  assert_true(result.payload.find("\"capability_id\":\"agent.dataset\"") !=
                  std::string::npos &&
                  result.payload.find("\"projection\":\"default\"") !=
                      std::string::npos,
              "tools installed proof runner should preserve live services payload markers");
  assert_true(result.terminal_payload.find("\"operation\":\"agent.terminal\"") !=
                  std::string::npos,
              "tools installed proof runner should preserve the live execution payload markers for agent.terminal");
  assert_true(!result.observation_id.empty(),
              "tools installed proof runner should retain the observation id");
  assert_true(!result.observation_digest_summary.empty(),
              "tools installed proof runner should produce an observation digest summary");
  assert_true(result.route_citation_present,
              "tools installed proof runner should preserve route citations in the digest");
  assert_true(result.tool_call_citation_present,
              "tools installed proof runner should preserve tool-call citations in the digest");
  assert_true(result.terminal_projection_present,
              "tools installed proof runner should project agent.terminal into observation and digest together");
  assert_true(result.production_bridge_evidence_present,
              "tools installed proof runner should record the production bridge evidence marker");
  assert_true(result.production_observability_evidence_present,
              "tools installed proof runner should record the production observability marker");
}

}  // namespace

int main() {
  try {
    tools_installed_proof_runner_collects_live_dataset_evidence();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}