#include "config/ConfigPublisher.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace dasall::infra::config {

namespace {

constexpr std::string_view kPublisherSourceRef = "ConfigPublisher";

[[nodiscard]] ConfigPublishResult make_failure(std::string message) {
  return ConfigPublishResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                      std::move(message),
                                      "config.publish_config_changed",
                                      std::string(kPublisherSourceRef));
}

[[nodiscard]] std::string make_event_id(std::uint64_t to_version) {
  return "config-event://diff/" + std::to_string(to_version);
}

}  // namespace

bool ConfigPublisher::matches_namespace_filter(std::string_view key_path,
                                               std::string_view namespace_filter) {
  return key_path.starts_with(namespace_filter);
}

ConfigPublishResult ConfigPublisher::publish_config_changed(const ConfigDiff& diff) {
  if (!diff.is_valid()) {
    return make_failure("config diff must keep ordered versions and key-granular changes");
  }

  std::size_t delivered_subscriber_count = 0;
  for (const auto& subscription : subscriptions_) {
    const bool should_deliver = std::any_of(diff.changes.begin(),
                                            diff.changes.end(),
                                            [&](const ConfigDiffEntry& change) {
                                              return matches_namespace_filter(
                                                  change.key_path,
                                                  subscription.handle.namespace_filter);
                                            });
    if (!should_deliver) {
      continue;
    }

    try {
      subscription.callback(diff);
      ++delivered_subscriber_count;
    } catch (...) {
    }
  }

  return ConfigPublishResult::success(make_event_id(diff.to_version),
                                      delivered_subscriber_count);
}

std::optional<ConfigSubscriptionHandle> ConfigPublisher::subscribe(
    const ConfigSubscriptionRequest& subscription_request) {
  if (!subscription_request.is_valid()) {
    return std::nullopt;
  }

  ConfigSubscriptionHandle handle{
      .subscription_id = "subscription://config/" + std::to_string(next_subscription_id_++),
      .namespace_filter = subscription_request.namespace_filter,
      .subscriber_id = subscription_request.subscriber_id,
      .active = true,
  };
  subscriptions_.push_back(SubscriptionRecord{
      .handle = handle,
      .callback = subscription_request.callback,
  });
  return handle;
}

}  // namespace dasall::infra::config