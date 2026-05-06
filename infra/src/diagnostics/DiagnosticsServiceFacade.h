#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "audit/IAuditLogger.h"
#include "diagnostics/DiagnosticsAuditBridge.h"
#include "diagnostics/DiagnosticsMetricsBridge.h"
#include "diagnostics/IDiagnosticsService.h"
#include "diagnostics/SnapshotAssembler.h"
#include "diagnostics/SnapshotStore.h"

namespace dasall::infra::diagnostics {

struct DiagnosticsServiceFacadeOptions {
  std::uint32_t safe_mode_failure_threshold = 5;
  std::uint32_t snapshot_retention_days = 7;
  std::size_t snapshot_max_count = 500;
  std::string profile_id = "unknown";
  std::shared_ptr<metrics::IMetricsProvider> metrics_provider;
  std::shared_ptr<audit::IAuditLogger> audit_logger;
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
  void inject_snapshot_store_commit_failure_for_test(std::string reason);
  void inject_snapshot_store_current_time_for_test(std::string now_rfc3339);

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
  void note_failure(std::string reason);
  void reset_failures();

  DiagnosticsServiceFacadeOptions options_{};
  LifecycleState lifecycle_state_ = LifecycleState::Created;
  std::uint32_t consecutive_failures_ = 0;
  std::optional<std::string> safe_mode_reason_;
  SnapshotAssembler snapshot_assembler_{};
  SnapshotStore snapshot_store_;
  DiagnosticsAuditBridge audit_bridge_;
  DiagnosticsMetricsBridge metrics_bridge_;
};

}  // namespace dasall::infra::diagnostics