#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::tracing {

inline constexpr std::size_t kTraceIdHexLength = 32;
inline constexpr std::size_t kSpanIdHexLength = 16;
inline constexpr std::size_t kTraceStateMaxLength = 512;
inline constexpr std::string_view kPlanningTraceStageAttrKey = "stage";
inline constexpr std::string_view kPlanningTraceStageAttrValue = "planning";
inline constexpr std::string_view kPlanningTraceBudgetAttrKey = "budget_ms";
inline constexpr std::string_view kPlanningTraceOutcomeAttrKey = "outcome";
inline constexpr std::array<std::string_view, 2> kPlanningTraceAllowedOutcomes{
  "success",
  "degraded",
};

enum class TraceContextState {
  Invalid = 0,
  Noop = 1,
  Active = 2,
};

enum class SpanKind {
  Internal = 0,
  Server = 1,
  Client = 2,
  Producer = 3,
  Consumer = 4,
};

enum class SpanStatusCode {
  Unset = 0,
  Ok = 1,
  Error = 2,
};

enum class SamplingDecisionKind {
  Drop = 0,
  RecordOnly = 1,
  RecordAndSample = 2,
};

using TraceAttributeValue = std::variant<bool, std::int64_t, std::uint64_t, double, std::string>;
using TraceAttributeMap = std::map<std::string, TraceAttributeValue>;
using TraceCarrier = std::map<std::string, std::string>;

[[nodiscard]] inline bool is_lower_hex_string(std::string_view value, std::size_t expected_length) {
  if (value.size() != expected_length) {
    return false;
  }

  bool all_zero = true;
  for (const char ch : value) {
    const auto code = static_cast<unsigned char>(ch);
    if (!std::isdigit(code) && (ch < 'a' || ch > 'f')) {
      return false;
    }

    if (ch != '0') {
      all_zero = false;
    }
  }

  return !all_zero;
}

[[nodiscard]] inline bool is_printable_ascii(std::string_view value) {
  return std::all_of(value.begin(), value.end(), [](const char ch) {
    const auto code = static_cast<unsigned char>(ch);
    return code >= 32 && code <= 126;
  });
}

[[nodiscard]] inline bool is_valid_trace_attr_key(std::string_view key) {
  if (key.empty()) {
    return false;
  }

  return std::all_of(key.begin(), key.end(), [](const char ch) {
    const auto code = static_cast<unsigned char>(ch);
    return std::isalnum(code) || ch == '_' || ch == '.' || ch == '-' || ch == '/';
  });
}

[[nodiscard]] inline bool is_valid_trace_attribute_value(const TraceAttributeValue& value) {
  return std::visit(
      [](const auto& current_value) -> bool {
        using CurrentType = std::decay_t<decltype(current_value)>;
        if constexpr (std::is_same_v<CurrentType, double>) {
          return std::isfinite(current_value);
        } else if constexpr (std::is_same_v<CurrentType, std::string>) {
          return is_printable_ascii(current_value);
        } else {
          return true;
        }
      },
      value);
}

[[nodiscard]] inline bool trace_attributes_are_serializable(const TraceAttributeMap& attrs) {
  return std::all_of(attrs.begin(), attrs.end(), [](const auto& entry) {
    return is_valid_trace_attr_key(entry.first) && is_valid_trace_attribute_value(entry.second);
  });
}

struct TracerScope {
  std::string name;
  std::string version;
  std::string schema_url;

  [[nodiscard]] bool is_valid() const {
    return !name.empty() && is_printable_ascii(name) &&
           (version.empty() || is_printable_ascii(version)) &&
           (schema_url.empty() || is_printable_ascii(schema_url));
  }
};

struct TraceOperationStatus {
  bool ok = false;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;
  std::string state_ref;

  [[nodiscard]] static TraceOperationStatus success(std::string state_ref = {}) {
    return TraceOperationStatus{
        .ok = true,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
        .state_ref = std::move(state_ref),
    };
  }

  [[nodiscard]] static TraceOperationStatus failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return TraceOperationStatus{
        .ok = false,
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.tracing",
                .ref_id = std::move(source_ref),
            },
        },
        .state_ref = std::string(),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return ok;
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

struct TraceContext {
  std::string trace_id;
  std::string span_id;
  std::uint8_t trace_flags = 0;
  std::string trace_state;
  std::string parent_span_id;
  TraceContextState state = TraceContextState::Noop;
  bool is_remote = false;

  [[nodiscard]] static TraceContext invalid() {
    return TraceContext{
        .trace_id = std::string(),
        .span_id = std::string(),
        .trace_flags = 0,
        .trace_state = std::string(),
        .parent_span_id = std::string(),
        .state = TraceContextState::Invalid,
        .is_remote = false,
    };
  }

  [[nodiscard]] static TraceContext noop() {
    return TraceContext{
        .trace_id = std::string(),
        .span_id = std::string(),
        .trace_flags = 0,
        .trace_state = std::string(),
        .parent_span_id = std::string(),
        .state = TraceContextState::Noop,
        .is_remote = false,
    };
  }

  [[nodiscard]] bool has_explicit_empty_ids() const {
    return trace_id.empty() && span_id.empty() && parent_span_id.empty() && trace_state.empty();
  }

  [[nodiscard]] bool is_valid() const {
    switch (state) {
      case TraceContextState::Invalid:
      case TraceContextState::Noop:
        return has_explicit_empty_ids();
      case TraceContextState::Active:
        return is_lower_hex_string(trace_id, kTraceIdHexLength) &&
               is_lower_hex_string(span_id, kSpanIdHexLength) &&
               (parent_span_id.empty() || is_lower_hex_string(parent_span_id, kSpanIdHexLength)) &&
               trace_state.size() <= kTraceStateMaxLength &&
               is_printable_ascii(trace_state);
    }

    return false;
  }
};

