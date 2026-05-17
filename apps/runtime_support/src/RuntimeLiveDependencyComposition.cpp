#include "RuntimeLiveDependencyComposition.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "ICognitionEngine.h"
#include "ObservabilityLiveComposition.h"
#include "LLMProductionFactory.h"
#include "IMemoryManager.h"
#include "IMultiAgentCoordinator.h"
#include "KnowledgeServiceFactory.h"
#include "KnowledgeTypes.h"
#include "IResponseBuilder.h"
#include "RuntimeDependencySet.h"
#include "ServiceLiveComposition.h"
#include "ToolManager.h"
#include "BuildProfileResolver.h"
#include "health/IHealthMonitor.h"
#include "ops/ToolAuditBridge.h"
#include "ops/ToolHealthProbe.h"
#include "ops/ToolMetricsBridge.h"
#include "ops/ToolTraceBridge.h"
#include "ProfileCatalog.h"
#include "tool/ToolDescriptor.h"
#include "config/MemoryConfigProjector.h"
#include "config/InstallLayout.h"
#include "execution/BuiltinExecutorLane.h"
#include "bridge/ToolServiceBridge.h"
#include "registry/ToolRegistry.h"

namespace dasall::apps::runtime_support {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] bool environment_flag_enabled(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return false;
  }

  const std::string text(value);
  return text == "1" || text == "true" || text == "TRUE" ||
         text == "on" || text == "yes";
}

[[nodiscard]] bool runtime_cognition_first_requested() {
  return environment_flag_enabled("DASALL_RUNTIME_COGNITION_FIRST");
}

struct RuntimeObservabilityBundle {
  std::shared_ptr<infra::logging::ILogger> logger;
  std::shared_ptr<infra::audit::IAuditLogger> audit_logger;
  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider;
  std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider;
  std::shared_ptr<infra::IHealthMonitor> health_monitor;
  std::shared_ptr<tools::ops::ToolAuditBridge> tool_audit_bridge;
  std::shared_ptr<tools::ops::ToolMetricsBridge> tool_metrics_bridge;
  std::shared_ptr<tools::ops::ToolTraceBridge> tool_trace_bridge;
  std::shared_ptr<tools::ops::ToolHealthProbe> tool_health_probe;
  std::vector<std::shared_ptr<infra::IHealthProbe>> health_probes;
  std::string error;

  [[nodiscard]] bool ok() const {
      return logger != nullptr && audit_logger != nullptr &&
        metrics_provider != nullptr &&
           tracer_provider != nullptr && health_monitor != nullptr &&
           tool_audit_bridge != nullptr && tool_metrics_bridge != nullptr &&
           tool_trace_bridge != nullptr && tool_health_probe != nullptr &&
           error.empty();
  }
};

class RuntimeToolHealthSignalProvider final
    : public tools::ops::IToolHealthSignalProvider {
 public:
  RuntimeToolHealthSignalProvider(
      std::shared_ptr<tools::ops::ToolAuditBridge> audit_bridge,
      std::shared_ptr<tools::ops::ToolMetricsBridge> metrics_bridge,
      std::shared_ptr<tools::ops::ToolTraceBridge> trace_bridge,
      std::function<std::int64_t()> now_ms)
      : audit_bridge_(std::move(audit_bridge)),
        metrics_bridge_(std::move(metrics_bridge)),
        trace_bridge_(std::move(trace_bridge)),
        now_ms_(std::move(now_ms)) {}

  [[nodiscard]] tools::ops::ToolHealthSample sample(std::int64_t) override {
    tools::ops::ToolHealthSample sample;
    sample.registry.revision = 1U;
    sample.registry.descriptor_catalog_ready = true;
    sample.registry.delta_pipeline_degraded = false;
    sample.builtin_lane.available = true;
    sample.builtin_lane.concurrency_budget = 1U;
    sample.workflow_lane.available = true;
    sample.workflow_lane.concurrency_budget = 1U;
    sample.mcp.session_ready = true;
    sample.mcp.freshness = tools::CapabilityFreshness::fresh;
    sample.mcp.stale_read_allowed = true;
    sample.audit_bridge_degraded = audit_bridge_ == nullptr ||
        audit_bridge_->get_status().degraded;
    sample.metrics_bridge_degraded = metrics_bridge_ == nullptr ||
        metrics_bridge_->is_degraded();
    sample.trace_bridge_degraded = trace_bridge_ == nullptr ||
        trace_bridge_->is_degraded();
    sample.latency_ms = 0;
    sample.sampled_at_unix_ms = now_ms_ != nullptr ? now_ms_() : current_time_ms();
    sample.detail_ref = "status://runtime_support/tools/health";
    return sample;
  }

 private:
  std::shared_ptr<tools::ops::ToolAuditBridge> audit_bridge_;
  std::shared_ptr<tools::ops::ToolMetricsBridge> metrics_bridge_;
  std::shared_ptr<tools::ops::ToolTraceBridge> trace_bridge_;
  std::function<std::int64_t()> now_ms_;
};

