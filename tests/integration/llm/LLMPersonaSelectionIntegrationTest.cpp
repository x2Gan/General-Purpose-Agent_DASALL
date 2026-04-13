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
#include "../../../llm/include/prompt/PromptQuery.h"
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
using dasall::llm::prompt::PromptQuery;
using dasall::llm::prompt::PromptRegistry;
using dasall::llm::prompt::PromptRegistryResult;
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

struct PromptPackageOptions {
  std::string version;
  std::string source_label;
  std::string scene_id;
  std::string persona_id;
  std::vector<std::string> profile_tags;
  bool default_release = false;
};

struct PersonaSelectionExpectation {
  std::string selected_version;
  std::string selection_reason;
  std::string system_message;
  std::string task_message;
};

void write_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << content;
}

void append_optional_list(std::string& manifest,
                          const std::string& key,
                          const std::vector<std::string>& values) {
  if (values.empty()) {
    return;
  }

  manifest.append(key);
  manifest.append(":\n");
  for (const auto& value : values) {
    manifest.append("  - ");
    manifest.append(value);
    manifest.push_back('\n');
  }
}

void create_prompt_package(const std::filesystem::path& root,
                           const PromptPackageOptions& options) {
  const std::filesystem::path package_root = root / "planner" / options.version;

  std::string manifest;
  manifest.append("schema_version: \"1\"\n");
  manifest.append("min_loader_version: \"1\"\n");
  manifest.append("package_id: planner.");
  manifest.append(options.version);
  manifest.append("\n");
  manifest.append("prompt_id: planner\n");
  manifest.append("version: \"");
  manifest.append(options.version);
  manifest.append("\"\n");
  manifest.append("stage: planning\n");
  manifest.append("eval_status: stable\n");
  manifest.append("release_scope: stable\n");
  manifest.append("output_schema_ref: schema://planner/default\n");
  manifest.append("trusted_source: profiles\n");
  manifest.append(options.default_release ? "default_release: true\n"
                                          : "default_release: false\n");
  manifest.append("language: zh-cn\n");
  manifest.append("task_types:\n");
  manifest.append("  - plan\n");
  manifest.append("tags:\n");
  manifest.append("  - planner\n");
  manifest.append("  - ");
  manifest.append(options.source_label);
  manifest.append("\n");
  manifest.append("scene_id: ");
  manifest.append(options.scene_id);
  manifest.append("\n");
  manifest.append("persona_id: ");
  manifest.append(options.persona_id);
  manifest.append("\n");
  append_optional_list(manifest, "profile_tags", options.profile_tags);

  write_file(package_root / "manifest.yaml", manifest);
  write_file(package_root / "system.md", options.source_label + " system");
  write_file(package_root / "task.md", options.source_label + " task");
}

void create_persona_prompt_catalog(const std::filesystem::path& root) {
  create_prompt_package(root,
                        PromptPackageOptions{
                            .version = "2026.04.11",
                            .source_label = "general-planner-default",
                            .scene_id = "general",
                            .persona_id = "planner",
                            .profile_tags = {"desktop_full"},
                            .default_release = true,
                        });
  create_prompt_package(root,
                        PromptPackageOptions{
                            .version = "2026.04.12",
                            .source_label = "operator-planner",
                            .scene_id = "operator",
                            .persona_id = "planner",
                            .profile_tags = {"edge_balanced"},
                            .default_release = false,
                        });
  create_prompt_package(root,
                        PromptPackageOptions{
                            .version = "2026.04.13",
                            .source_label = "general-explainer",
                            .scene_id = "general",
                            .persona_id = "explainer",
                            .profile_tags = {"desktop_full"},
                            .default_release = false,
                        });
  create_prompt_package(root,
                        PromptPackageOptions{
                            .version = "2026.04.14",
                            .source_label = "cloud-profile",
                            .scene_id = "profile-default",
                            .persona_id = "profile-default",
                            .profile_tags = {"cloud_full"},
                            .default_release = false,
                        });
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
                               std::string scene_id,
                               std::string persona_id,
                               std::string profile_id) {
  auto config = dasall::llm::test_support::make_config(
      "planning", "cloud.default", std::nullopt, {"local.small"}, false, false);
  config.profile_id = std::move(profile_id);
  config.prompt_asset_sources.baseline_root = baseline_root.generic_string();
  config.prompt_selector_overlay.active_scene = std::move(scene_id);
  config.prompt_selector_overlay.active_persona = std::move(persona_id);
  return config;
}

