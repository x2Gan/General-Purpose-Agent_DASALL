#include "audit/AuditService.h"

#include <string>
#include <utility>

#include "InfraErrorCode.h"

namespace dasall::infra::audit {

namespace {

constexpr std::string_view kAuditServiceSourceRef = "AuditService";

AuditWriteResult make_audit_write_failure(std::string message,
                                          std::string stage,
                                          bool fallback_used = false) {
  return AuditWriteResult::failure(map_infra_error_code(InfraErrorCode::AuditWriteFail).result_code,
                                   std::move(message),
                                   std::move(stage),
                                   std::string(kAuditServiceSourceRef),
                                   fallback_used);
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

AuditWriteResult AuditService::write_audit(const AuditEvent& event) {
  if (lifecycle_state_ != LifecycleState::Started) {
    return make_audit_write_failure("audit service must be started before accepting events",
                                    "audit.lifecycle");
  }

  if (!event.has_required_fields() || !event.side_effects_are_serializable() ||
      !event.references_contract_outcome()) {
    return AuditWriteResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                     "audit event must stay valid, serializable, and aligned to contracts references",
                                     "audit.validate",
                                     std::string(kAuditServiceSourceRef));
  }

  if (primary_records_.size() < config_.primary_capacity) {
    primary_records_.push_back(event);
    return AuditWriteResult::success(false);
  }

  degraded_ = true;
  if (fallback_records_.size() < config_.fallback_capacity) {
    fallback_records_.push_back(event);
    return AuditWriteResult::success(true);
  }

  return make_audit_write_failure(
      "audit fallback pipeline exhausted after primary write path became unavailable",
      "audit.fallback",
      true);
}

AuditExportResult AuditService::export_audit(const AuditExportFilter& filter) {
  if (lifecycle_state_ != LifecycleState::Started) {
    return AuditExportResult::failure(map_infra_error_code(InfraErrorCode::AuditWriteFail).result_code,
                                      "audit service must be started before exporting records",
                                      "audit.lifecycle",
                                      std::string(kAuditServiceSourceRef));
  }

  if (!filter.is_specified()) {
    return AuditExportResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                      "audit export filter must stay explicitly specified",
                                      "audit.export",
                                      std::string(kAuditServiceSourceRef));
  }

  return AuditExportResult::success(select_records(filter.opaque_selector), false);
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

std::vector<AuditEvent> AuditService::select_records(std::string_view selector) const {
  if (selector == "primary") {
    return primary_records_;
  }

  if (selector == "fallback") {
    return fallback_records_;
  }

  std::vector<AuditEvent> records = primary_records_;
  records.insert(records.end(), fallback_records_.begin(), fallback_records_.end());
  return records;
}

}  // namespace dasall::infra::audit