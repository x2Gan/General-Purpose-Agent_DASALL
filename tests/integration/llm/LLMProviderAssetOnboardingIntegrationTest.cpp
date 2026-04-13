#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/include/LLMAdapterConfig.h"
#include "../../../llm/include/LLMManagerResult.h"
#include "../../../llm/include/LLMSubsystemConfig.h"
#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/execution/ResponseNormalizer.h"
#include "../../../llm/src/prompt/PromptPipeline.h"
#include "../../../llm/src/prompt/PromptRegistry.h"
#include "../../../llm/src/provider/ProviderCatalogRepository.h"
#include "../../../profiles/include/RuntimePolicySnapshot.h"

#include "../../mocks/include/MockLLMAdapter.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::llm::AdapterCallResult;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMManager;
using dasall::llm::LLMManagerResult;
using dasall::llm::LLMSubsystemConfig;
using dasall::llm::LLMSubsystemConfigOverlay;
using dasall::llm::ModelSelectionHint;
using dasall::llm::ProviderCatalogSourceConfig;
using dasall::llm::ProviderRuntimeProjectionView;
using dasall::llm::project_llm_subsystem_config;
using dasall::llm::prompt::PromptPipeline;
using dasall::llm::prompt::PromptRegistry;
using dasall::llm::provider::ProviderCatalogProvider;
using dasall::llm::provider::ProviderCatalogRepository;
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

constexpr std::string_view kDeepSeekRoute = "deepseek-prod/deepseek-chat";
constexpr std::string_view kOpenClawRoute = "openclaw-prod/openclaw-chat";

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
  std::string prompt_id;
  std::string stage;
  std::string task_type;
  std::string release_scope;
  std::string system_message;
  std::string task_message;
};

struct ProviderPackageOptions {
  std::string package_name;
  std::string provider_id;
  std::string display_name;
  std::string adapter_family;
  std::string api_family;
  std::string base_url;
  std::string base_url_alias;
  std::string auth_ref;
  std::string source_version;
  std::string model_id;
  std::string model_display_name;
  std::string model_version;
  std::string tier_family;
  std::string latency_tier;
  std::string cost_tier;
  std::string reasoning_depth_tier;
  std::string model_alias;
  bool activation_flag = true;
};

struct ProfileProjectionSpec {
  std::string profile_id;
  std::string response_route;
  std::optional<std::string> response_fallback;
  std::vector<std::string> allowed_prompt_releases;
  std::vector<std::string> trusted_sources;
  std::int64_t llm_timeout_ms;
  std::uint32_t retry_budget;
};

void write_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << content;
}

std::filesystem::path repo_root() {
  return std::filesystem::absolute(std::filesystem::path(__FILE__))
      .parent_path()
      .parent_path()
      .parent_path()
      .parent_path();
}

std::filesystem::path repo_provider_baseline_root() {
  return repo_root() / "llm" / "assets" / "providers";
}

void create_catalog_index(const std::filesystem::path& root,
                          const std::string& source_version,
                          const std::vector<std::string>& packages) {
  std::ostringstream builder;
  builder << "schema_version: \"1\"\n";
  builder << "default_source_version: " << source_version << "\n";
  builder << "packages:\n";
  for (const auto& package : packages) {
    builder << "  - " << package << "\n";
  }

  write_file(root / "catalog.yaml", builder.str());
}

void create_prompt_package(const std::filesystem::path& root,
                           const PromptPackageOptions& options) {
  const std::filesystem::path package_root = root / options.prompt_id / options.version;

  std::ostringstream builder;
  builder << "schema_version: \"1\"\n";
  builder << "min_loader_version: \"1\"\n";
  builder << "package_id: " << options.prompt_id << "." << options.version << "\n";
  builder << "prompt_id: " << options.prompt_id << "\n";
  builder << "version: \"" << options.version << "\"\n";
  builder << "stage: " << options.stage << "\n";
  builder << "eval_status: stable\n";
  builder << "release_scope: " << options.release_scope << "\n";
  builder << "output_schema_ref: schema://responder/default\n";
  builder << "trusted_source: profiles\n";
  builder << "default_release: true\n";
  builder << "language: zh-cn\n";
  builder << "task_types:\n";
  builder << "  - " << options.task_type << "\n";
  builder << "tags:\n";
  builder << "  - responder\n";

  write_file(package_root / "manifest.yaml", builder.str());
  write_file(package_root / "system.md", options.system_message);
  write_file(package_root / "task.md", options.task_message);
}

