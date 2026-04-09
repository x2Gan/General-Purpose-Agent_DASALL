#include "adapters/RemoteServiceAdapter.h"

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

RemoteServiceAdapter::RemoteServiceAdapter(RemoteServiceAdapterOptions options)
    : options_(std::move(options)) {}

std::string_view RemoteServiceAdapter::adapter_id() const {
  return options_.adapter_id;
}

AdapterRouteKind RemoteServiceAdapter::route_kind() const {
  return AdapterRouteKind::remote_service;
}

AdapterInvocationResult RemoteServiceAdapter::invoke(const AdapterInvocationRequest& request) const {
  if (!options_.remote_endpoint_available) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "adapter_unavailable",
        .payload_json = make_error_payload("remote_service_unavailable",
                                           "remote service endpoint is currently unreachable"),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  }

  if (options_.timeout_on_invoke) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::timeout,
        .provider_status_code = "adapter_unavailable",
        .payload_json = make_error_payload("remote_service_timeout",
                                           "remote service invocation exceeded timeout budget"),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  }

  if (!options_.invoke_remote) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "remote_service_stub",
        .payload_json = make_error_payload("remote_service_stub",
                                           "no remote service invocation handler is configured"),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  }

  try {
    return options_.invoke_remote(request);
  } catch (const std::exception& ex) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "adapter_unavailable",
        .payload_json = make_error_payload("remote_service_exception", ex.what()),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  } catch (...) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::unreachable,
        .provider_status_code = "adapter_unavailable",
        .payload_json = make_error_payload("remote_service_exception",
                                           "unknown remote service invocation failure"),
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  }
}

}  // namespace dasall::services::internal