#include "audit/AuditService.h"
#include "audit/AuditErrors.h"

#include <optional>
#include <string>
#include <vector>
#include <utility>

namespace dasall::infra::audit {

namespace {

constexpr std::string_view kAuditServiceSourceRef = "AuditService";

AuditWriteOutcome make_audit_write_failure(AuditErrorCode error_code,
                                           bool accepted = false,
                                           bool fallback_used = false) {
  return AuditWriteOutcome{
      .accepted = accepted,
      .persisted = false,
      .fallback_used = fallback_used,
      .error_code = map_audit_error_code(error_code).result_code,
  };
}

std::string make_export_checksum(const std::vector<AuditEvent>& records) {
  if (records.empty()) {
    return "audit-export:empty";
  }

  return std::string("audit-export:") + records.front().event_id + ":" +
         records.back().event_id + ":" + std::to_string(records.size());
}

}  // namespace

InfraOperationResult AuditService::init(const AuditServiceConfig& config) {
  if (lifecycle_state_ != LifecycleState::Created) {
    return invalid_transition("init", "created");
  }

  if (!config.is_valid()) {
    return InfraOperationResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                         "audit service requires primary or fallback capacity",
                                         "audit.init",
                                         std::string(kAuditServiceSourceRef));
  }

  config_ = config;
  primary_records_.clear();
  fallback_records_.clear();
  degraded_ = false;
  lifecycle_state_ = LifecycleState::Initialized;
  return InfraOperationResult::success();
}

InfraOperationResult AuditService::start() {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return invalid_transition("start", "initialized");
  }

  lifecycle_state_ = LifecycleState::Started;
  return InfraOperationResult::success();
}

InfraOperationResult AuditService::stop() {
  if (lifecycle_state_ != LifecycleState::Started) {
    return invalid_transition("stop", "started");
  }

  lifecycle_state_ = LifecycleState::Stopped;
  return InfraOperationResult::success();
}

AuditWriteOutcome AuditService::write_audit(const AuditEvent& event,
                                            const AuditContext& context) {
  if (lifecycle_state_ != LifecycleState::Started) {
    return make_audit_write_failure(AuditErrorCode::WriteFail);
  }

  if (!event.has_required_fields() || !event.side_effects_are_serializable() ||
      !event.references_contract_outcome() || !context.has_non_empty_fields()) {
    return make_audit_write_failure(AuditErrorCode::InvalidEvent);
  }

  if (primary_records_.size() < config_.primary_capacity) {
    primary_records_.push_back(event);
    return AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
      .error_code = std::nullopt,
    };
  }

  degraded_ = true;
  if (fallback_records_.size() < config_.fallback_capacity) {
    fallback_records_.push_back(event);
    return AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = true,
      .error_code = std::nullopt,
    };
  }

  return make_audit_write_failure(AuditErrorCode::FallbackFail,
                                  true,
                                  true);
}

ExportResult AuditService::export_audit(const ExportQuery& query) {
  if (lifecycle_state_ != LifecycleState::Started) {
    return ExportResult{};
  }

  if (!query.has_ordered_window()) {
    return ExportResult{};
  }

  auto records = select_records(query);
  auto checksum = make_export_checksum(records);

  return ExportResult{
      .records = std::move(records),
      .next_page_token = std::string(),
      .truncated = false,
      .checksum = std::move(checksum),
  };
}

std::string_view AuditService::lifecycle_state_name() const {
  switch (lifecycle_state_) {
    case LifecycleState::Created:
      return "created";
    case LifecycleState::Initialized:
      return "initialized";
    case LifecycleState::Started:
      return "started";
    case LifecycleState::Stopped:
      return "stopped";
  }

  return "unknown";
}

InfraOperationResult AuditService::invalid_transition(
    std::string_view operation,
    std::string_view expected_state) const {
  return InfraOperationResult::failure(
      contracts::ResultCode::RuntimeRetryExhausted,
      "invalid audit lifecycle transition for operation " + std::string(operation) +
          ": expected state " + std::string(expected_state) + ", actual state " +
          std::string(lifecycle_state_name()),
      "audit.lifecycle",
      std::string(kAuditServiceSourceRef));
}

std::vector<AuditEvent> AuditService::select_records(const ExportQuery& query) const {
  std::vector<AuditEvent> records;

  const auto matches = [&query](const AuditEvent& event) {
    if (event.timestamp < query.start_ts || event.timestamp > query.end_ts) {
      return false;
    }

    if (!query.actor.empty() && event.actor != query.actor) {
      return false;
    }

    if (!query.action.empty() && event.action != query.action) {
      return false;
    }

    if (!query.target.empty() && event.target != query.target) {
      return false;
    }

    if (query.filters_on_outcome() && event.outcome != query.outcome) {
      return false;
    }

    return true;
  };

  for (const auto& event : primary_records_) {
    if (matches(event)) {
      records.push_back(event);
    }
  }

  for (const auto& event : fallback_records_) {
    if (matches(event)) {
      records.push_back(event);
    }
  }

  return records;
}

}  // namespace dasall::infra::audit