#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tool/ToolDescriptor.h"
#include "tool/ToolIR.h"
#include "tool/ToolRequest.h"

namespace dasall::tools::validation {

struct ValidationDiagnostics {
  std::string error_code;
  std::string message;

  [[nodiscard]] bool has_error() const {
    return !error_code.empty();
  }
};

struct ToolValidationResult {
  std::optional<contracts::ToolIR> tool_ir;
  std::optional<ValidationDiagnostics> diagnostics;

  [[nodiscard]] bool ok() const {
    return tool_ir.has_value() && !diagnostics.has_value();
  }
};

class ToolValidator {
 public:
  [[nodiscard]] ToolValidationResult validate(
      const contracts::ToolRequest& request,
      const contracts::ToolDescriptor& descriptor) const;
  [[nodiscard]] contracts::ToolIR inject_defaults(
      const contracts::ToolRequest& request,
      const contracts::ToolDescriptor& descriptor,
      contracts::ToolIROperation operation,
      std::string normalized_arguments) const;
  [[nodiscard]] std::string normalize_arguments(std::string_view arguments_payload) const;
  [[nodiscard]] std::optional<contracts::ToolIROperation> derive_operation(
      const contracts::ToolRequest& request) const;

 private:
  [[nodiscard]] static ToolValidationResult invalid(
      std::string error_code,
      std::string message);
  [[nodiscard]] static bool invocation_matches_category(
      contracts::ToolInvocationKind invocation_kind,
      contracts::ToolCategory category);
  [[nodiscard]] static bool has_tag(
      const std::optional<std::vector<std::string>>& tags,
      std::string_view target_tag);
  [[nodiscard]] static std::string trim_ascii(std::string_view value);
  [[nodiscard]] static contracts::ToolIRRoute build_route_hint(
      const contracts::ToolDescriptor& descriptor,
      const contracts::ToolRequest& request);
};

}  // namespace dasall::tools::validation