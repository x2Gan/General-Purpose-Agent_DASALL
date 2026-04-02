#pragma once

#include <string>

#include "InfraContext.h"
#include "LogEvent.h"
#include "audit/AuditTypes.h"

namespace dasall::infra::logging {

using LogContext = ::dasall::infra::InfraContext;
using LogLevel = ::dasall::infra::LogLevel;
using LogEvent = ::dasall::infra::LogEvent;
using AuditOutcome = ::dasall::infra::AuditOutcome;
using AuditEvidenceKind = ::dasall::infra::AuditEvidenceKind;
using AuditEvent = ::dasall::infra::AuditEvent;
using AuditContext = ::dasall::infra::AuditContext;
using AuditEvidenceRef = ::dasall::infra::AuditEvidenceRef;

struct AuditRef {
  AuditEvidenceRef evidence_ref;
  std::string trace_id = std::string(LogContext::kUnknownIdentifier);
  std::string task_id = std::string(LogContext::kUnknownIdentifier);

  [[nodiscard]] bool has_value() const {
    return evidence_ref.has_value();
  }

  [[nodiscard]] bool has_non_empty_fields() const {
    return !trace_id.empty() && !task_id.empty();
  }

  [[nodiscard]] bool uses_unknown_defaults() const {
    return has_non_empty_fields() &&
           trace_id == LogContext::kUnknownIdentifier &&
           task_id == LogContext::kUnknownIdentifier;
  }
};

}  // namespace dasall::infra::logging