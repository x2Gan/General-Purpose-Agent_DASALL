#pragma once

#include <atomic>
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
      bool diagnostics_enabled,
      std::shared_ptr<std::atomic_bool> diagnostics_enabled_state = {});

  [[nodiscard]] RuntimeDispatchResult handle_diag(
      std::string_view command_name,
      std::string_view request_id,
      std::string_view actor_ref) const;

  [[nodiscard]] static bool is_read_only_diag_command(std::string_view command_name);

 private:
    [[nodiscard]] bool diagnostics_enabled() const;

  std::shared_ptr<dasall::infra::diagnostics::IDiagnosticsService> diagnostics_service_;
  bool diagnostics_enabled_ = false;
    std::shared_ptr<std::atomic_bool> diagnostics_enabled_state_;
};

}  // namespace dasall::access::daemon
