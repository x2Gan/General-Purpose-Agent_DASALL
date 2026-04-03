#pragma once

#include <cstdint>
#include <string>

#include "logging/IAuditLinkAdapter.h"
#include "logging/LogTypes.h"

namespace dasall::infra::logging {

class AuditLinkAdapter final : public IAuditLinkAdapter {
 public:
  LogWriteResult attach_audit_ref(LogEvent& event,
                                  const AuditRef& audit_ref) override;
  void report_link_failure(std::string_view reason) override;

  [[nodiscard]] std::uint64_t link_failure_count() const {
    return link_failure_count_;
  }

  [[nodiscard]] const std::string& last_failure_reason() const {
    return last_failure_reason_;
  }

 private:
  [[nodiscard]] static bool is_high_risk_event(const LogEvent& event);
  [[nodiscard]] static std::string evidence_kind_name(AuditEvidenceKind kind);

  std::uint64_t link_failure_count_ = 0;
  std::string last_failure_reason_;
};

}  // namespace dasall::infra::logging