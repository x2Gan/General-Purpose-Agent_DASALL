#include "AgentFacade.h"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>

#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "AgentOrchestrator.h"
#include "RuntimeDependencySet.h"
#include "error/ResultCode.h"
#include "logging/RuntimeStructuredLogUtils.h"

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

[[nodiscard]] std::optional<std::string> runtime_instance_id_attr(
    const RuntimeCompositionRoot& root) {
  return optional_string(root.runtime_instance_id);
}

[[nodiscard]] std::shared_ptr<infra::logging::ILogger> logger_from_root(
    const RuntimeCompositionRoot& root) {
  if (root.dependency_set == nullptr) {
    return nullptr;
  }

  return root.dependency_set->logger;
}

[[nodiscard]] std::shared_ptr<infra::logging::ILogger> logger_from_request(
    const AgentInitRequest& request) {
  if (request.dependency_set == nullptr) {
    return nullptr;
  }

  return request.dependency_set->logger;
}

[[nodiscard]] const char* agent_result_status_name(
    const std::optional<contracts::AgentResultStatus>& status) {
  if (!status.has_value()) {
    return "Unspecified";
  }

  switch (*status) {
    case contracts::AgentResultStatus::Unspecified:
      return "Unspecified";
    case contracts::AgentResultStatus::Completed:
      return "Completed";
    case contracts::AgentResultStatus::Failed:
      return "Failed";
    case contracts::AgentResultStatus::PartiallyCompleted:
      return "PartiallyCompleted";
    case contracts::AgentResultStatus::Cancelled:
      return "Cancelled";
    case contracts::AgentResultStatus::Timeout:
      return "Timeout";
  }

  return "Unknown";
}

[[nodiscard]] infra::LogLevel facade_init_log_level(const AgentInitResult& result) {
  return result.accepted ? infra::LogLevel::Info : infra::LogLevel::Error;
}

[[nodiscard]] infra::LogLevel facade_result_log_level(
    const contracts::AgentResult& result) {
  if (!result.status.has_value()) {
    return infra::LogLevel::Warn;
  }

  switch (*result.status) {
    case contracts::AgentResultStatus::Completed:
      return infra::LogLevel::Info;
    case contracts::AgentResultStatus::PartiallyCompleted:
    case contracts::AgentResultStatus::Cancelled:
      return infra::LogLevel::Warn;
    case contracts::AgentResultStatus::Failed:
    case contracts::AgentResultStatus::Timeout:
      return infra::LogLevel::Error;
    case contracts::AgentResultStatus::Unspecified:
      return infra::LogLevel::Warn;
  }

  return infra::LogLevel::Warn;
}

[[nodiscard]] std::optional<std::string> find_runtime_path_tag(
    const contracts::AgentResult& result) {
  if (!result.tags.has_value()) {
    return std::nullopt;
  }

  const auto tag_it = std::find_if(
      result.tags->begin(),
      result.tags->end(),
      [](const std::string& tag) {
        return tag.rfind(kRuntimePathTagPrefix, 0) == 0;
      });
  if (tag_it == result.tags->end()) {
    return std::nullopt;
  }

  return *tag_it;
}

void emit_facade_init_log(
    const std::shared_ptr<infra::logging::ILogger>& logger,
    const AgentInitRequest& request,
    const AgentInitResult& result) {
  infra::LogEvent::AttributeMap attrs;
  detail::add_string_attr(attrs, "profile_id", request.profile_id);
  detail::add_string_attr(attrs, "resolved_profile_id", result.resolved_profile_id);
  detail::add_bool_attr(attrs, "accepted", result.accepted);
  detail::add_bool_attr(attrs, "degraded", result.degraded);
  detail::add_bool_attr(attrs, "cold_start", request.cold_start);
  detail::add_string_attr(attrs, "readiness_level", result.readiness_label());
  detail::add_integer_attr(attrs,
                           "missing_required_port_count",
                           result.missing_required_ports.size());
  detail::add_integer_attr(attrs,
                           "missing_optional_port_count",
                           result.missing_optional_ports.size());
  detail::add_integer_attr(attrs, "degraded_reason_count", result.degraded_reasons.size());
  if (result.error_code != 0) {
    detail::add_integer_attr(attrs, "error_code", result.error_code);
  }
  detail::add_string_attr(
      attrs,
      "detail",
      !result.diagnostics.empty() ? result.diagnostics : result.health_summary);
  detail::emit_runtime_log(
      logger,
      facade_init_log_level(result),
      "runtime.facade.init",
      "agent_facade",
      optional_string(result.runtime_instance_id),
      std::move(attrs));
}

