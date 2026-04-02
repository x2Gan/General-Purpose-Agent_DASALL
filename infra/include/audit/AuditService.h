#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "IInfrastructureService.h"
#include "IAuditLogger.h"

namespace dasall::infra::audit {

struct AuditServiceConfig {
  std::size_t primary_capacity = 64;
  std::size_t fallback_capacity = 16;

  [[nodiscard]] bool is_valid() const {
    return primary_capacity > 0 || fallback_capacity > 0;
  }
};

class AuditService final : public IAuditLogger {
 public:
  AuditService() = default;

  InfraOperationResult init(const AuditServiceConfig& config);
  InfraOperationResult start();
  InfraOperationResult stop();

  AuditWriteOutcome write_audit(const AuditEvent& event,
                                const AuditContext& context) override;
  ExportResult export_audit(const ExportQuery& query) override;

  [[nodiscard]] bool is_degraded() const {
    return degraded_;
  }

  [[nodiscard]] std::size_t primary_record_count() const {
    return primary_records_.size();
  }

  [[nodiscard]] std::size_t fallback_record_count() const {
    return fallback_records_.size();
  }

  [[nodiscard]] std::string_view lifecycle_state_name() const;

 private:
  enum class LifecycleState {
    Created,
    Initialized,
    Started,
    Stopped,
  };

  [[nodiscard]] InfraOperationResult invalid_transition(
      std::string_view operation,
      std::string_view expected_state) const;

  [[nodiscard]] std::vector<AuditEvent> select_records(const ExportQuery& query) const;

  AuditServiceConfig config_{};
  LifecycleState lifecycle_state_ = LifecycleState::Created;
  std::vector<AuditEvent> primary_records_;
  std::vector<AuditEvent> fallback_records_;
  bool degraded_ = false;
};

}  // namespace dasall::infra::audit