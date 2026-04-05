#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "audit/IAuditLogger.h"
#include "policy/PolicyTypes.h"

namespace dasall::infra::policy {

struct PolicyAuditBridgeOptions {
  std::string detail_ref_prefix = "status://policy/audit/";
  std::string event_id_prefix = "policy-audit-event-";
};

struct PolicyAuditEmitResult {
  bool emitted = false;
  AuditEvent audit_event;
  AuditContext audit_context;
  AuditWriteOutcome write_outcome;

  [[nodiscard]] bool is_valid() const {
    if (!emitted) {
      return write_outcome.has_consistent_state();
    }

    return audit_event.has_required_fields() &&
           audit_event.side_effects_are_serializable() &&
           audit_context.has_non_empty_fields() &&
           (write_outcome.is_success() || write_outcome.is_degraded_success());
  }
};

struct PolicyAuditBridgeStatus {
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

class PolicyAuditBridge {
 public:
  explicit PolicyAuditBridge(std::shared_ptr<audit::IAuditLogger> audit_logger = nullptr,
                             PolicyAuditBridgeOptions options = {});

  void set_audit_logger(std::shared_ptr<audit::IAuditLogger> audit_logger);

  [[nodiscard]] PolicyAuditEmitResult emit_load_result(const PolicyBundle& bundle,
                                                       const PolicyOpResult& result);
  [[nodiscard]] PolicyAuditEmitResult emit_patch_result(const PolicyPatch& patch,
                                                        const PolicyOpResult& result);
  [[nodiscard]] PolicyAuditEmitResult emit_rollback_result(
      const std::string& rollback_target_snapshot_id,
      const PolicyOpResult& result);
  [[nodiscard]] PolicyAuditEmitResult emit_high_risk_deny(
      const PolicyQueryContext& query,
      const PolicyDecisionRef& decision);

  [[nodiscard]] PolicyAuditBridgeStatus get_status() const;

 private:
  [[nodiscard]] PolicyAuditEmitResult emit_event(AuditEvent audit_event,
                                                 AuditContext audit_context,
                                                 const std::string& detail_suffix);
  [[nodiscard]] AuditEvent make_load_event(const PolicyBundle& bundle,
                                           const PolicyOpResult& result);
  [[nodiscard]] AuditEvent make_patch_event(const PolicyPatch& patch,
                                            const PolicyOpResult& result);
  [[nodiscard]] AuditEvent make_rollback_event(const std::string& rollback_target_snapshot_id,
                                               const PolicyOpResult& result);
  [[nodiscard]] AuditEvent make_deny_event(const PolicyQueryContext& query,
                                           const PolicyDecisionRef& decision);
  [[nodiscard]] AuditContext make_default_context() const;
  [[nodiscard]] AuditContext make_query_context(const PolicyQueryContext& query) const;

  void record_success(const std::string& detail_suffix);
  void record_failure(std::optional<contracts::ResultCode> result_code,
                      const std::string& detail_suffix);

  std::shared_ptr<audit::IAuditLogger> audit_logger_;
  PolicyAuditBridgeOptions options_;
  std::uint64_t next_event_sequence_ = 1;
  std::uint64_t emitted_total_ = 0;
  std::uint64_t emit_failures_ = 0;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_;
};

}  // namespace dasall::infra::policy