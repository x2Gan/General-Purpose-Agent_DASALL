#pragma once

#include <array>
#include <string_view>

namespace dasall::contracts {

// ContextBoundaryDecision captures whether a field name is allowed to exist in
// ContextPacket under ADR-006. This guard is intentionally field-name based
// so WP01 stays at object/boundary scope and avoids schema-level expansion.
enum class ContextBoundaryDecision {
  AllowField,
  RejectForbiddenField,
};

// ContextBoundaryResult provides a stable assertion surface for contract tests
// and CI gates: one boolean outcome, one normalized decision, and one reason.
struct ContextBoundaryResult {
  bool allowed = true;
  ContextBoundaryDecision decision = ContextBoundaryDecision::AllowField;
  std::string_view reason = "context field is allowed by ADR-006";
};

// ADR-006 explicitly forbids these message-layer/provider-layer field names
// from appearing in ContextPacket.
inline constexpr std::array<std::string_view, 3> kForbiddenContextFields = {
    "final_messages",
    "provider_payload",
    "rendered_prompt",
};

// Evaluates a candidate field name against the ADR-006 ContextPacket boundary.
constexpr ContextBoundaryResult evaluate_context_field_boundary(std::string_view field_name) {
  for (const auto forbidden_field : kForbiddenContextFields) {
    if (field_name == forbidden_field) {
      return ContextBoundaryResult{
          .allowed = false,
          .decision = ContextBoundaryDecision::RejectForbiddenField,
          .reason = "context packet must not contain message or provider payload fields",
      };
    }
  }

  return ContextBoundaryResult{};
}

// Helper for callers that only require boolean pass/fail semantics.
constexpr bool is_allowed_context_field(std::string_view field_name) {
  return evaluate_context_field_boundary(field_name).allowed;
}

}  // namespace dasall::contracts