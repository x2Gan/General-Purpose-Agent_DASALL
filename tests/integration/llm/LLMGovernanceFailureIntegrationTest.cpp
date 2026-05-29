#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/include/LLMManagerResult.h"
#include "../../../llm/include/prompt/PromptQuery.h"
#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/execution/ResponseNormalizer.h"
#include "../../../llm/src/observability/LLMMetricsBridge.h"
#include "../../../llm/src/observability/LLMTraceBridge.h"
#include "../../../llm/src/prompt/PromptPipeline.h"
#include "../../../llm/src/prompt/PromptRegistry.h"

#include "../../mocks/include/MockLLMAdapter.h"
#include "LLMIntegrationTestSupport.h"
#include "../../unit/llm/ModelRouterTestSupport.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::contracts::ResultCode;
using dasall::llm::AdapterCallResult;
using dasall::llm::LLMFailureCategory;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMManager;
using dasall::llm::LLMManagerResult;
using dasall::llm::LLMSubsystemConfig;
using dasall::llm::ModelSelectionHint;
using dasall::llm::observability::LLMMetricsBridge;
using dasall::llm::observability::LLMTraceBridge;
using dasall::llm::prompt::PromptPolicyDisposition;
using dasall::llm::prompt::PromptPipeline;
using dasall::llm::prompt::PromptQuery;
using dasall::llm::prompt::PromptRegistry;
using dasall::llm::prompt::PromptRegistryResult;
using dasall::llm::provider::ProviderCatalogSnapshot;
using dasall::tests::integration::llm_support::RecordingLogger;
using dasall::tests::integration::llm_support::RecordingMeter;
using dasall::tests::integration::llm_support::RecordingMetricsProvider;
using dasall::tests::integration::llm_support::RecordingTracer;
using dasall::tests::integration::llm_support::find_log_attr;
using dasall::tests::integration::llm_support::find_sample;
using dasall::tests::integration::llm_support::trace_attr_as_string;
using dasall::tests::mocks::MockLLMAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

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
  std::string version = "2026.04.11";
  std::string trusted_source = "profiles";
  std::string system_message = "governance system";
  std::string task_message = "governance task";
};

void write_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << content;
}

