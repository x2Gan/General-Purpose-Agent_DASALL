#include "MultiAgentRuntimeFold.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "observation/ObservationSource.h"

namespace dasall::multi_agent {
namespace {

[[nodiscard]] std::optional<contracts::Observation> build_fallback_observation(
    const MultiAgentExecutionReport& report) {
  if (!report.multi_agent_result.has_value()) {
    return std::nullopt;
  }

  contracts::Observation observation;
  observation.observation_id = std::string("observation:multi-agent:fallback");
  observation.source = contracts::ObservationSource::WorkerAgent;
  observation.success = !report.multi_agent_result->failure_summary.has_value();
  observation.payload = report.multi_agent_result->merged_result;
  observation.created_at = 1;
  observation.tags = std::vector<std::string>{
      "multi_agent",
      report.disabled ? "disabled" : "enabled",
  };
  return observation;
}

}  // namespace

MultiAgentRuntimeFoldResult fold_multi_agent_report_for_runtime(
    const MultiAgentExecutionReport& report) {
  MultiAgentRuntimeFoldResult result;
  result.compensation_hints = report.compensation_hints;
  result.recovery_request = report.recovery_request;
  result.audit_refs = report.audit_refs;

  if (!report.emitted_observations.empty()) {
    result.latest_observation = report.emitted_observations.back();
    return result;
  }

  result.latest_observation = build_fallback_observation(report);
  return result;
}

}  // namespace dasall::multi_agent