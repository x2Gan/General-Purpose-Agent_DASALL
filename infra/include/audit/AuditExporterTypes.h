#pragma once

#include <cstdint>
#include <string>

#include "audit/AuditTypes.h"

namespace dasall::infra {

struct ExportQuery {
  std::int64_t start_ts = 0;
  std::int64_t end_ts = 0;
  std::string actor;
  std::string action;
  std::string target;
  AuditOutcome outcome = AuditOutcome::Unspecified;
  std::string page_token;

  [[nodiscard]] bool has_required_window() const {
    return start_ts > 0 && end_ts > 0;
  }

  [[nodiscard]] bool has_ordered_window() const {
    return has_required_window() && end_ts >= start_ts;
  }

  [[nodiscard]] bool requests_page_resume() const {
    return !page_token.empty();
  }

  [[nodiscard]] bool filters_on_outcome() const {
    return outcome != AuditOutcome::Unspecified;
  }
};

}  // namespace dasall::infra