void emit_facade_result_log(
    const std::shared_ptr<infra::logging::ILogger>& logger,
    const std::optional<std::string>& runtime_instance_id,
    const std::string_view event_name,
    const std::optional<std::string>& request_id,
    const std::optional<std::string>& session_id,
    const std::optional<std::string>& trace_id,
    const std::optional<std::string>& checkpoint_ref,
    const contracts::AgentResult& result,
    const std::string_view outcome_reason,
    const bool waiting_session_active) {
  infra::LogEvent::AttributeMap attrs;
  detail::add_optional_string_attr(attrs, "request_id", request_id);
  detail::add_optional_string_attr(attrs, "session_id", session_id);
  detail::add_optional_string_attr(attrs, "trace_id", trace_id);
  detail::add_optional_string_attr(attrs, "checkpoint_ref", checkpoint_ref);
  detail::add_optional_string_attr(attrs, "result_id", result.result_id);
  detail::add_string_attr(attrs, "result_status", agent_result_status_name(result.status));
  if (result.result_code.has_value()) {
    detail::add_integer_attr(attrs, "result_code", *result.result_code);
  }
  detail::add_bool_attr(attrs, "task_completed", result.task_completed.value_or(false));
  detail::add_bool_attr(attrs, "waiting_session_active", waiting_session_active);
  detail::add_optional_string_attr(attrs, "runtime_path_tag", find_runtime_path_tag(result));
  if (result.tags.has_value()) {
    detail::add_integer_attr(attrs, "tag_count", result.tags->size());
  }
  detail::add_string_attr(attrs, "outcome_reason", outcome_reason);
  detail::emit_runtime_log(
      logger,
      facade_result_log_level(result),
      event_name,
      "agent_facade",
      runtime_instance_id,
      std::move(attrs));
}

