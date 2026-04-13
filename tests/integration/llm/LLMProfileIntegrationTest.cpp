#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/include/LLMManagerResult.h"
#include "../../../llm/include/LLMSubsystemConfig.h"
#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/execution/ResponseNormalizer.h"
#include "../../../llm/src/prompt/PromptPipeline.h"
#include "../../../llm/src/prompt/PromptRegistry.h"
#include "../../../profiles/include/RuntimePolicySnapshot.h"

#include "../../mocks/include/MockLLMAdapter.h"
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
using dasall::llm::LLMSubsystemConfigOverlay;
using dasall::llm::ModelSelectionHint;
using dasall::llm::project_llm_subsystem_config;
using dasall::llm::prompt::PromptPipeline;
using dasall::llm::prompt::PromptRegistry;
using dasall::llm::provider::ProviderCatalogSnapshot;
using dasall::profiles::CapabilityCachePolicy;
using dasall::profiles::DegradePolicy;
using dasall::profiles::ExecutionPolicy;
using dasall::profiles::ModelProfile;
using dasall::profiles::ModelRoutePolicy;
using dasall::profiles::OpsPolicy;
using dasall::profiles::PromptPolicy;
using dasall::profiles::RuntimePolicySnapshot;
using dasall::profiles::TimeoutBudget;
using dasall::profiles::TimeoutPolicy;
using dasall::profiles::TokenBudgetPolicy;
using dasall::tests::mocks::MockLLMAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr std::string_view kCloudResolvedRoute = "deepseek-prod/deepseek-chat";
constexpr std::string_view kLanResolvedRoute = "lan-ollama/lan-general";
constexpr std::string_view kLocalResolvedRoute = "local-runtime/local-small";

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
  std::string release_scope;
  std::string system_message;
  std::string task_message;
};

struct ProfileProjectionSpec {
  std::string profile_id;
  std::string planning_route;
  std::optional<std::string> planning_fallback;
  bool streaming_enabled;
  std::vector<std::string> allowed_prompt_releases;
  std::vector<std::string> trusted_sources;
  std::int64_t llm_timeout_ms;
  std::uint32_t retry_budget;
  std::vector<std::string> degrade_chain;
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
             "release_scope: " + options.release_scope + "\n"
             "output_schema_ref: schema://planner/default\n"
             "trusted_source: profiles\n"
             "default_release: true\n"
             "language: zh-cn\n"
             "task_types:\n"
             "  - plan\n"
             "tags:\n"
             "  - planner\n");
  write_file(package_root / "system.md", options.system_message);
  write_file(package_root / "task.md", options.task_message);
}

RuntimePolicySnapshot make_runtime_policy_snapshot(const ProfileProjectionSpec& spec) {
  return RuntimePolicySnapshot{
      12U,
      spec.profile_id,
      dasall::contracts::RuntimeBudget{
          .max_tokens = 8192U,
          .max_turns = 16U,
          .max_tool_calls = 8U,
          .max_latency_ms = 5000U,
          .max_replan_count = 2U,
      },
      ModelProfile{
          .stage_routes = {
              {"planning",
               ModelRoutePolicy{.route = spec.planning_route,
                                .fallback_route = spec.planning_fallback,
                                .streaming_enabled = spec.streaming_enabled}},
          },
      },
      TokenBudgetPolicy{.max_input_tokens = 4096U,
                        .max_output_tokens = 1024U,
                        .max_history_turns = 8U,
                        .compression_threshold = 3000U},
      PromptPolicy{.allowed_prompt_releases = spec.allowed_prompt_releases,
                   .trusted_sources = spec.trusted_sources,
                   .tool_visibility_rules = {"builtin:all"}},
      CapabilityCachePolicy{.refresh_interval_ms = 1000,
                            .expire_after_ms = 5000,
                            .stale_read_allowed = false,
                            .failure_backoff_ms = 500},
      DegradePolicy{.fallback_chain = spec.degrade_chain,
                    .allow_model_failover = true,
                    .allow_budget_degrade = true},
      TimeoutPolicy{.llm = TimeoutBudget{.timeout_ms = spec.llm_timeout_ms,
                                         .retry_budget = spec.retry_budget,
                                         .circuit_breaker_threshold = 4U},
                    .tool = TimeoutBudget{.timeout_ms = 1500,
                                          .retry_budget = 1U,
                                          .circuit_breaker_threshold = 3U},
                    .mcp = TimeoutBudget{.timeout_ms = 1500,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 3U},
                    .workflow = TimeoutBudget{.timeout_ms = 3000,
                                              .retry_budget = 1U,
                                              .circuit_breaker_threshold = 3U}},
      ExecutionPolicy{.requires_high_risk_confirmation = true,
                      .safe_mode_enabled = true,
                      .audit_level = "full",
                      .allowed_tool_domains = {"builtin", "mcp"}},
      OpsPolicy{.log_level = "info",
                .metrics_granularity = "full",
                .trace_sample_ratio = 0.25,
                .remote_diagnostics_enabled = true,
                .upgrade_strategy = "rolling"},
      3U,
  };
}

