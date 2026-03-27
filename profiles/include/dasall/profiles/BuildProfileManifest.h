#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::profiles {

struct BuildProfileManifest {
  using NameList = std::vector<std::string>;

  NameList enabled_modules;
  NameList enabled_adapters;
  std::string observability_level;
  NameList build_tags;
  std::optional<std::string> toolchain_hint;

  [[nodiscard]] bool has_consistent_values() const {
    return !enabled_modules.empty() && !observability_level.empty() &&
           has_unique_names(enabled_modules) && has_unique_names(enabled_adapters) &&
           has_unique_names(build_tags) && (!toolchain_hint.has_value() || !toolchain_hint->empty());
  }

  [[nodiscard]] bool enables_module(std::string_view module_name) const {
    return contains_name(enabled_modules, module_name);
  }

  [[nodiscard]] bool enables_adapter(std::string_view adapter_name) const {
    return contains_name(enabled_adapters, adapter_name);
  }

  [[nodiscard]] bool has_build_tag(std::string_view tag) const {
    return contains_name(build_tags, tag);
  }

 private:
  [[nodiscard]] static bool contains_name(const NameList& names, std::string_view target) {
    return std::any_of(names.begin(), names.end(), [target](const std::string& name) {
      return name == target;
    });
  }

  [[nodiscard]] static bool has_unique_names(const NameList& names) {
    NameList sorted_names = names;
    std::sort(sorted_names.begin(), sorted_names.end());
    return std::adjacent_find(sorted_names.begin(), sorted_names.end()) == sorted_names.end();
  }
};

}  // namespace dasall::profiles