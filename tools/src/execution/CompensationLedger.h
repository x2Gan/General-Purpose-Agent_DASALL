#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ToolInvocationEnvelope.h"
#include "tool/ToolResult.h"

namespace dasall::tools::execution {

struct CompensationRecord {
  std::string step_id;
  std::string route_kind;
  std::optional<std::string> tool_call_id;
  std::optional<std::string> tool_name;
  std::vector<std::string> side_effects;
  std::vector<std::string> evidence_refs;
  bool reversible = true;
};

class CompensationLedger {
 public:
  CompensationLedger() = default;

  void register_result(
      std::string step_id,
      std::string route_kind,
      const contracts::ToolResult& result,
      bool reversible);
  void record_irreversible_effect(
      std::string step_id,
      std::string route_kind,
      const contracts::ToolResult& result);
  [[nodiscard]] std::optional<CompensationRecord> lookup(
      std::string_view step_id) const;
  [[nodiscard]] std::vector<ToolCompensationHint> build_hints() const;

 private:
  std::vector<CompensationRecord> records_;
};

}  // namespace dasall::tools::execution