[[nodiscard]] std::string register_health_probe(
    const std::shared_ptr<infra::IHealthMonitor>& health_monitor,
    const std::string& probe_name,
    const std::string& probe_group,
    infra::IHealthProbe* probe) {
  if (health_monitor == nullptr || probe == nullptr) {
    return "health monitor registration requires concrete monitor and probe";
  }

  const auto result = health_monitor->register_probe(infra::HealthProbeRegistration{
      .probe_name = probe_name,
      .probe_group = probe_group,
      .probe = probe,
  });
  if (!result.ok) {
    return std::string("health probe registration failed for ") + probe_name;
  }

  return {};
}

[[nodiscard]] RuntimeObservabilityBundle compose_runtime_observability_bundle(
    const profiles::RuntimePolicySnapshot& policy_snapshot) {
  const auto live_observability = infra::compose_live_observability(
      infra::ObservabilityLiveCompositionOptions{
          .profile_id = policy_snapshot.effective_profile_id(),
          .metrics_granularity = policy_snapshot.ops_policy().metrics_granularity,
          .trace_sample_ratio = policy_snapshot.ops_policy().trace_sample_ratio,
      });
  if (!live_observability.ok()) {
    return RuntimeObservabilityBundle{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .tool_audit_bridge = nullptr,
        .tool_metrics_bridge = nullptr,
        .tool_trace_bridge = nullptr,
        .tool_health_probe = nullptr,
        .health_probes = {},
        .error = live_observability.error,
    };
  }

  auto tool_audit_bridge = std::make_shared<tools::ops::ToolAuditBridge>(
      live_observability.audit_logger.get());
  auto tool_metrics_bridge = std::make_shared<tools::ops::ToolMetricsBridge>(
      live_observability.metrics_provider,
      tools::ops::ToolMetricsBridgeOptions{
          .enabled = true,
          .profile_id = policy_snapshot.effective_profile_id(),
          .metrics_granularity = policy_snapshot.ops_policy().metrics_granularity,
          .now_ms = []() {
            return current_time_ms();
          },
      });
  auto tool_trace_bridge = std::make_shared<tools::ops::ToolTraceBridge>(
      live_observability.tracer_provider,
      tools::ops::ToolTraceBridgeOptions{
          .enabled = true,
          .profile_id = policy_snapshot.effective_profile_id(),
          .trace_sample_ratio = policy_snapshot.ops_policy().trace_sample_ratio,
      });
  auto tool_health_probe = std::make_shared<tools::ops::ToolHealthProbe>(
      std::make_shared<RuntimeToolHealthSignalProvider>(
          tool_audit_bridge,
          tool_metrics_bridge,
          tool_trace_bridge,
          []() {
            return current_time_ms();
          }),
      tools::ops::ToolHealthProbeOptions{
          .now_ms = []() {
            return current_time_ms();
          },
      });

  if (const auto register_error = register_health_probe(
          live_observability.health_monitor,
          std::string(tools::ops::kToolHealthProbeName),
          std::string(tools::ops::kToolHealthProbeGroup),
          tool_health_probe.get());
      !register_error.empty()) {
    return RuntimeObservabilityBundle{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .tool_audit_bridge = nullptr,
        .tool_metrics_bridge = nullptr,
        .tool_trace_bridge = nullptr,
        .tool_health_probe = nullptr,
        .health_probes = {},
        .error = register_error,
    };
  }

  return RuntimeObservabilityBundle{
      .logger = live_observability.logger,
      .audit_logger = live_observability.audit_logger,
      .metrics_provider = live_observability.metrics_provider,
      .tracer_provider = live_observability.tracer_provider,
      .health_monitor = live_observability.health_monitor,
      .tool_audit_bridge = tool_audit_bridge,
      .tool_metrics_bridge = tool_metrics_bridge,
      .tool_trace_bridge = tool_trace_bridge,
      .tool_health_probe = tool_health_probe,
      .health_probes = {tool_health_probe},
      .error = {},
  };
}

