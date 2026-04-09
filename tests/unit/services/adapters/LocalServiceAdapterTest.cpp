#include <exception>
#include <iostream>
#include <stdexcept>

#include "adapters/LocalServiceAdapter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::services::internal::AdapterInvocationRequest;
using dasall::services::internal::AdapterInvocationResult;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterRouteRequestKind;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::LocalServiceAdapter;
using dasall::services::internal::LocalServiceAdapterOptions;

[[nodiscard]] AdapterInvocationRequest make_request() {
  return AdapterInvocationRequest{
      .request_id = "req-038",
      .capability_id = "cap.exec",
      .target_id = "target-038",
      .request_kind = AdapterRouteRequestKind::action,
      .operation_name = "toggle",
      .payload_json = "{\"state\":\"on\"}",
  };
}

void test_local_service_adapter_reports_fixed_identity() {
  using dasall::tests::support::assert_equal;

  const LocalServiceAdapter adapter(LocalServiceAdapterOptions{
      .service_endpoint_available = true,
      .adapter_id = "service-main",
      .invoke_service = {},
  });

  assert_equal(std::string("service-main"),
               std::string(adapter.adapter_id()),
               "adapter should expose the configured adapter_id");
  assert_equal(static_cast<int>(AdapterRouteKind::local_service),
               static_cast<int>(adapter.route_kind()),
               "adapter should always report local_service route kind");
}

void test_local_service_adapter_reports_unreachable_service_endpoint() {
  using dasall::tests::support::assert_equal;

  bool handler_called = false;
  const LocalServiceAdapter adapter(LocalServiceAdapterOptions{
      .service_endpoint_available = false,
      .adapter_id = "service-unavailable",
      .invoke_service = [&](const AdapterInvocationRequest&) {
        handler_called = true;
        return AdapterInvocationResult{};
      },
  });

  const auto result = adapter.invoke(make_request());

  assert_equal(0,
               handler_called ? 1 : 0,
               "service handler should not be called when endpoint is unavailable");
  assert_equal(static_cast<int>(AdapterTransportOutcome::unreachable),
               static_cast<int>(result.transport_outcome),
               "unavailable service endpoint should surface as unreachable");
  assert_equal(std::string("adapter_unavailable"),
               result.provider_status_code,
               "unavailable service endpoint should report adapter_unavailable");
}

void test_local_service_adapter_delegates_to_injected_service_handler() {
  using dasall::tests::support::assert_equal;

  const LocalServiceAdapter adapter(LocalServiceAdapterOptions{
      .service_endpoint_available = true,
      .adapter_id = "service-loopback",
      .invoke_service = [](const AdapterInvocationRequest& request) {
        return AdapterInvocationResult{
            .transport_outcome = AdapterTransportOutcome::acknowledged,
            .provider_status_code = "200",
            .payload_json = request.payload_json,
            .latency_ms = 9U,
            .side_effects = {"service.state_changed"},
            .evidence_refs = {"service://loopback/ack-1"},
        };
      },
  });

  const auto result = adapter.invoke(make_request());

  assert_equal(static_cast<int>(AdapterTransportOutcome::acknowledged),
               static_cast<int>(result.transport_outcome),
               "enabled local service adapter should preserve successful invocation results");
  assert_equal(std::string("{\"state\":\"on\"}"),
               result.payload_json,
               "loopback service handler should receive and return payload_json unchanged");
  assert_equal(9,
               static_cast<int>(result.latency_ms),
               "adapter should preserve latency_ms from the injected service handler");
}

void test_local_service_adapter_surfaces_service_exceptions_as_unreachable() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const LocalServiceAdapter adapter(LocalServiceAdapterOptions{
      .service_endpoint_available = true,
      .adapter_id = "service-exploding",
      .invoke_service = [](const AdapterInvocationRequest&) -> AdapterInvocationResult {
        throw std::runtime_error("service endpoint crashed");
      },
  });

  const auto result = adapter.invoke(make_request());

  assert_equal(static_cast<int>(AdapterTransportOutcome::unreachable),
               static_cast<int>(result.transport_outcome),
               "service exceptions should surface as unreachable results");
  assert_equal(std::string("adapter_unavailable"),
               result.provider_status_code,
               "service exceptions should keep adapter_unavailable semantics");
  assert_true(result.payload_json.find("service endpoint crashed") != std::string::npos,
              "adapter should preserve exception text in payload_json for diagnostics");
}

}  // namespace

int main() {
  try {
    test_local_service_adapter_reports_fixed_identity();
    test_local_service_adapter_reports_unreachable_service_endpoint();
    test_local_service_adapter_delegates_to_injected_service_handler();
    test_local_service_adapter_surfaces_service_exceptions_as_unreachable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}