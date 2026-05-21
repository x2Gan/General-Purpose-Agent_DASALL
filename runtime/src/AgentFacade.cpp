#include "AgentFacade.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "AgentOrchestrator.h"
#include "RuntimeDependencySet.h"
#include "error/ResultCode.h"

namespace dasall::runtime {
namespace {

struct RuntimeCompositionRoot {
  std::string runtime_instance_id;
  std::string profile_id;
  std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot;
  std::shared_ptr<RuntimeDependencySet> dependency_set;
  std::unique_ptr<AgentOrchestrator> orchestrator;
  std::optional<SessionSnapshot> waiting_session;
  RuntimeDependencyReadiness readiness;
  bool degraded = false;
};

constexpr char kRuntimePathTagPrefix[] = "runtime_path:";
constexpr char kRuntimePathDirectLlmTag[] = "runtime_path:direct_llm";
constexpr char kRuntimePathCognitionFirstTag[] = "runtime_path:cognition_first";
constexpr char kRuntimePathToolPositiveTag[] = "runtime_path:tool_positive";
constexpr char kRuntimePathRecoveryPositiveTag[] = "runtime_path:recovery_positive";

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] std::optional<std::string> optional_string(const std::string& value) {
  if (value.empty()) {
    return std::nullopt;
  }

  return value;
}

void append_diagnostic_fragment(std::string& diagnostics,
                                const std::string& fragment) {
  if (fragment.empty()) {
    return;
  }

  if (!diagnostics.empty()) {
    diagnostics += ";";
  }
  diagnostics += fragment;
}

void append_unique_tag(std::vector<std::string>& tags,
                       const std::string& tag) {
  if (tag.empty()) {
    return;
  }

  if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
    tags.push_back(tag);
  }
}

void append_unique_value(std::vector<std::string>& values,
                         const std::string& value) {
  if (value.empty()) {
    return;
  }

  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

[[nodiscard]] std::string join_values(const std::vector<std::string>& values) {
  std::string joined;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      joined += ",";
    }
    joined += values[index];
  }
  return joined;
}

[[nodiscard]] bool contains_evidence_fragment(
    const std::vector<std::string>& evidence,
    const std::string& fragment) {
  return std::any_of(evidence.begin(), evidence.end(),
                     [&fragment](const std::string& value) {
                       return value.find(fragment) != std::string::npos;
                     });
}

void clear_runtime_path_tags(std::vector<std::string>& tags) {
  tags.erase(std::remove_if(tags.begin(), tags.end(), [](const std::string& tag) {
               return tag.rfind(kRuntimePathTagPrefix, 0) == 0;
             }),
             tags.end());
}

[[nodiscard]] bool result_is_path_tag_eligible(
    const contracts::AgentResult& result) {
  return result.status == contracts::AgentResultStatus::Completed ||
         result.status == contracts::AgentResultStatus::PartiallyCompleted;
}

[[nodiscard]] std::optional<std::string> classify_runtime_path_tag(
    const RuntimeCompositionRoot& root,
    const OrchestratorRunResult& run_result) {
  if (root.dependency_set == nullptr ||
      !result_is_path_tag_eligible(run_result.agent_result)) {
    return std::nullopt;
  }

  if (run_result.used_recovery_round) {
    return std::string{kRuntimePathRecoveryPositiveTag};
  }

  if (run_result.used_tool_round) {
    return std::string{kRuntimePathToolPositiveTag};
  }

  const auto& evidence = root.dependency_set->external_evidence;
  if (contains_evidence_fragment(evidence, "required-live-baseline")) {
    return std::string{kRuntimePathDirectLlmTag};
  }

  if (contains_evidence_fragment(evidence, "cognition-first-forced") ||
      root.dependency_set->llm_manager != nullptr) {
    return std::string{kRuntimePathCognitionFirstTag};
  }

  return std::nullopt;
}

void apply_runtime_path_tag(const RuntimeCompositionRoot& root,
                            OrchestratorRunResult& run_result) {
  const auto path_tag = classify_runtime_path_tag(root, run_result);
  if (!path_tag.has_value()) {
    return;
  }

  if (!run_result.agent_result.tags.has_value()) {
    run_result.agent_result.tags = std::vector<std::string>{};
  }

  auto& tags = *run_result.agent_result.tags;
  clear_runtime_path_tags(tags);
  append_unique_tag(tags, *path_tag);
}