PromptQuery make_query(const LLMSubsystemConfig& config) {
  return PromptQuery{
      .stage = "planning",
      .task_type = "plan",
      .language = "zh-cn",
      .model_family = std::string(),
      .prompt_release_id = std::string(),
      .scene_id = config.prompt_selector_overlay.active_scene,
      .persona_id = config.prompt_selector_overlay.active_persona,
      .profile_id = config.profile_id,
      .available_tools = {},
      .trusted_sources = config.trusted_sources,
  };
}

LLMGenerateRequest make_request(std::string request_id, std::string llm_call_id) {
  LLMRequest request;
  request.request_id = std::move(request_id);
  request.llm_call_id = std::move(llm_call_id);
  request.model_route = "cloud.default";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"validate persona selection"};
  request.created_at = 1712966405000LL;
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
  request.tags = std::vector<std::string>{"integration", "persona-selection"};

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
  response.content_payload =
      request.prompt_version.value_or(std::string("missing-prompt-version"));
  response.finish_reason = "stop";

  AdapterCallResult result;
  result.response = std::move(response);
  result.provider_diagnostics.provider_trace_id = "trace-033-persona-selection";
  return result;
}

struct PersonaSelectionFixture {
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

  PersonaSelectionFixture() {
    assert_true(
        registry->init(dasall::llm::route::AdapterRegistryConfig{
            .blocked_failure_threshold = 6U,
        }),
        "Persona selection integration should initialize AdapterRegistry before manager wiring");

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
                "Persona selection integration should register the chat route");
  }

  void init_manager(const LLMSubsystemConfig& config) {
    assert_true(manager.init(config),
                "Persona selection integration should initialize LLMManager with the requested scene/persona selector");
  }

  PromptRegistryResult select(const LLMSubsystemConfig& config) const {
    return prompt_registry->select(make_query(config));
  }
};

void assert_selection_anchors(const PromptRegistryResult& selection,
                              const PersonaSelectionExpectation& expectation,
                              std::string_view message_prefix) {
  assert_true(selection.has_consistent_values() && selection.release.has_value(),
              std::string(message_prefix) + " should resolve a consistent prompt release");
  assert_equal(std::string("planner"),
               selection.selected_prompt_id,
               std::string(message_prefix) + " should keep the planner prompt family");
  assert_equal(expectation.selected_version,
               selection.selected_version,
               std::string(message_prefix) + " should select the expected prompt version");
  assert_equal(expectation.selection_reason,
               selection.selection_reason,
               std::string(message_prefix) + " should expose the expected selection reason");
}

void assert_manager_result(const LLMManagerResult& result,
                           const std::shared_ptr<MockLLMAdapter>& adapter,
                           const PersonaSelectionExpectation& expectation,
                           std::string_view message_prefix) {
  if (!(result.has_consistent_values() && result.response.has_value())) {
    throw std::runtime_error(std::string(message_prefix) + " did not succeed");
  }

  assert_equal(std::string(kResolvedRoute),
               result.resolved_route,
               std::string(message_prefix) + " should keep the chat route stable while switching personas");
  assert_true(result.response->prompt_id.has_value() &&
                  *result.response->prompt_id == "planner" &&
                  result.response->prompt_version.has_value() &&
                  *result.response->prompt_version == expectation.selected_version,
              std::string(message_prefix) + " should stamp the selected persona prompt identity onto the normalized response");
  assert_true(adapter->last_request().has_value() &&
                  adapter->last_request()->prompt_version.has_value() &&
                  *adapter->last_request()->prompt_version == expectation.selected_version &&
                  adapter->last_request()->messages.has_value() &&
                  adapter->last_request()->messages->size() == 2U &&
                  adapter->last_request()->messages->at(0) ==
                      "system: " + expectation.system_message &&
                  adapter->last_request()->messages->at(1) ==
                      "user: " + expectation.task_message,
              std::string(message_prefix) + " should compose the expected scene/persona prompt content before dispatch");
}