[[nodiscard]] RuntimeDependencyCompositionResult make_error(std::string error) {
  return RuntimeDependencyCompositionResult{
      .dependency_set = nullptr,
      .error = std::move(error),
  };
}

[[nodiscard]] fs::path selected_root(const fs::path& default_root,
                                     const fs::path& override_root) {
  return override_root.empty() ? default_root : override_root;
}

[[nodiscard]] std::string create_memory_state_dir(const fs::path& state_root,
                                                  const std::string_view& composition_owner) {
  const fs::path memory_state_root = state_root / "memory";
  std::error_code error;
  fs::create_directories(memory_state_root, error);
  if (error) {
    return std::string("memory state directory unavailable for ") +
           std::string(composition_owner) + ": " + memory_state_root.string() +
           ": " + error.message();
  }
  return {};
}

[[nodiscard]] std::optional<profiles::BuildProfileManifest> resolve_build_manifest(
    const fs::path& profiles_root,
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    std::string& error) {
  const profiles::ProfileCatalog catalog(profiles_root);
  const profiles::BuildProfileResolver resolver(catalog);
  const auto manifest_result = resolver.resolve_build_manifest(
      profiles::BuildProfileResolveRequest{
          .profile_id = policy_snapshot.effective_profile_id(),
      });
  if (!manifest_result.ok()) {
    error = std::string("build manifest unavailable for profile ") +
            policy_snapshot.effective_profile_id();
    return std::nullopt;
  }

  return manifest_result.manifest;
}

[[nodiscard]] std::optional<memory::MemoryConfig> make_sqlite_memory_config(
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    const fs::path& profiles_root,
  const fs::path& runtime_library_root,
    const fs::path& readonly_assets_root,
    const fs::path& state_root,
  std::string& error,
  bool& vector_fail_closed) {
  const auto build_manifest =
      resolve_build_manifest(profiles_root, policy_snapshot, error);
  if (!build_manifest.has_value()) {
    return std::nullopt;
  }

  auto memory_config =
      memory::config::project_memory_config(policy_snapshot, *build_manifest);
  if (!memory_config.has_value()) {
    error = std::string("memory config projection failed for profile ") +
            policy_snapshot.effective_profile_id();
    return std::nullopt;
  }

  memory_config->storage.db_path = (state_root / "memory" / "memory.db").string();
  memory_config->storage.migrations_dir =
      (readonly_assets_root / "sql" / "memory").string();

#if defined(__APPLE__)
  constexpr const char* kSqliteExtensionSuffix = ".dylib";
#else
  constexpr const char* kSqliteExtensionSuffix = ".so";
#endif

  memory_config->vector.sqlite_vss_vector0_path =
      (runtime_library_root / "sqlite-vss" /
       (std::string("vector0") + kSqliteExtensionSuffix))
          .string();
  memory_config->vector.sqlite_vss_vss0_path =
      (runtime_library_root / "sqlite-vss" /
       (std::string("vss0") + kSqliteExtensionSuffix))
          .string();

  if (memory_config->vector.enabled &&
      (!fs::exists(memory_config->vector.sqlite_vss_vector0_path) ||
       !fs::exists(memory_config->vector.sqlite_vss_vss0_path))) {
    memory_config->vector.enabled = false;
    memory_config->vector.backend_type = memory::VectorBackend::None;
    memory_config->vector.search_top_k = 0;
    vector_fail_closed = true;
  }

  return memory_config;
}

[[nodiscard]] std::string format_knowledge_error(
    const std::optional<contracts::ErrorInfo>& error) {
  if (!error.has_value()) {
    return "none";
  }

  return !error->details.message.empty() ? error->details.message
                                         : std::string("knowledge.error");
}

[[nodiscard]] std::string join_reason_codes(
    const std::vector<std::string>& reason_codes) {
  std::ostringstream builder;
  for (std::size_t index = 0; index < reason_codes.size(); ++index) {
    if (index != 0U) {
      builder << ',';
    }
    builder << reason_codes[index];
  }
  return builder.str();
}