LLMSubsystemConfig project_profile_config(const ProfileProjectionSpec& spec,
                                          const std::filesystem::path& prompt_root) {
  LLMSubsystemConfigOverlay overlay;
  overlay.prompt_asset_sources.baseline_root = prompt_root.generic_string();

  const auto config =
      project_llm_subsystem_config(make_runtime_policy_snapshot(spec), overlay);
  if (!config.has_value()) {
    throw std::runtime_error("failed to project llm config for profile: " + spec.profile_id);
  }

  return *config;
}

ProviderCatalogSnapshot make_default_catalog() {
  return dasall::llm::test_support::make_default_catalog();
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

LLMGenerateRequest make_request(std::string request_id,
                                std::string llm_call_id,
                                std::uint32_t runtime_budget_tokens = 1024U) {
  LLMRequest request;
  request.request_id = std::move(request_id);
  request.llm_call_id = std::move(llm_call_id);
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"validate profile projection"};
  request.created_at = 1712966407000LL;
  request.output_schema_ref = "schema://planner/default";
  request.response_format = "json_object";
  request.max_output_tokens = 128U;
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = runtime_budget_tokens,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{"integration", "profile-projection"};

  return LLMGenerateRequest{
      .stage = "planning",
      .task_type = "plan",
      .request = std::move(request),
      .selection_hint = std::make_shared<const ModelSelectionHint>(ModelSelectionHint{
          .stage = "planning",
          .task_type = "plan",
          .complexity_tier = "standard",
          .latency_sla_tier = "interactive",
          .budget_tier = "balanced",
          .requires_tools = false,
          .requires_reasoning = false,
          .prefers_visible_reasoning = false,
          .estimated_input_tokens = 512U,
          .target_output_tokens = 128U,
          .previous_route_failures = 0U,
      }),
  };
}

AdapterCallResult make_success_result(const LLMRequest& request, std::string model_name) {
  LLMResponse response;
  response.request_id = request.request_id;
  response.llm_call_id = request.llm_call_id;
  response.response_kind = LLMResponseKind::DirectResponse;
  response.content_payload = model_name;
  response.finish_reason = "stop";

  AdapterCallResult result;
  result.response = std::move(response);
  result.provider_diagnostics.provider_trace_id = "trace-035-profile-integration";
  return result;
}

struct ProfileFixture {
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
      std::make_shared<const ProviderCatalogSnapshot>(make_default_catalog());
  std::shared_ptr<MockLLMAdapter> cloud_adapter = std::make_shared<MockLLMAdapter>();
  std::shared_ptr<MockLLMAdapter> lan_adapter = std::make_shared<MockLLMAdapter>();
  std::shared_ptr<MockLLMAdapter> local_adapter = std::make_shared<MockLLMAdapter>();
  LLMManager manager{prompt_pipeline,
                     router,
                     registry,
                     executor,
                     normalizer,
                     aggregator,
                     catalog_snapshot};

  ProfileFixture() {
    assert_true(
        registry->init(dasall::llm::route::AdapterRegistryConfig{
            .blocked_failure_threshold = 6U,
        }),
        "Profile integration should initialize AdapterRegistry before manager wiring");

    cloud_adapter->set_generate_handler([](const LLMRequest& request) {
      return make_success_result(request, "deepseek-chat");
    });
    lan_adapter->set_generate_handler([](const LLMRequest& request) {
      return make_success_result(request, "lan-general");
    });
    local_adapter->set_generate_handler([](const LLMRequest& request) {
      return make_success_result(request, "local-small");
    });

    assert_true(registry->register_adapter(make_registration(
                    "deepseek-prod",
                    "deepseek-chat",
                    "deepseek-chat-cloud",
                    "cloud",
                    {"cloud", "external", "deepseek"},
                    cloud_adapter)),
                "Profile integration should register the cloud route");
    assert_true(registry->register_adapter(make_registration(
                    "lan-ollama",
                    "lan-general",
                    "lan-general-adapter",
                    "lan",
                    {"lan", "internal"},
                    lan_adapter)),
                "Profile integration should register the lan route");
    assert_true(registry->register_adapter(make_registration(
                    "local-runtime",
                    "local-small",
                    "local-small-adapter",
                    "local",
                    {"local", "internal"},
                    local_adapter)),
                "Profile integration should register the local route");
  }

  void init_manager(const LLMSubsystemConfig& config) {
    assert_true(manager.init(config),
                "Profile integration should initialize LLMManager from the projected profile config");
  }
};

