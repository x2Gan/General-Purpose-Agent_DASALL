#pragma once

#include <cstdint>
#include <string>

namespace dasall::cognition {

// Thresholds for various decision points in the cognition pipeline. These thresholds can be used to
// determine when to ask for clarification, when to provide a direct response, and when to provide
// a replan hint. By configuring these thresholds, developers can control the behavior of the cognition
// pipeline and tailor it to the specific needs of their application and user experience goals. For example,
// in a safety-critical application, developers might choose to set a lower threshold for asking for
// clarification to ensure that the system has a clear understanding of the user's intent before taking
// any action. Conversely, in a more flexible application, developers might set a higher threshold for
// asking for clarification to allow for a more natural and conversational user experience.
struct CognitionThresholds {
  float ask_clarification = 0.45F;      // Threshold for asking the user for clarification when the system's confidence in its understanding of the user's intent is low.
  float direct_response = 0.70F;        // Threshold for providing a direct response to the user when the system's confidence in its understanding of the user's intent is high enough to take action without further clarification. 
  float replan_hint = 0.50F;            // Threshold for providing a replan hint to the user when the system's confidence in its understanding of the user's intent is not high enough to take action, but is high enough to suggest an alternative course of action or ask for more information.
};

// Policies related to the perception stage of the cognition pipeline. These policies can be used to
// enable or disable certain features of the perception stage, such as the use of large language models
// (LLMs) for understanding user input, or the use of rule-based fallback mechanisms when the LLM is
// unable to generate a valid response. By configuring these policies, developers can control the
// behavior of the perception stage and tailor it to the specific needs of their application and user
// experience goals. For example, in a safety-critical application, developers might choose to disable LLMs
// and rely solely on rule-based mechanisms to ensure predictable and safe behavior. Conversely, in a
// more flexible application, developers might enable LLMs to provide a richer and more natural user experience.
struct CognitionPerceptionPolicy {
  bool llm_enabled = true;                // Enable or disable the use of large language models for perception.
  bool rule_fallback_enabled = true;      // Enable or disable rule-based fallback mechanisms when LLMs are unable to generate a valid response.  
};

// Response templates for various fallback scenarios. These can be used to provide more informative
// responses to the user when the system is unable to generate a valid response, or when it needs 
// to ask for clarification. The {summary} placeholder can be used to include a summary of the 
// current state or understanding of the system, which can help the user provide more relevant 
// information or understand the limitations of the system's response. These templates can be 
// customized to fit the specific use case and user experience goals of the application.   
struct CognitionResponseTemplates {
  std::string clarification =
      "I need more detail before I can complete this request. Current understanding: {summary}";
  std::string safe_converge =
      "I am returning a safe degraded response while preserving the current goal state. {summary}";
  std::string fallback_failure =
      "I could not produce a validated final response. Best available summary: {summary}";
};

/**
 * Policies related to the response generation stage of the cognition pipeline. These policies can be used to
 * enable or disable certain features of the response generation stage, such as the use of template-based
 * fallback responses when the system is unable to generate a valid response. By configuring these policies,
 * developers can control the behavior of the response generation stage and tailor it to the specific needs of
 * their application and user experience goals. For example, in a safety-critical application, developers might 
 * choose to enable template-based fallback responses to ensure that the system always provides a safe and informative response,
 * even when it is unable to generate a valid response. Conversely, in a more flexible application, developers might choose 
 * to disable template-based fallback responses to allow for a more natural and conversational user experience, 
 * even if it means that the system may occasionally fail to provide a valid response.
 */
struct CognitionResponsePolicy {
  bool template_fallback_enabled = true;    // Enable or disable the use of template-based fallback responses when the system is unable to generate a valid response.
  CognitionResponseTemplates templates;
};

/**
 * Policies related to the reasoning stage of the cognition pipeline. These policies can be used to enable or disable 
 * certain features of the reasoning stage, such as the use of delegate hints to suggest alternative courses of action 
 * when the system is unable to generate a valid response. By configuring these policies, developers can control the 
 * behavior of the reasoning stage and tailor it to the specific needs of their application and user experience goals. 
 * For example, in a safety-critical application, developers might choose to enable delegate hints to ensure that the 
 * system always provides a safe and informative suggestion for how to proceed, even when it is unable to generate a 
 * valid response. Conversely, in a more flexible application, developers might choose to disable delegate hints to 
 * allow for a more natural and conversational user experience, even if it means that the system may occasionally 
 * fail to provide a valid suggestion for how to proceed.
 */
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