[[nodiscard]] knowledge::KnowledgeQuery make_installed_knowledge_probe_query() {
  knowledge::KnowledgeQuery query;
  query.request_id = "runtime-support-installed-knowledge-probe";
  query.query_text = "DeepSeek Chat";
  query.query_kind = knowledge::KnowledgeQueryKind::FactLookup;
  query.top_k = 3U;
  query.max_context_projection_items = 3U;
  return query;
}

[[nodiscard]] std::string validate_installed_knowledge_positive_probe(
    const std::shared_ptr<knowledge::IKnowledgeService>& knowledge_service) {
  if (knowledge_service == nullptr) {
    return "knowledge-service-null";
  }

  const auto refresh_result = knowledge_service->request_refresh(knowledge::CorpusChangeSet{});
  if (refresh_result.status != knowledge::RefreshStatus::Accepted) {
    return refresh_result.status == knowledge::RefreshStatus::Busy
        ? std::string("refresh-busy")
        : std::string("refresh-failed:") + format_knowledge_error(refresh_result.error);
  }

  const auto health_snapshot = knowledge_service->health_snapshot();
  if (health_snapshot.freshness_state != knowledge::FreshnessState::Fresh ||
      health_snapshot.active_snapshot_id.empty()) {
    return std::string("health:") + join_reason_codes(health_snapshot.reason_codes);
  }

  const auto retrieve_result = knowledge_service->retrieve(
      make_installed_knowledge_probe_query());
  if (!retrieve_result.ok || !retrieve_result.evidence.has_value() ||
      retrieve_result.evidence->slices.empty()) {
    return std::string("retrieve:") + format_knowledge_error(retrieve_result.error);
  }

  const auto matching_slice = std::find_if(
      retrieve_result.evidence->slices.begin(),
      retrieve_result.evidence->slices.end(),
      [](const knowledge::EvidenceSlice& slice) {
        return slice.snippet.find("DeepSeek Chat") != std::string::npos ||
               slice.citation_ref.find("deepseek") != std::string::npos;
      });
  if (matching_slice == retrieve_result.evidence->slices.end()) {
    return "retrieve-missing-installed-evidence";
  }

  return {};
}

[[nodiscard]] contracts::ToolDescriptor make_runtime_dataset_descriptor() {
  return contracts::ToolDescriptor{
      .tool_name = std::string{"agent.dataset"},
      .display_name = std::string{"Agent Dataset"},
      .category = contracts::ToolCategory::Information,
      .capability_tier = contracts::ToolCapabilityTier::Preview,
      .is_read_only = true,
      .supports_compensation = false,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string{"schema://tools/agent.dataset/input/v1"},
      .output_schema_ref = std::string{"schema://tools/agent.dataset/output/v1"},
      .required_scopes = std::vector<std::string>{"tools.read"},
      .tags = std::vector<std::string>{"builtin", "query", "runtime"},
      .version = std::string{"1.0.0"},
  };
}

[[nodiscard]] std::shared_ptr<tools::ToolManager> compose_runtime_tool_manager(
    std::shared_ptr<services::IExecutionService> execution_service,
    std::shared_ptr<services::IDataService> data_service,
    const RuntimeObservabilityBundle& observability) {
  if (execution_service == nullptr || data_service == nullptr ||
      observability.tool_audit_bridge == nullptr ||
      observability.tool_metrics_bridge == nullptr ||
      observability.tool_trace_bridge == nullptr) {
    return nullptr;
  }

  auto registry = std::make_shared<tools::registry::ToolRegistry>();
  if (!registry->register_builtin(make_runtime_dataset_descriptor())) {
    return nullptr;
  }

  auto builtin_lane = std::make_shared<tools::execution::BuiltinExecutorLane>(
      tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = std::make_shared<tools::bridge::ToolServiceBridge>(),
          .execution_service = std::move(execution_service),
          .data_service = std::move(data_service),
          .now_ms = {},
      });

  tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = registry;
  dependencies.metrics_bridge = observability.tool_metrics_bridge;
  dependencies.trace_bridge = observability.tool_trace_bridge;
  dependencies.audit_hooks =
      tools::ops::ToolAuditBridge::bind_hooks(observability.tool_audit_bridge);
  dependencies.executor = [builtin_lane](const auto& execution_request) {
    return builtin_lane->execute(
        execution_request.tool_ir,
        tools::ToolExecutionContext{
            .invocation_context = execution_request.invocation_context,
            .lane_key = execution_request.route_decision.lane_key,
        });
  };
  return std::make_shared<tools::ToolManager>(std::move(dependencies));
}

}  // namespace

