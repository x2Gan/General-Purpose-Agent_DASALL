#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "health/HealthEvaluator.h"
#include "health/IHealthMonitor.h"
#include "health/ProbeExecutor.h"
#include "health/ProbeRegistry.h"

namespace dasall::infra {

class HealthMonitorFacade final : public IHealthMonitor {
 public:
  HealthMonitorFacade();

  HealthMonitorRegistrationResult register_probe(
      const HealthProbeRegistration& registration) override;
  HealthSnapshotResult evaluate_now() override;
  [[nodiscard]] HealthSnapshotResult get_snapshot() const override;
  HealthListenerSubscriptionResult subscribe(IHealthStateListener& listener) override;

  [[nodiscard]] bool is_ready() const;
  [[nodiscard]] bool is_in_safe_observe_mode() const;
  [[nodiscard]] std::size_t registered_probe_count() const;
  [[nodiscard]] std::size_t listener_count() const;
  [[nodiscard]] std::optional<std::string> safe_observe_reason() const;

  void enter_safe_observe_mode_for_test(std::string reason);

 private:
  enum class LifecycleState {
    Created,
    Ready,
    SafeObserveMode,
  };

  [[nodiscard]] std::vector<ProbeResult> execute_registered_probes();
  [[nodiscard]] HealthSnapshot finalize_snapshot(HealthSnapshot snapshot);
  void notify_transition_if_needed(
      const std::optional<HealthSnapshot>& previous_snapshot,
      const HealthSnapshot& current_snapshot);

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  std::uint64_t next_snapshot_version_ = 1;
  ProbeRegistry registry_;
  HealthEvaluator evaluator_;
  ProbeExecutor executor_;
  std::vector<IHealthStateListener*> listeners_;
  std::optional<HealthSnapshot> latest_snapshot_;
  std::optional<std::string> safe_observe_reason_;
};

}  // namespace dasall::infra