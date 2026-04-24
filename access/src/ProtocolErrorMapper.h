#pragma once

#include "AccessErrors.h"
#include "agent/AgentResult.h"

namespace dasall::access {

// 将 AgentResult 状态映射到协议层错误矩阵（成功路径也返回 2xx 对应映射）。
[[nodiscard]] AccessProtocolErrorMapping map_agent_result_to_protocol(
    const dasall::contracts::AgentResult& result);

}  // namespace dasall::access
