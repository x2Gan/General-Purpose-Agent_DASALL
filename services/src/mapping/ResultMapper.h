#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ServiceTypes.h"
#include "adapters/AdapterBridge.h"

namespace dasall::services::internal {

enum class ServiceErrorClass {
  none = 0,
  invalid_request,
  capability_unsupported,
  policy_denied,
  route_unavailable,
  adapter_unavailable,
  target_busy,
  partial_side_effect,
  data_stale,
  subscription_overflow,
};

struct ResultMapping {
  ServiceErrorClass error_class = ServiceErrorClass::none;
  bool success = false;
  contracts::ResultCode code = contracts::ResultCode::ToolExecutionFailed;
  std::optional<contracts::ErrorInfo> error;
  std::vector<std::string> side_effects;
  std::vector<std::string> compensation_hints;
  std::vector<std::string> evidence_refs;
  std::string payload_json;
  bool resync_required = false;
  std::uint32_t dropped_count = 0U;
};

class ResultMapper {
 public:
  [[nodiscard]] ResultMapping map_result(
      const AdapterReceipt& receipt,
      const std::vector<std::string>& compensation_hints) const;

  [[nodiscard]] ExecutionCommandResult to_execution_command_result(
      const AdapterReceipt& receipt,
      const std::vector<std::string>& compensation_hints,
      std::string execution_id = {},
      std::optional<contracts::ResultCode> success_code = std::nullopt) const;

  [[nodiscard]] ExecutionQueryResult to_execution_query_result(
      const AdapterReceipt& receipt,
      std::string state = {},
      bool from_cache = false,
      std::optional<contracts::ResultCode> success_code = std::nullopt) const;

  [[nodiscard]] ExecutionSubscriptionResult to_execution_subscription_result(
      const AdapterReceipt& receipt,
      std::optional<std::string> next_cursor = std::nullopt,
      std::uint32_t dropped_count = 0U,
      std::optional<contracts::ResultCode> success_code = std::nullopt) const;

  [[nodiscard]] ExecutionDiagnoseResult to_execution_diagnose_result(
      const AdapterReceipt& receipt,
      bool target_reachable_on_success = true,
      std::optional<contracts::ResultCode> success_code = std::nullopt) const;

  [[nodiscard]] DataQueryResult to_data_query_result(
      const AdapterReceipt& receipt,
      bool from_cache = false,
      std::optional<contracts::ResultCode> success_code = std::nullopt) const;

  [[nodiscard]] DataCatalogResult to_data_catalog_result(
      const AdapterReceipt& receipt,
      std::optional<contracts::ResultCode> success_code = std::nullopt) const;
};

[[nodiscard]] std::string_view service_error_class_name(ServiceErrorClass error_class);

}  // namespace dasall::services::internal