void emit_facade_stop_log(
    const std::shared_ptr<infra::logging::ILogger>& logger,
    const std::optional<std::string>& runtime_instance_id,
    const bool was_initialized,
    const std::uint32_t timeout_ms) {
  infra::LogEvent::AttributeMap attrs;
  detail::add_bool_attr(attrs, "was_initialized", was_initialized);
  detail::add_bool_attr(attrs, "stopped", true);
  detail::add_integer_attr(attrs, "timeout_ms", timeout_ms);
  detail::emit_runtime_log(
      logger,
      infra::LogLevel::Info,
      "runtime.facade.stop",
      "agent_facade",
      runtime_instance_id,
      std::move(attrs));
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
        .logger = request.dependency_set->logger,
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
        .logger = request.dependency_set->logger,
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
    const auto init_logger = logger_from_request(request);

    if (!request.has_minimum_requirements()) {
      result.health_summary = "runtime facade skeleton rejected incomplete init request";
      result.error_code = static_cast<std::int32_t>(contracts::ResultCode::RuntimeRetryExhausted);
      result.diagnostics =
          "runtime_instance_id, profile_id, policy_snapshot and dependency_set are required";
      emit_facade_init_log(init_logger, request, result);
      return result;
    }

    if (!request.policy_snapshot->effective_profile_id().empty()) {
      result.resolved_profile_id = request.policy_snapshot->effective_profile_id();
    }

    if (!compose_cognition_ports_if_needed(request, result)) {
      emit_facade_init_log(init_logger, request, result);
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
        emit_facade_init_log(init_logger, request, result);
      return result;
    }

    if (readiness.degraded && !degraded_ready_allowed(*request.policy_snapshot)) {
      result.health_summary =
          "runtime facade rejected degraded optional-port init under current profile";
      result.error_code =
          static_cast<std::int32_t>(contracts::ResultCode::PolicyDenied);
        emit_facade_init_log(init_logger, request, result);
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
    emit_facade_init_log(init_logger, request, result);
    return result;
  }

  contracts::AgentResult handle(const contracts::AgentRequest& request) {
    if (!initialized_) {
      auto result = make_failed_result(request.request_id, request.trace_id,
                                       "runtime facade is not initialized");
      emit_facade_result_log(
          logger_from_root(root_),
          runtime_instance_id_attr(root_),
          "runtime.facade.handle",
          request.request_id,
          request.session_id,
          request.trace_id,
          std::nullopt,
          result,
          "not_initialized",
          false);
      return result;
    }

    if (!root_.orchestrator) {
      auto result = make_failed_result(request.request_id,
                                       request.trace_id,
                                       "runtime facade is missing orchestrator composition");
      emit_facade_result_log(
          logger_from_root(root_),
          runtime_instance_id_attr(root_),
          "runtime.facade.handle",
          request.request_id,
          request.session_id,
          request.trace_id,
          std::nullopt,
          result,
          "missing_orchestrator",
          root_.waiting_session.has_value() &&
              is_active_waiting_session(*root_.waiting_session));
      return result;
    }

    auto run_result = root_.orchestrator->run_once(request);
    apply_runtime_path_tag(root_, run_result);
    apply_runtime_readiness_tags(root_.readiness, run_result.agent_result);
    update_waiting_session(run_result);
    emit_facade_result_log(
        logger_from_root(root_),
        runtime_instance_id_attr(root_),
        "runtime.facade.handle",
        request.request_id,
        request.session_id,
        request.trace_id,
        run_result.agent_result.checkpoint_ref,
        run_result.agent_result,
        "completed",
        root_.waiting_session.has_value() && is_active_waiting_session(*root_.waiting_session));
    return run_result.agent_result;
  }

  contracts::AgentResult resume(const ResumeHandleRequest& request) {
    if (!initialized_) {
      auto result = make_failed_result(optional_string(request.request_id),
                                       optional_string(request.trace_context),
                                       "runtime facade is not initialized");
      emit_facade_result_log(
          logger_from_root(root_),
          runtime_instance_id_attr(root_),
          "runtime.facade.resume",
          optional_string(request.request_id),
          optional_string(request.session_id),
          optional_string(request.trace_context),
          optional_string(request.checkpoint_ref),
          result,
          "not_initialized",
          false);
      return result;
    }

    if (!request.has_minimum_requirements()) {
      auto result = make_failed_result(optional_string(request.request_id),
                                       optional_string(request.trace_context),
                                       "resume request is missing required checkpoint anchors");
      emit_facade_result_log(
          logger_from_root(root_),
          runtime_instance_id_attr(root_),
          "runtime.facade.resume",
          optional_string(request.request_id),
          optional_string(request.session_id),
          optional_string(request.trace_context),
          optional_string(request.checkpoint_ref),
          result,
          "missing_required_anchors",
          root_.waiting_session.has_value() &&
              is_active_waiting_session(*root_.waiting_session));
      return result;
    }

    if (!root_.orchestrator) {
      auto result = make_failed_result(optional_string(request.request_id),
                                       optional_string(request.trace_context),
                                       "runtime facade is missing orchestrator composition");
      emit_facade_result_log(
          logger_from_root(root_),
          runtime_instance_id_attr(root_),
          "runtime.facade.resume",
          optional_string(request.request_id),
          optional_string(request.session_id),
          optional_string(request.trace_context),
          optional_string(request.checkpoint_ref),
          result,
          "missing_orchestrator",
          root_.waiting_session.has_value() &&
              is_active_waiting_session(*root_.waiting_session));
      return result;
    }

    SessionSnapshot resume_session;
    if (root_.waiting_session.has_value() &&
        is_active_waiting_session(*root_.waiting_session)) {
      if (root_.waiting_session->session_id != request.session_id) {
        auto result = make_failed_result(optional_string(request.request_id),
                                         optional_string(request.trace_context),
                                         "runtime resume request session does not match active waiting session");
        emit_facade_result_log(
            logger_from_root(root_),
            runtime_instance_id_attr(root_),
            "runtime.facade.resume",
            optional_string(request.request_id),
            optional_string(request.session_id),
            optional_string(request.trace_context),
            optional_string(request.checkpoint_ref),
            result,
            "session_mismatch",
            true);
        return result;
      }

      if (root_.waiting_session->active_checkpoint_ref != request.checkpoint_ref) {
        auto result = make_failed_result(optional_string(request.request_id),
                                         optional_string(request.trace_context),
                                         "runtime resume request checkpoint does not match active waiting anchor");
        emit_facade_result_log(
            logger_from_root(root_),
            runtime_instance_id_attr(root_),
            "runtime.facade.resume",
            optional_string(request.request_id),
            optional_string(request.session_id),
            optional_string(request.trace_context),
            optional_string(request.checkpoint_ref),
            result,
            "checkpoint_mismatch",
            true);
        return result;
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
      auto result = make_failed_result(optional_string(request.request_id),
                                       optional_string(request.trace_context),
                                       "runtime resume request token does not match waiting checkpoint binding");
      emit_facade_result_log(
          logger_from_root(root_),
          runtime_instance_id_attr(root_),
          "runtime.facade.resume",
          optional_string(request.request_id),
          optional_string(request.session_id),
          optional_string(request.trace_context),
          optional_string(request.checkpoint_ref),
          result,
          "token_mismatch",
          root_.waiting_session.has_value() &&
              is_active_waiting_session(*root_.waiting_session));
      return result;
    }

    auto run_result = root_.orchestrator->handle_waiting_state(resume_session,
                                                               request);
    apply_runtime_path_tag(root_, run_result);
    apply_runtime_readiness_tags(root_.readiness, run_result.agent_result);
    update_waiting_session(run_result);
    emit_facade_result_log(
        logger_from_root(root_),
        runtime_instance_id_attr(root_),
        "runtime.facade.resume",
        optional_string(request.request_id),
        optional_string(request.session_id),
        optional_string(request.trace_context),
        optional_string(request.checkpoint_ref),
        run_result.agent_result,
        "completed",
        root_.waiting_session.has_value() && is_active_waiting_session(*root_.waiting_session));
    return run_result.agent_result;
  }

  bool stop(std::uint32_t timeout_ms) {
    emit_facade_stop_log(
        logger_from_root(root_),
        runtime_instance_id_attr(root_),
        initialized_,
        timeout_ms);
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