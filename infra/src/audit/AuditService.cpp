#include "audit/AuditService.h"
#include "audit/AuditErrors.h"
#include "AuditFallbackPipeline.h"
#include "AuditPipeline.h"
#include "AuditValidator.h"

#include <memory>
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

AuditWriteOutcome make_audit_write_success(bool fallback_used) {
  return AuditWriteOutcome{
      .accepted = true,
      .persisted = true,
      .fallback_used = fallback_used,
      .error_code = std::nullopt,
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

class AuditServiceFacade {
 public:
  [[nodiscard]] std::unique_ptr<AuditServiceFacade> clone() const {
    return std::make_unique<AuditServiceFacade>(*this);
  }

  InfraOperationResult init(const AuditServiceConfig& config) {
    if (lifecycle_state_ != LifecycleState::Created) {
      return invalid_transition("init", "created");
    }

    if (!config.is_valid()) {
      return InfraOperationResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
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

  InfraOperationResult start() {
    if (lifecycle_state_ != LifecycleState::Initialized) {
      return invalid_transition("start", "initialized");
    }

    lifecycle_state_ = LifecycleState::Started;
    return InfraOperationResult::success();
  }

  InfraOperationResult stop() {
    if (lifecycle_state_ != LifecycleState::Started) {
      return invalid_transition("stop", "started");
    }

    lifecycle_state_ = LifecycleState::Stopped;
    return InfraOperationResult::success();
  }

  AuditWriteOutcome write_audit(const AuditEvent& event,
                                const AuditContext& context) {
    if (lifecycle_state_ != LifecycleState::Started) {
      return make_audit_write_failure(AuditErrorCode::WriteFail);
    }

    const auto validation_result = validator_.validate_write_input(event, context);
    if (!validation_result.ok) {
      return make_audit_write_failure(validation_result.error_code);
    }

    AuditPipeline primary_pipeline(&primary_records_, config_.primary_capacity);
    const auto pipeline_result = primary_pipeline.append(event);
    if (pipeline_result.ok) {
      return make_audit_write_success(false);
    }

    degraded_ = true;
    AuditFallbackPipeline fallback_pipeline(&fallback_records_,
                                            config_.fallback_capacity);
    const auto fallback_result = fallback_pipeline.append(event);
    if (fallback_result.ok) {
      return make_audit_write_success(true);
    }

    return make_audit_write_failure(fallback_result.error_code,
                                    true,
                                    true);
  }

  ExportResult export_audit(const ExportQuery& query) const {
    if (lifecycle_state_ != LifecycleState::Started) {
      return ExportResult{};
    }

    const auto validation_result = validator_.validate_export_query(query);
    if (!validation_result.ok) {
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

  [[nodiscard]] bool is_degraded() const {
    return degraded_;
  }

  [[nodiscard]] std::size_t primary_record_count() const {
    return primary_records_.size();
  }

  [[nodiscard]] std::size_t fallback_record_count() const {
    return fallback_records_.size();
  }

  [[nodiscard]] std::string_view lifecycle_state_name() const {
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

 private:
  enum class LifecycleState {
    Created,
    Initialized,
    Started,
    Stopped,
  };

  [[nodiscard]] InfraOperationResult invalid_transition(
      std::string_view operation,
      std::string_view expected_state) const {
    return InfraOperationResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "invalid audit lifecycle transition for operation " +
            std::string(operation) + ": expected state " +
            std::string(expected_state) + ", actual state " +
            std::string(lifecycle_state_name()),
        "audit.lifecycle",
        std::string(kAuditServiceSourceRef));
  }

  [[nodiscard]] std::vector<AuditEvent> select_records(
      const ExportQuery& query) const {
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

  AuditServiceConfig config_{};
  LifecycleState lifecycle_state_ = LifecycleState::Created;
  std::vector<AuditEvent> primary_records_;
  std::vector<AuditEvent> fallback_records_;
  bool degraded_ = false;
  AuditValidator validator_{};
};

AuditService::AuditService() : facade_(std::make_unique<AuditServiceFacade>()) {}

AuditService::~AuditService() = default;

AuditService::AuditService(const AuditService& other)
    : facade_(other.facade_ ? other.facade_->clone()
                            : std::make_unique<AuditServiceFacade>()) {}

AuditService& AuditService::operator=(const AuditService& other) {
  if (this != &other) {
    facade_ = other.facade_ ? other.facade_->clone()
                            : std::make_unique<AuditServiceFacade>();
  }

  return *this;
}

AuditService::AuditService(AuditService&&) noexcept = default;

AuditService& AuditService::operator=(AuditService&&) noexcept = default;

InfraOperationResult AuditService::init(const AuditServiceConfig& config) {
  return facade_->init(config);
}

InfraOperationResult AuditService::start() {
  return facade_->start();
}

InfraOperationResult AuditService::stop() {
  return facade_->stop();
}

AuditWriteOutcome AuditService::write_audit(const AuditEvent& event,
                                            const AuditContext& context) {
  return facade_->write_audit(event, context);
}

ExportResult AuditService::export_audit(const ExportQuery& query) {
  return facade_->export_audit(query);
}

std::string_view AuditService::lifecycle_state_name() const {
  return facade_->lifecycle_state_name();
}

bool AuditService::is_degraded() const {
  return facade_->is_degraded();
}

std::size_t AuditService::primary_record_count() const {
  return facade_->primary_record_count();
}

std::size_t AuditService::fallback_record_count() const {
  return facade_->fallback_record_count();
}

}  // namespace dasall::infra::audit