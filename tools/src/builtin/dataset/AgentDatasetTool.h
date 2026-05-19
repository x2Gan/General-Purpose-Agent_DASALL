#pragma once

#include <string_view>

#include "ServiceTypes.h"
#include "tool/ToolDescriptor.h"
#include "tool/ToolIR.h"

namespace dasall::tools::builtin::dataset {

inline constexpr std::string_view kToolName = "agent.dataset";

[[nodiscard]] bool matches(std::string_view tool_name);
[[nodiscard]] contracts::ToolDescriptor build_descriptor();
[[nodiscard]] services::DataQueryRequest build_query_request(
    const contracts::ToolIR& tool_ir,
    const services::ServiceCallContext& context,
    services::ServiceDataFreshness freshness);

}  // namespace dasall::tools::builtin::dataset