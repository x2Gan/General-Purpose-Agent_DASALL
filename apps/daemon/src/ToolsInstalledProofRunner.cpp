#include "ToolsInstalledProofRunner.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "DaemonEntryConfigLoader.h"
#include "IToolManager.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "ToolInvocationContext.h"
#include "config/InstallLayout.h"
#include "tool/ToolRequest.h"

namespace dasall::apps::daemon {
namespace {

namespace fs = std::filesystem;

constexpr char kToolName[] = "agent.dataset";
constexpr char kCompositionOwner[] = "daemon.local-control-plane";
constexpr char kProductionBridgeEvidence[] =
    "runtime:daemon.local-control-plane:tool-services-production-bridge";
constexpr char kProductionObservabilityEvidence[] =
    "runtime:daemon.local-control-plane:production-observability-health";

[[nodiscard]] std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string select_profile_id(
    const ToolsInstalledProofOptions& options) {
  return options.requested_profile_id.empty() ? kDefaultDaemonEntryProfileId
                                              : options.requested_profile_id;
}

void apply_assets_override(infra::config::InstallLayout& install_layout,
                           const ToolsInstalledProofOptions& options) {
  if (!options.readonly_assets_root_override.has_value()) {
    return;
  }

  const auto& assets_root = *options.readonly_assets_root_override;
  install_layout.readonly_assets_root = assets_root;
  install_layout.profiles_root = assets_root / "profiles";
  install_layout.llm_prompts_root = assets_root / "llm" / "prompts";
  install_layout.llm_providers_root = assets_root / "llm" / "providers";
}

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  std::string_view expected_value) {
  return std::find(values.begin(), values.end(), expected_value) != values.end();
}

[[nodiscard]] bool has_citation_prefix(
    const std::optional<std::vector<std::string>>& citations,
    std::string_view prefix) {
  if (!citations.has_value()) {
    return false;
  }

  return std::any_of(citations->begin(), citations->end(),
                     [prefix](const std::string& citation) {
                       return citation.rfind(prefix, 0U) == 0U;
                     });
}

}  // namespace

