#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/include/LLMManagerResult.h"
#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/execution/ResponseNormalizer.h"
#include "../../../llm/src/prompt/PromptPipeline.h"
#include "../../../llm/src/prompt/PromptRegistry.h"

#include "../../mocks/include/MockLLMAdapter.h"
#include "../../unit/llm/ModelRouterTestSupport.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::llm::AdapterCallResult;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMManager;
using dasall::llm::LLMManagerResult;
using dasall::llm::LLMSubsystemConfig;
using dasall::llm::ModelSelectionHint;
using dasall::llm::prompt::PromptPipeline;
using dasall::llm::prompt::PromptRegistry;
using dasall::llm::prompt::PromptRegistryConfig;
using dasall::llm::provider::ProviderCatalogSnapshot;
using dasall::tests::mocks::MockLLMAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr std::string_view kResolvedRoute = "deepseek-prod/deepseek-chat";

class TempDirectory {
 public:
  explicit TempDirectory(const std::string& prefix) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            (prefix + "_" + std::to_string(unique));
    std::filesystem::create_directories(path_);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << content;
}

void create_prompt_package(const std::filesystem::path& root,
                           const std::string& version,
                           const std::string& source_label) {
  const std::filesystem::path package_root = root / "planner" / version;

  write_file(package_root / "manifest.yaml",
             "schema_version: \"1\"\n"
             "min_loader_version: \"1\"\n"
             "package_id: planner." + version + "\n"
             "prompt_id: planner\n"
             "version: \"" + version + "\"\n"
             "stage: planning\n"
             "eval_status: stable\n"
             "release_scope: stable\n"
             "output_schema_ref: schema://planner/default\n"
             "trusted_source: profiles\n"
             "default_release: true\n"
             "language: zh-cn\n"
             "task_types:\n"
             "  - plan\n"
             "tags:\n"
             "  - planner\n"
             "  - " + source_label + "\n");
  write_file(package_root / "system.md", source_label + " system");
  write_file(package_root / "task.md", source_label + " task");
}

void corrupt_prompt_manifest(const std::filesystem::path& root,
                             const std::string& version) {
  write_file(root / "planner" / version / "manifest.yaml",
             "schema_version: \"2\"\n"
             "min_loader_version: \"1\"\n"
             "prompt_id: planner\n"
             "version: \"" + version + "\"\n"
             "stage: planning\n"
             "eval_status: stable\n"
             "release_scope: stable\n"
             "output_schema_ref: schema://planner/default\n"
             "trusted_source: profiles\n"
             "default_release: true\n"
             "language: zh-cn\n"
             "task_types:\n"
             "  - plan\n"
             "tags:\n"
             "  - planner\n");
}

ProviderCatalogSnapshot make_chat_only_catalog() {
  auto catalog = dasall::llm::test_support::make_default_catalog();
  catalog.models.erase(
      std::remove_if(catalog.models.begin(),
                     catalog.models.end(),
                     [](const auto& model) {
                       return !(model.summary.provider_id == "deepseek-prod" &&
                                model.summary.model_id == "deepseek-chat");
                     }),
      catalog.models.end());
  return catalog;
}

dasall::llm::route::AdapterRegistration make_registration(
    std::string provider_id,
    std::string model_id,
    std::string adapter_id,
    std::string deployment_type,
    std::vector<std::string> capability_tags,
    std::shared_ptr<MockLLMAdapter> adapter) {
  return dasall::llm::route::AdapterRegistration{
      .provider_id = std::move(provider_id),
      .model_id = std::move(model_id),
      .adapter_id = std::move(adapter_id),
      .deployment_type = std::move(deployment_type),
      .capability_tags = std::move(capability_tags),
      .supports_streaming = false,
      .adapter = std::move(adapter),
  };
}

LLMSubsystemConfig make_config(const std::filesystem::path& baseline_root,
                               const std::filesystem::path& deployment_root = {},
                               const std::filesystem::path& snapshot_root = {}) {
  auto config = dasall::llm::test_support::make_config(
      "planning", "cloud.default", std::nullopt, {"local.small"}, false, false);
  config.profile_id = "desktop_full";
  config.prompt_asset_sources.baseline_root = baseline_root.generic_string();
  config.prompt_asset_sources.deployment_root =
      deployment_root.empty() ? std::string() : deployment_root.generic_string();
  config.prompt_asset_sources.snapshot_cache_root =
      snapshot_root.empty() ? std::string() : snapshot_root.generic_string();
  config.prompt_asset_sources.cache_ttl_ms = snapshot_root.empty() ? 0U : 60000U;
  config.prompt_selector_overlay.active_scene.clear();
  config.prompt_selector_overlay.active_persona.clear();
  return config;
}

PromptRegistryConfig make_registry_config(const LLMSubsystemConfig& config) {
  return PromptRegistryConfig{
      .asset_sources = config.prompt_asset_sources,
      .trusted_sources = config.trusted_sources,
  };
}

