#include "BuiltinCatalog.h"

#include "builtin/dataset/AgentDatasetTool.h"
#include "builtin/terminal/AgentTerminalTool.h"

namespace dasall::tools::registry {

std::vector<contracts::ToolDescriptor> build_builtin_catalog() {
  return {
      builtin::terminal::build_descriptor(),
      builtin::dataset::build_descriptor(),
  };
}

}  // namespace dasall::tools::registry