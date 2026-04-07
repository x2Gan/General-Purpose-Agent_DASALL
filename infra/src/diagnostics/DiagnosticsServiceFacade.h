#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "diagnostics/IDiagnosticsService.h"

namespace dasall::infra::diagnostics {

struct DiagnosticsServiceFacadeOptions {
  std::uint32_t safe_mode_failure_threshold = 5;
};

class DiagnosticsServiceFacade final : public IDiagnosticsService {
 public:
  explicit DiagnosticsServiceFacade(DiagnosticsServiceFacadeOptions options = {});

  [[nodiscard]] bool start();
  void enter_safe_mode_for_test(std::string reason);

  [[nodiscard]] bool is_ready() const;
  [[nodiscard]] bool is_in_safe_mode() const;
  [[nodiscard]] std::uint32_t consecutive_failures() const;
  [[nodiscard]] std::optional<std::string> safe_mode_reason() const;

  [[nodiscard]] DiagnosticsSnapshotResult execute(const DiagnosticsCommand& command) override;
  [[nodiscard]] DiagnosticsSnapshotResult get_snapshot(const SnapshotQuery& query) override;
  [[nodiscard]] SnapshotExportResult export_snapshot(const SnapshotExportRequest& request) override;

 private:
  enum class LifecycleState {
    Created,
    Ready,
    SafeMode,
  };

  [[nodiscard]] bool allows_command_in_current_mode(const DiagnosticsCommand& command) const;
  [[nodiscard]] DiagnosticsSnapshot build_snapshot(const DiagnosticsCommand& command);
  void note_failure(std::string reason);
  void reset_failures();

  DiagnosticsServiceFacadeOptions options_{};
  LifecycleState lifecycle_state_ = LifecycleState::Created;
  std::uint32_t consecutive_failures_ = 0;
  std::uint64_t next_snapshot_index_ = 1;
  std::optional<std::string> safe_mode_reason_;
  std::unordered_map<std::string, DiagnosticsSnapshot> snapshots_;
};

}  // namespace dasall::infra::diagnostics