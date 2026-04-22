#include "AgentFacade.h"

#include <chrono>
#include <utility>

#include "error/ResultCode.h"

namespace dasall::runtime {
namespace {

struct RuntimeCompositionRoot {
  std::string runtime_instance_id;
  std::string profile_id;
  std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot;
  std::shared_ptr<RuntimeDependencySet> dependency_set;
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

[[nodiscard]] HandleOptions normalize_handle_options(const contracts::AgentRequest& request) {
  HandleOptions options;
  options.request_id = request.request_id.value_or("");
  options.session_id = request.session_id.value_or("");
  options.entrypoint = "handle";
  options.trace_context = request.trace_id.value_or("");
  return options;
}

[[nodiscard]] HandleOptions normalize_resume_options(const ResumeHandleRequest& request) {
  HandleOptions options = request.override_options.value_or(HandleOptions{});
  if (options.request_id.empty()) {
    options.request_id = request.request_id;
  }
  if (options.session_id.empty()) {
    options.session_id = request.session_id;
  }
  if (!options.checkpoint_ref.has_value()) {
    options.checkpoint_ref = request.checkpoint_ref;
  }
  if (options.entrypoint.empty()) {
    options.entrypoint = "resume";
  }
  if (options.trace_context.empty()) {
    options.trace_context = request.trace_context;
  }
  return options;
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

    root_ = RuntimeCompositionRoot{
        .runtime_instance_id = request.runtime_instance_id,
        .profile_id = request.profile_id,
        .policy_snapshot = request.policy_snapshot,
        .dependency_set = request.dependency_set,
        .degraded = false,
    };
    initialized_ = true;

    result.accepted = true;
    result.health_summary = "runtime facade skeleton initialized";
    return result;
  }

  contracts::AgentResult handle(const contracts::AgentRequest& request) {
    if (!initialized_) {
      return make_failed_result(request.request_id, request.trace_id,
                                "runtime facade is not initialized");
    }

    const HandleOptions options = normalize_handle_options(request);
    (void)options;
    return make_failed_result(
        request.request_id, request.trace_id,
        "runtime control-plane skeleton is initialized but AgentOrchestrator is not wired yet");
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

    const HandleOptions options = normalize_resume_options(request);
    (void)options;
    return make_failed_result(optional_string(request.request_id),
                              optional_string(request.trace_context),
                              "runtime resume skeleton is not wired to checkpoint/session flow yet");
  }

  bool stop(std::uint32_t timeout_ms) {
    (void)timeout_ms;
    initialized_ = false;
    root_ = RuntimeCompositionRoot{};
    return true;
  }

 private:
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