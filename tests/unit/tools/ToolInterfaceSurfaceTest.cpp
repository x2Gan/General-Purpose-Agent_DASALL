#include <exception>
#include <iostream>

#include <span>
#include <type_traits>
#include <vector>

#include "ITool.h"
#include "IToolManager.h"

namespace {

using dasall::tools::CompensationRequest;
using dasall::tools::ITool;
using dasall::tools::IToolManager;
using dasall::tools::ToolExecutionContext;
using dasall::tools::ToolInvocationContext;
using dasall::tools::ToolInvocationEnvelope;

static_assert(std::is_same_v<decltype(&ITool::descriptor),
                             const dasall::contracts::ToolDescriptor& (ITool::*)() const>);
static_assert(std::is_same_v<decltype(&ITool::execute),
                             dasall::contracts::ToolResult (ITool::*)(
                                 const dasall::contracts::ToolIR&, const ToolExecutionContext&)>);
static_assert(std::is_abstract_v<ITool>);

static_assert(std::is_same_v<decltype(&IToolManager::invoke),
                             ToolInvocationEnvelope (IToolManager::*)(
                                 const dasall::contracts::ToolRequest&,
                                 const ToolInvocationContext&)>);
static_assert(std::is_same_v<decltype(&IToolManager::invoke_batch),
                             std::vector<ToolInvocationEnvelope> (IToolManager::*)(
                                 std::span<const dasall::contracts::ToolRequest>,
                                 const ToolInvocationContext&)>);
static_assert(std::is_same_v<decltype(&IToolManager::compensate),
                             ToolInvocationEnvelope (IToolManager::*)(
                                 const CompensationRequest&,
                                 const ToolInvocationContext&)>);
static_assert(std::is_abstract_v<IToolManager>);

void tool_manager_batch_surface_keeps_non_owning_request_view() {
  using BatchType = std::span<const dasall::contracts::ToolRequest>;

  static_assert(std::is_same_v<BatchType::element_type, const dasall::contracts::ToolRequest>);
  static_cast<void>(sizeof(BatchType));
}

}  // namespace

int main() {
  try {
    tool_manager_batch_surface_keeps_non_owning_request_view();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}