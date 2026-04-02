#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config/IConfigCenter.h"

namespace dasall::infra::config {

class ConfigCenterFacade final : public IConfigCenter {
 public:
  ConfigCenterFacade() = default;

  ConfigApplyResult load_layers(const ConfigStartupContext& startup_context) override;
  [[nodiscard]] std::optional<TypedConfig> get_typed(const ConfigQuery& query) const override;
  ConfigApplyResult apply_override(const ConfigPatch& config_patch) override;
  ConfigApplyResult rollback(const ConfigRollbackToken& rollback_token) override;
  [[nodiscard]] std::optional<ConfigSubscriptionHandle> subscribe(
      const ConfigSubscriptionRequest& subscription_request) override;

 private:
  enum class LifecycleState {
    Created,
    Ready,
  };

  struct RollbackRecord {
    std::string token;
    std::string actor_ref;
    ConfigSnapshot snapshot;
  };

  struct SubscriptionRecord {
    ConfigSubscriptionHandle handle;
    ConfigChangedCallback callback;
  };

  [[nodiscard]] bool is_ready() const;

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  std::uint64_t next_version_ = 1;
  std::uint64_t next_subscription_id_ = 1;
  ConfigSnapshot current_snapshot_;
  std::vector<RollbackRecord> rollback_records_;
  std::vector<SubscriptionRecord> subscriptions_;
};

}  // namespace dasall::infra::config