#pragma once

#include <cstdint>
#include <string>

namespace dasall::cognition {

struct CognitionThresholds {
  float ask_clarification = 0.45F;
  float direct_response = 0.70F;
  float replan_hint = 0.50F;
};

struct CognitionPerceptionPolicy {
  bool llm_enabled = true;
  bool rule_fallback_enabled = true;
};

struct CognitionResponseTemplates {
  std::string clarification =
      "I need more detail before I can complete this request. Current understanding: {summary}";
  std::string safe_converge =
      "I am returning a safe degraded response while preserving the current goal state. {summary}";
  std::string fallback_failure =
      "I could not produce a validated final response. Best available summary: {summary}";
};

struct CognitionResponsePolicy {
  bool template_fallback_enabled = true;
  CognitionResponseTemplates templates;
};

struct CognitionReasonerCandidateWeights {
  float tool_call = 1.0F;
  float direct_response = 1.0F;
  float clarification = 1.0F;
  float converge_safe = 1.0F;
};

struct CognitionReasonerPolicy {
  bool allow_delegate_hint = false;
  CognitionReasonerCandidateWeights candidate_weights;
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
