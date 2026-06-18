#include "ReflectionLessonProjector.h"

#include <chrono>
#include <vector>

namespace dasall::runtime {
namespace {

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] std::string fallback_turn_id(const contracts::AgentRequest& request) {
  return request.request_id.value_or(std::string{"req-live-unary"}) +
         "-reflection-lesson";
}

}  // namespace

std::optional<memory::MemoryWritebackRequest>
ReflectionLessonProjector::make_writeback_request(
    const contracts::AgentRequest& request,
    const std::string& goal_id,
    const contracts::Observation& latest_observation,
    const cognition::CognitionReflectionResult& reflection_result) const {
  if (!reflection_result.reflection_lesson.has_value()) {
    return std::nullopt;
  }

  const auto request_id = request.request_id.value_or(std::string{"req-live-unary"});
  const auto session_id = request.session_id.value_or(std::string{"session-live-unary"});
  const auto created_at = latest_observation.created_at.value_or(current_time_ms());

  memory::MemoryWritebackRequest writeback_request;
  writeback_request.request_id = request_id;
  writeback_request.session_id = session_id;
  writeback_request.trace_id = request.trace_id.value_or(std::string{});
  writeback_request.turn.turn_id = fallback_turn_id(request);
  writeback_request.turn.session_id = session_id;
  writeback_request.turn.user_input = request.user_input.value_or(goal_id);
  writeback_request.turn.agent_response =
      reflection_result.reflection_lesson->lesson_summary.value_or(
          reflection_result.reflection_decision.has_value()
              ? reflection_result.reflection_decision->rationale.value_or(
                    std::string{"reflection lesson projected through runtime"})
              : std::string{"reflection lesson projected through runtime"});
  writeback_request.turn.created_at = created_at;
  writeback_request.turn.tags = std::vector<std::string>{
      "runtime", "cognition", "reflection_writeback"};
  if (latest_observation.observation_id.has_value() &&
      !latest_observation.observation_id->empty()) {
    writeback_request.turn.observation_refs =
        std::vector<std::string>{*latest_observation.observation_id};
  }
  writeback_request.reflection_lesson = *reflection_result.reflection_lesson;
  return writeback_request;
}

}  // namespace dasall::runtime