void create_provider_package(const std::filesystem::path& root,
                             const ProviderPackageOptions& options) {
  const std::filesystem::path provider_root = root / options.package_name;

  std::ostringstream manifest;
  manifest << "schema_version: \"1\"\n";
  manifest << "provider_id: " << options.provider_id << "\n";
  manifest << "display_name: " << options.display_name << "\n";
  manifest << "adapter_family: " << options.adapter_family << "\n";
  manifest << "api_family: " << options.api_family << "\n";
  manifest << "base_url: " << options.base_url << "\n";
  manifest << "base_url_alias: " << options.base_url_alias << "\n";
  manifest << "auth_mode: bearer_api_key\n";
  manifest << "auth_ref: " << options.auth_ref << "\n";
  manifest << "header_refs:\n";
  manifest << "  - header://llm/providers/" << options.provider_id << "-org\n";
  manifest << "trusted_source: profiles\n";
  manifest << "source_version: " << options.source_version << "\n";
  manifest << "activation_flag: " << (options.activation_flag ? "true" : "false") << "\n";
  manifest << "tags:\n";
  manifest << "  - cloud\n";
  manifest << "  - external\n";
  manifest << "  - openclaw\n";
  manifest << "mutable_overlay_fields:\n";
  manifest << "  - auth_ref\n";
  manifest << "  - header_refs\n";
  manifest << "  - base_url_alias\n";
  manifest << "  - activation_flag\n";
  write_file(provider_root / "manifest.yaml", manifest.str());

  std::ostringstream models;
  models << "schema_version: \"1\"\n";
  models << "models:\n";
  models << "  " << options.model_id << ":\n";
  models << "    id: " << options.model_id << "\n";
  models << "    display_name: " << options.model_display_name << "\n";
  models << "    model_version: " << options.model_version << "\n";
  models << "    reasoning_mode: non_thinking\n";
  models << "    tier_family: " << options.tier_family << "\n";
  models << "    latency_tier: " << options.latency_tier << "\n";
  models << "    cost_tier: " << options.cost_tier << "\n";
  models << "    reasoning_depth_tier: " << options.reasoning_depth_tier << "\n";
  models << "    aliases:\n";
  models << "      - " << options.model_alias << "\n";
  models << "    context_window: 200000\n";
  models << "    default_max_output_tokens: 8192\n";
  models << "    max_output_tokens_hard_limit: 16384\n";
  models << "    input_modalities:\n";
  models << "      - text\n";
  models << "    supports_tools: true\n";
  models << "    supports_streaming: true\n";
  models << "    supports_json_object: true\n";
  models << "    supports_json_schema: false\n";
  models << "    supports_reasoning: false\n";
  models << "    supports_visible_reasoning: false\n";
  models << "    supports_prompt_cache: true\n";
  models << "    supports_native_stream_usage: false\n";
  models << "    pricing:\n";
  models << "      pricing_ref: openclaw-premium-2026-04-13\n";
  models << "      input_cache_hit_usd_per_1m: 0.02\n";
  models << "      input_cache_miss_usd_per_1m: 0.20\n";
  models << "      output_usd_per_1m: 2.50\n";
  models << "    metadata_source_uri: https://example.invalid/openclaw/pricing\n";
  models << "    metadata_effective_at: 2026-04-13\n";
  models << "    verification_state:\n";
  models << "      tools: verified\n";
  models << "      json_output: verified\n";
  models << "    feature_notes:\n";
  models << "      - asset_only_onboarding\n";
  write_file(provider_root / "models.yaml", models.str());
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
              {"response",
               ModelRoutePolicy{.route = spec.response_route,
                                .fallback_route = spec.response_fallback,
                                .streaming_enabled = false}},
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
      DegradePolicy{.fallback_chain = {"local.small"},
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
      2U,
  };
}

LLMSubsystemConfig project_profile_config(const ProfileProjectionSpec& spec,
                                          const std::filesystem::path& prompt_root,
                                          const std::filesystem::path& provider_deployment_root) {
  LLMSubsystemConfigOverlay overlay;
  overlay.prompt_asset_sources.baseline_root = prompt_root.generic_string();
  overlay.provider_catalog_sources.baseline_root =
      repo_provider_baseline_root().generic_string();
  overlay.provider_catalog_sources.deployment_root =
      provider_deployment_root.generic_string();

  const auto config =
      project_llm_subsystem_config(make_runtime_policy_snapshot(spec), overlay);
  if (!config.has_value()) {
    throw std::runtime_error("failed to project llm config for profile: " + spec.profile_id);
  }

  return *config;
}

