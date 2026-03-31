#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/ConfigTypes.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::config {

struct ConfigLayerDocument {
  ConfigLayerRef layer_ref;
  std::vector<TypedConfig> entries;

  [[nodiscard]] bool is_valid() const {
    if (!layer_ref.is_valid() || entries.empty()) {
      return false;
    }

    return std::all_of(entries.begin(), entries.end(), [this](const TypedConfig& entry) {
      return entry.is_valid() && entry.source_kind == layer_ref.source_kind &&
             entry.source_id == layer_ref.source_id &&
             entry.schema_version == layer_ref.schema_version;
    });
  }
};

struct ConfigLoadResult {
  bool loaded = false;
  ConfigLayerDocument document;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static ConfigLoadResult success(ConfigLayerDocument document) {
    return ConfigLoadResult{
        .loaded = true,
        .document = std::move(document),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static ConfigLoadResult failure(contracts::ResultCode result_code,
                                                std::string message,
                                                std::string stage,
                                                std::string source_ref) {
    return ConfigLoadResult{
        .loaded = false,
        .document = {},
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.config",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return loaded;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

class IConfigLoader {
 public:
  virtual ~IConfigLoader() = default;

  virtual ConfigLoadResult load_default() = 0;
  virtual ConfigLoadResult load_profile(std::string_view profile_id) = 0;
  virtual ConfigLoadResult load_deploy(std::string_view source_ref) = 0;
  virtual ConfigLoadResult load_runtime_overlay() = 0;
};

}  // namespace dasall::infra::config