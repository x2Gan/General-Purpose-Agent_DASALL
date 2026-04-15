#pragma once

#include <string_view>
#include <vector>

#include "tool/ToolDescriptor.h"

namespace dasall::tools::registry {

inline constexpr std::string_view kBuiltinSourceKey = "builtin.static";

[[nodiscard]] std::vector<contracts::ToolDescriptor> build_builtin_catalog();

}  // namespace dasall::tools::registry