#pragma once

#include <span>
#include <vector>

#include "ToolInvocationContext.h"
#include "ToolInvocationEnvelope.h"
#include "tool/ToolRequest.h"

namespace dasall::tools {

struct CompensationRequest;

class IToolManager {
 public:
  virtual ~IToolManager() = default;

  virtual ToolInvocationEnvelope invoke(
	  const dasall::contracts::ToolRequest& request,
	  const ToolInvocationContext& context) = 0;

  virtual std::vector<ToolInvocationEnvelope> invoke_batch(
	  std::span<const dasall::contracts::ToolRequest> requests,
	  const ToolInvocationContext& context) = 0;

  virtual ToolInvocationEnvelope compensate(
	  const CompensationRequest& request,
	  const ToolInvocationContext& context) = 0;
};

}  // namespace dasall::tools