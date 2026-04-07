#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "InfraContext.h"
#include "audit/IAuditLogger.h"
#include "tracing/TraceErrors.h"
#include "tracing/TraceTypes.h"

namespace dasall::infra::tracing {

enum class TraceAuditEventKind {
  SamplerConfigChange = 0,
  ExportRecoveryTransition,
  ShutdownFallback,
};

enum class TraceAuditEventOutcome {
  Success = 0,
  Failure,
  Degraded,
};

inline constexpr std::string_view kTraceAuditDefaultWorkerType =
    "infra.tracing";
inline constexpr std::array<std::string_view, 1> kTraceSamplerAuditActions{
    "sampler_changed",
};
inline constexpr std::array<std::string_view, 3> kTraceExportRecoveryActions{
    "enter_degraded",
    "degraded_still_active",
    "recover_to_healthy",
};
inline constexpr std::array<std::string_view, 1> kTraceShutdownAuditActions{
    "shutdown_force_fallback",
};

[[nodiscard]] inline constexpr std::string_view trace_audit_event_outcome_name(
    TraceAuditEventOutcome outcome) {
  switch (outcome) {
    case TraceAuditEventOutcome::Success:
      return "success";
    case TraceAuditEventOutcome::Failure:
      return "failure";
    case TraceAuditEventOutcome::Degraded:
      return "degraded";
  }

  return "unknown";
}

[[nodiscard]] inline bool is_trace_sampler_audit_action(
  const std::string_view& action) {
  return std::find(kTraceSamplerAuditActions.begin(),
                   kTraceSamplerAuditActions.end(),
                   action) != kTraceSamplerAuditActions.end();
}

[[nodiscard]] inline bool is_trace_export_recovery_action(
  const std::string_view& action) {
  return std::find(kTraceExportRecoveryActions.begin(),
                   kTraceExportRecoveryActions.end(),
                   action) != kTraceExportRecoveryActions.end();
}

[[nodiscard]] inline bool is_trace_shutdown_audit_action(
  const std::string_view& action) {
  return std::find(kTraceShutdownAuditActions.begin(),
                   kTraceShutdownAuditActions.end(),
                   action) != kTraceShutdownAuditActions.end();
}

struct TraceAuditContext {
  InfraContext infra_context{};
  std::string worker_type = std::string(kTraceAuditDefaultWorkerType);

  [[nodiscard]] bool is_valid() const {
    return !infra_context.request_id.empty() && !infra_context.session_id.empty() &&
           !infra_context.trace_id.empty() && !infra_context.task_id.empty() &&
           !infra_context.parent_task_id.empty() && !infra_context.lease_id.empty() &&
           !worker_type.empty();
  }
};

struct TraceAuditEvent {
  TraceAuditEventKind kind = TraceAuditEventKind::ExportRecoveryTransition;
  std::string action;
  std::string stage;
  TraceAuditEventOutcome outcome = TraceAuditEventOutcome::Success;
  std::string reason;
  std::optional<TraceErrorCode> error_code;
  TraceModuleSnapshot module_snapshot{.queue_depth = 0U,
                                      .dropped_total = 0U,
                                      .exporter_state = "unknown",
                                      .degraded = false};
  TraceAuditContext context{};
  std::string detail_ref;
  std::string current_sampler_type;
  std::string previous_sampler_type;
  std::uint64_t consecutive_failure_total = 0;
  std::uint64_t degrade_enter_total = 0;
  std::uint64_t recovery_success_total = 0;
  std::int64_t timestamp_ms = 0;

  [[nodiscard]] bool is_valid() const {
    if (action.empty() || stage.empty() || reason.empty() || detail_ref.empty() ||
        timestamp_ms <= 0 || !module_snapshot.is_valid() || !context.is_valid()) {
      return false;
    }

    if (outcome == TraceAuditEventOutcome::Success && error_code.has_value()) {
      return false;
    }

    switch (kind) {
      case TraceAuditEventKind::SamplerConfigChange:
        return is_trace_sampler_audit_action(action) &&
               !current_sampler_type.empty() && !previous_sampler_type.empty() &&
               outcome == TraceAuditEventOutcome::Success && !error_code.has_value();
      case TraceAuditEventKind::ExportRecoveryTransition:
        if (!is_trace_export_recovery_action(action) ||
            !current_sampler_type.empty() || !previous_sampler_type.empty()) {
          return false;
        }

        if (action == "recover_to_healthy") {
          return outcome == TraceAuditEventOutcome::Success &&
                 !error_code.has_value();
        }

        return outcome == TraceAuditEventOutcome::Degraded &&
               error_code.has_value();
      case TraceAuditEventKind::ShutdownFallback:
        return is_trace_shutdown_audit_action(action) &&
               current_sampler_type.empty() &&
               previous_sampler_type.empty() &&
               error_code.has_value() &&
               outcome != TraceAuditEventOutcome::Success;
    }

    return false;
  }
};

struct TraceAuditBridgeOptions {
  std::string detail_ref_prefix = "status://tracing/audit/";
  std::string event_id_prefix = "trace-audit-event-";
};

struct TraceAuditWriteResult {
  bool emitted = false;
  AuditEvent audit_event{};
  AuditContext audit_context{};
  AuditWriteOutcome write_outcome{};

  [[nodiscard]] bool has_consistent_state() const {
    if (!emitted) {
      return write_outcome.has_consistent_state();
    }

    return audit_event.has_required_fields() &&
           audit_event.side_effects_are_serializable() &&
           audit_context.has_non_empty_fields() &&
           (write_outcome.is_success() || write_outcome.is_degraded_success());
  }
};

struct TraceAuditBridgeStatus {
  std::uint64_t emitted_total = 0;
  std::uint64_t emit_failures = 0;
  bool degraded = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref;

  [[nodiscard]] bool is_valid() const {
    if (detail_ref.empty()) {
      return false;
    }

    if (last_error_code.has_value() &&
        contracts::classify_result_code(*last_error_code) ==
            contracts::ResultCodeCategory::Unknown) {
      return false;
    }

    return true;
  }
};

class TraceAuditBridge {
 public:
  explicit TraceAuditBridge(
      std::shared_ptr<audit::IAuditLogger> audit_logger = nullptr,
      TraceAuditBridgeOptions options = {});

  void set_audit_logger(std::shared_ptr<audit::IAuditLogger> audit_logger);

  [[nodiscard]] bool has_audit_logger() const {
    return static_cast<bool>(audit_logger_);
  }

  [[nodiscard]] TraceAuditWriteResult write_audit_event(
      const TraceAuditEvent& event);
  [[nodiscard]] TraceAuditBridgeStatus get_status() const;

 private:
  [[nodiscard]] AuditEvent make_audit_event(const TraceAuditEvent& event);
  [[nodiscard]] AuditContext make_audit_context(const TraceAuditEvent& event) const;
  void record_success(const std::string& detail_ref);
  void record_failure(std::optional<contracts::ResultCode> result_code,
                      const std::string& detail_ref);

  std::shared_ptr<audit::IAuditLogger> audit_logger_;
  TraceAuditBridgeOptions options_{};
  std::uint64_t next_event_sequence_ = 1;
  std::uint64_t emitted_total_ = 0;
  std::uint64_t emit_failures_ = 0;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_;
};

}  // namespace dasall::infra::tracing