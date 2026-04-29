#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "AccessTypes.h"
#include "diagnostics/IDiagnosticsService.h"

namespace dasall::access::daemon {

class DaemonDiagnosticsHandler {
 public:
  explicit DaemonDiagnosticsHandler(
      std::shared_ptr<dasall::infra::diagnostics::IDiagnosticsService> diagnostics_service,
      bool diagnostics_enabled);

  [[nodiscard]] RuntimeDispatchResult handle_diag(
      std::string_view command_name,
      std::string_view request_id,
      std::string_view actor_ref) const;

  [[nodiscard]] static bool is_read_only_diag_command(std::string_view command_name);

 private:
  std::shared_ptr<dasall::infra::diagnostics::IDiagnosticsService> diagnostics_service_;
  bool diagnostics_enabled_ = false;
};

}  // namespace dasall::access::daemon
