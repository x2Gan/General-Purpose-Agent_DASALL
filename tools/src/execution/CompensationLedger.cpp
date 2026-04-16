#include "execution/CompensationLedger.h"

#include <algorithm>
#include <utility>

namespace {

using dasall::contracts::ToolResult;
using dasall::tools::ToolCompensationHint;
using dasall::tools::execution::CompensationRecord;

[[nodiscard]] std::vector<std::string> collect_evidence_refs(
    std::string_view step_id,
    const ToolResult& result) {
  std::vector<std::string> evidence_refs;
  evidence_refs.emplace_back("workflow.step." + std::string(step_id));
  if (result.tool_call_id.has_value() && !result.tool_call_id->empty()) {
    evidence_refs.emplace_back("workflow.tool_call." + *result.tool_call_id);
  }
  if (result.tool_name.has_value() && !result.tool_name->empty()) {
    evidence_refs.emplace_back("workflow.tool." + *result.tool_name);
  }
  if (result.side_effects.has_value()) {
    for (const auto& side_effect : *result.side_effects) {
      if (!side_effect.empty()) {
        evidence_refs.emplace_back("workflow.side_effect." + side_effect);
      }
    }
  }
  return evidence_refs;
}

[[nodiscard]] std::optional<CompensationRecord> build_record(
    std::string step_id,
    std::string route_kind,
    const ToolResult& result,
    bool reversible) {
  const std::string step_ref = step_id;
  if (!result.side_effects.has_value() || result.side_effects->empty()) {
    return std::nullopt;
  }

  return CompensationRecord{
      .step_id = std::move(step_id),
      .route_kind = std::move(route_kind),
      .tool_call_id = result.tool_call_id,
      .tool_name = result.tool_name,
      .side_effects = *result.side_effects,
      .evidence_refs = collect_evidence_refs(step_ref, result),
      .reversible = reversible,
  };
}

}  // namespace

namespace dasall::tools::execution {

void CompensationLedger::register_result(
    std::string step_id,
    std::string route_kind,
    const contracts::ToolResult& result,
    bool reversible) {
  const auto record = build_record(step_id, route_kind, result, reversible);
  if (!record.has_value()) {
    return;
  }

  const auto existing = std::find_if(
      records_.begin(),
      records_.end(),
      [&](const CompensationRecord& candidate) {
        return candidate.step_id == record->step_id;
      });
  if (existing != records_.end()) {
    *existing = *record;
    return;
  }
  records_.push_back(*record);
}

void CompensationLedger::record_irreversible_effect(
    std::string step_id,
    std::string route_kind,
    const contracts::ToolResult& result) {
  register_result(
      std::move(step_id),
      std::move(route_kind),
      result,
      false);
}

std::optional<CompensationRecord> CompensationLedger::lookup(
    std::string_view step_id) const {
  const auto found = std::find_if(
      records_.begin(),
      records_.end(),
      [&](const CompensationRecord& candidate) {
        return candidate.step_id == step_id;
      });
  if (found == records_.end()) {
    return std::nullopt;
  }
  return *found;
}

std::vector<ToolCompensationHint> CompensationLedger::build_hints() const {
  std::vector<ToolCompensationHint> hints;
  for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
    if (!it->reversible || it->side_effects.empty()) {
      continue;
    }
    hints.push_back(ToolCompensationHint{
        .compensation_action = it->tool_name.value_or(it->step_id),
        .target_ref = it->tool_call_id.value_or(it->step_id),
        .reason_code = std::string("workflow.compensation_available"),
        .evidence_refs = it->evidence_refs,
    });
  }
  return hints;
}

}  // namespace dasall::tools::execution