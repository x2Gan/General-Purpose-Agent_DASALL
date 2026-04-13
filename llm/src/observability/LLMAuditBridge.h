#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "InfraContext.h"
#include "audit/IAuditLogger.h"

namespace dasall::llm::observability {

enum class LLMAuditEventKind {
  TrustedSourceFailure = 0,
  ReasoningContentStripped,
  MetadataDrift,
};

inline constexpr std::string_view kLLMAuditDefaultWorkerType =
    "llm.observability";
inline constexpr std::string_view kLLMAuditDefaultDetailRef =
    "llm://audit/idle";

struct LLMAuditContext {
  infra::InfraContext infra_context{};
  std::string worker_type = std::string(kLLMAuditDefaultWorkerType);

  [[nodiscard]] bool is_valid() const;
};

struct LLMAuditEvent {
  LLMAuditEventKind kind = LLMAuditEventKind::TrustedSourceFailure;
  std::string stage;
  std::string reason;
  LLMAuditContext context{};
  std::string detail_ref = std::string(kLLMAuditDefaultDetailRef);
  std::string llm_call_id;
  std::string prompt_id;
  std::string prompt_version;
  std::string resolved_route;
  std::string model_name;
  std::string profile_id = "unknown";
  std::string trusted_source;
  std::string metadata_field;
  std::string expected_value;
  std::string observed_value;
  std::string reasoning_mode_requested;
  std::string reasoning_mode_effective;
  std::int64_t timestamp_ms = 0;

  [[nodiscard]] bool has_consistent_values() const;
};

struct LLMAuditWriteResult {
  bool emitted = false;
  infra::AuditEvent audit_event{};
  infra::AuditContext audit_context{};
  infra::AuditWriteOutcome write_outcome{};

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

struct LLMAuditBridgeStatus {
  std::uint64_t emitted_total = 0U;
  std::uint64_t emit_failures = 0U;
  bool degraded = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref = std::string(kLLMAuditDefaultDetailRef);

  [[nodiscard]] bool is_valid() const;
};

class LLMAuditBridge {
 public:
  explicit LLMAuditBridge(
      std::shared_ptr<infra::audit::IAuditLogger> audit_logger = nullptr);

  void set_audit_logger(std::shared_ptr<infra::audit::IAuditLogger> audit_logger);

  [[nodiscard]] bool has_audit_logger() const {
    return static_cast<bool>(audit_logger_);
  }

  [[nodiscard]] LLMAuditWriteResult write_audit_event(const LLMAuditEvent& event);
  [[nodiscard]] LLMAuditBridgeStatus get_status() const;

 private:
  [[nodiscard]] infra::AuditEvent make_audit_event(const LLMAuditEvent& event);
  [[nodiscard]] infra::AuditContext make_audit_context(const LLMAuditEvent& event) const;
  void record_success(const std::string& detail_ref);
  void record_failure(std::optional<contracts::ResultCode> result_code,
                      const std::string& detail_ref);

  std::shared_ptr<infra::audit::IAuditLogger> audit_logger_;
  std::uint64_t next_event_sequence_ = 1U;
  std::uint64_t emitted_total_ = 0U;
  std::uint64_t emit_failures_ = 0U;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_ = std::string(kLLMAuditDefaultDetailRef);
};

[[nodiscard]] constexpr std::string_view llm_audit_action_name(
    LLMAuditEventKind kind) {
  switch (kind) {
    case LLMAuditEventKind::TrustedSourceFailure:
      return "trusted_source_rejected";
    case LLMAuditEventKind::ReasoningContentStripped:
      return "reasoning_content_stripped";
    case LLMAuditEventKind::MetadataDrift:
      return "metadata_drift_detected";
  }

  return "llm_audit_unknown";
}

}  // namespace dasall::llm::observability