[[nodiscard]] bool degraded_ready_allowed(
    const profiles::RuntimePolicySnapshot& snapshot) {
  const auto& degrade_policy = snapshot.degrade_policy();
  return degrade_policy.allow_model_failover || degrade_policy.allow_budget_degrade;
}

[[nodiscard]] std::vector<std::string> make_init_degraded_reasons(
    const RuntimeDependencyReadiness& readiness) {
  std::vector<std::string> reasons;
  if (!readiness.degraded) {
    return reasons;
  }

  append_unique_value(reasons, "runtime_optional_port_gap");
  for (const auto& port : readiness.missing_optional_ports) {
    append_unique_value(reasons, std::string{"runtime_missing_optional:"} + port);
  }

  return reasons;
}

[[nodiscard]] bool uses_runtime_local_stub_path(const std::string& diagnostics) {
  return diagnostics.find("cognition_ports=stub_runtime_path") != std::string::npos;
}

void apply_runtime_readiness_tags(const RuntimeDependencyReadiness& readiness,
                                  contracts::AgentResult& result) {
  if (!readiness.degraded) {
    return;
  }

  if (!result.tags.has_value()) {
    result.tags = std::vector<std::string>{};
  }

  append_unique_tag(*result.tags, "runtime_readiness:degraded");
  append_unique_tag(*result.tags, "runtime_degraded_reason:optional_port_gap");
  for (const auto& port : readiness.missing_optional_ports) {
    append_unique_tag(*result.tags, std::string{"runtime_missing_optional:"} + port);
    if (port == "knowledge") {
      append_unique_tag(*result.tags, "knowledge_unavailable");
    } else if (port == "llm") {
      append_unique_tag(*result.tags, "llm_unavailable");
    } else {
      append_unique_tag(*result.tags, port + "_unavailable");
    }
  }
}

[[nodiscard]] contracts::AgentResult make_failed_result(
    const std::optional<std::string>& request_id,
    const std::optional<std::string>& trace_id,
    std::string message) {
  contracts::AgentResult result;
  result.result_id = std::string{"rt-fail-closed-"} + std::to_string(current_time_ms());
  result.status = contracts::AgentResultStatus::Failed;
  result.result_code = static_cast<std::int32_t>(contracts::ResultCode::RuntimeRetryExhausted);
  result.response_text = std::move(message);
  result.task_completed = false;
  result.created_at = current_time_ms();
  result.request_id = request_id;
  result.trace_id = trace_id;
  return result;
}

[[nodiscard]] StubMainLoopExit to_stub_main_loop_exit(
    const RuntimeStubMainLoopExit main_loop_exit) {
  switch (main_loop_exit) {
    case RuntimeStubMainLoopExit::DirectResponse:
      return StubMainLoopExit::DirectResponse;
    case RuntimeStubMainLoopExit::ToolRound:
      return StubMainLoopExit::ToolRound;
    case RuntimeStubMainLoopExit::WaitingClarify:
      return StubMainLoopExit::WaitingClarify;
  }

  return StubMainLoopExit::DirectResponse;
}

[[nodiscard]] StubRecoveryExit to_stub_recovery_exit(
    const RuntimeStubRecoveryExit recovery_exit) {
  switch (recovery_exit) {
    case RuntimeStubRecoveryExit::ContinueResponse:
      return StubRecoveryExit::ContinueResponse;
    case RuntimeStubRecoveryExit::AbortSafe:
      return StubRecoveryExit::AbortSafe;
  }

  return StubRecoveryExit::ContinueResponse;
}

