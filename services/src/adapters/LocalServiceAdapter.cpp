#include "adapters/LocalServiceAdapter.h"

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

LocalServiceAdapter::LocalServiceAdapter(LocalServiceAdapterOptions options)
    : options_(std::move(options)) {}

std::string_view LocalServiceAdapter::adapter_id() const {
  return options_.adapter_id;
}

AdapterRouteKind LocalServiceAdapter::route_kind() const {
  return AdapterRouteKind::local_service;
}

AdapterInvocationResult LocalServiceAdapter::invoke(const AdapterInvocationRequest& request) const {
  if (!options_.service_endpoint_available) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "adapter_unavailable",
        .payload_json = make_error_payload("local_service_unavailable",
                                           "local service endpoint is currently unreachable"),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  }

  if (!options_.invoke_service) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "local_service_unbound",
        .payload_json = make_error_payload("local_service_unbound",
                                           "no local service invocation handler is configured"),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  }

  try {
    return options_.invoke_service(request);
  } catch (const std::exception& ex) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "adapter_unavailable",
        .payload_json = make_error_payload("local_service_exception", ex.what()),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  } catch (...) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "adapter_unavailable",
        .payload_json = make_error_payload("local_service_exception",
                                           "unknown local service invocation failure"),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  }
}

}  // namespace dasall::services::internal