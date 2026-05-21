#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>

#include "AgentFacade.h"
#include "IToolManager.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "agent/AgentRequest.h"
#include "ProfileCatalog.h"
#include "RuntimePolicyProvider.h"
#include "config/InstallLayout.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;

constexpr char kDefaultProfileId[] = "desktop_full";
constexpr char kRuntimeCognitionFirstEnv[] = "DASALL_RUNTIME_COGNITION_FIRST";

class TempStateRoot {
 public:
  explicit TempStateRoot(const std::string& stem)
      : path_(std::filesystem::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count()))) {
    std::filesystem::create_directories(path_);
  }

  ~TempStateRoot() {
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
  ScopedEnvVar(const char* name, const std::string& value) : name_(name) {
    if (const char* current = std::getenv(name); current != nullptr) {
      had_previous_ = true;
      previous_value_ = current;
    }
    ::setenv(name, value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if (had_previous_) {
      ::setenv(name_, previous_value_.c_str(), 1);
      return;
    }

    ::unsetenv(name_);
  }

 private:
  const char* name_;
  bool had_previous_ = false;
  std::string previous_value_;
};

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
load_runtime_policy_snapshot(const std::filesystem::path& profiles_root) {
  const dasall::profiles::ProfileCatalog catalog(profiles_root);
  const dasall::profiles::RuntimePolicyProvider provider(catalog);
  const auto snapshot_result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{
          .profile_id = kDefaultProfileId,
      });
  assert_true(snapshot_result.ok() && snapshot_result.snapshot != nullptr,
              "daemon runtime live dependency composition should load a runtime policy snapshot");
  return snapshot_result.snapshot;
}

[[nodiscard]] bool contains_port(const std::vector<std::string>& ports,
                                 const std::string& expected_port) {
  return std::find(ports.begin(), ports.end(), expected_port) != ports.end();
}

[[nodiscard]] bool contains_prefix(const std::vector<std::string>& values,
                                   const std::string& expected_prefix) {
  return std::any_of(values.begin(), values.end(),
                     [&expected_prefix](const std::string& value) {
                       return value.rfind(expected_prefix, 0) == 0;
                     });
}

[[nodiscard]] std::string describe_tool_envelope(
    const dasall::tools::ToolInvocationEnvelope& envelope) {
  std::string description = "failure_reason=";
  description += envelope.failure_reason_code.value_or("<none>");
  description += ", payload=";
  if (envelope.tool_result.has_value() && envelope.tool_result->payload.has_value()) {
    description += *envelope.tool_result->payload;
  } else {
    description += "<none>";
  }
  return description;
}

void copy_memory_assets_only(const std::filesystem::path& assets_root) {
  std::filesystem::create_directories(assets_root / "sql");
  std::filesystem::copy(std::filesystem::path(DASALL_SOURCE_ROOT) / "sql" / "memory",
                        assets_root / "sql" / "memory",
                        std::filesystem::copy_options::recursive);
}

void copy_installed_runtime_assets(const std::filesystem::path& assets_root) {
  copy_memory_assets_only(assets_root);
  std::filesystem::copy(std::filesystem::path(DASALL_SOURCE_ROOT) / "profiles",
                        assets_root / "profiles",
                        std::filesystem::copy_options::recursive);
  std::filesystem::copy(std::filesystem::path(DASALL_SOURCE_ROOT) / "skills",
                        assets_root / "skills",
                        std::filesystem::copy_options::recursive);
  std::filesystem::create_directories(assets_root / "llm");
  std::filesystem::copy(std::filesystem::path(DASALL_SOURCE_ROOT) / "llm" / "assets" /
                            "providers",
                        assets_root / "llm" / "providers",
                        std::filesystem::copy_options::recursive);
}