[[nodiscard]] OrchestratorStubPorts make_orchestrator_stub_ports(
    const RuntimeDependencySet& dependency_set) {
  return OrchestratorStubPorts{
      .reject_preflight = dependency_set.local_stub_ports.reject_preflight,
      .main_loop_exit = to_stub_main_loop_exit(
          dependency_set.local_stub_ports.main_loop_exit),
      .recovery_exit = to_stub_recovery_exit(
          dependency_set.local_stub_ports.recovery_exit),
      .success_response_text = dependency_set.local_stub_ports.success_response_text,
      .fail_safe_response_text = dependency_set.local_stub_ports.fail_safe_response_text,
      .waiting_response_text = dependency_set.local_stub_ports.waiting_response_text,
  };
}

[[nodiscard]] bool is_active_waiting_session(const SessionSnapshot& session_snapshot) {
  return session_snapshot.has_active_checkpoint() &&
         session_snapshot.pending_interaction.has_value() &&
         session_snapshot.pending_interaction->active();
}

[[nodiscard]] bool compose_cognition_ports_if_needed(
    const AgentInitRequest& request,
    AgentInitResult& result) {
  if (request.dependency_set == nullptr || request.policy_snapshot == nullptr) {
    return false;
  }

  const auto needs_cognition_engine = request.dependency_set->cognition_engine == nullptr;
  const auto needs_response_builder = request.dependency_set->response_builder == nullptr;
  if (!needs_cognition_engine && !needs_response_builder) {
    return true;
  }

  const auto requires_policy_projected_cognition_ports =
      request.dependency_set->memory_manager != nullptr ||
      request.dependency_set->tool_manager != nullptr ||
      request.dependency_set->llm_manager != nullptr ||
      request.dependency_set->knowledge_service != nullptr;
  if (!requires_policy_projected_cognition_ports) {
    result.diagnostics = "cognition_ports=stub_runtime_path";
    return true;
  }

  if (needs_cognition_engine) {
    auto cognition_engine = cognition::create_cognition_engine(
        *request.policy_snapshot,
        cognition::CognitionRuntimeDependencies{
            .llm_manager = request.dependency_set->llm_manager,
            .policy_snapshot = request.policy_snapshot,
        .audit_logger = request.dependency_set->audit_logger,
        .metrics_provider = request.dependency_set->metrics_provider,
        .tracer_provider = request.dependency_set->tracer_provider,
        });
    if (!cognition_engine) {
      result.health_summary =
          "runtime facade rejected policy snapshot during cognition engine composition";
      result.error_code = static_cast<std::int32_t>(contracts::ResultCode::PolicyDenied);
      result.diagnostics =
          "cognition_engine composition failed: runtime policy snapshot missing canonical stage routes or profile projection";
      return false;
    }
    request.dependency_set->cognition_engine =
        std::shared_ptr<cognition::ICognitionEngine>(cognition_engine.release());
  }

  if (needs_response_builder) {
    auto response_builder = cognition::create_response_builder(
        *request.policy_snapshot,
        cognition::CognitionRuntimeDependencies{
            .llm_manager = request.dependency_set->llm_manager,
            .policy_snapshot = request.policy_snapshot,
        .audit_logger = request.dependency_set->audit_logger,
        .metrics_provider = request.dependency_set->metrics_provider,
        .tracer_provider = request.dependency_set->tracer_provider,
        });
    if (!response_builder) {
      result.health_summary =
          "runtime facade rejected policy snapshot during response builder composition";
      result.error_code = static_cast<std::int32_t>(contracts::ResultCode::PolicyDenied);
      result.diagnostics =
          "response_builder composition failed: runtime policy snapshot missing canonical stage routes or profile projection";
      return false;
    }
    request.dependency_set->response_builder =
        std::shared_ptr<cognition::IResponseBuilder>(response_builder.release());
  }

  result.diagnostics = "cognition_ports=composed_from_policy_snapshot";
  return true;
}

}  // namespace

