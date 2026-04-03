#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

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

class AuditServiceFacade;

class AuditService final : public IAuditLogger {
 public:
  AuditService();
  ~AuditService() override;

  AuditService(const AuditService& other);
  AuditService& operator=(const AuditService& other);
  AuditService(AuditService&&) noexcept;
  AuditService& operator=(AuditService&&) noexcept;

  InfraOperationResult init(const AuditServiceConfig& config);
  InfraOperationResult start();
  InfraOperationResult stop();

  AuditWriteOutcome write_audit(const AuditEvent& event,
                                const AuditContext& context) override;
  ExportResult export_audit(const ExportQuery& query) override;

  [[nodiscard]] bool is_degraded() const;

  [[nodiscard]] std::size_t primary_record_count() const;

  [[nodiscard]] std::size_t fallback_record_count() const;

  [[nodiscard]] std::string_view lifecycle_state_name() const;

 private:
  std::unique_ptr<AuditServiceFacade> facade_;
};

}  // namespace dasall::infra::audit