#pragma once

#include "MultiAgentTypes.h"

namespace dasall::multi_agent {

[[nodiscard]] MultiAgentRuntimeFoldResult fold_multi_agent_report_for_runtime(
    const MultiAgentExecutionReport& report);

}  // namespace dasall::multi_agent