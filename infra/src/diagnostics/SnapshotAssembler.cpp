#include "diagnostics/SnapshotAssembler.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace dasall::infra::diagnostics {
namespace {

[[nodiscard]] std::string fallback_collected_at() {
  const auto now = std::chrono::system_clock::now();
  const auto now_time = std::chrono::system_clock::to_time_t(now);
  std::tm utc_time{};
  gmtime_r(&now_time, &utc_time);

  std::ostringstream stream;
  stream << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

[[nodiscard]] std::string fallback_summary() {
  return "diagnostics snapshot assembled";
}

[[nodiscard]] std::vector<std::string> bind_evidence_refs(const EvidenceBundle& evidence) {
  std::vector<std::string> evidence_refs{evidence.logs_ref,
                                         evidence.metrics_ref,
                                         evidence.health_ref,
                                         evidence.errors_ref};
  evidence_refs.insert(evidence_refs.end(), evidence.artifacts.begin(), evidence.artifacts.end());
  return evidence_refs;
}

}  // namespace

DiagnosticsSnapshot SnapshotAssembler::assemble(const DiagnosticsCommand& command,
                                                const CommandExecutionResult& execution,
                                                const EvidenceBundle& evidence) {
  return DiagnosticsSnapshot{
      .snapshot_id = next_snapshot_id(),
      .command = command,
      .collected_at = execution.executed_at.empty() ? fallback_collected_at() : execution.executed_at,
      .summary = execution.summary.empty() ? fallback_summary() : execution.summary,
      .evidence_refs = bind_evidence_refs(evidence),
      .redaction_profile = RedactionProfile::Strict,
      .exporter_hint = std::string("local_file"),
  };
}

std::string SnapshotAssembler::next_snapshot_id() {
  return std::string("diag-snapshot-") + std::to_string(next_snapshot_index_++);
}

}  // namespace dasall::infra::diagnostics