struct SpanLink {
  TraceContext context;
  TraceAttributeMap attrs;

  [[nodiscard]] bool is_valid() const {
    return context.state == TraceContextState::Active && context.is_valid() &&
           trace_attributes_are_serializable(attrs);
  }
};

struct SpanDescriptor {
  std::string name;
  SpanKind kind = SpanKind::Internal;
  std::optional<std::int64_t> start_ts_unix_ms;
  TraceAttributeMap attrs;
  std::vector<SpanLink> links;

  [[nodiscard]] bool is_valid() const {
    if (name.empty() || !is_printable_ascii(name) || !trace_attributes_are_serializable(attrs)) {
      return false;
    }

    if (start_ts_unix_ms.has_value() && *start_ts_unix_ms < 0) {
      return false;
    }

    return std::all_of(links.begin(), links.end(), [](const SpanLink& link) {
      return link.is_valid();
    });
  }
};

struct SpanEndResult {
  std::optional<std::int64_t> end_ts_unix_ms;
  SpanStatusCode status_code = SpanStatusCode::Unset;
  std::string status_message;
  std::uint32_t dropped_attr_count = 0;

  [[nodiscard]] bool is_valid() const {
    return end_ts_unix_ms.has_value() && *end_ts_unix_ms >= 0 &&
           is_printable_ascii(status_message) &&
           (status_code == SpanStatusCode::Error || status_message.empty());
  }
};

[[nodiscard]] inline const TraceAttributeValue* find_trace_attribute(
    const TraceAttributeMap& attrs,
    std::string_view key) {
  const auto it = attrs.find(std::string(key));
  if (it == attrs.end()) {
    return nullptr;
  }

  return &it->second;
}

[[nodiscard]] inline bool planning_trace_outcome_is_allowed(
    std::string_view outcome) {
  return std::find(kPlanningTraceAllowedOutcomes.begin(),
                   kPlanningTraceAllowedOutcomes.end(),
                   outcome) != kPlanningTraceAllowedOutcomes.end();
}

[[nodiscard]] inline bool span_has_planning_stage_contract(
    const SpanDescriptor& descriptor) {
  if (!descriptor.is_valid()) {
    return false;
  }

  const auto* stage_attr = find_trace_attribute(descriptor.attrs,
                                                kPlanningTraceStageAttrKey);
  const auto* budget_attr = find_trace_attribute(descriptor.attrs,
                                                 kPlanningTraceBudgetAttrKey);
  const auto* outcome_attr = find_trace_attribute(descriptor.attrs,
                                                  kPlanningTraceOutcomeAttrKey);
  if (stage_attr == nullptr || budget_attr == nullptr || outcome_attr == nullptr) {
    return false;
  }

  const auto* stage = std::get_if<std::string>(stage_attr);
  const auto* outcome = std::get_if<std::string>(outcome_attr);
  if (stage == nullptr || outcome == nullptr ||
      *stage != kPlanningTraceStageAttrValue ||
      !planning_trace_outcome_is_allowed(*outcome)) {
    return false;
  }

  return std::visit(
      [](const auto& value) -> bool {
        using CurrentType = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<CurrentType, std::int64_t> ||
                      std::is_same_v<CurrentType, std::uint64_t>) {
          return value > 0;
        } else if constexpr (std::is_same_v<CurrentType, double>) {
          return std::isfinite(value) && value > 0.0;
        } else {
          return false;
        }
      },
      *budget_attr);
}

[[nodiscard]] inline bool planning_stage_trace_has_correlation(
    const TraceContext& context) {
  return context.state == TraceContextState::Active && context.is_valid();
}

[[nodiscard]] inline bool planning_stage_outcome_is_consistent(
    const SpanDescriptor& descriptor,
    const SpanEndResult& end_result) {
  if (!span_has_planning_stage_contract(descriptor) || !end_result.is_valid()) {
    return false;
  }

  const auto* outcome_attr = find_trace_attribute(descriptor.attrs,
                                                  kPlanningTraceOutcomeAttrKey);
  const auto* outcome = outcome_attr == nullptr ? nullptr : std::get_if<std::string>(outcome_attr);
  if (outcome == nullptr) {
    return false;
  }

  if (*outcome == "success") {
    return end_result.status_code == SpanStatusCode::Ok &&
           end_result.status_message.empty();
  }

  if (*outcome == "degraded") {
    return end_result.status_code == SpanStatusCode::Error &&
           !end_result.status_message.empty();
  }

  return false;
}

struct SamplingDecision {
  SamplingDecisionKind decision = SamplingDecisionKind::Drop;
  std::string reason;
  std::string sampler_desc;

  [[nodiscard]] bool is_valid() const {
    return !reason.empty() && !sampler_desc.empty() && is_printable_ascii(reason) &&
           is_printable_ascii(sampler_desc);
  }
};

struct ExportBatchReport {
  std::uint32_t batch_size = 0;
  std::uint32_t success_count = 0;
  std::uint32_t failure_count = 0;
  std::uint32_t latency_ms = 0;

  [[nodiscard]] bool is_valid() const {
    return success_count + failure_count <= batch_size;
  }
};

struct TraceModuleSnapshot {
  std::uint32_t queue_depth = 0;
  std::uint64_t dropped_total = 0;
  std::string exporter_state = "unknown";
  bool degraded = false;

  [[nodiscard]] bool is_valid() const {
    return !exporter_state.empty() && is_printable_ascii(exporter_state);
  }
};

}  // namespace dasall::infra::tracing