void daemon_runtime_live_dependency_composition_establishes_default_ready_baseline() {
  const TempStateRoot assets_root("dasall-daemon-runtime-live-assets");
  const TempStateRoot state_root("dasall-daemon-runtime-live-memory");
  copy_installed_runtime_assets(assets_root.path());
  const auto policy_snapshot = load_runtime_policy_snapshot(assets_root.path() / "profiles");
  const auto composition =
      dasall::apps::runtime_support::compose_minimal_live_dependency_set(
          policy_snapshot,
      "daemon.local-control-plane",
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
        .readonly_assets_root_override = assets_root.path(),
        .state_root_override = state_root.path(),
      });
  assert_true(composition.ok(),
              "daemon runtime live dependency composition should materialize required ports: " +
                  composition.error);
  assert_true(std::filesystem::exists(state_root.path() / "memory" / "memory.db"),
        "daemon runtime live dependency composition should materialize sqlite memory state");

  const auto readiness = composition.dependency_set->describe_readiness();
  assert_true(readiness.has_required_ports,
              "daemon runtime live dependency composition should provide all required ports: " +
                  readiness.summary());
    assert_true(!readiness.degraded,
          "daemon runtime live dependency composition should not be degraded after live optional ports are composed: " +
        readiness.summary());
  assert_true(readiness.missing_required_ports.empty(),
              "daemon runtime live dependency composition should not leave required ports empty");
      assert_true(!contains_port(readiness.missing_optional_ports, "knowledge") &&
        !contains_port(readiness.missing_optional_ports, "llm"),
      "daemon runtime live dependency composition should inject llm and knowledge: " +
        readiness.summary());
      assert_true(composition.dependency_set->tool_manager != nullptr,
        "daemon runtime live dependency composition should expose a concrete IToolManager");
      assert_true(contains_port(composition.dependency_set->visible_tools, "agent.dataset"),
        "daemon runtime live dependency composition should advertise the registered runtime builtin tool surface");
      assert_true(contains_port(composition.dependency_set->visible_tools, "agent.terminal"),
        "daemon runtime live dependency composition should advertise the high-risk runtime builtin tool surface");
      assert_true(contains_port(composition.dependency_set->visible_tools, "skill.runtime-state-snapshot"),
        "daemon runtime live dependency composition should advertise the runtime skill workflow surface once readonly skill assets are present");
    assert_true(composition.dependency_set->llm_manager != nullptr,
          "daemon runtime live dependency composition should expose a production ILLMManager");
      assert_true(composition.dependency_set->knowledge_service != nullptr,
      "daemon runtime live dependency composition should expose an installed-asset IKnowledgeService");

  dasall::runtime::AgentInitRequest init_request;
  init_request.runtime_instance_id =
      "daemon.local-control-plane:" + policy_snapshot->effective_profile_id();
  init_request.profile_id = policy_snapshot->effective_profile_id();
  init_request.policy_snapshot = policy_snapshot;
  init_request.dependency_set = composition.dependency_set;
  init_request.boot_reason = "daemon-local-control-plane";
  init_request.cold_start = true;

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(init_request);
  assert_true(init_result.accepted,
              "daemon runtime live dependency composition should keep init accepted: " +
                  init_result.health_summary + " (" + init_result.diagnostics + ")");
    assert_true(init_result.default_ready(),
          "daemon runtime live dependency composition should surface default-ready entrypoint: " +
            init_result.diagnostics);
  assert_true(!init_result.stub_ready(),
              "daemon runtime live dependency composition should not fall back to stub-ready: " +
                  init_result.diagnostics);
    assert_true(!init_result.degraded_ready(),
          "daemon runtime live dependency composition should not remain degraded-ready after knowledge is composed: " +
            init_result.diagnostics);

    const auto tool_envelope = composition.dependency_set->tool_manager->invoke(
      dasall::contracts::ToolRequest{
        .request_id = std::string("req-daemon-live-tool"),
        .tool_call_id = std::string("call-daemon-live-tool"),
        .tool_name = std::string("agent.dataset"),
        .invocation_kind = dasall::contracts::ToolInvocationKind::InformationQuery,
        .arguments_payload = std::string("{\"scope\":\"session\"}"),
        .created_at = 1710000000000,
        .goal_id = std::string("goal-daemon-live-tool"),
        .worker_task_id = std::string("worker-daemon-live-tool"),
        .runtime_budget = std::nullopt,
        .timeout_ms = 2500U,
        .idempotency_key = std::string("idem-daemon-live-tool"),
        .tags = std::vector<std::string>{"integration", "runtime", "tool"},
      },
      dasall::tools::ToolInvocationContext{
        .caller_domain = std::string("runtime.agent_orchestrator"),
        .session_id = std::string("session-daemon-live-tool"),
        .profile_snapshot = policy_snapshot.get(),
        .trace = {
          .trace_id = std::string("trace-daemon-live-tool"),
          .span_id = std::nullopt,
          .parent_span_id = std::nullopt,
        },
        .confirmation_facts = std::nullopt,
      });
    assert_true(tool_envelope.tool_result.has_value() &&
            tool_envelope.tool_result->success.value_or(false),
          "daemon runtime live dependency composition should keep agent.dataset on the successful tools->services path");
    assert_true(tool_envelope.route_facts.has_value() &&
            tool_envelope.route_facts->route_kind.has_value() &&
            *tool_envelope.route_facts->route_kind == "builtin",
          "daemon runtime live dependency composition should keep runtime builtin calls on the governed builtin route");
    assert_true(tool_envelope.observation.has_value() && tool_envelope.observation_digest.has_value(),
          "daemon runtime live dependency composition should project runtime builtin calls into observation and digest together");
    assert_true(tool_envelope.tool_result->payload.has_value() &&
        tool_envelope.tool_result->payload->find("\"capability_id\":\"agent.dataset\"") != std::string::npos &&
        tool_envelope.tool_result->payload->find("\"projection\":\"default\"") != std::string::npos,
          "daemon runtime live dependency composition should route agent.dataset through the live services backend payload");
    assert_true(!tool_envelope.failure_reason_code.has_value(),
          "daemon runtime live dependency composition should not surface a failure reason on the successful builtin query path");

    const auto skill_envelope = composition.dependency_set->tool_manager->invoke(
      dasall::contracts::ToolRequest{
        .request_id = std::string("req-daemon-live-skill"),
        .tool_call_id = std::string("call-daemon-live-skill"),
        .tool_name = std::string("skill.runtime-state-snapshot"),
        .invocation_kind = dasall::contracts::ToolInvocationKind::Workflow,
        .arguments_payload = std::string("{}"),
        .created_at = 1710000000003,
        .goal_id = std::string("goal-daemon-live-skill"),
        .worker_task_id = std::string("worker-daemon-live-skill"),
        .runtime_budget = std::nullopt,
        .timeout_ms = 2500U,
        .idempotency_key = std::string("idem-daemon-live-skill"),
        .tags = std::vector<std::string>{"integration", "runtime", "skill"},
      },
      dasall::tools::ToolInvocationContext{
        .caller_domain = std::string("runtime.agent_orchestrator"),
        .session_id = std::string("session-daemon-live-skill"),
        .profile_snapshot = policy_snapshot.get(),
        .trace = {
          .trace_id = std::string("trace-daemon-live-skill"),
          .span_id = std::nullopt,
          .parent_span_id = std::nullopt,
        },
        .confirmation_facts = std::nullopt,
      });
    assert_true(skill_envelope.tool_result.has_value() &&
            skill_envelope.tool_result->success.value_or(false),
          "daemon runtime live dependency composition should execute the runtime skill workflow on the successful path: " +
              describe_tool_envelope(skill_envelope));
    assert_true(skill_envelope.route_facts.has_value() &&
            skill_envelope.route_facts->route_kind == std::string("workflow"),
          "daemon runtime live dependency composition should preserve workflow route facts for runtime skill invocations");
    assert_true(skill_envelope.tool_result->payload.has_value() &&
            skill_envelope.tool_result->payload->find("\"workflow_id\":\"skill.runtime-state-snapshot\"") != std::string::npos &&
            skill_envelope.tool_result->payload->find("\"status\":\"completed\"") != std::string::npos,
          "daemon runtime live dependency composition should serialize the completed runtime skill workflow receipt into the payload");
    assert_true(skill_envelope.observation.has_value() && skill_envelope.observation_digest.has_value(),
          "daemon runtime live dependency composition should project the runtime skill workflow into observation and digest");
    assert_true(!skill_envelope.failure_reason_code.has_value(),
          "daemon runtime live dependency composition should not surface failure_reason_code on the successful runtime skill path");

    const auto denied_terminal_envelope = composition.dependency_set->tool_manager->invoke(
      dasall::contracts::ToolRequest{
        .request_id = std::string("req-daemon-live-terminal-deny"),
        .tool_call_id = std::string("call-daemon-live-terminal-deny"),
        .tool_name = std::string("agent.terminal"),
        .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
        .arguments_payload = std::string("{\"command\":\"echo daemon terminal deny\"}"),
        .created_at = 1710000000001,
        .goal_id = std::string("goal-daemon-live-terminal-deny"),
        .worker_task_id = std::string("worker-daemon-live-terminal-deny"),
        .runtime_budget = std::nullopt,
        .timeout_ms = 2500U,
        .idempotency_key = std::string("idem-daemon-live-terminal-deny"),
        .tags = std::vector<std::string>{"integration", "runtime", "tool", "terminal"},
      },
      dasall::tools::ToolInvocationContext{
        .caller_domain = std::string("runtime.agent_orchestrator"),
        .session_id = std::string("session-daemon-live-terminal-deny"),
        .profile_snapshot = policy_snapshot.get(),
        .trace = {
          .trace_id = std::string("trace-daemon-live-terminal-deny"),
          .span_id = std::nullopt,
          .parent_span_id = std::nullopt,
        },
        .confirmation_facts = std::nullopt,
      });
    assert_true(denied_terminal_envelope.failure_reason_code.has_value() &&
            *denied_terminal_envelope.failure_reason_code == "policy.confirmation_required",
          "daemon runtime live dependency composition should deny agent.terminal without confirmation");

    const auto allowed_terminal_envelope = composition.dependency_set->tool_manager->invoke(
      dasall::contracts::ToolRequest{
        .request_id = std::string("req-daemon-live-terminal-allow"),
        .tool_call_id = std::string("call-daemon-live-terminal-allow"),
        .tool_name = std::string("agent.terminal"),
        .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
        .arguments_payload = std::string("{\"command\":\"echo daemon terminal allow\"}"),
        .created_at = 1710000000002,
        .goal_id = std::string("goal-daemon-live-terminal-allow"),
        .worker_task_id = std::string("worker-daemon-live-terminal-allow"),
        .runtime_budget = std::nullopt,
        .timeout_ms = 2500U,
        .idempotency_key = std::string("idem-daemon-live-terminal-allow"),
        .tags = std::vector<std::string>{"integration", "runtime", "tool", "terminal"},
      },
      dasall::tools::ToolInvocationContext{
        .caller_domain = std::string("runtime.agent_orchestrator"),
        .session_id = std::string("session-daemon-live-terminal-allow"),
        .profile_snapshot = policy_snapshot.get(),
        .trace = {
          .trace_id = std::string("trace-daemon-live-terminal-allow"),
          .span_id = std::nullopt,
          .parent_span_id = std::nullopt,
        },
        .confirmation_facts = std::vector<dasall::tools::ToolConfirmationFact>{
          dasall::tools::ToolConfirmationFact{
            .confirmation_id = std::string("confirm-daemon-live-terminal"),
            .subject_ref = std::string("goal://daemon-live-terminal"),
            .proof_type = std::string("user.approved"),
            .confirmed_at_ms = 1710000000000,
          },
        },
      });
    assert_true(allowed_terminal_envelope.tool_result.has_value() &&
            allowed_terminal_envelope.tool_result->success.value_or(false),
          "daemon runtime live dependency composition should keep agent.terminal on the successful tools->services path once confirmation is present");
    assert_true(allowed_terminal_envelope.route_facts.has_value() &&
            allowed_terminal_envelope.route_facts->route_kind.has_value() &&
            *allowed_terminal_envelope.route_facts->route_kind == "builtin",
          "daemon runtime live dependency composition should keep agent.terminal on the governed builtin route");
    assert_true(allowed_terminal_envelope.observation.has_value() &&
            allowed_terminal_envelope.observation_digest.has_value(),
          "daemon runtime live dependency composition should project agent.terminal into observation and digest together");
    assert_true(allowed_terminal_envelope.tool_result->payload.has_value() &&
            allowed_terminal_envelope.tool_result->payload->find("\"operation\":\"agent.terminal\"") != std::string::npos,
          "daemon runtime live dependency composition should route agent.terminal through the live execution service payload");
        assert_true(composition.dependency_set->health_monitor != nullptr,
          "daemon runtime live dependency composition should retain a concrete health monitor for production observability");
        assert_true(composition.dependency_set->runtime_event_bus != nullptr &&
            composition.dependency_set->runtime_telemetry_bridge != nullptr &&
            composition.dependency_set->runtime_health_probe != nullptr &&
            composition.dependency_set->background_maintenance_hooks != nullptr,
          "daemon runtime live dependency composition should wire runtime control-plane observability sinks into the live dependency set");
        assert_true(composition.dependency_set->health_probes.size() == 3U,
          "daemon runtime live dependency composition should retain tool, services, and runtime control-plane health probes");
        assert_true(contains_port(composition.dependency_set->external_evidence,
        "runtime:daemon.local-control-plane:tool-services-production-bridge"),
          "daemon runtime live dependency composition should record the production services bridge evidence marker");
        assert_true(contains_port(composition.dependency_set->external_evidence,
        "runtime:daemon.local-control-plane:production-observability-health"),
          "daemon runtime live dependency composition should record the production observability and health evidence marker");
        assert_true(contains_port(composition.dependency_set->external_evidence,
        "runtime:daemon.local-control-plane:runtime-control-plane-observability-wired"),
          "daemon runtime live dependency composition should record the runtime control-plane observability evidence marker once the sinks are wired");
        assert_true(contains_port(composition.dependency_set->external_evidence,
          "runtime:daemon.local-control-plane:knowledge-installed-assets-ready"),
            "daemon runtime live dependency composition should record the installed knowledge positive marker after the probe succeeds");
        assert_true(contains_port(composition.dependency_set->external_evidence,
                "runtime:daemon.local-control-plane:skill-runtime-production-bridge"),
              "daemon runtime live dependency composition should record the runtime skill bridge evidence marker once readonly skill assets are wired");
        assert_true(!contains_prefix(composition.dependency_set->external_evidence,
            "runtime:daemon.local-control-plane:knowledge-degraded:"),
          "daemon runtime live dependency composition should not mix degraded knowledge markers into the ready baseline");
      }

  void daemon_runtime_live_dependency_composition_supports_cognition_first_gate() {
    const TempStateRoot assets_root("dasall-daemon-runtime-live-assets-cognition-first");
    const TempStateRoot state_root("dasall-daemon-runtime-live-memory-cognition-first");
    copy_installed_runtime_assets(assets_root.path());
    const auto policy_snapshot = load_runtime_policy_snapshot(assets_root.path() / "profiles");

    const ScopedEnvVar cognition_first_override(kRuntimeCognitionFirstEnv, "1");
    const auto composition =
        dasall::apps::runtime_support::compose_minimal_live_dependency_set(
            policy_snapshot,
            "daemon.local-control-plane",
            dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
                .readonly_assets_root_override = assets_root.path(),
                .state_root_override = state_root.path(),
            });
    assert_true(composition.ok(),
                "daemon runtime live dependency composition should keep required ports available when cognition-first is forced: " +
                    composition.error);
    assert_true(!contains_port(composition.dependency_set->external_evidence,
                               "runtime:daemon.local-control-plane:required-live-baseline"),
                "daemon runtime live dependency composition should drop the required-live-baseline marker when cognition-first is forced");
    assert_true(contains_port(composition.dependency_set->external_evidence,
                              "runtime:daemon.local-control-plane:cognition-first-forced"),
                "daemon runtime live dependency composition should record an explicit cognition-first marker when the evidence gate is enabled");
  }

      void daemon_runtime_live_dependency_composition_degrades_when_knowledge_probe_fails() {
        const auto policy_snapshot = load_runtime_policy_snapshot(
            std::filesystem::path(DASALL_SOURCE_ROOT) / "profiles");
        const TempStateRoot assets_root("dasall-daemon-runtime-live-assets-minimal");
        const TempStateRoot state_root("dasall-daemon-runtime-live-memory-degraded");
        copy_memory_assets_only(assets_root.path());

        const auto composition =
          dasall::apps::runtime_support::compose_minimal_live_dependency_set(
            policy_snapshot,
            "daemon.local-control-plane",
            dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
              .readonly_assets_root_override = assets_root.path(),
              .state_root_override = state_root.path(),
            });
        assert_true(composition.ok(),
              "daemon runtime live dependency composition should keep required baseline available even when knowledge positive probe fails: " +
                composition.error);

        const auto readiness = composition.dependency_set->describe_readiness();
        assert_true(readiness.has_required_ports,
              "daemon runtime live dependency composition should keep required ports ready when only the knowledge probe fails: " +
                readiness.summary());
        assert_true(readiness.degraded &&
                contains_port(readiness.missing_optional_ports, "knowledge"),
              "daemon runtime live dependency composition should expose knowledge as a degraded optional port when the positive probe fails: " +
                readiness.summary());
        assert_true(composition.dependency_set->knowledge_service == nullptr,
              "daemon runtime live dependency composition should not publish a knowledge service when the installed positive probe fails");
        assert_true(contains_prefix(composition.dependency_set->external_evidence,
                "runtime:daemon.local-control-plane:knowledge-degraded:"),
              "daemon runtime live dependency composition should record a degraded knowledge marker when the positive probe fails");
        assert_true(!contains_port(composition.dependency_set->external_evidence,
          "runtime:daemon.local-control-plane:knowledge-installed-assets-ready"),
              "daemon runtime live dependency composition should not retain the ready knowledge marker when the positive probe fails");
}

}  // namespace

int main() {
  try {
    daemon_runtime_live_dependency_composition_establishes_default_ready_baseline();
    daemon_runtime_live_dependency_composition_supports_cognition_first_gate();
    daemon_runtime_live_dependency_composition_degrades_when_knowledge_probe_fails();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonRuntimeLiveDependencyCompositionTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}