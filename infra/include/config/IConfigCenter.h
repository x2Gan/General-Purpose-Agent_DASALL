#pragma once

#include <functional>
#include <optional>
#include <string>

#include "config/ConfigTypes.h"

namespace dasall::infra::config {

using ConfigChangedCallback = std::function<void(const ConfigDiff&)>;

struct ConfigStartupContext {
  std::string requested_profile_id;
  std::string deployment_source_ref;
  std::string runtime_overlay_source_ref;
  std::string actor_ref;
  bool load_runtime_overlay = true;

  [[nodiscard]] bool is_valid() const {
    return is_supported_profile_id(requested_profile_id) && !actor_ref.empty();
  }
};

struct ConfigRollbackToken {
  std::string token;
  std::string actor_ref;

  [[nodiscard]] bool is_valid() const {
    return !token.empty() && !actor_ref.empty();
  }
};

struct ConfigSubscriptionRequest {
  std::string namespace_filter;
  std::string subscriber_id;
  ConfigChangedCallback callback;

  [[nodiscard]] bool is_valid() const {
    return !namespace_filter.empty() && !subscriber_id.empty() &&
           static_cast<bool>(callback);
  }
};

struct ConfigSubscriptionHandle {
  std::string subscription_id;
  std::string namespace_filter;
  std::string subscriber_id;
  bool active = false;

  [[nodiscard]] bool is_valid() const {
    return !subscription_id.empty() && !namespace_filter.empty() &&
           !subscriber_id.empty();
  }
};

class IConfigCenter {
 public:
  virtual ~IConfigCenter() = default;

  virtual ConfigApplyResult load_layers(const ConfigStartupContext& startup_context) = 0;
  virtual std::optional<TypedConfig> get_typed(const ConfigQuery& query) const = 0;
  virtual ConfigApplyResult apply_override(const ConfigPatch& config_patch) = 0;
  virtual ConfigApplyResult rollback(const ConfigRollbackToken& rollback_token) = 0;
  virtual std::optional<ConfigSubscriptionHandle> subscribe(
      const ConfigSubscriptionRequest& subscription_request) = 0;
};

}  // namespace dasall::infra::config