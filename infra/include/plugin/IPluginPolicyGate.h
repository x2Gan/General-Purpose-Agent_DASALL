#pragma once

#include <string>

#include "plugin/PluginDescriptor.h"
#include "policy/PolicyDecisionRef.h"
#include "policy/PolicySnapshot.h"

namespace dasall::infra::plugin {

struct PluginPolicyRequest {
  PluginDescriptor descriptor;
  std::string manifest_ref = std::string(kPluginUnknownValue);
  std::string profile_id = std::string(kPluginUnknownValue);

  [[nodiscard]] bool is_valid() const {
    const bool has_allowed_status = descriptor.status == PluginStatus::Discovered ||
                                    descriptor.status == PluginStatus::Validated;
    return descriptor.is_governance_ready() && has_allowed_status &&
           manifest_ref != kPluginUnknownValue && profile_id != kPluginUnknownValue;
  }
};

class IPluginPolicyGate {
 public:
  virtual ~IPluginPolicyGate() = default;

  [[nodiscard]] virtual policy::PolicyDecisionRef evaluate(
      const PluginPolicyRequest& request,
      const policy::PolicySnapshot& policy_snapshot) const = 0;
};

}  // namespace dasall::infra::plugin