ToolsInstalledProofResult collect_tools_installed_proof(
    const ToolsInstalledProofOptions& options) {
  ToolsInstalledProofResult result;

  auto install_layout = infra::config::resolve_install_layout();
  apply_assets_override(install_layout, options);

  const DaemonEntryConfigLoader loader;
  const auto load_result = loader.load(
      DaemonEntryConfigLoadRequest{
          .profiles_root = install_layout.profiles_root,
          .requested_profile_id = select_profile_id(options),
          .deployment_config_path = options.deployment_config_path,
          .socket_path_override = std::nullopt,
      });
  if (!load_result.ok()) {
    result.error = std::string("failed to load daemon entry config: ") +
                   load_result.message;
    return result;
  }

  result.effective_profile_id = load_result.entry_config->effective_profile_id;
  const fs::path state_root =
      options.state_root_override.value_or(install_layout.state_root);
  const auto composition = runtime_support::compose_minimal_live_dependency_set(
      load_result.entry_config->runtime_policy_snapshot,
      kCompositionOwner,
      runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = install_layout.readonly_assets_root,
          .runtime_library_root_override = install_layout.runtime_library_root,
          .state_root_override = state_root,
      });
  if (!composition.ok()) {
    result.error = std::string("failed to compose runtime live dependency set: ") +
                   composition.error;
    return result;
  }

  result.visible_tools = composition.dependency_set->visible_tools;
  result.external_evidence = composition.dependency_set->external_evidence;
  result.agent_dataset_visible = contains_value(result.visible_tools, kToolName);
  result.production_bridge_evidence_present =
      contains_value(result.external_evidence, kProductionBridgeEvidence);
  result.production_observability_evidence_present =
      contains_value(result.external_evidence, kProductionObservabilityEvidence);
  if (composition.dependency_set->tool_manager == nullptr) {
    result.error = "runtime live dependency set did not expose a concrete tool manager";
    return result;
  }
  if (!result.agent_dataset_visible) {
    result.error = "runtime visible tools missing agent.dataset";
    return result;
  }

  const auto tool_manager = composition.dependency_set->tool_manager;
  const auto envelope = tool_manager->invoke(
      contracts::ToolRequest{
          .request_id = std::string("req-tools-installed-proof"),
          .tool_call_id = std::string("call-tools-installed-proof"),
          .tool_name = std::string(kToolName),
          .invocation_kind = contracts::ToolInvocationKind::InformationQuery,
          .arguments_payload = std::string("{\"scope\":\"session\"}"),
          .created_at = current_time_millis(),
          .goal_id = std::string("goal-tools-installed-proof"),
          .worker_task_id = std::string("worker-tools-installed-proof"),
          .runtime_budget = std::nullopt,
          .timeout_ms = 2500U,
          .idempotency_key = std::string("idem-tools-installed-proof"),
          .tags = std::vector<std::string>{"installed", "packaging", "tool", "proof"},
      },
      tools::ToolInvocationContext{
          .caller_domain = std::string("runtime.agent_orchestrator"),
          .session_id = std::string("session-tools-installed-proof"),
          .profile_snapshot = load_result.entry_config->runtime_policy_snapshot.get(),
          .trace = {
              .trace_id = std::string("trace-tools-installed-proof"),
              .span_id = std::nullopt,
              .parent_span_id = std::nullopt,
          },
          .confirmation_facts = std::nullopt,
      });

  result.failure_reason_code = envelope.failure_reason_code.value_or(std::string{});
  if (envelope.route_facts.has_value() && envelope.route_facts->route_kind.has_value()) {
    result.route_kind = *envelope.route_facts->route_kind;
  }
  if (envelope.tool_result.has_value() && envelope.tool_result->payload.has_value()) {
    result.payload = *envelope.tool_result->payload;
  }
  if (envelope.observation.has_value() && envelope.observation->observation_id.has_value()) {
    result.observation_id = *envelope.observation->observation_id;
  }
  if (envelope.observation_digest.has_value() &&
      envelope.observation_digest->summary.has_value()) {
    result.observation_digest_summary = *envelope.observation_digest->summary;
  }

  result.tool_invocation_succeeded =
      envelope.tool_result.has_value() && envelope.tool_result->success.value_or(false);
  result.projection_present =
      envelope.observation.has_value() && envelope.observation_digest.has_value();
  const std::optional<std::vector<std::string>> digest_citations =
    envelope.observation_digest.has_value()
      ? envelope.observation_digest->citations
      : std::optional<std::vector<std::string>>{};
  result.route_citation_present =
    has_citation_prefix(digest_citations, "route_kind:");
  result.tool_call_citation_present =
    has_citation_prefix(digest_citations, "tool_call:");

  if (!result.tool_invocation_succeeded) {
    result.error = result.failure_reason_code.empty()
                       ? std::string("agent.dataset invocation failed")
                       : std::string("agent.dataset invocation failed: ") +
                             result.failure_reason_code;
    return result;
  }
  if (result.route_kind != "builtin") {
    result.error = "agent.dataset invocation escaped the builtin lane";
    return result;
  }
  if (!result.projection_present) {
    result.error = "agent.dataset invocation did not produce observation and digest together";
    return result;
  }
  if (!result.route_citation_present || !result.tool_call_citation_present) {
    result.error = "agent.dataset observation digest is missing route/tool call citations";
    return result;
  }
  if (result.payload.find("\"capability_id\":\"agent.dataset\"") ==
          std::string::npos ||
      result.payload.find("\"projection\":\"default\"") ==
          std::string::npos) {
    result.error = "agent.dataset payload did not preserve the live services markers";
    return result;
  }
  if (!result.production_bridge_evidence_present ||
      !result.production_observability_evidence_present) {
    result.error =
        "runtime external evidence is missing the production bridge or observability marker";
    return result;
  }
  if (result.observation_digest_summary.empty()) {
    result.error = "agent.dataset observation digest summary is empty";
  }

  return result;
}

}  // namespace dasall::apps::daemon