std::shared_ptr<const ProviderCatalogSnapshot> load_provider_snapshot(
    const std::filesystem::path& provider_deployment_root) {
  ProviderCatalogRepository repository;
  ProviderCatalogSourceConfig config;
  config.baseline_root = repo_provider_baseline_root().generic_string();
  config.deployment_root = provider_deployment_root.generic_string();

  assert_true(repository.init(config),
              std::string("asset-only onboarding should load provider catalog snapshot from baseline plus deployment overlay: ") +
                  repository.last_error_message());
  const auto snapshot = repository.snapshot();
  assert_true(snapshot != nullptr && snapshot->has_consistent_values(),
              "asset-only onboarding should publish a consistent provider catalog snapshot");
  return snapshot;
}

ProviderRuntimeProjectionView make_runtime_view(const ProviderCatalogProvider& provider) {
  return ProviderRuntimeProjectionView{
      .provider_instance_id = provider.descriptor.provider_id,
      .base_url_alias = provider.runtime.base_url_alias,
      .snapshot_version = provider.descriptor.source_version,
      .runtime_tags = provider.runtime.tags,
      .activation_flag = provider.runtime.activation_flag,
  };
}

LLMGenerateRequest make_request(std::string request_id, std::string llm_call_id) {
  LLMRequest request;
  request.request_id = std::move(request_id);
  request.llm_call_id = std::move(llm_call_id);
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"summarize the onboarding route"};
  request.created_at = 1712966408000LL;
  request.output_schema_ref = "schema://responder/default";
  request.response_format = "json_object";
  request.max_output_tokens = 128U;
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = 1024U,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{"integration", "asset-only-onboarding"};

  return LLMGenerateRequest{
      .stage = "response",
      .task_type = "summary",
      .request = std::move(request),
      .selection_hint = std::make_shared<const ModelSelectionHint>(ModelSelectionHint{
          .stage = "response",
          .task_type = "summary",
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
  result.provider_diagnostics.provider_trace_id = "trace-042-provider-onboarding";
  return result;
}

void register_provider_routes(
    const ProviderCatalogSnapshot& snapshot,
    const LLMSubsystemConfig& config,
    const std::shared_ptr<dasall::llm::route::AdapterRegistry>& registry,
    const std::unordered_map<std::string, std::shared_ptr<MockLLMAdapter>>& adapters_by_provider) {
  for (const auto& provider : snapshot.providers) {
    const auto adapter_it = adapters_by_provider.find(provider.descriptor.provider_id);
    assert_true(adapter_it != adapters_by_provider.end(),
                "asset-only onboarding should provide an admitted family adapter fixture for every catalog provider");

    for (const auto& model : snapshot.models) {
      if (model.summary.provider_id != provider.descriptor.provider_id) {
        continue;
      }

      assert_true(
          registry->initialize_and_register_provider_route(provider.descriptor,
                                                           make_runtime_view(provider),
                                                           model.summary.model_id,
                                                           model.supports_streaming,
                                                           config,
                                                           adapter_it->second),
          "asset-only onboarding should register provider routes from the catalog snapshot into the adapter registry");
    }
  }
}

struct AssetOnboardingFixture {
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
  std::shared_ptr<MockLLMAdapter> deepseek_adapter = std::make_shared<MockLLMAdapter>();
  std::shared_ptr<MockLLMAdapter> openclaw_adapter = std::make_shared<MockLLMAdapter>();
  std::shared_ptr<const ProviderCatalogSnapshot> catalog_snapshot;
  LLMManager manager;

  explicit AssetOnboardingFixture(std::shared_ptr<const ProviderCatalogSnapshot> snapshot)
      : catalog_snapshot(std::move(snapshot)),
        manager(prompt_pipeline,
                router,
                registry,
                executor,
                normalizer,
                aggregator,
                catalog_snapshot) {
    assert_true(
        registry->init(dasall::llm::route::AdapterRegistryConfig{
            .blocked_failure_threshold = 6U,
        }),
        "asset-only onboarding should initialize AdapterRegistry before wiring the manager fixture");

    deepseek_adapter->set_generate_handler([](const LLMRequest& request) {
      return make_success_result(request, "deepseek-chat");
    });
    openclaw_adapter->set_generate_handler([](const LLMRequest& request) {
      return make_success_result(request, "openclaw-chat");
    });
  }

  std::unordered_map<std::string, std::shared_ptr<MockLLMAdapter>> adapters_by_provider() const {
    return {
        {"deepseek-prod", deepseek_adapter},
        {"openclaw-prod", openclaw_adapter},
    };
  }

  void register_routes(const LLMSubsystemConfig& config) {
    register_provider_routes(*catalog_snapshot, config, registry, adapters_by_provider());
  }

  void init_manager(const LLMSubsystemConfig& config) {
    assert_true(manager.init(config),
                "asset-only onboarding should initialize LLMManager with the projected profile config");
  }
};

void assert_success_route(const LLMManagerResult& result,
                          std::string_view expected_route,
                          const std::shared_ptr<MockLLMAdapter>& expected_adapter,
                          std::string_view message_prefix) {
  assert_true(result.has_consistent_values() && result.response.has_value(),
              std::string(message_prefix) + " should complete through the admitted provider route");
  assert_true(result.resolved_route == expected_route,
              std::string(message_prefix) + " should resolve the expected provider/model route");
  assert_true(expected_adapter->generate_call_count() == 1 && expected_adapter->last_request().has_value(),
              std::string(message_prefix) + " should dispatch exactly one request to the selected provider instance");
}

void assert_openclaw_init_projection(const std::shared_ptr<MockLLMAdapter>& adapter,
                                     std::string_view message_prefix) {
  assert_true(adapter->last_init_config().has_value(),
              std::string(message_prefix) + " should initialize the admitted openai-compatible family adapter");
  assert_equal(std::string("openai_compatible"),
               adapter->last_init_config()->adapter_family,
               std::string(message_prefix) + " should keep the provider instance on the existing openai-compatible family");
  assert_equal(std::string("openclaw-prod"),
               adapter->last_init_config()->provider_instance_id,
               std::string(message_prefix) + " should project provider_instance_id from the provider asset package");
  assert_equal(std::string("openclaw/premium"),
               adapter->last_init_config()->base_url_alias,
               std::string(message_prefix) + " should project base_url_alias from the provider asset package");
  assert_equal(std::string("secret://llm/providers/openclaw-prod"),
               adapter->last_init_config()->auth_ref,
               std::string(message_prefix) + " should project auth_ref from the provider asset package");
  assert_equal(std::string("2026.04.14-openclaw"),
               adapter->last_init_config()->snapshot_version,
               std::string(message_prefix) + " should project snapshot_version from the provider asset package");
}

void test_asset_only_onboarding_selects_new_provider_instance_when_profile_declares_route() {
  TempDirectory prompt_root("dasall_provider_asset_onboarding_prompt");
  TempDirectory provider_deployment_root("dasall_provider_asset_onboarding_deployment");

  create_prompt_package(prompt_root.path(),
                        PromptPackageOptions{
                            .version = "2026.04.14",
                            .prompt_id = "responder",
                            .stage = "response",
                            .task_type = "summary",
                            .release_scope = "stable",
                            .system_message = "respond with the selected provider route",
                            .task_message = "summarize the selected provider instance",
                        });
  create_catalog_index(provider_deployment_root.path(), "2026.04.14", {"openclaw"});
  create_provider_package(provider_deployment_root.path(),
                          ProviderPackageOptions{
                              .package_name = "openclaw",
                              .provider_id = "openclaw-prod",
                              .display_name = "OpenClaw Production",
                              .adapter_family = "openai_compatible",
                              .api_family = "openai-completions",
                              .base_url = "https://api.openclaw.invalid/v1",
                              .base_url_alias = "openclaw/premium",
                              .auth_ref = "secret://llm/providers/openclaw-prod",
                              .source_version = "2026.04.14-openclaw",
                              .model_id = "openclaw-chat",
                              .model_display_name = "OpenClaw Chat",
                              .model_version = "OpenClaw-2026.04",
                              .tier_family = "premium",
                              .latency_tier = "low",
                              .cost_tier = "medium",
                              .reasoning_depth_tier = "standard",
                              .model_alias = "openclaw/premium",
                              .activation_flag = true,
                          });

  const auto catalog_snapshot = load_provider_snapshot(provider_deployment_root.path());
  const auto* openclaw_provider = catalog_snapshot->find_provider("openclaw-prod");
  assert_true(openclaw_provider != nullptr,
              "asset-only onboarding should merge the new provider package into the catalog snapshot");
  assert_equal(std::string("deployment"),
               openclaw_provider->runtime.source_layer,
               "asset-only onboarding should mark the new provider package as a deployment-layer asset");

  AssetOnboardingFixture fixture(catalog_snapshot);
  const auto config = project_profile_config(ProfileProjectionSpec{
                                                 .profile_id = "cloud_full_onboarding",
                                                 .response_route = "cloud.premium",
                                                 .response_fallback = std::nullopt,
                                                 .allowed_prompt_releases = {"stable"},
                                                 .trusted_sources = {"profiles"},
                                                 .llm_timeout_ms = 3200,
                                                 .retry_budget = 1U,
                                             },
                                             prompt_root.path(),
                                             provider_deployment_root.path());
  fixture.register_routes(config);
  fixture.init_manager(config);

  const auto result = fixture.manager.generate(
      make_request("req-042-onboard-openclaw", "call-042-onboard-openclaw"));
  assert_success_route(result,
                       kOpenClawRoute,
                       fixture.openclaw_adapter,
                       "asset-only onboarding explicit profile route");
  assert_equal(0,
               fixture.deepseek_adapter->generate_call_count(),
               "asset-only onboarding explicit profile route should not dispatch the baseline deepseek provider");
  assert_openclaw_init_projection(fixture.openclaw_adapter,
                                  "asset-only onboarding explicit profile route");
}

void test_asset_only_provider_instance_stays_dormant_without_profile_route_declaration() {
  TempDirectory prompt_root("dasall_provider_asset_dormant_prompt");
  TempDirectory provider_deployment_root("dasall_provider_asset_dormant_deployment");

  create_prompt_package(prompt_root.path(),
                        PromptPackageOptions{
                            .version = "2026.04.14",
                            .prompt_id = "responder",
                            .stage = "response",
                            .task_type = "summary",
                            .release_scope = "stable",
                            .system_message = "respond with the selected provider route",
                            .task_message = "summarize the selected provider instance",
                        });
  create_catalog_index(provider_deployment_root.path(), "2026.04.14", {"openclaw"});
  create_provider_package(provider_deployment_root.path(),
                          ProviderPackageOptions{
                              .package_name = "openclaw",
                              .provider_id = "openclaw-prod",
                              .display_name = "OpenClaw Production",
                              .adapter_family = "openai_compatible",
                              .api_family = "openai-completions",
                              .base_url = "https://api.openclaw.invalid/v1",
                              .base_url_alias = "openclaw/premium",
                              .auth_ref = "secret://llm/providers/openclaw-prod",
                              .source_version = "2026.04.14-openclaw",
                              .model_id = "openclaw-chat",
                              .model_display_name = "OpenClaw Chat",
                              .model_version = "OpenClaw-2026.04",
                              .tier_family = "premium",
                              .latency_tier = "low",
                              .cost_tier = "medium",
                              .reasoning_depth_tier = "standard",
                              .model_alias = "openclaw/premium",
                              .activation_flag = true,
                          });

  const auto catalog_snapshot = load_provider_snapshot(provider_deployment_root.path());
  AssetOnboardingFixture fixture(catalog_snapshot);
  const auto config = project_profile_config(ProfileProjectionSpec{
                                                 .profile_id = "cloud_full_default",
                                                 .response_route = "cloud.default",
                                                 .response_fallback = std::nullopt,
                                                 .allowed_prompt_releases = {"stable"},
                                                 .trusted_sources = {"profiles"},
                                                 .llm_timeout_ms = 3200,
                                                 .retry_budget = 1U,
                                             },
                                             prompt_root.path(),
                                             provider_deployment_root.path());
  fixture.register_routes(config);
  fixture.init_manager(config);

  const auto result = fixture.manager.generate(
      make_request("req-042-dormant-openclaw", "call-042-dormant-openclaw"));
  assert_success_route(result,
                       kDeepSeekRoute,
                       fixture.deepseek_adapter,
                       "asset-only onboarding without profile declaration");
  assert_equal(0,
               fixture.openclaw_adapter->generate_call_count(),
               "asset-only onboarding without profile declaration should keep the new provider instance dormant at dispatch time");
  assert_openclaw_init_projection(fixture.openclaw_adapter,
                                  "asset-only onboarding without profile declaration");
}

}  // namespace

int main() {
  try {
    test_asset_only_onboarding_selects_new_provider_instance_when_profile_declares_route();
    test_asset_only_provider_instance_stays_dormant_without_profile_route_declaration();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}