#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "KnowledgeTypes.h"
#include "health/FreshnessController.h"
#include "health/KnowledgeTelemetry.h"

namespace dasall::knowledge {

struct HealthProbeDeps {
  std::function<bool()> knowledge_enabled;
  std::function<bool()> lifecycle_ready;
  std::function<std::optional<IndexManifest>()> active_manifest;
  std::function<FreshnessSnapshot()> freshness_snapshot;
  std::function<bool()> vector_backend_available;
  std::function<bool()> last_known_good_available;
  std::function<KnowledgeTelemetryStatus()> telemetry_status;
  std::function<std::uint64_t()> degraded_return_count;
  std::function<std::vector<std::string>()> recent_reason_codes;
};

class KnowledgeHealthProbe {
 public:
  explicit KnowledgeHealthProbe(HealthProbeDeps deps);

  [[nodiscard]] KnowledgeHealthSnapshot collect() const;

 private:
  [[nodiscard]] HealthState classify_state(
      const KnowledgeHealthSnapshot& snapshot) const;

  HealthProbeDeps deps_{};
};

}  // namespace dasall::knowledge