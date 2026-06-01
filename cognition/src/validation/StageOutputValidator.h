#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/ReflectionDecision.h"
#include "decision/ActionDecision.h"
#include "error/ErrorInfo.h"
#include "llm/CognitionLlmBridge.h"
#include "perception/PerceptionResult.h"
#include "plan/PlanGraph.h"
#include "response/ResponseBuildResult.h"
#include "validation/StageSchemaRegistry.h"

namespace dasall::cognition::validation {

enum class ValidationIssueCode : std::uint8_t {
  MissingRequiredField = 0,
  InvalidEnumLiteral = 1,
  NumericOutOfRange = 2,
  ListSizeOutOfRange = 3,
  PlanGraphInvariant = 4,
  ActionDecisionInvariant = 5,
  ResponseEnvelopeInvariant = 6,
  MalformedJson = 7,
  UnknownField = 8,
  ReflectionDecisionInvariant = 9,
  PerceptionInvariant = 10,
};

struct ValidationIssue {
  ValidationIssueCode code = ValidationIssueCode::MissingRequiredField;
  std::string field_path;
  std::string message;
};

struct ValidationIssueSet {
  std::vector<ValidationIssue> issues;

  void add(ValidationIssueCode code, std::string field_path, std::string message);
  [[nodiscard]] bool empty() const { return issues.empty(); }
};

struct ValidationResult {
  bool ok = false;
  ValidationIssueSet issue_set;
  std::optional<contracts::ErrorInfo> error_info;
  std::vector<std::string> diagnostics;
};

class StageOutputValidator {
 public:
  [[nodiscard]] ValidationResult validate_stage_output(
      const llm_bridge::StageLlmCallResult& stage_result,
      const StageSchemaSpec& schema_spec) const;
  [[nodiscard]] ValidationResult validate_plan_graph_invariants(
      const plan::PlanGraph& plan_graph,
      std::uint32_t max_plan_nodes,
      std::uint32_t max_plan_depth) const;
  [[nodiscard]] ValidationResult validate_action_decision_invariants(
      const decision::ActionDecision& action_decision,
      const plan::PlanGraph* active_plan = nullptr) const;
  [[nodiscard]] ValidationResult validate_perception_invariants(
      const perception::PerceptionResult& perception_result) const;
    [[nodiscard]] ValidationResult validate_reflection_decision_invariants(
      const contracts::ReflectionDecision& reflection_decision) const;
  [[nodiscard]] ValidationResult validate_response_envelope(
      const ResponseBuildResult& response_result) const;
};

}  // namespace dasall::cognition::validation