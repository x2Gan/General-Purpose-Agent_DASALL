#pragma once

#include "diagnostics/DiagnosticsTypes.h"

namespace dasall::infra::diagnostics {

class IDiagnosticsService {
 public:
  virtual ~IDiagnosticsService() = default;

  virtual DiagnosticsSnapshotResult execute(const DiagnosticsCommand& command) = 0;
  virtual DiagnosticsSnapshotResult get_snapshot(const SnapshotQuery& query) = 0;
  virtual SnapshotExportResult export_snapshot(const SnapshotExportRequest& request) = 0;
};

}  // namespace dasall::infra::diagnostics