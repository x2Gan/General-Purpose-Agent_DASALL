#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "plugin/PluginDescriptor.h"

namespace dasall::infra::plugin {

struct RejectedPluginRecord {
  PluginDescriptor descriptor;
  std::string reason_code;
  std::string evidence_ref;

  [[nodiscard]] bool is_traceable() const {
    return descriptor.plugin_id != kPluginUnknownValue && !reason_code.empty() &&
           !evidence_ref.empty();
  }
};

[[nodiscard]] inline bool has_unique_plugin_ids(
    const std::vector<PluginDescriptor>& descriptors) {
  for (std::size_t index = 0; index < descriptors.size(); ++index) {
    if (!descriptors[index].is_governance_ready()) {
      return false;
    }

    for (std::size_t probe = index + 1; probe < descriptors.size(); ++probe) {
      if (descriptors[index].plugin_id == descriptors[probe].plugin_id) {
        return false;
      }
    }
  }

  return true;
}

[[nodiscard]] inline bool has_unique_rejected_plugin_ids(
    const std::vector<RejectedPluginRecord>& rejected_plugins) {
  for (std::size_t index = 0; index < rejected_plugins.size(); ++index) {
    if (!rejected_plugins[index].is_traceable()) {
      return false;
    }

    for (std::size_t probe = index + 1; probe < rejected_plugins.size(); ++probe) {
      if (rejected_plugins[index].descriptor.plugin_id ==
          rejected_plugins[probe].descriptor.plugin_id) {
        return false;
      }
    }
  }

  return true;
}

[[nodiscard]] inline bool has_disjoint_plugin_ids(
    const std::vector<PluginDescriptor>& discovered_plugins,
    const std::vector<RejectedPluginRecord>& rejected_plugins) {
  return std::none_of(
      discovered_plugins.begin(),
      discovered_plugins.end(),
      [&rejected_plugins](const PluginDescriptor& descriptor) {
        return std::any_of(rejected_plugins.begin(),
                           rejected_plugins.end(),
                           [&descriptor](const RejectedPluginRecord& rejected_plugin) {
                             return rejected_plugin.descriptor.plugin_id == descriptor.plugin_id;
                           });
      });
}

struct PluginCatalog {
  std::vector<PluginDescriptor> discovered_plugins;
  std::vector<RejectedPluginRecord> rejected_plugins;

  [[nodiscard]] bool empty() const {
    return discovered_plugins.empty() && rejected_plugins.empty();
  }

  [[nodiscard]] bool has_traceable_rejections() const {
    return std::all_of(rejected_plugins.begin(),
                       rejected_plugins.end(),
                       [](const RejectedPluginRecord& rejected_plugin) {
                         return rejected_plugin.is_traceable();
                       });
  }

  [[nodiscard]] bool has_consistent_entries() const {
    return has_unique_plugin_ids(discovered_plugins) &&
           has_unique_rejected_plugin_ids(rejected_plugins) &&
           has_disjoint_plugin_ids(discovered_plugins, rejected_plugins) &&
           has_traceable_rejections();
  }
};

}  // namespace dasall::infra::plugin