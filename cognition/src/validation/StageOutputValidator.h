#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "decision/ActionDecision.h"
#include "error/ErrorInfo.h"
#include "llm/CognitionLlmBridge.h"
#include "plan/PlanGraph.h"
#include "response/ResponseBuildResult.h"

namespace dasall::cognition::validation {

struct EnumConstraint {
  std::string field_path;
  std::vector<std::string> allowed_values;
};

struct NumericConstraint {
  std::string field_path;
  std::optional<double> min_value;
  std::optional<double> max_value;
};

struct ListConstraint {
  std::string field_path;
  std::size_t min_items = 0U;
  std::optional<std::size_t> max_items;
};

struct StageSchemaSpec {
  std::string stage_name;
  std::vector<std::string> required_fields;
  std::vector<EnumConstraint> enum_constraints;
  std::vector<NumericConstraint> numeric_bounds;
  std::vector<ListConstraint> list_constraints;
  std::vector<std::string> stage_specific_invariants;
};

enum class ValidationIssueCode : std::uint8_t {
  MissingRequiredField = 0,
  InvalidEnumLiteral = 1,
  NumericOutOfRange = 2,
  ListSizeOutOfRange = 3,
  PlanGraphInvariant = 4,
  ActionDecisionInvariant = 5,
  ResponseEnvelopeInvariant = 6,
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
      const decision::ActionDecision& action_decision) const;
  [[nodiscard]] ValidationResult validate_response_envelope(
      const ResponseBuildResult& response_result) const;
};

}  // namespace dasall::cognition::validation