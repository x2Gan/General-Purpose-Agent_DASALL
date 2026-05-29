#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tracing/ITracer.h"
#include "tracing/TraceErrors.h"
#include "tracing/TraceTypes.h"

namespace dasall::llm::observability {

enum class LLMTraceSpanKind {
  PromptSelect = 0,
  PromptCompose,
  PromptPolicy,
  RouteResolve,
  AdapterInvoke,
  ResponseNormalize,
};

inline constexpr std::array<std::string_view, 4> kLLMTraceAllowedOutcomes{
    "success",
    "degraded",
    "failure",
    "rejected",
};
inline constexpr std::string_view kLLMTraceDefaultDetailRef =
    "llm://trace/idle";

[[nodiscard]] inline bool is_llm_trace_outcome(std::string_view outcome) {
  return std::find(kLLMTraceAllowedOutcomes.begin(),
                   kLLMTraceAllowedOutcomes.end(),
                   outcome) != kLLMTraceAllowedOutcomes.end();
}

struct LLMTraceSpanSignal {
  LLMTraceSpanKind kind = LLMTraceSpanKind::AdapterInvoke;
  std::string request_id;
  std::string llm_call_id;
  std::string stage;
  std::string resolved_route;
  std::string model_name;
  std::string prompt_id;
  std::string prompt_version;
  bool fallback_used = false;
  std::uint32_t latency_ms = 0;
  std::string failure_category;
  std::string error_type;
  std::vector<std::string> selection_reason_codes;
  std::uint32_t estimated_input_tokens = 0;
  std::uint32_t prompt_cache_hit_tokens = 0;
  std::uint32_t prompt_cache_miss_tokens = 0;
  double actual_cost_estimate_usd = 0.0;
  std::string reasoning_mode_requested;
  std::string reasoning_mode_effective;
  std::int64_t completed_at_ms = 0;
  std::optional<infra::tracing::TraceContext> parent_context;
  std::string detail_ref = std::string(kLLMTraceDefaultDetailRef);
  std::string outcome = "success";
  std::string request_mode = "unary";
  std::string result_code;
  std::string result_code_category;
  std::string error_stage;
  std::vector<std::string> attempted_routes;
  bool retryable = false;
  bool safe_to_replan = false;

  [[nodiscard]] bool has_consistent_values() const;
};

struct LLMTraceWriteResult {
  bool emitted = false;
  infra::tracing::TraceOperationStatus status =
      infra::tracing::TraceOperationStatus::success();
  std::optional<infra::tracing::TraceErrorCode> trace_error_code;

  [[nodiscard]] bool has_consistent_state() const {
    if (emitted) {
      return status.ok && !trace_error_code.has_value();
    }

    return !status.ok && trace_error_code.has_value();
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    return status.references_only_contract_error_types();
  }
};

struct LLMTraceBridgeStatus {
  std::uint64_t emitted_total = 0U;
  std::uint64_t emit_failures = 0U;
  bool degraded = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref = std::string(kLLMTraceDefaultDetailRef);

  [[nodiscard]] bool is_valid() const;
};

class LLMTraceBridge {
 public:
  explicit LLMTraceBridge(std::shared_ptr<infra::tracing::ITracer> tracer = nullptr);

  void set_tracer(std::shared_ptr<infra::tracing::ITracer> tracer);

  [[nodiscard]] bool has_tracer() const {
    return static_cast<bool>(tracer_);
  }

  [[nodiscard]] LLMTraceWriteResult record_span(const LLMTraceSpanSignal& signal);
  [[nodiscard]] LLMTraceBridgeStatus get_status() const;

 private:
  [[nodiscard]] infra::tracing::SpanDescriptor make_descriptor(
      const LLMTraceSpanSignal& signal) const;
  void record_success(const std::string& detail_ref);
  void record_failure(std::optional<contracts::ResultCode> result_code,
                      const std::string& detail_ref);

  std::shared_ptr<infra::tracing::ITracer> tracer_;
  std::uint64_t emitted_total_ = 0U;
  std::uint64_t emit_failures_ = 0U;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_ = std::string(kLLMTraceDefaultDetailRef);
};

[[nodiscard]] constexpr std::string_view llm_trace_span_name(LLMTraceSpanKind kind) {
  switch (kind) {
    case LLMTraceSpanKind::PromptSelect:
      return "llm.prompt.select";
    case LLMTraceSpanKind::PromptCompose:
      return "llm.prompt.compose";
    case LLMTraceSpanKind::PromptPolicy:
      return "llm.prompt.policy";
    case LLMTraceSpanKind::RouteResolve:
      return "llm.route.resolve";
    case LLMTraceSpanKind::AdapterInvoke:
      return "llm.adapter.invoke";
    case LLMTraceSpanKind::ResponseNormalize:
      return "llm.response.normalize";
  }

  return "llm.unknown";
}

}  // namespace dasall::llm::observability