void run_persona_case(const LLMSubsystemConfig& config,
                      const PersonaSelectionExpectation& expectation,
                      std::string request_id,
                      std::string llm_call_id,
                      std::string_view message_prefix) {
  PersonaSelectionFixture fixture;
  fixture.init_manager(config);

  const auto selection = fixture.select(config);
  assert_selection_anchors(selection, expectation, message_prefix);

  const auto result = fixture.manager.generate(
      make_request(std::move(request_id), std::move(llm_call_id)));
  assert_manager_result(result, fixture.adapter, expectation, message_prefix);
}

void test_persona_selection_prefers_scene_persona_release_over_default() {
  TempDirectory baseline_root("dasall_persona_selection_scene_persona");
  create_persona_prompt_catalog(baseline_root.path());

  run_persona_case(make_config(baseline_root.path(), "operator", "planner", "edge_balanced"),
                   PersonaSelectionExpectation{
                       .selected_version = "2026.04.12",
                       .selection_reason = "scene_persona_selector",
                       .system_message = "operator-planner system",
                       .task_message = "operator-planner task",
                   },
                   "req-033-scene-persona",
                   "call-033-scene-persona",
                   "scene/persona selector");
}

void test_persona_selection_prefers_persona_variant_within_same_stage() {
  TempDirectory baseline_root("dasall_persona_selection_persona_variant");
  create_persona_prompt_catalog(baseline_root.path());

  run_persona_case(make_config(baseline_root.path(), "general", "explainer", "desktop_full"),
                   PersonaSelectionExpectation{
                       .selected_version = "2026.04.13",
                       .selection_reason = "scene_persona_selector",
                       .system_message = "general-explainer system",
                       .task_message = "general-explainer task",
                   },
                   "req-033-persona-variant",
                   "call-033-persona-variant",
                   "persona variant selector");
}

void test_persona_selection_falls_back_to_profile_selector_when_scene_persona_missing() {
  TempDirectory baseline_root("dasall_persona_selection_profile_fallback");
  create_persona_prompt_catalog(baseline_root.path());

  run_persona_case(make_config(baseline_root.path(), "market", "reviewer", "cloud_full"),
                   PersonaSelectionExpectation{
                       .selected_version = "2026.04.14",
                       .selection_reason = "profile_selector",
                       .system_message = "cloud-profile system",
                       .task_message = "cloud-profile task",
                   },
                   "req-033-profile-fallback",
                   "call-033-profile-fallback",
                   "profile fallback selector");
}

void test_persona_selection_falls_back_to_default_release_when_selectors_miss() {
  TempDirectory baseline_root("dasall_persona_selection_default_fallback");
  create_persona_prompt_catalog(baseline_root.path());

  run_persona_case(make_config(baseline_root.path(), "market", "reviewer", "factory_test"),
                   PersonaSelectionExpectation{
                       .selected_version = "2026.04.11",
                       .selection_reason = "default_release",
                       .system_message = "general-planner-default system",
                       .task_message = "general-planner-default task",
                   },
                   "req-033-default-fallback",
                   "call-033-default-fallback",
                   "default fallback selector");
}

}  // namespace

int main() {
  try {
    test_persona_selection_prefers_scene_persona_release_over_default();
    test_persona_selection_prefers_persona_variant_within_same_stage();
    test_persona_selection_falls_back_to_profile_selector_when_scene_persona_missing();
    test_persona_selection_falls_back_to_default_release_when_selectors_miss();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}