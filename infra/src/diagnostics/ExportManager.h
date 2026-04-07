#pragma once

#include <string>
#include <vector>

#include "diagnostics/DiagnosticsTypes.h"

namespace dasall::infra::diagnostics {

struct ExportManagerOptions {
  bool local_export_enabled = true;
  bool remote_export_enabled = false;
  std::vector<std::string> remote_allowed_targets;
};

class ExportManager final {
 public:
  explicit ExportManager(ExportManagerOptions options = {});

  [[nodiscard]] SnapshotExportResult export_snapshot(const DiagnosticsSnapshot& snapshot,
                                                     const SnapshotExportRequest& request) const;

 private:
  ExportManagerOptions options_{};
};

}  // namespace dasall::infra::diagnostics