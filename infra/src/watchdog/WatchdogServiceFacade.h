#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "watchdog/IWatchdogService.h"

namespace dasall::infra::watchdog {

class WatchdogServiceFacade final : public IWatchdogService {
 public:
  WatchdogServiceFacade() = default;

  WatchdogOperationResult init(const WatchdogServiceConfig& config) override;
  WatchdogOperationResult start() override;
  WatchdogOperationResult stop(std::uint32_t timeout_ms) override;
  WatchdogOperationResult register_entity(
      const WatchedEntityDescriptor& descriptor) override;
  WatchdogOperationResult unregister_entity(std::string_view entity_id) override;
  WatchdogOperationResult heartbeat(const HeartbeatSample& sample) override;
  [[nodiscard]] WatchdogSnapshotQueryResult snapshot() const override;

  [[nodiscard]] std::string_view lifecycle_state_name() const;
  [[nodiscard]] bool has_snapshot() const;
  [[nodiscard]] std::optional<std::uint32_t> last_stop_timeout_ms() const;

 private:
  enum class LifecycleState {
    Created,
    Initialized,
    Started,
    Stopped,
  };

  [[nodiscard]] WatchdogOperationResult invalid_transition(
      std::string_view operation,
      std::string_view expected_state) const;
  [[nodiscard]] WatchdogOperationResult component_not_ready(
      std::string_view operation,
      std::string_view component) const;
  void refresh_snapshot();

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  WatchdogServiceConfig last_config_{};
  std::uint64_t next_snapshot_version_ = 1;
  std::shared_ptr<WatchdogSnapshot> latest_snapshot_;
  std::optional<std::uint32_t> last_stop_timeout_ms_;
};

}  // namespace dasall::infra::watchdog