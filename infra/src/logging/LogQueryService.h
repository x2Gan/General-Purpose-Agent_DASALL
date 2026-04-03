#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "InfraContext.h"
#include "LogEvent.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "policy/PolicyTypes.h"

namespace dasall::infra::logging {

inline constexpr std::string_view kLogQueryArtifactNamespace =
    "diag://infra/logging/query";

enum class LogQuerySelectorKind {
  Unspecified = 0,
  TraceId,
  SessionId,
};

[[nodiscard]] inline constexpr std::string_view log_query_selector_name(
    LogQuerySelectorKind selector_kind) {
  switch (selector_kind) {
    case LogQuerySelectorKind::TraceId:
      return "trace_id";
    case LogQuerySelectorKind::SessionId:
      return "session_id";
    case LogQuerySelectorKind::Unspecified:
      break;
  }

  return "unspecified";
}

struct LogQueryRequest {
  std::string query_id;
  LogQuerySelectorKind selector_kind = LogQuerySelectorKind::Unspecified;
  std::string selector_value;
  std::int64_t start_ts_ms = 0;
  std::int64_t end_ts_ms = 0;
  std::uint32_t max_records = 0;

  [[nodiscard]] bool has_precise_selector() const {
    return selector_kind == LogQuerySelectorKind::TraceId ||
           selector_kind == LogQuerySelectorKind::SessionId;
  }

  [[nodiscard]] bool has_ordered_window() const {
    return start_ts_ms > 0 && end_ts_ms >= start_ts_ms;
  }

  [[nodiscard]] bool has_required_fields() const {
    return !query_id.empty() && has_precise_selector() && !selector_value.empty() &&
           has_ordered_window() && max_records > 0;
  }
};

struct LogQueryAccessContext {
  std::string actor_ref;
  std::string consumer_module;
  policy::PolicyDecisionRef policy_decision_ref;
  InfraContext infra_context;

  [[nodiscard]] bool has_required_fields() const {
    return !actor_ref.empty() && !consumer_module.empty() &&
           policy_decision_ref.is_valid();
  }

  [[nodiscard]] bool has_allow_proof() const {
    return has_required_fields() &&
           policy_decision_ref.decision == policy::PolicyDecision::Allow;
  }
};

struct LogQueryResult {
  bool ok = false;
  std::string artifact_ref;
  std::uint32_t match_count = 0;
  bool truncated = false;
  std::string checksum;
  std::int64_t created_at = 0;
  contracts::ResultCode result_code =
      contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static LogQueryResult success(std::string artifact_ref,
                                              std::uint32_t match_count,
                                              bool truncated,
                                              std::string checksum,
                                              std::int64_t created_at);
  [[nodiscard]] static LogQueryResult failure(contracts::ResultCode result_code,
                                              std::string message,
                                              std::string stage,
                                              std::string source_ref);

  [[nodiscard]] bool has_success_payload() const {
    return ok && !artifact_ref.empty() && !checksum.empty() && created_at > 0 &&
           !error_info.has_value();
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return ok;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type ==
               contracts::classify_result_code(result_code);
  }
};

class ILogQueryRecordReader {
 public:
  virtual ~ILogQueryRecordReader() = default;

  [[nodiscard]] virtual std::vector<LogEvent> read_window(
      std::int64_t start_ts_ms,
      std::int64_t end_ts_ms) = 0;
};

struct LogQueryServiceOptions {
  bool enable_diag_pull = true;
  std::string artifact_namespace = std::string(kLogQueryArtifactNamespace);

  [[nodiscard]] bool has_consistent_values() const {
    return !artifact_namespace.empty();
  }
};

class LogQueryService {
 public:
  using ClockNowMs = std::function<std::int64_t()>;

  explicit LogQueryService(
      std::shared_ptr<ILogQueryRecordReader> record_reader,
      LogQueryServiceOptions options = {},
      ClockNowMs clock_now_ms = {});

  [[nodiscard]] LogQueryResult query(const LogQueryRequest& request,
                                     const LogQueryAccessContext& access_context) const;

 private:
  [[nodiscard]] static std::string_view selector_attr_key(
      LogQuerySelectorKind selector_kind);
  [[nodiscard]] static bool matches_selector(const LogEvent& event,
                                            const LogQueryRequest& request);
  [[nodiscard]] std::string make_artifact_ref(const LogQueryRequest& request) const;
  [[nodiscard]] static std::string make_checksum(const LogQueryRequest& request,
                                                 std::uint32_t match_count,
                                                 bool truncated);
  [[nodiscard]] static std::int64_t default_now_ms();

  std::shared_ptr<ILogQueryRecordReader> record_reader_;
  LogQueryServiceOptions options_;
  ClockNowMs clock_now_ms_;
};

}  // namespace dasall::infra::logging