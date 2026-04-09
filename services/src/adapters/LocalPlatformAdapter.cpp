#include "adapters/LocalPlatformAdapter.h"

#include <exception>
#include <utility>

namespace dasall::services::internal {

namespace {

[[nodiscard]] std::string make_error_payload(std::string_view error_code,
                                             std::string_view message) {
  return std::string("{\"error\":\"") + std::string(error_code) +
         "\",\"message\":\"" + std::string(message) + "\"}";
}

}  // namespace

LocalPlatformAdapter::LocalPlatformAdapter(LocalPlatformAdapterOptions options)
    : options_(std::move(options)) {}

std::string_view LocalPlatformAdapter::adapter_id() const {
  return options_.adapter_id;
}

AdapterRouteKind LocalPlatformAdapter::route_kind() const {
  return AdapterRouteKind::local_platform;
}

AdapterInvocationResult LocalPlatformAdapter::invoke(const AdapterInvocationRequest& request) const {
  if (!options_.platform_hal_enabled) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::rejected,
        .provider_status_code = "route_unavailable",
        .payload_json = make_error_payload("platform_hal_disabled",
                                           "local platform route is disabled by profile"),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  }

  if (!options_.invoke_platform) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "platform_hal_unbound",
        .payload_json = make_error_payload("platform_hal_unbound",
                                           "no platform HAL invocation handler is configured"),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  }

  try {
    return options_.invoke_platform(request);
  } catch (const std::exception& ex) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "platform_hal_exception",
        .payload_json = make_error_payload("platform_hal_exception", ex.what()),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  } catch (...) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "platform_hal_exception",
        .payload_json = make_error_payload("platform_hal_exception",
                                           "unknown platform HAL invocation failure"),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  }
}

}  // namespace dasall::services::internal