void assert_success_route(const LLMManagerResult& result,
                          std::string_view expected_route,
                          std::string_view expected_prompt_version,
                          const std::shared_ptr<MockLLMAdapter>& adapter,
                          std::string_view message_prefix) {
  assert_true(result.has_consistent_values() && result.response.has_value(),
              std::string(message_prefix) + " should succeed on the projected profile route");
  assert_true(result.resolved_route == expected_route,
              std::string(message_prefix) + " should resolve the expected route from the projected profile");
  assert_true(result.response->prompt_version.has_value() &&
                  *result.response->prompt_version == expected_prompt_version,
              std::string(message_prefix) + " should stamp the selected prompt version onto the normalized response");
  assert_true(adapter->generate_call_count() == 1 && adapter->last_request().has_value(),
              std::string(message_prefix) + " should dispatch exactly one adapter request on the resolved route");
}

void test_profile_projection_changes_primary_route_between_cloud_full_and_edge_minimal() {
  TempDirectory prompt_root("dasall_profile_integration_routes");
  create_prompt_package(prompt_root.path(),
                        PromptPackageOptions{
                            .version = "2026.04.11",
                            .release_scope = "stable",
                            .system_message = "stable system",
                            .task_message = "stable task",
                        });

  const ProfileProjectionSpec cloud_full{
      .profile_id = "cloud_full",
      .planning_route = "cloud.default",
      .planning_fallback = std::string("lan.general"),
      .streaming_enabled = true,
      .allowed_prompt_releases = {"stable", "canary"},
      .trusted_sources = {"profiles", "infra_config"},
      .llm_timeout_ms = 5000,
      .retry_budget = 2U,
      .degrade_chain = {"lan.general", "local.small"},
  };
  const ProfileProjectionSpec edge_minimal{
      .profile_id = "edge_minimal",
      .planning_route = "local.small",
      .planning_fallback = std::nullopt,
      .streaming_enabled = false,
      .allowed_prompt_releases = {"stable"},
      .trusted_sources = {"profiles"},
      .llm_timeout_ms = 1200,
      .retry_budget = 0U,
      .degrade_chain = {"local.small"},
  };

  ProfileFixture cloud_fixture;
  const auto cloud_config = project_profile_config(cloud_full, prompt_root.path());
  cloud_fixture.init_manager(cloud_config);
  const auto cloud_result = cloud_fixture.manager.generate(
      make_request("req-035-cloud-route", "call-035-cloud-route"));
  assert_success_route(cloud_result,
                       kCloudResolvedRoute,
                       "2026.04.11",
                       cloud_fixture.cloud_adapter,
                       "cloud_full route projection");
  assert_equal(0,
               cloud_fixture.local_adapter->generate_call_count(),
               "cloud_full route projection should not dispatch the local adapter");

  ProfileFixture edge_fixture;
  const auto edge_config = project_profile_config(edge_minimal, prompt_root.path());
  edge_fixture.init_manager(edge_config);
  const auto edge_result = edge_fixture.manager.generate(
      make_request("req-035-edge-route", "call-035-edge-route"));
  assert_success_route(edge_result,
                       kLocalResolvedRoute,
                       "2026.04.11",
                       edge_fixture.local_adapter,
                       "edge_minimal route projection");
  assert_equal(0,
               edge_fixture.cloud_adapter->generate_call_count(),
               "edge_minimal route projection should not dispatch the cloud adapter");
}

