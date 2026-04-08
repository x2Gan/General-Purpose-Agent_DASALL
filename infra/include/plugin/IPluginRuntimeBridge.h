#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "error/ResultCode.h"
#include "plugin/PluginDescriptor.h"

namespace dasall::infra::plugin {

struct PluginRuntimeLoadRequest {
  std::string plugin_id = std::string(kPluginUnknownValue);
  std::string binary_path;
  std::string entry_symbol = std::string("plugin_entry");
  std::string sandbox_hint;
  std::uint32_t timeout_ms = 3000;

  [[nodiscard]] bool is_valid() const {
    return plugin_id != kPluginUnknownValue && !binary_path.empty() &&
           !entry_symbol.empty() && timeout_ms > 0;
  }
};

struct PluginRuntimeLoadResult {
  bool loaded = false;
  std::string handle_ref;
  std::string evidence_ref;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::string reason_code;
  std::string message;

  [[nodiscard]] static PluginRuntimeLoadResult success(
      std::string handle_ref,
      std::string evidence_ref,
      std::string reason_code = {}) {
    return PluginRuntimeLoadResult{
        .loaded = true,
        .handle_ref = std::move(handle_ref),
        .evidence_ref = std::move(evidence_ref),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .reason_code = std::move(reason_code),
        .message = {},
    };
  }

  [[nodiscard]] static PluginRuntimeLoadResult failure(
      contracts::ResultCode result_code,
      std::string reason_code,
      std::string evidence_ref,
      std::string message) {
    return PluginRuntimeLoadResult{
        .loaded = false,
        .handle_ref = {},
        .evidence_ref = std::move(evidence_ref),
        .result_code = result_code,
        .reason_code = std::move(reason_code),
        .message = std::move(message),
    };
  }

  [[nodiscard]] bool is_valid() const {
    if (loaded) {
      return !handle_ref.empty() && !evidence_ref.empty();
    }

    return !evidence_ref.empty() && !reason_code.empty() && !message.empty() &&
           contracts::classify_result_code(result_code) !=
               contracts::ResultCodeCategory::Unknown;
  }
};

struct PluginRuntimeUnloadResult {
  bool unloaded = false;
  std::string evidence_ref;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::string reason_code;
  std::string message;

  [[nodiscard]] static PluginRuntimeUnloadResult success(
      std::string evidence_ref,
      std::string reason_code = {}) {
    return PluginRuntimeUnloadResult{
        .unloaded = true,
        .evidence_ref = std::move(evidence_ref),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .reason_code = std::move(reason_code),
        .message = {},
    };
  }

  [[nodiscard]] static PluginRuntimeUnloadResult failure(
      contracts::ResultCode result_code,
      std::string reason_code,
      std::string evidence_ref,
      std::string message) {
    return PluginRuntimeUnloadResult{
        .unloaded = false,
        .evidence_ref = std::move(evidence_ref),
        .result_code = result_code,
        .reason_code = std::move(reason_code),
        .message = std::move(message),
    };
  }

  [[nodiscard]] bool is_valid() const {
    if (unloaded) {
      return !evidence_ref.empty();
    }

    return !evidence_ref.empty() && !reason_code.empty() && !message.empty() &&
           contracts::classify_result_code(result_code) !=
               contracts::ResultCodeCategory::Unknown;
  }
};

class IPluginRuntimeBridge {
 public:
  virtual ~IPluginRuntimeBridge() = default;

  virtual PluginRuntimeLoadResult load(
      const PluginRuntimeLoadRequest& request) = 0;
  virtual PluginRuntimeUnloadResult unload(std::string_view plugin_id,
                                           std::string_view handle_ref) = 0;
};

}  // namespace dasall::infra::plugin