class AgentFacade::State {
 public:
  AgentInitResult init(const AgentInitRequest& request) {
    AgentInitResult result;
    result.runtime_instance_id = request.runtime_instance_id;
    result.resolved_profile_id = request.profile_id;

    if (!request.has_minimum_requirements()) {
      result.health_summary = "runtime facade skeleton rejected incomplete init request";
      result.error_code = static_cast<std::int32_t>(contracts::ResultCode::RuntimeRetryExhausted);
      result.diagnostics =
          "runtime_instance_id, profile_id, policy_snapshot and dependency_set are required";
      return result;
    }

    if (!request.policy_snapshot->effective_profile_id().empty()) {
      result.resolved_profile_id = request.policy_snapshot->effective_profile_id();
    }

    if (!compose_cognition_ports_if_needed(request, result)) {
      return result;
    }

    auto readiness = request.dependency_set->describe_readiness();
    result.missing_required_ports = readiness.missing_required_ports;
    result.missing_optional_ports = readiness.missing_optional_ports;
    result.degraded_reasons = make_init_degraded_reasons(readiness);
    const bool runtime_local_stub_path =
        uses_runtime_local_stub_path(result.diagnostics);
    if (runtime_local_stub_path) {
      readiness.has_required_ports = true;
      readiness.has_optional_ports = true;
      readiness.degraded = false;
      readiness.missing_required_ports.clear();
      readiness.missing_optional_ports.clear();
      result.missing_required_ports.clear();
      result.missing_optional_ports.clear();
      result.degraded_reasons.clear();
      append_diagnostic_fragment(result.diagnostics, "readiness=stub_runtime_path");
    } else {
      append_diagnostic_fragment(result.diagnostics,
                                 std::string{"readiness="} + readiness.summary());
      if (!result.degraded_reasons.empty()) {
        append_diagnostic_fragment(
            result.diagnostics,
            std::string{"degraded_reasons="} + join_values(result.degraded_reasons));
      }
    }
    if (!readiness.has_required_ports) {
      result.health_summary =
          "runtime facade rejected missing required dependency ports";
      result.error_code =
          static_cast<std::int32_t>(contracts::ResultCode::RuntimeRetryExhausted);
      return result;
    }

    if (readiness.degraded && !degraded_ready_allowed(*request.policy_snapshot)) {
      result.health_summary =
          "runtime facade rejected degraded optional-port init under current profile";
      result.error_code =
          static_cast<std::int32_t>(contracts::ResultCode::PolicyDenied);
      return result;
    }

    auto orchestrator = std::make_unique<AgentOrchestrator>(OrchestratorComposition{
        .runtime_instance_id = request.runtime_instance_id,
        .profile_id = result.resolved_profile_id,
        .policy_snapshot = request.policy_snapshot,
        .dependency_set = request.dependency_set,
        .stub_ports = make_orchestrator_stub_ports(*request.dependency_set),
        .fsm_factory = {},
        .default_runtime_budget = request.policy_snapshot->runtime_budget(),
    });
    orchestrator->seed_for_test(request.dependency_set->seeded_waiting_session,
                                request.dependency_set->seeded_checkpoints);

    root_ = RuntimeCompositionRoot{
        .runtime_instance_id = request.runtime_instance_id,
      .profile_id = result.resolved_profile_id,
        .policy_snapshot = request.policy_snapshot,
        .dependency_set = request.dependency_set,
        .orchestrator = std::move(orchestrator),
        .waiting_session = request.dependency_set->seeded_waiting_session,
        .readiness = readiness,
        .degraded = readiness.degraded,
    };
    initialized_ = true;

    result.accepted = true;
    result.degraded = readiness.degraded;
    result.projected_readiness = runtime_local_stub_path
                                     ? AgentInitReadinessLevel::StubReady
                                 : readiness.degraded
                                     ? AgentInitReadinessLevel::DegradedReady
                                     : AgentInitReadinessLevel::DefaultReady;
    append_diagnostic_fragment(
      result.diagnostics,
      std::string{"entrypoint_ready="} +
        (runtime_local_stub_path
           ? "stub-ready"
           : readiness.degraded ? "degraded-ready" : "default-ready"));
    result.health_summary = readiness.degraded
                                ? result.degraded_reasons.empty()
                                      ? "runtime facade initialized in degraded mode"
                                      : std::string{"runtime facade initialized in degraded mode: "} +
                                            join_values(result.degraded_reasons)
                : runtime_local_stub_path
                  ? "runtime facade skeleton initialized"
                                : result.diagnostics.empty()
                                      ? "runtime facade skeleton initialized"
                                      : "runtime facade initialized with policy-projected cognition ports";
    return result;
  }

