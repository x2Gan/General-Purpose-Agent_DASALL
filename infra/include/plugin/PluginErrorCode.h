#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::plugin {

enum class PluginErrorCode {
  ValidateFail = 1,
  PolicyDenied = 2,
  SignatureFail = 3,
  CompatibilityFail = 4,
  LoadFail = 5,
  UnloadFail = 6,
};

struct PluginErrorMapping {
  PluginErrorCode plugin_code;
  contracts::ResultCode result_code;
  std::string_view reason;
};

inline constexpr std::string_view plugin_error_code_name(PluginErrorCode code) {
  switch (code) {
    case PluginErrorCode::ValidateFail:
      return "INF_E_PLUGIN_VALIDATE_FAIL";
    case PluginErrorCode::PolicyDenied:
      return "INF_E_PLUGIN_POLICY_DENIED";
    case PluginErrorCode::SignatureFail:
      return "INF_E_PLUGIN_SIGNATURE_FAIL";
    case PluginErrorCode::CompatibilityFail:
      return "INF_E_PLUGIN_COMPATIBILITY_FAIL";
    case PluginErrorCode::LoadFail:
      return "INF_E_PLUGIN_LOAD_FAIL";
    case PluginErrorCode::UnloadFail:
      return "INF_E_PLUGIN_UNLOAD_FAIL";
  }

  return "INF_E_PLUGIN_UNKNOWN";
}

inline constexpr PluginErrorMapping map_plugin_error_code(PluginErrorCode code) {
  switch (code) {
    case PluginErrorCode::ValidateFail:
      return PluginErrorMapping{
          .plugin_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "plugin validate aggregation failures stay inside contracts validation category",
      };
    case PluginErrorCode::PolicyDenied:
      return PluginErrorMapping{
          .plugin_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .reason = "plugin admission policy denials stay inside contracts policy category",
      };
    case PluginErrorCode::SignatureFail:
      return PluginErrorMapping{
          .plugin_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "plugin signature verification failures stay inside contracts validation category",
      };
    case PluginErrorCode::CompatibilityFail:
      return PluginErrorMapping{
          .plugin_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "plugin compatibility failures stay inside contracts validation category until ABI rules are frozen",
      };
    case PluginErrorCode::LoadFail:
      return PluginErrorMapping{
          .plugin_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "plugin load failures stay inside contracts runtime category",
      };
    case PluginErrorCode::UnloadFail:
      return PluginErrorMapping{
          .plugin_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "plugin unload failures stay inside contracts runtime category",
      };
  }

  return PluginErrorMapping{
      .plugin_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .reason = "unknown plugin errors fall back to contracts runtime category",
  };
}

}  // namespace dasall::infra::plugin