LLMGenerateRequest make_request(std::string request_id, std::string llm_call_id) {
  LLMRequest request;
  request.request_id = std::move(request_id);
  request.llm_call_id = std::move(llm_call_id);
  request.model_route = "cloud.default";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"validate prompt source switching"};
  request.created_at = 1712966404000LL;
  request.output_schema_ref = "schema://planner/default";
  request.response_format = "json_object";
  request.max_output_tokens = 512U;
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = 4096U,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{"integration", "prompt-source-switch"};

  return LLMGenerateRequest{
      .stage = "planning",
      .task_type = "plan",
      .request = std::move(request),
      .prompt_release_id_override = std::nullopt,
      .selection_hint = std::make_shared<const ModelSelectionHint>(ModelSelectionHint{
          .stage = "planning",
          .task_type = "plan",
          .complexity_tier = "standard",
          .latency_sla_tier = "interactive",
          .budget_tier = "balanced",
          .requires_tools = false,
          .requires_reasoning = false,
          .prefers_visible_reasoning = false,
          .estimated_input_tokens = 1024U,
          .target_output_tokens = 512U,
          .previous_route_failures = 0U,
      }),
  };
}

AdapterCallResult make_success_result(const LLMRequest& request) {
  LLMResponse response;
  response.request_id = request.request_id;
  response.llm_call_id = request.llm_call_id;
  response.response_kind = LLMResponseKind::DirectResponse;
  response.content_payload = request.prompt_version.value_or(std::string("missing-prompt-version"));
  response.finish_reason = "stop";

  AdapterCallResult result;
  result.response = std::move(response);
  result.provider_diagnostics.provider_trace_id = "trace-032-prompt-source";
  return result;
}

struct PromptSourceSwitchFixture {
  std::shared_ptr<PromptRegistry> prompt_registry = std::make_shared<PromptRegistry>();
  std::shared_ptr<PromptPipeline> prompt_pipeline =
      std::make_shared<PromptPipeline>(prompt_registry);
  std::shared_ptr<dasall::llm::route::ModelRouter> router =
      std::make_shared<dasall::llm::route::ModelRouter>();
  std::shared_ptr<dasall::llm::route::AdapterRegistry> registry =
      std::make_shared<dasall::llm::route::AdapterRegistry>();
  std::shared_ptr<dasall::llm::LLMCallExecutor> executor =
      std::make_shared<dasall::llm::LLMCallExecutor>();
  std::shared_ptr<dasall::llm::execution::ResponseNormalizer> normalizer =
      std::make_shared<dasall::llm::execution::ResponseNormalizer>();
  std::shared_ptr<dasall::llm::UsageAggregator> aggregator =
      std::make_shared<dasall::llm::UsageAggregator>();
  std::shared_ptr<const ProviderCatalogSnapshot> catalog_snapshot =
      std::make_shared<const ProviderCatalogSnapshot>(make_chat_only_catalog());
  std::shared_ptr<MockLLMAdapter> adapter = std::make_shared<MockLLMAdapter>();
  LLMManager manager{prompt_pipeline,
                     router,
                     registry,
                     executor,
                     normalizer,
                     aggregator,
                     catalog_snapshot};

  PromptSourceSwitchFixture() {
    assert_true(
        registry->init(dasall::llm::route::AdapterRegistryConfig{
            .blocked_failure_threshold = 6U,
        }),
        "Prompt source switch integration should initialize AdapterRegistry before manager wiring");

    adapter->set_generate_handler([](const LLMRequest& request) {
      return make_success_result(request);
    });

    assert_true(registry->register_adapter(make_registration(
                    "deepseek-prod",
                    "deepseek-chat",
                    "deepseek-chat-cloud",
                    "cloud",
                    {"cloud", "external", "deepseek"},
                    adapter)),
                "Prompt source switch integration should register the chat route");
  }

  void init_manager(const LLMSubsystemConfig& config) {
    assert_true(manager.init(config),
                "Prompt source switch integration should initialize LLMManager with the requested prompt asset source chain");
  }

  bool reload_registry(const LLMSubsystemConfig& config) {
    return prompt_registry->init(make_registry_config(config));
  }
};

void assert_prompt_source_result(const LLMManagerResult& result,
                                 const std::shared_ptr<MockLLMAdapter>& adapter,
                                 const std::string& expected_prompt_version,
                                 const std::string& expected_system_message,
                                 const std::string& expected_task_message,
                                 std::string_view message_prefix) {
  if (!(result.has_consistent_values() && result.response.has_value())) {
    throw std::runtime_error(std::string(message_prefix) + " did not succeed");
  }

  assert_equal(std::string(kResolvedRoute),
               result.resolved_route,
               std::string(message_prefix) + " should keep the chat route stable while switching prompt sources");
  assert_true(result.response->prompt_version.has_value() &&
                  *result.response->prompt_version == expected_prompt_version &&
                  result.response->prompt_id.has_value() &&
                  *result.response->prompt_id == "planner",
              std::string(message_prefix) + " should stamp the selected prompt identity onto the normalized response");
  assert_true(adapter->last_request().has_value() &&
                  adapter->last_request()->prompt_version.has_value() &&
                  *adapter->last_request()->prompt_version == expected_prompt_version &&
                  adapter->last_request()->messages.has_value() &&
                  adapter->last_request()->messages->size() == 2U &&
                  adapter->last_request()->messages->at(0) ==
                      "system: " + expected_system_message &&
                  adapter->last_request()->messages->at(1) ==
                      "user: " + expected_task_message,
              std::string(message_prefix) + " should compose the expected prompt source content before dispatch");
}

