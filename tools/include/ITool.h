#pragma once

#include "tool/ToolDescriptor.h"
#include "tool/ToolIR.h"
#include "tool/ToolResult.h"

namespace dasall::tools {

struct ToolExecutionContext;

class ITool {
 public:
  virtual ~ITool() = default;

  [[nodiscard]] virtual const dasall::contracts::ToolDescriptor& descriptor() const = 0;

  virtual dasall::contracts::ToolResult execute(
	  const dasall::contracts::ToolIR& tool_ir,
	  const ToolExecutionContext& context) = 0;
};

}  // namespace dasall::tools