RuntimeDependencyCompositionResult compose_minimal_live_dependency_set(
    std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot,
    const std::string_view& composition_owner,
    const RuntimeLiveDependencyCompositionOptions& options) {
  if (policy_snapshot == nullptr) {
    return make_error("runtime policy snapshot is required for live dependency composition");
  }

  const auto install_layout = infra::config::resolve_install_layout();
  const fs::path readonly_assets_root = selected_root(
      install_layout.readonly_assets_root, options.readonly_assets_root_override);
    const fs::path runtime_library_root = selected_root(
      install_layout.runtime_library_root, options.runtime_library_root_override);
  const fs::path state_root = selected_root(
      install_layout.state_root, options.state_root_override);
  if (const auto state_error = create_memory_state_dir(state_root, composition_owner);
      !state_error.empty()) {
    return make_error(state_error);
  }

  auto dependency_set = std::make_shared<runtime::RuntimeDependencySet>();

  std::string memory_config_error;
  bool memory_vector_fail_closed = false;
  auto memory_config = make_sqlite_memory_config(
      *policy_snapshot,
      install_layout.profiles_root,
      runtime_library_root,
      readonly_assets_root,
      state_root,
      memory_config_error,
      memory_vector_fail_closed);
  if (!memory_config.has_value()) {
    return make_error(std::string("memory config composition failed for ") +
              std::string(composition_owner) + ": " + memory_config_error);
  }

  auto memory_manager = std::shared_ptr<memory::IMemoryManager>(
      memory::create_memory_manager(*memory_config));
  if (memory_manager == nullptr) {
    return make_error(std::string("memory manager factory returned null for ") +
                      std::string(composition_owner));
  }

  const auto init_code = memory_manager->init(*memory_config);
  if (static_cast<int>(init_code) != 0) {
    return make_error(std::string("memory manager init failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->memory_manager = std::move(memory_manager);

  const auto observability = compose_runtime_observability_bundle(*policy_snapshot);
  if (!observability.ok()) {
    return make_error(std::string("runtime observability composition failed for ") +
                      std::string(composition_owner) + ": " + observability.error);
  }
  dependency_set->audit_logger = observability.audit_logger;
  dependency_set->metrics_provider = observability.metrics_provider;
  dependency_set->tracer_provider = observability.tracer_provider;
  dependency_set->health_monitor = observability.health_monitor;
  dependency_set->health_probes = observability.health_probes;

  auto llm_result = llm::create_production_llm_manager(
      *policy_snapshot,
      llm::LLMProductionFactoryOptions{
          .secret_backend = nullptr,
          .transport = nullptr,
          .provider_catalog_baseline_root = {},
          .logger = observability.logger,
          .metrics_provider = observability.metrics_provider,
          .tracer_provider = observability.tracer_provider,
          .audit_logger = observability.audit_logger,
      });
  if (!llm_result.ok()) {
    return make_error(std::string("llm manager composition failed for ") +
                      std::string(composition_owner) + ": " + llm_result.error);
  }
  dependency_set->llm_manager = std::move(llm_result.manager);

  auto cognition_engine = cognition::create_cognition_engine(
      *policy_snapshot,
      cognition::CognitionRuntimeDependencies{
          .llm_manager = dependency_set->llm_manager,
          .policy_snapshot = policy_snapshot,
          .audit_logger = dependency_set->audit_logger,
          .metrics_provider = dependency_set->metrics_provider,
          .tracer_provider = dependency_set->tracer_provider,
      });
  if (!cognition_engine) {
    return make_error(std::string("cognition engine composition failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->cognition_engine =
      std::shared_ptr<cognition::ICognitionEngine>(cognition_engine.release());

  auto response_builder = cognition::create_response_builder(
      *policy_snapshot,
      cognition::CognitionRuntimeDependencies{
          .llm_manager = dependency_set->llm_manager,
          .policy_snapshot = policy_snapshot,
        .audit_logger = dependency_set->audit_logger,
        .metrics_provider = dependency_set->metrics_provider,
        .tracer_provider = dependency_set->tracer_provider,
      });
  if (!response_builder) {
    return make_error(std::string("response builder composition failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->response_builder =
      std::shared_ptr<cognition::IResponseBuilder>(response_builder.release());

  const auto live_services = services::compose_live_services(
      *policy_snapshot,
      services::ServiceLiveCompositionOptions{
          .execution_capability_id = "agent.terminal",
          .data_capability_id = "agent.dataset",
          .local_service_available = true,
          .remote_service_available = false,
          .remote_timeout = false,
          .allow_route_degrade = true,
          .local_platform_route_enabled = false,
          .observability_enabled = true,
          .observability_level = policy_snapshot->ops_policy().metrics_granularity,
          .toolchain_hint = "x86_64-linux-gnu",
          .audit_logger = observability.audit_logger,
          .metrics_provider = observability.metrics_provider,
          .tracer_provider = observability.tracer_provider,
          .health_probe_enabled = true,
          .critical_actions = {},
          .high_risk_actions = {"agent.terminal"},
      });
  if (!live_services.ok()) {
    return make_error(std::string("services live composition failed for ") +
                      std::string(composition_owner) + ": " + live_services.error);
  }

  if (live_services.health_probe == nullptr) {
    return make_error(std::string("services health probe composition failed for ") +
                      std::string(composition_owner));
  }
  if (const auto register_error = register_health_probe(
          dependency_set->health_monitor,
      std::string("services.capability"),
      std::string("readiness"),
          live_services.health_probe.get());
      !register_error.empty()) {
    return make_error(std::string("services health probe registration failed for ") +
                      std::string(composition_owner) + ": " + register_error);
  }
  dependency_set->health_probes.push_back(live_services.health_probe);

  dependency_set->tool_manager = compose_runtime_tool_manager(
      live_services.execution_service,
      live_services.data_service,
      observability);
  if (dependency_set->tool_manager == nullptr) {
    return make_error(std::string("tool manager composition failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->multi_agent_coordinator =
      multi_agent::create_multi_agent_coordinator(policy_snapshot->multi_agent_enabled());
  if (dependency_set->multi_agent_coordinator == nullptr) {
    return make_error(std::string("multi_agent coordinator composition failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->visible_tools = {"agent.dataset"};
  const bool cognition_first_requested = runtime_cognition_first_requested();
  dependency_set->external_evidence = {
      std::string("runtime:") + std::string(composition_owner) +
      (cognition_first_requested ? ":cognition-first-forced"
                                 : ":required-live-baseline"),
      std::string("runtime:") + std::string(composition_owner) +
      ":tool-services-production-bridge",
      std::string("runtime:") + std::string(composition_owner) +
      ":production-observability-health",
      std::string("runtime:") + std::string(composition_owner) +
      (policy_snapshot->multi_agent_enabled() ? ":multi-agent-enabled"
                                              : ":multi-agent-disabled"),
  };
  if (memory_vector_fail_closed) {
    dependency_set->external_evidence.push_back(
        std::string("runtime:") + std::string(composition_owner) +
        ":memory-vector-fail-closed:sqlite-vss-assets-missing");
  }

  const auto knowledge_result = knowledge::create_installed_asset_knowledge_service(
      knowledge::InstalledAssetKnowledgeServiceOptions{
          .readonly_assets_root = readonly_assets_root,
          .state_root = state_root,
          .service_instance_id = std::string(composition_owner) + ":knowledge",
      });
  if (knowledge_result.ok()) {
    const auto positive_probe_error =
      validate_installed_knowledge_positive_probe(knowledge_result.service);
    if (positive_probe_error.empty()) {
      dependency_set->knowledge_service = knowledge_result.service;
      dependency_set->external_evidence.push_back(
        std::string("runtime:") + std::string(composition_owner) +
        ":knowledge-installed-assets-ready");
    } else {
      dependency_set->external_evidence.push_back(
        std::string("runtime:") + std::string(composition_owner) +
        ":knowledge-degraded:" + positive_probe_error);
    }
  } else {
    dependency_set->external_evidence.push_back(
        std::string("runtime:") + std::string(composition_owner) +
        ":knowledge-unavailable:" + knowledge_result.error);
  }

  return RuntimeDependencyCompositionResult{
      .dependency_set = std::move(dependency_set),
      .error = {},
  };
}

}  // namespace dasall::apps::runtime_support