#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::watchdog {

enum class WatchdogErrorCode {
  EntityDuplicate = 1,
  EntityNotFound = 2,
  HeartbeatStale = 3,
  ScanOverdue = 4,
  TimeoutCritical = 5,
  EventPublishFail = 6,
  AuditWriteFail = 7,
};

struct WatchdogErrorMapping {
  WatchdogErrorCode watchdog_code;
  contracts::ResultCode result_code;
  std::string_view source_anchor;
  std::string_view reason;
};

inline constexpr std::string_view watchdog_error_code_name(
    WatchdogErrorCode code) {
  switch (code) {
    case WatchdogErrorCode::EntityDuplicate:
      return "INF_E_WATCHDOG_ENTITY_DUPLICATE";
    case WatchdogErrorCode::EntityNotFound:
      return "INF_E_WATCHDOG_ENTITY_NOT_FOUND";
    case WatchdogErrorCode::HeartbeatStale:
      return "INF_E_WATCHDOG_HEARTBEAT_STALE";
    case WatchdogErrorCode::ScanOverdue:
      return "INF_E_WATCHDOG_SCAN_OVERDUE";
    case WatchdogErrorCode::TimeoutCritical:
      return "INF_E_WATCHDOG_TIMEOUT_CRITICAL";
    case WatchdogErrorCode::EventPublishFail:
      return "INF_E_WATCHDOG_EVENT_PUBLISH_FAIL";
    case WatchdogErrorCode::AuditWriteFail:
      return "INF_E_WATCHDOG_AUDIT_WRITE_FAIL";
  }

  return "INF_E_WATCHDOG_UNKNOWN";
}

inline constexpr WatchdogErrorMapping map_watchdog_error_code(
    WatchdogErrorCode code) {
  switch (code) {
    case WatchdogErrorCode::EntityDuplicate:
      return WatchdogErrorMapping{
          .watchdog_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.6 entity duplicate",
          .reason = "duplicate watchdog entity registrations stay inside the contracts validation category until callers reconcile entity_id ownership",
      };
    case WatchdogErrorCode::EntityNotFound:
      return WatchdogErrorMapping{
          .watchdog_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.6 entity lookup",
          .reason = "watchdog unregister and query misses stay inside the contracts validation category until callers fix the entity reference",
      };
    case WatchdogErrorCode::HeartbeatStale:
      return WatchdogErrorMapping{
          .watchdog_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.3 heartbeat stale",
          .reason = "stale watchdog heartbeats stay inside the contracts validation category because the caller submitted an out-of-date sample",
      };
    case WatchdogErrorCode::ScanOverdue:
      return WatchdogErrorMapping{
          .watchdog_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .source_anchor = "6.8 scan overdue",
          .reason = "watchdog scan overruns stay inside the contracts runtime category because scheduler lag is an internal execution fault",
      };
    case WatchdogErrorCode::TimeoutCritical:
      return WatchdogErrorMapping{
          .watchdog_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .source_anchor = "6.8 timeout critical",
          .reason = "critical watchdog timeouts stay inside the contracts provider category because the supervised entity failed to respond before its deadline",
      };
    case WatchdogErrorCode::EventPublishFail:
      return WatchdogErrorMapping{
          .watchdog_code = code,
          .result_code = contracts::ResultCode::ToolExecutionFailed,
          .source_anchor = "6.8 event publish failure",
          .reason = "watchdog timeout event publish failures stay inside the contracts tool execution category because the observable export step failed",
      };
    case WatchdogErrorCode::AuditWriteFail:
      return WatchdogErrorMapping{
          .watchdog_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .source_anchor = "6.8 audit write failure",
          .reason = "watchdog audit persistence failures stay inside the contracts runtime category because critical evidence could not be durably recorded",
      };
  }

  return WatchdogErrorMapping{
      .watchdog_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .source_anchor = "unknown",
      .reason = "unknown watchdog errors fall back to the contracts runtime category",
  };
}

}  // namespace dasall::infra::watchdog