void test_profile_projection_changes_prompt_allowlist_between_cloud_full_and_edge_minimal() {
  TempDirectory prompt_root("dasall_profile_integration_allowlist");
  create_prompt_package(prompt_root.path(),
                        PromptPackageOptions{
                            .version = "2026.04.12",
                            .release_scope = "canary",
                            .system_message = "canary system",
                            .task_message = "canary task",
                        });

  const ProfileProjectionSpec cloud_full{
      .profile_id = "cloud_full",
      .planning_route = "cloud.default",
      .planning_fallback = std::string("lan.general"),
      .streaming_enabled = true,
      .allowed_prompt_releases = {"stable", "canary"},
      .trusted_sources = {"profiles", "infra_config"},
      .llm_timeout_ms = 5000,
      .retry_budget = 2U,
      .degrade_chain = {"lan.general", "local.small"},
  };
  const ProfileProjectionSpec edge_minimal{
      .profile_id = "edge_minimal",
      .planning_route = "local.small",
      .planning_fallback = std::nullopt,
      .streaming_enabled = false,
      .allowed_prompt_releases = {"stable"},
      .trusted_sources = {"profiles"},
      .llm_timeout_ms = 1200,
      .retry_budget = 0U,
      .degrade_chain = {"local.small"},
  };

  ProfileFixture cloud_fixture;
  cloud_fixture.init_manager(project_profile_config(cloud_full, prompt_root.path()));
  const auto cloud_result = cloud_fixture.manager.generate(
      make_request("req-035-cloud-allowlist", "call-035-cloud-allowlist"));
  assert_success_route(cloud_result,
                       kCloudResolvedRoute,
                       "2026.04.12",
                       cloud_fixture.cloud_adapter,
                       "cloud_full allowlist projection");

  ProfileFixture edge_fixture;
  edge_fixture.init_manager(project_profile_config(edge_minimal, prompt_root.path()));
  const auto edge_result = edge_fixture.manager.generate(
      make_request("req-035-edge-allowlist", "call-035-edge-allowlist"));
  assert_true(edge_result.has_consistent_values() && !edge_result.response.has_value() &&
                  edge_result.code.has_value() && *edge_result.code == ResultCode::PolicyDenied &&
                  edge_result.failure_category.has_value() &&
                  *edge_result.failure_category == LLMFailureCategory::PromptGovernance &&
                  edge_result.error.has_value() &&
                  edge_result.error->details.message == "prompt_release_not_allowed",
              "edge_minimal allowlist projection should deny the canary prompt release before adapter dispatch");
  assert_true(edge_result.attempted_routes.empty(),
              "edge_minimal allowlist projection should stop before any route attempt");
  assert_equal(0,
               edge_fixture.cloud_adapter->generate_call_count(),
               "edge_minimal allowlist projection should not dispatch any adapter request");
  assert_equal(0,
               edge_fixture.local_adapter->generate_call_count(),
               "edge_minimal allowlist projection should not dispatch the local adapter when prompt governance denies first");
}

void test_profile_projection_changes_adapter_timeout_between_desktop_full_and_edge_balanced() {
  TempDirectory prompt_root("dasall_profile_integration_timeout");
  create_prompt_package(prompt_root.path(),
                        PromptPackageOptions{
                            .version = "2026.04.11",
                            .release_scope = "stable",
                            .system_message = "stable system",
                            .task_message = "stable task",
                        });

  const ProfileProjectionSpec desktop_full{
      .profile_id = "desktop_full",
      .planning_route = "cloud.default",
      .planning_fallback = std::string("lan.general"),
      .streaming_enabled = true,
      .allowed_prompt_releases = {"stable", "canary"},
      .trusted_sources = {"profiles", "infra_config"},
      .llm_timeout_ms = 4500,
      .retry_budget = 2U,
      .degrade_chain = {"lan.general", "local.small"},
  };
  const ProfileProjectionSpec edge_balanced{
      .profile_id = "edge_balanced",
      .planning_route = "lan.general",
      .planning_fallback = std::string("local.small"),
      .streaming_enabled = false,
      .allowed_prompt_releases = {"stable"},
      .trusted_sources = {"profiles"},
      .llm_timeout_ms = 1800,
      .retry_budget = 1U,
      .degrade_chain = {"local.small"},
  };

  ProfileFixture desktop_fixture;
  desktop_fixture.init_manager(project_profile_config(desktop_full, prompt_root.path()));
  const auto desktop_result = desktop_fixture.manager.generate(
      make_request("req-035-desktop-timeout", "call-035-desktop-timeout"));
  assert_success_route(desktop_result,
                       kCloudResolvedRoute,
                       "2026.04.11",
                       desktop_fixture.cloud_adapter,
                       "desktop_full timeout projection");
  assert_true(desktop_fixture.cloud_adapter->last_request().has_value() &&
                  desktop_fixture.cloud_adapter->last_request()->timeout_ms.has_value() &&
                  *desktop_fixture.cloud_adapter->last_request()->timeout_ms == 4500U,
              "desktop_full timeout projection should pass the projected cloud timeout to the adapter request");

  ProfileFixture edge_fixture;
  edge_fixture.init_manager(project_profile_config(edge_balanced, prompt_root.path()));
  const auto edge_result = edge_fixture.manager.generate(
      make_request("req-035-edge-balanced-timeout", "call-035-edge-balanced-timeout"));
  assert_success_route(edge_result,
                       kLanResolvedRoute,
                       "2026.04.11",
                       edge_fixture.lan_adapter,
                       "edge_balanced timeout projection");
  assert_true(edge_fixture.lan_adapter->last_request().has_value() &&
                  edge_fixture.lan_adapter->last_request()->timeout_ms.has_value() &&
                  *edge_fixture.lan_adapter->last_request()->timeout_ms == 1800U,
              "edge_balanced timeout projection should pass the projected lan timeout to the adapter request");
}

}  // namespace

int main() {
  try {
    test_profile_projection_changes_primary_route_between_cloud_full_and_edge_minimal();
    test_profile_projection_changes_prompt_allowlist_between_cloud_full_and_edge_minimal();
    test_profile_projection_changes_adapter_timeout_between_desktop_full_and_edge_balanced();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}