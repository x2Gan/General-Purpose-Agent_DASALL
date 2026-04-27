#include "validation/InputBoundaryValidator.h"

#include <string>
#include <vector>

namespace dasall::cognition::validation {
namespace {

void require_non_empty(const std::string& value,
                       const char* field_name,
                       std::vector<std::string>& missing_fields) {
  if (value.empty()) {
    missing_fields.emplace_back(field_name);
  }
}

void require_present(const std::optional<std::string>& value,
                     const char* field_name,
                     std::vector<std::string>& missing_fields) {
  if (!value.has_value() || value->empty()) {
    missing_fields.emplace_back(field_name);
  }
}

template <typename T>
void require_present(const std::optional<T>& value,
                     const char* field_name,
                     std::vector<std::string>& missing_fields) {
  if (!value.has_value()) {
    missing_fields.emplace_back(field_name);
  }
}

template <typename T>
void require_vector_present(const std::optional<std::vector<T>>& value,
                            const char* field_name,
                            std::vector<std::string>& missing_fields) {
  if (!value.has_value()) {
    missing_fields.emplace_back(field_name);
  }
}

void validate_top_level_request_fields(const std::string& caller_domain,
                                       const std::string& request_id,
                                       const std::string& trace_id,
                                       const std::string& profile_id,
                                       std::vector<std::string>& missing_fields) {
  require_non_empty(caller_domain, "caller_domain", missing_fields);
  require_non_empty(request_id, "request_id", missing_fields);
  require_non_empty(trace_id, "trace_id", missing_fields);
  require_non_empty(profile_id, "profile_id", missing_fields);
}

void validate_goal_contract(const contracts::GoalContract& goal_contract,
                            std::vector<std::string>& missing_fields) {
  require_present(goal_contract.goal_id, "goal_contract.goal_id", missing_fields);
  require_present(goal_contract.request_id, "goal_contract.request_id", missing_fields);
  require_present(goal_contract.goal_description, "goal_contract.goal_description",
                  missing_fields);
  require_present(goal_contract.success_criteria, "goal_contract.success_criteria",
                  missing_fields);
  require_present(goal_contract.status, "goal_contract.status", missing_fields);
  require_present(goal_contract.created_at, "goal_contract.created_at", missing_fields);
}

void validate_context_packet(const contracts::ContextPacket& context_packet,
                             std::vector<std::string>& missing_fields) {
  require_present(context_packet.request_id, "context_packet.request_id", missing_fields);
  require_present(context_packet.user_turn, "context_packet.user_turn", missing_fields);
  require_present(context_packet.current_goal_summary,
                  "context_packet.current_goal_summary", missing_fields);
  require_vector_present(context_packet.recent_history, "context_packet.recent_history",
                         missing_fields);
}

void validate_belief_state(const contracts::BeliefState& belief_state,
                           std::vector<std::string>& missing_fields) {
  require_present(belief_state.request_id, "belief_state.request_id", missing_fields);
  require_vector_present(belief_state.confirmed_facts, "belief_state.confirmed_facts",
                         missing_fields);
  require_vector_present(belief_state.hypotheses, "belief_state.hypotheses", missing_fields);
  require_vector_present(belief_state.assumptions, "belief_state.assumptions", missing_fields);
  require_vector_present(belief_state.evidence_refs, "belief_state.evidence_refs",
                         missing_fields);
  require_present(belief_state.confidence, "belief_state.confidence", missing_fields);
}

void validate_observation(const contracts::Observation& observation,
                          std::vector<std::string>& missing_fields,
                          const char* prefix) {
  require_present(observation.observation_id,
                  (std::string(prefix) + ".observation_id").c_str(),
                  missing_fields);
  require_present(observation.source,
                  (std::string(prefix) + ".source").c_str(),
                  missing_fields);
  require_present(observation.success,
                  (std::string(prefix) + ".success").c_str(),
                  missing_fields);
  require_present(observation.payload,
                  (std::string(prefix) + ".payload").c_str(),
                  missing_fields);
  require_present(observation.created_at,
                  (std::string(prefix) + ".created_at").c_str(),
                  missing_fields);
}

[[nodiscard]] contracts::ErrorInfo make_validation_error(
    const std::vector<std::string>& missing_fields,
    const char* stage_name) {
  std::string message = "missing required input fields: ";
  for (std::size_t index = 0; index < missing_fields.size(); ++index) {
    if (index > 0U) {
      message.append(", ");
    }
    message.append(missing_fields[index]);
  }

  return contracts::ErrorInfo{
      .failure_type = contracts::classify_result_code(
          contracts::ResultCode::ValidationFieldMissing),
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = static_cast<int>(contracts::ResultCode::ValidationFieldMissing),
          .message = std::move(message),
          .stage = stage_name,
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "cognition",
          .ref_id = stage_name,
      },
  };
}

[[nodiscard]] InputBoundaryValidationResult finalize_validation(
    std::vector<std::string> missing_fields,
    const char* stage_name) {
  InputBoundaryValidationResult result;
  result.missing_fields = std::move(missing_fields);
  if (!result.missing_fields.empty()) {
    result.error_info = make_validation_error(result.missing_fields, stage_name);
  }
  return result;
}

}  // namespace

InputBoundaryValidationResult InputBoundaryValidator::validate_decide_request(
    const CognitionStepRequest& request) {
  std::vector<std::string> missing_fields;
  validate_top_level_request_fields(request.caller_domain, request.request_id, request.trace_id,
                                    request.profile_id, missing_fields);
  validate_goal_contract(request.goal_contract, missing_fields);
  validate_context_packet(request.context_packet, missing_fields);
  validate_belief_state(request.belief_state, missing_fields);
  if (request.latest_observation.has_value()) {
    validate_observation(*request.latest_observation, missing_fields, "latest_observation");
  }

  return finalize_validation(std::move(missing_fields), "cognition.decide.validation");
}

InputBoundaryValidationResult InputBoundaryValidator::validate_reflection_request(
    const ReflectionRequest& request) {
  std::vector<std::string> missing_fields;
  validate_top_level_request_fields(request.caller_domain, request.request_id, request.trace_id,
                                    request.profile_id, missing_fields);
  validate_goal_contract(request.goal_contract, missing_fields);
  validate_context_packet(request.context_packet, missing_fields);
  validate_belief_state(request.belief_state, missing_fields);
  validate_observation(request.latest_observation, missing_fields, "latest_observation");

  return finalize_validation(std::move(missing_fields), "cognition.reflect.validation");
}

InputBoundaryValidationResult InputBoundaryValidator::validate_response_request(
    const ResponseBuildRequest& request) {
  std::vector<std::string> missing_fields;
  validate_top_level_request_fields(request.caller_domain, request.request_id, request.trace_id,
                                    request.profile_id, missing_fields);
  validate_goal_contract(request.goal_contract, missing_fields);
  validate_context_packet(request.context_packet, missing_fields);
  if (request.belief_state.has_value()) {
    validate_belief_state(*request.belief_state, missing_fields);
  }
  if (request.latest_observation.has_value()) {
    validate_observation(*request.latest_observation, missing_fields, "latest_observation");
  }

  return finalize_validation(std::move(missing_fields), "cognition.response.validation");
}

}  // namespace dasall::cognition::validation