void test_prompt_source_switch_uses_baseline_when_only_baseline_exists() {
  TempDirectory baseline_root("dasall_prompt_source_switch_baseline");
  create_prompt_package(baseline_root.path(), "2026.04.11", "baseline");

  PromptSourceSwitchFixture fixture;
  fixture.init_manager(make_config(baseline_root.path()));

  const auto result = fixture.manager.generate(
      make_request("req-032-baseline", "call-032-baseline"));

  assert_prompt_source_result(result,
                              fixture.adapter,
                              "2026.04.11",
                              "baseline system",
                              "baseline task",
                              "baseline prompt source");
}

void test_prompt_source_switch_prefers_deployment_override_over_baseline() {
  TempDirectory baseline_root("dasall_prompt_source_switch_deployment_baseline");
  TempDirectory deployment_root("dasall_prompt_source_switch_deployment_overlay");
  create_prompt_package(baseline_root.path(), "2026.04.11", "baseline");
  create_prompt_package(deployment_root.path(), "2026.04.12", "deployment");

  PromptSourceSwitchFixture fixture;
  fixture.init_manager(make_config(baseline_root.path(), deployment_root.path()));

  const auto result = fixture.manager.generate(
      make_request("req-032-deployment", "call-032-deployment"));

  assert_prompt_source_result(result,
                              fixture.adapter,
                              "2026.04.12",
                              "deployment system",
                              "deployment task",
                              "deployment override prompt source");
}

void test_prompt_source_switch_prefers_snapshot_over_deployment_and_baseline() {
  TempDirectory baseline_root("dasall_prompt_source_switch_snapshot_baseline");
  TempDirectory deployment_root("dasall_prompt_source_switch_snapshot_deployment");
  TempDirectory snapshot_root("dasall_prompt_source_switch_snapshot_runtime");
  create_prompt_package(baseline_root.path(), "2026.04.11", "baseline");
  create_prompt_package(deployment_root.path(), "2026.04.12", "deployment");
  create_prompt_package(snapshot_root.path(), "2026.04.13", "snapshot");

  PromptSourceSwitchFixture fixture;
  fixture.init_manager(
      make_config(baseline_root.path(), deployment_root.path(), snapshot_root.path()));

  const auto result = fixture.manager.generate(
      make_request("req-032-snapshot", "call-032-snapshot"));

  assert_prompt_source_result(result,
                              fixture.adapter,
                              "2026.04.13",
                              "snapshot system",
                              "snapshot task",
                              "trusted snapshot prompt source");
}

void test_prompt_source_switch_keeps_last_valid_catalog_when_snapshot_turns_invalid() {
  TempDirectory baseline_root("dasall_prompt_source_switch_reload_baseline");
  TempDirectory deployment_root("dasall_prompt_source_switch_reload_deployment");
  TempDirectory snapshot_root("dasall_prompt_source_switch_reload_snapshot");
  create_prompt_package(baseline_root.path(), "2026.04.11", "baseline");
  create_prompt_package(deployment_root.path(), "2026.04.12", "deployment");
  create_prompt_package(snapshot_root.path(), "2026.04.13", "snapshot");

  const auto config =
      make_config(baseline_root.path(), deployment_root.path(), snapshot_root.path());
  PromptSourceSwitchFixture fixture;
  fixture.init_manager(config);

  const auto before_reload = fixture.manager.generate(
      make_request("req-032-reload-before", "call-032-reload-before"));
  assert_prompt_source_result(before_reload,
                              fixture.adapter,
                              "2026.04.13",
                              "snapshot system",
                              "snapshot task",
                              "snapshot prompt source before corruption");

  corrupt_prompt_manifest(snapshot_root.path(), "2026.04.13");
  assert_true(!fixture.reload_registry(config),
              "Prompt source switch integration should surface reload failure when the trusted snapshot becomes invalid");

  const auto after_reload = fixture.manager.generate(
      make_request("req-032-reload-after", "call-032-reload-after"));
  assert_prompt_source_result(after_reload,
                              fixture.adapter,
                              "2026.04.13",
                              "snapshot system",
                              "snapshot task",
                              "prompt source after failed snapshot reload");
}

}  // namespace

int main() {
  try {
    test_prompt_source_switch_uses_baseline_when_only_baseline_exists();
    test_prompt_source_switch_prefers_deployment_override_over_baseline();
    test_prompt_source_switch_prefers_snapshot_over_deployment_and_baseline();
    test_prompt_source_switch_keeps_last_valid_catalog_when_snapshot_turns_invalid();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}