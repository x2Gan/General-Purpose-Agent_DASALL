#pragma once

#include <optional>
#include <string>
#include <vector>

#include "diagnostics/DiagnosticsTypes.h"

namespace dasall::infra::diagnostics {

struct CommandExecutionResult {
  bool executed = false;
  std::string command_ref;
  std::string summary;
  std::vector<std::string> evidence_refs;
  std::string executed_at;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static CommandExecutionResult success(std::string command_ref,
                                                      std::string summary,
                                                      std::vector<std::string> evidence_refs,
                                                      std::string executed_at);
  [[nodiscard]] static CommandExecutionResult failure(contracts::ResultCode result_code,
                                                      std::string message,
                                                      std::string stage,
                                                      std::string source_ref);
  [[nodiscard]] bool is_valid() const;
};

class CommandExecutor final {
 public:
  [[nodiscard]] CommandExecutionResult execute(const DiagnosticsCommand& command) const;
};

}  // namespace dasall::infra::diagnostics