#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "config/IConfigCenter.h"
#include "config/IConfigPublisher.h"

namespace dasall::infra::config {

class ConfigPublisher final : public IConfigPublisher {
 public:
  ConfigPublisher() = default;

  ConfigPublishResult publish_config_changed(const ConfigDiff& diff) override;
  [[nodiscard]] std::optional<ConfigSubscriptionHandle> subscribe(
      const ConfigSubscriptionRequest& subscription_request);

 private:
  struct SubscriptionRecord {
    ConfigSubscriptionHandle handle;
    ConfigChangedCallback callback;
  };

  [[nodiscard]] static bool matches_namespace_filter(const std::string_view& key_path,
                                                     const std::string_view& namespace_filter);

  std::uint64_t next_subscription_id_ = 1;
  std::vector<SubscriptionRecord> subscriptions_;
};

}  // namespace dasall::infra::config