  contracts::AgentResult handle(const contracts::AgentRequest& request) {
    if (!initialized_) {
      return make_failed_result(request.request_id, request.trace_id,
                                "runtime facade is not initialized");
    }

    if (!root_.orchestrator) {
      return make_failed_result(request.request_id,
                                request.trace_id,
                                "runtime facade is missing orchestrator composition");
    }

    auto run_result = root_.orchestrator->run_once(request);
    apply_runtime_path_tag(root_, run_result);
    apply_runtime_readiness_tags(root_.readiness, run_result.agent_result);
    update_waiting_session(run_result);
    return run_result.agent_result;
  }

  contracts::AgentResult resume(const ResumeHandleRequest& request) {
    if (!initialized_) {
      return make_failed_result(optional_string(request.request_id),
                                optional_string(request.trace_context),
                                "runtime facade is not initialized");
    }

    if (!request.has_minimum_requirements()) {
      return make_failed_result(optional_string(request.request_id),
                                optional_string(request.trace_context),
                                "resume request is missing required checkpoint anchors");
    }

    if (!root_.orchestrator) {
      return make_failed_result(optional_string(request.request_id),
                                optional_string(request.trace_context),
                                "runtime facade is missing orchestrator composition");
    }

    SessionSnapshot resume_session;
    if (root_.waiting_session.has_value() &&
        is_active_waiting_session(*root_.waiting_session)) {
      if (root_.waiting_session->session_id != request.session_id) {
        return make_failed_result(optional_string(request.request_id),
                                  optional_string(request.trace_context),
                                  "runtime resume request session does not match active waiting session");
      }

      if (root_.waiting_session->active_checkpoint_ref != request.checkpoint_ref) {
        return make_failed_result(optional_string(request.request_id),
                                  optional_string(request.trace_context),
                                  "runtime resume request checkpoint does not match active waiting anchor");
      }

      resume_session = *root_.waiting_session;
    } else {
      resume_session = SessionSnapshot{
          .session_id = request.session_id,
          .request_id = request.request_id,
          .turn_index = 0,
          .active_checkpoint_ref = request.checkpoint_ref,
          .fsm_state = RuntimeState::WaitingClarify,
          .budget_snapshot_ref = std::nullopt,
          .pending_interaction = std::nullopt,
          .last_result_summary = std::nullopt,
      };
    }

    if (request.resume_token !=
        make_resume_binding_token(request.session_id, request.checkpoint_ref)) {
      return make_failed_result(optional_string(request.request_id),
                                optional_string(request.trace_context),
                                "runtime resume request token does not match waiting checkpoint binding");
    }

    auto run_result = root_.orchestrator->handle_waiting_state(resume_session,
                                                               request);
    apply_runtime_path_tag(root_, run_result);
    apply_runtime_readiness_tags(root_.readiness, run_result.agent_result);
    update_waiting_session(run_result);
    return run_result.agent_result;
  }

  bool stop(std::uint32_t timeout_ms) {
    (void)timeout_ms;
    initialized_ = false;
    root_ = RuntimeCompositionRoot{};
    return true;
  }

 private:
  void update_waiting_session(const OrchestratorRunResult& run_result) {
    if (!run_result.effective_session.has_value()) {
      return;
    }

    if (is_active_waiting_session(*run_result.effective_session)) {
      root_.waiting_session = run_result.effective_session;
      return;
    }

    root_.waiting_session = std::nullopt;
  }

  bool initialized_ = false;
  RuntimeCompositionRoot root_;
};

AgentFacade::AgentFacade() : state_(std::make_unique<State>()) {}

AgentFacade::~AgentFacade() = default;

AgentInitResult AgentFacade::init(const AgentInitRequest& request) {
  return state_->init(request);
}

contracts::AgentResult AgentFacade::handle(const contracts::AgentRequest& request) {
  return state_->handle(request);
}

contracts::AgentResult AgentFacade::resume(const ResumeHandleRequest& request) {
  return state_->resume(request);
}

bool AgentFacade::stop(std::uint32_t timeout_ms) {
  return state_->stop(timeout_ms);
}

}  // namespace dasall::runtime