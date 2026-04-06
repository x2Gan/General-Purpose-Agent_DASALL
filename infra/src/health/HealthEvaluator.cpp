#include "health/HealthEvaluator.h"

#include "health/HealthErrors.h"
#include "health/ProbeTypes.h"

#include <algorithm>
#include <chrono>
#include <string>

namespace dasall::infra {

namespace {

constexpr std::string_view kHealthEvaluatorPolicyVersion = "health-evaluator/default-v1";

[[nodiscard]] bool contains_component(const HealthSnapshot::ComponentList& components,
                                      std::string_view probe_name) {
  return std::find(components.begin(), components.end(), probe_name) != components.end();
}

}  // namespace

HealthEvaluator::HealthEvaluator(HealthEvaluatorOptions options)
    : options_(std::move(options)) {}

HealthPolicyEvaluationResult HealthEvaluator::evaluate(ProbeResultView results) const {
  if (!results.is_valid() || results.empty()) {
    const auto mapping = map_health_error_code(HealthErrorCode::PolicyInvalid);
    return HealthPolicyEvaluationResult::failure(
        mapping.result_code,
        "health evaluator requires a non-empty, valid ProbeResultView",
        "health.evaluate",
        "HealthEvaluator");
  }

  HealthSnapshot snapshot{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
      .version = static_cast<std::uint64_t>(current_time_unix_ms()),
      .timestamp = current_time_unix_ms(),
  };

  std::size_t failing_probe_count = 0;
  bool has_unhealthy_probe = false;
  for (std::size_t index = 0; index < results.size; ++index) {
    const ProbeResult& result = results.data[index];
    if (result.status == ProbeStatus::Healthy && !result.error_code.has_value()) {
      continue;
    }

    ++failing_probe_count;
    has_unhealthy_probe = has_unhealthy_probe || result.status == ProbeStatus::Unhealthy;
    if (!result.probe_name.empty() &&
        !contains_component(snapshot.failed_components, result.probe_name)) {
      snapshot.failed_components.push_back(result.probe_name);
    }
  }

  if (has_unhealthy_probe) {
    snapshot.liveness = false;
    snapshot.readiness = false;
    snapshot.degraded = false;
  } else if (failing_probe_count >= options_.degraded_threshold) {
    snapshot.liveness = true;
    snapshot.readiness = false;
    snapshot.degraded = true;
  }

  return HealthPolicyEvaluationResult::success(std::move(snapshot));
}

std::string_view HealthEvaluator::policy_version() const {
  return kHealthEvaluatorPolicyVersion;
}

HealthTransition HealthEvaluator::evaluate_transition(const HealthSnapshot& previous,
                                                      const HealthSnapshot& current) const {
  const HealthState previous_state = previous.state();
  const HealthState current_state = current.state();
  if (previous_state == HealthState::Unknown || current_state == HealthState::Unknown ||
      previous_state == current_state) {
    return HealthTransition{};
  }

  const std::string trigger_probe = current.failed_components.empty()
                                        ? std::string("health_evaluator")
                                        : current.failed_components.front();
  return HealthTransition{
      .from_state = previous_state,
      .to_state = current_state,
      .reason = std::string("health_state_changed:") +
                std::string(state_name(previous_state)) + "->" +
                std::string(state_name(current_state)),
      .trigger_probe = trigger_probe,
      .timestamp = current.timestamp > 0 ? current.timestamp : current_time_unix_ms(),
  };
}

std::int64_t HealthEvaluator::current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string_view HealthEvaluator::state_name(HealthState state) {
  switch (state) {
    case HealthState::Healthy:
      return "healthy";
    case HealthState::Degraded:
      return "degraded";
    case HealthState::Unhealthy:
      return "unhealthy";
    case HealthState::Unknown:
      break;
  }

  return "unknown";
}

}  // namespace dasall::infra