void create_prompt_package(const std::filesystem::path& root,
                           const PromptPackageOptions& options) {
  const std::filesystem::path package_root = root / "planner" / options.version;

  write_file(package_root / "manifest.yaml",
             "schema_version: \"1\"\n"
             "min_loader_version: \"1\"\n"
             "package_id: planner." + options.version + "\n"
             "prompt_id: planner\n"
             "version: \"" + options.version + "\"\n"
             "stage: planning\n"
             "eval_status: stable\n"
             "release_scope: stable\n"
             "output_schema_ref: schema://planner/default\n"
             "trusted_source: " + options.trusted_source + "\n"
             "default_release: true\n"
             "language: zh-cn\n"
             "task_types:\n"
             "  - plan\n"
             "tags:\n"
             "  - planner\n"
             "scene_id: general\n"
             "persona_id: planner\n"
             "profile_tags:\n"
             "  - desktop_full\n");
  write_file(package_root / "system.md", options.system_message);
  write_file(package_root / "task.md", options.task_message);
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

LLMSubsystemConfig make_config(const std::filesystem::path& baseline_root) {
  auto config = dasall::llm::test_support::make_config(
      "planning", "cloud.default", std::nullopt, {"local.small"}, false, false);
  config.profile_id = "desktop_full";
  config.prompt_asset_sources.baseline_root = baseline_root.generic_string();
  config.prompt_selector_overlay.active_scene = "general";
  config.prompt_selector_overlay.active_persona = "planner";
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

LLMGenerateRequest make_request(std::string request_id,
                                std::string llm_call_id,
                                std::uint32_t runtime_budget_tokens) {
  LLMRequest request;
  request.request_id = std::move(request_id);
  request.llm_call_id = std::move(llm_call_id);
  request.model_route = "cloud.default";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"validate governance failure"};
  request.created_at = 1712966406000LL;
  request.output_schema_ref = "schema://planner/default";
  request.response_format = "json_object";
  request.max_output_tokens = std::min<std::uint32_t>(runtime_budget_tokens, 8U);
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = runtime_budget_tokens,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{"integration", "governance-failure"};

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
          .estimated_input_tokens = 256U,
          .target_output_tokens = 64U,
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
  result.provider_diagnostics.provider_trace_id = "trace-034-governance-failure";
  return result;
}

bool contains_text(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

struct GovernanceFailureFixture {
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
    std::shared_ptr<RecordingLogger> logger = std::make_shared<RecordingLogger>();
    std::shared_ptr<RecordingMeter> meter = std::make_shared<RecordingMeter>();
    std::shared_ptr<RecordingMetricsProvider> metrics_provider =
      std::make_shared<RecordingMetricsProvider>(meter);
    std::shared_ptr<RecordingTracer> tracer = std::make_shared<RecordingTracer>();
    std::shared_ptr<LLMMetricsBridge> metrics_bridge =
      std::make_shared<LLMMetricsBridge>(logger, metrics_provider);
    std::shared_ptr<LLMTraceBridge> trace_bridge =
      std::make_shared<LLMTraceBridge>(tracer);
  std::shared_ptr<MockLLMAdapter> adapter = std::make_shared<MockLLMAdapter>();
  LLMManager manager{prompt_pipeline,
                     router,
                     registry,
                     executor,
                     normalizer,
                     aggregator,
             catalog_snapshot,
             nullptr,
             metrics_bridge,
             trace_bridge};

  GovernanceFailureFixture() {
    assert_true(
        registry->init(dasall::llm::route::AdapterRegistryConfig{
            .blocked_failure_threshold = 6U,
        }),
        "Governance failure integration should initialize AdapterRegistry before manager wiring");

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
                "Governance failure integration should register the chat route");
  }

  void init_manager(const LLMSubsystemConfig& config) {
    assert_true(manager.init(config),
                "Governance failure integration should initialize LLMManager with the requested prompt governance config");
  }

  PromptRegistryResult select(const LLMSubsystemConfig& config) const {
    return prompt_registry->select(make_query(config));
  }
};

void assert_failure_result(const LLMManagerResult& result,
                           ResultCode expected_code,
                           LLMFailureCategory expected_category,
                           std::string_view expected_message_fragment,
                           std::optional<PromptPolicyDisposition> expected_disposition,
                           bool expected_safe_to_replan,
                           const std::shared_ptr<MockLLMAdapter>& adapter,
                           std::string_view message_prefix) {
  assert_true(result.has_consistent_values() && !result.response.has_value() &&
                  result.code.has_value() && result.error.has_value() &&
                  result.failure_category.has_value(),
              std::string(message_prefix) + " should return a consistent failure result");
  assert_true(*result.code == expected_code,
              std::string(message_prefix) + " should surface the expected result code");
  assert_true(*result.failure_category == expected_category,
              std::string(message_prefix) + " should surface the expected failure category");
  assert_true(contains_text(result.error->details.message, expected_message_fragment),
              std::string(message_prefix) + " should preserve the expected failure reason");
  assert_true(result.governance_disposition == expected_disposition,
              std::string(message_prefix) + " should preserve the expected governance disposition boundary");
  assert_true(result.error->safe_to_replan.value_or(false) == expected_safe_to_replan,
              std::string(message_prefix) + " should expose the expected safe_to_replan runtime handoff");
  assert_true(result.error->details.stage == "llm.manager.generate",
              std::string(message_prefix) + " should fail before the adapter execution stage");
  assert_true(result.attempted_routes.empty(),
              std::string(message_prefix) + " should not attempt any model route when prompt governance fails before dispatch");
  assert_equal(0,
               adapter->generate_call_count(),
               std::string(message_prefix) + " should not dispatch any adapter request");
}

  void assert_failure_observability(const GovernanceFailureFixture& fixture,
                    std::string_view request_id,
                    std::string_view llm_call_id,
                    std::string_view expected_outcome,
                    std::string_view expected_failure_category,
                    std::string_view expected_result_code,
                    std::string_view expected_result_category,
                    std::string_view expected_governance_disposition,
                    std::string_view expected_span_name,
                    std::string_view message_prefix) {
    assert_true(fixture.logger->events.size() == 1U,
          std::string(message_prefix) + " should emit one structured failure log");
    const auto& log_event = fixture.logger->events.front();
    assert_true(find_log_attr(log_event, "request_id") != nullptr &&
            *find_log_attr(log_event, "request_id") == request_id &&
            *find_log_attr(log_event, "llm_call_id") == llm_call_id &&
            *find_log_attr(log_event, "request_mode") == "unary" &&
            *find_log_attr(log_event, "outcome") == expected_outcome &&
            *find_log_attr(log_event, "failure_category") ==
              expected_failure_category &&
            *find_log_attr(log_event, "result_code") == expected_result_code &&
            *find_log_attr(log_event, "result_code_category") ==
              expected_result_category &&
            *find_log_attr(log_event, "error_stage") == "llm.manager.generate" &&
            *find_log_attr(log_event, "route_attempt_count") == "0" &&
            *find_log_attr(log_event, "governance_disposition") ==
              expected_governance_disposition,
          std::string(message_prefix) + " should preserve governance failure identity and classification in structured logging");

    const auto* calls_sample = find_sample(fixture.meter->recorded_samples,
                       "llm_calls_total");
    assert_true(calls_sample != nullptr && calls_sample->labels.outcome == expected_outcome,
          std::string(message_prefix) + " should emit a classified llm call metric");
    assert_equal(1, static_cast<int>(fixture.tracer->started_spans.size()),
           std::string(message_prefix) + " should emit one failure trace span");
    assert_equal(std::string(expected_span_name),
           fixture.tracer->started_spans.front().descriptor.name,
           std::string(message_prefix) + " should map failure category to the owning trace stage");
    assert_true(trace_attr_as_string(fixture.tracer->started_spans.front().descriptor.attrs,
                     "outcome") ==
              std::optional<std::string>(std::string(expected_outcome)) &&
            trace_attr_as_string(fixture.tracer->started_spans.front().descriptor.attrs,
                       "result_code") ==
              std::optional<std::string>(std::string(expected_result_code)),
          std::string(message_prefix) + " should expose low-cardinality failure attributes on trace");
  }

void test_governance_failure_denies_release_outside_allowlist() {
  TempDirectory baseline_root("dasall_governance_failure_allowlist");
  create_prompt_package(baseline_root.path(), {});

  auto config = make_config(baseline_root.path());
  config.allowed_prompt_releases = {"canary"};

  GovernanceFailureFixture fixture;
  fixture.init_manager(config);

  const auto selection = fixture.select(config);
  assert_true(selection.release.has_value() &&
                  selection.selected_version == "2026.04.11" &&
                  selection.selection_reason == "scene_persona_selector",
              "Allowlist governance failure should still complete prompt selection before PromptPolicy denies the release");

  const auto result = fixture.manager.generate(
      make_request("req-034-allowlist", "call-034-allowlist", 512U));

  assert_failure_result(result,
                        ResultCode::PolicyDenied,
                        LLMFailureCategory::PromptGovernance,
                        "prompt_release_not_allowed",
                        PromptPolicyDisposition::Deny,
                        false,
                        fixture.adapter,
                        "allowlist governance failure");
  assert_failure_observability(fixture,
                               "req-034-allowlist",
                               "call-034-allowlist",
                               "rejected",
                               "prompt_governance",
                               "2001",
                               "policy",
                               "deny",
                               "llm.prompt.policy",
                               "allowlist governance failure");
}

void test_governance_failure_rejects_untrusted_prompt_source_before_policy() {
  TempDirectory baseline_root("dasall_governance_failure_trusted_source");
  create_prompt_package(baseline_root.path(), PromptPackageOptions{
                                                .trusted_source = "external_untrusted",
                                            });

  auto config = make_config(baseline_root.path());
  config.trusted_sources = {"profiles"};

  GovernanceFailureFixture fixture;
  fixture.init_manager(config);

  const auto selection = fixture.select(config);
  assert_true(!selection.release.has_value() && selection.code.has_value() &&
                  *selection.code == ResultCode::PolicyDenied &&
                  selection.selection_reason == "trusted_source_rejected",
              "Trusted-source failure should be rejected by PromptRegistry before PromptPolicy evaluation");

  const auto result = fixture.manager.generate(
      make_request("req-034-trusted-source", "call-034-trusted-source", 512U));

  assert_failure_result(result,
                        ResultCode::ValidationFieldMissing,
                        LLMFailureCategory::PromptAsset,
                        "trusted_source_rejected",
                        std::nullopt,
                        false,
                        fixture.adapter,
                        "trusted-source gating failure");
  assert_failure_observability(fixture,
                               "req-034-trusted-source",
                               "call-034-trusted-source",
                               "failure",
                               "prompt_asset",
                               "1001",
                               "validation",
                               "none",
                               "llm.prompt.select",
                               "trusted-source gating failure");
}

void test_governance_failure_surfaces_over_budget_without_adapter_dispatch() {
  TempDirectory baseline_root("dasall_governance_failure_over_budget");
  create_prompt_package(baseline_root.path(),
                        PromptPackageOptions{
                            .system_message = std::string(512U, 's'),
                            .task_message = std::string(512U, 't'),
                        });

  auto config = make_config(baseline_root.path());

  GovernanceFailureFixture fixture;
  fixture.init_manager(config);

  const auto selection = fixture.select(config);
  assert_true(selection.release.has_value() &&
                  selection.selected_version == "2026.04.11",
              "Over-budget governance failure should still complete prompt selection before PromptPolicy rejects the composed payload");

  const auto result = fixture.manager.generate(
      make_request("req-034-over-budget", "call-034-over-budget", 32U));

  assert_failure_result(result,
                        ResultCode::PolicyDenied,
                        LLMFailureCategory::PromptGovernance,
                        "render_budget_exceeded",
                        PromptPolicyDisposition::OverBudget,
                        true,
                        fixture.adapter,
                        "over-budget governance failure");
  assert_failure_observability(fixture,
                               "req-034-over-budget",
                               "call-034-over-budget",
                               "rejected",
                               "prompt_governance",
                               "2001",
                               "policy",
                               "over_budget",
                               "llm.prompt.policy",
                               "over-budget governance failure");
}

}  // namespace

int main() {
  try {
    test_governance_failure_denies_release_outside_allowlist();
    test_governance_failure_rejects_untrusted_prompt_source_before_policy();
    test_governance_failure_surfaces_over_budget_without_adapter_dispatch();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}