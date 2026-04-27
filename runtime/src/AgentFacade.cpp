#include "AgentFacade.h"

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
  bool degraded = false;
};

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

  if (needs_cognition_engine) {
    auto cognition_engine = cognition::create_cognition_engine(
        *request.policy_snapshot,
        cognition::CognitionRuntimeDependencies{
            .llm_manager = request.dependency_set->llm_manager,
            .policy_snapshot = request.policy_snapshot,
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

    auto orchestrator = std::make_unique<AgentOrchestrator>(OrchestratorComposition{
        .runtime_instance_id = request.runtime_instance_id,
        .profile_id = result.resolved_profile_id,
        .policy_snapshot = request.policy_snapshot,
        .dependency_set = request.dependency_set,
        .stub_ports = make_orchestrator_stub_ports(*request.dependency_set),
        .fsm_factory = {},
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
        .degraded = false,
    };
    initialized_ = true;

    result.accepted = true;
    result.health_summary = result.diagnostics.empty()
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

    const auto run_result = root_.orchestrator->run_once(request);
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

    if (!root_.waiting_session.has_value() ||
        !is_active_waiting_session(*root_.waiting_session)) {
      return make_failed_result(optional_string(request.request_id),
                                optional_string(request.trace_context),
                                "runtime facade has no active waiting session for resume request");
    }

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

    if (request.resume_token !=
        make_resume_binding_token(request.session_id, request.checkpoint_ref)) {
      return make_failed_result(optional_string(request.request_id),
                                optional_string(request.trace_context),
                                "runtime resume request token does not match waiting checkpoint binding");
    }

    const auto run_result = root_.orchestrator->handle_waiting_state(*root_.waiting_session,
                                                                     request);
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