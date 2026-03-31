#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::diagnostics {

enum class DiagnosticsErrorCode {
  CommandDenied = 1,
  CommandInvalid = 2,
  ExecTimeout = 3,
  ExecFail = 4,
  RedactionFail = 5,
  SnapshotStoreFail = 6,
  ExportFail = 7,
  RemoteExportDisabled = 8,
};

struct DiagnosticsErrorMapping {
  DiagnosticsErrorCode diagnostics_code;
  contracts::ResultCode result_code;
  std::string_view reason;
};

inline constexpr std::string_view diagnostics_error_code_name(DiagnosticsErrorCode code) {
  switch (code) {
    case DiagnosticsErrorCode::CommandDenied:
      return "INF_E_DIAG_COMMAND_DENIED";
    case DiagnosticsErrorCode::CommandInvalid:
      return "INF_E_DIAG_COMMAND_INVALID";
    case DiagnosticsErrorCode::ExecTimeout:
      return "INF_E_DIAG_EXEC_TIMEOUT";
    case DiagnosticsErrorCode::ExecFail:
      return "INF_E_DIAG_EXEC_FAIL";
    case DiagnosticsErrorCode::RedactionFail:
      return "INF_E_DIAG_REDACTION_FAIL";
    case DiagnosticsErrorCode::SnapshotStoreFail:
      return "INF_E_DIAG_SNAPSHOT_STORE_FAIL";
    case DiagnosticsErrorCode::ExportFail:
      return "INF_E_DIAG_EXPORT_FAIL";
    case DiagnosticsErrorCode::RemoteExportDisabled:
      return "INF_E_DIAG_REMOTE_EXPORT_DISABLED";
  }

  return "INF_E_DIAG_UNKNOWN";
}

inline constexpr DiagnosticsErrorMapping map_diagnostics_error_code(DiagnosticsErrorCode code) {
  switch (code) {
    case DiagnosticsErrorCode::CommandDenied:
      return DiagnosticsErrorMapping{
          .diagnostics_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .reason = "diagnostics authorization denials stay inside contracts policy category",
      };
    case DiagnosticsErrorCode::CommandInvalid:
      return DiagnosticsErrorMapping{
          .diagnostics_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "diagnostics command validation failures stay inside contracts validation category",
      };
    case DiagnosticsErrorCode::ExecTimeout:
      return DiagnosticsErrorMapping{
          .diagnostics_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .reason = "diagnostics execution timeouts stay inside contracts provider category",
      };
    case DiagnosticsErrorCode::ExecFail:
      return DiagnosticsErrorMapping{
          .diagnostics_code = code,
          .result_code = contracts::ResultCode::ToolExecutionFailed,
          .reason = "diagnostics executor failures stay inside contracts tool category",
      };
    case DiagnosticsErrorCode::RedactionFail:
      return DiagnosticsErrorMapping{
          .diagnostics_code = code,
          .result_code = contracts::ResultCode::ToolExecutionFailed,
          .reason = "diagnostics redaction failures stay inside contracts tool category",
      };
    case DiagnosticsErrorCode::SnapshotStoreFail:
      return DiagnosticsErrorMapping{
          .diagnostics_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "diagnostics snapshot persistence failures stay inside contracts runtime category",
      };
    case DiagnosticsErrorCode::ExportFail:
      return DiagnosticsErrorMapping{
          .diagnostics_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .reason = "diagnostics export dependency failures stay inside contracts provider category",
      };
    case DiagnosticsErrorCode::RemoteExportDisabled:
      return DiagnosticsErrorMapping{
          .diagnostics_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .reason = "diagnostics remote export disablement stays inside contracts policy category",
      };
  }

  return DiagnosticsErrorMapping{
      .diagnostics_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .reason = "unknown diagnostics errors fall back to contracts runtime category",
  };
}

}  // namespace dasall::infra::diagnostics