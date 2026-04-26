#pragma once

#include <cstdint>

namespace dasall::cognition {

struct CognitionThresholds {
  float ask_clarification = 0.45F;
  float direct_response = 0.70F;
  float replan_hint = 0.50F;
};

struct CognitionPerceptionPolicy {
  bool rule_fallback_enabled = true;
};

struct CognitionResponsePolicy {
  bool template_fallback_enabled = true;
};

struct CognitionReasonerPolicy {
  bool allow_delegate_hint = false;
};

struct CognitionObservabilityPolicy {
  bool emit_stage_spans = true;
  bool redact_context_payload = true;
};

struct CognitionConfig {
  bool enabled = true;
  std::uint32_t max_plan_nodes = 8;
  std::uint32_t max_plan_depth = 4;
  CognitionThresholds thresholds;
  CognitionPerceptionPolicy perception;
  CognitionResponsePolicy response;
  CognitionReasonerPolicy reasoner;
  CognitionObservabilityPolicy observability;
};

}  // namespace dasall::cognition
