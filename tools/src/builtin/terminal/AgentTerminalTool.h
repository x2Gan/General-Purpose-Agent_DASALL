#pragma once

#include <string_view>

#include "ServiceTypes.h"
#include "tool/ToolDescriptor.h"
#include "tool/ToolIR.h"

namespace dasall::tools::builtin::terminal {

inline constexpr std::string_view kToolName = "agent.terminal";

[[nodiscard]] bool matches(std::string_view tool_name);
[[nodiscard]] contracts::ToolDescriptor build_descriptor();
[[nodiscard]] services::ExecutionCommandRequest build_action_request(
    const contracts::ToolIR& tool_ir,
    const services::ServiceCallContext& context);

}  // namespace dasall::tools::builtin::terminal