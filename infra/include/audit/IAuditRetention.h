#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra {

enum class AuditCleanupTrigger {
  Unspecified = 0,
  Manual = 1,
  Scheduled = 2,
};

inline constexpr std::string_view kAuditRetentionDetailNamespace =
    "diag://infra/audit/retention";
inline constexpr std::string_view kAuditRetentionArchiveNamespace =
    "diag://infra/audit/retention/archive";
inline constexpr std::string_view kAuditRetentionCleanupNamespace =
    "diag://infra/audit/retention/cleanup";

[[nodiscard]] inline bool is_audit_retention_detail_ref(
    std::string_view detail_ref) {
  return detail_ref.starts_with(kAuditRetentionDetailNamespace);
}

[[nodiscard]] inline bool is_audit_retention_archive_ref(
    std::string_view archive_ref) {
  return archive_ref.starts_with(kAuditRetentionArchiveNamespace);
}

[[nodiscard]] inline bool is_audit_retention_cleanup_ref(
    std::string_view cleanup_ref) {
  return cleanup_ref.starts_with(kAuditRetentionCleanupNamespace);
}

struct AuditArchiveAction {
  std::string archive_ref;
  std::uint64_t archived_records = 0;
  std::int64_t archived_through_ts = 0;
  std::string checksum;

  [[nodiscard]] bool has_required_fields() const {
    return is_audit_retention_archive_ref(archive_ref) &&
           archived_records > 0 && archived_through_ts > 0 && !checksum.empty();
  }
};

struct AuditCleanupEvidence {
  AuditCleanupTrigger trigger = AuditCleanupTrigger::Unspecified;
  std::string cleanup_ref;
  std::string archive_ref;
  std::uint64_t deleted_records = 0;
  std::int64_t deleted_through_ts = 0;

  [[nodiscard]] bool has_required_fields() const {
    return trigger != AuditCleanupTrigger::Unspecified &&
           is_audit_retention_cleanup_ref(cleanup_ref) &&
           is_audit_retention_archive_ref(archive_ref) && deleted_records > 0 &&
           deleted_through_ts > 0;
  }

  [[nodiscard]] bool is_scheduled() const {
    return trigger == AuditCleanupTrigger::Scheduled;
  }
};

struct RetentionOutcome {
  bool completed = false;
  std::int64_t cutoff_ts = 0;
  std::uint64_t scanned_records = 0;
  std::uint64_t archived_records = 0;
  std::uint64_t deleted_records = 0;
  std::string detail_ref;
  std::optional<contracts::ResultCode> error_code;
  std::optional<AuditArchiveAction> archive_action;
  std::optional<AuditCleanupEvidence> cleanup_evidence;

  [[nodiscard]] bool has_valid_error_mapping() const {
    return !error_code.has_value() ||
           contracts::classify_result_code(*error_code) !=
               contracts::ResultCodeCategory::Unknown;
  }

  [[nodiscard]] bool has_consistent_state() const {
    if (cutoff_ts <= 0 || !has_valid_error_mapping() ||
        !is_audit_retention_detail_ref(detail_ref)) {
      return false;
    }

    if (scanned_records < archived_records || scanned_records < deleted_records) {
      return false;
    }

    if (completed) {
      if (error_code.has_value()) {
        return false;
      }
    } else if (!error_code.has_value()) {
      return false;
    }

    if (archived_records == 0) {
      if (archive_action.has_value()) {
        return false;
      }
    } else {
      if (!archive_action.has_value() || !archive_action->has_required_fields() ||
          archive_action->archived_records != archived_records ||
          archive_action->archived_through_ts != cutoff_ts) {
        return false;
      }
    }

    if (deleted_records == 0) {
      if (cleanup_evidence.has_value()) {
        return false;
      }
    } else {
      if (!cleanup_evidence.has_value() ||
          !cleanup_evidence->has_required_fields() ||
          cleanup_evidence->deleted_records != deleted_records ||
          cleanup_evidence->deleted_through_ts != cutoff_ts) {
        return false;
      }

      if (archive_action.has_value() &&
          cleanup_evidence->archive_ref != archive_action->archive_ref) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] bool is_success() const {
    return has_consistent_state() && completed;
  }

  [[nodiscard]] bool is_failure() const {
    return has_consistent_state() && !completed;
  }
};

}  // namespace dasall::infra

namespace dasall::infra::audit {

class IAuditRetention {
 public:
  virtual ~IAuditRetention() = default;

  [[nodiscard]] virtual RetentionOutcome apply_retention(
      std::int64_t now_ts) = 0;
};

}  // namespace dasall::infra::audit