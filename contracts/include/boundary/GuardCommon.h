#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace dasall::contracts {

inline bool has_non_empty_value(const std::optional<std::string>& value) {
  return value.has_value() && !value->empty();
}

inline constexpr bool is_supported_error_source_ref_type(std::string_view ref_type) {
  return ref_type == "observation" || ref_type == "tool_call" || ref_type == "worker_task" ||
         ref_type == "checkpoint";
}

}  // namespace dasall::contracts
