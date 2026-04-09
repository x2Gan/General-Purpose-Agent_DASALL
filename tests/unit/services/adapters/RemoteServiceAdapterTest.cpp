#include <exception>
#include <iostream>

#include "adapters/RemoteServiceAdapter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::services::internal::AdapterInvocationRequest;
using dasall::services::internal::AdapterInvocationResult;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterRouteRequestKind;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::RemoteServiceAdapter;
using dasall::services::internal::RemoteServiceAdapterOptions;

[[nodiscard]] AdapterInvocationRequest make_request() {
  return AdapterInvocationRequest{
      .request_id = "req-039",
      .capability_id = "cap.exec",
      .target_id = "target-039",
      .request_kind = AdapterRouteRequestKind::action,
      .operation_name = "toggle",
      .payload_json = "{\"state\":\"off\"}",
  };
}

void test_remote_service_adapter_reports_fixed_identity() {
  using dasall::tests::support::assert_equal;

  const RemoteServiceAdapter adapter(RemoteServiceAdapterOptions{
      .remote_endpoint_available = true,
      .timeout_on_invoke = false,
      .adapter_id = "remote-main",
      .invoke_remote = {},
  });

  assert_equal(std::string("remote-main"),
               std::string(adapter.adapter_id()),
               "adapter should expose the configured adapter_id");
  assert_equal(static_cast<int>(AdapterRouteKind::remote_service),
               static_cast<int>(adapter.route_kind()),
               "adapter should always report remote_service route kind");
}

void test_remote_service_adapter_returns_timeout_result() {
  using dasall::tests::support::assert_equal;

  const RemoteServiceAdapter adapter(RemoteServiceAdapterOptions{
      .remote_endpoint_available = true,
      .timeout_on_invoke = true,
      .adapter_id = "remote-timeout",
      .invoke_remote = {},
  });

  const auto result = adapter.invoke(make_request());

  assert_equal(static_cast<int>(AdapterTransportOutcome::timeout),
               static_cast<int>(result.transport_outcome),
               "timeout mode should surface a timeout transport outcome");
  assert_equal(std::string("adapter_unavailable"),
               result.provider_status_code,
               "timeout mode should keep adapter_unavailable semantics");
}

void test_remote_service_adapter_reports_unreachable_endpoint() {
  using dasall::tests::support::assert_equal;

  const RemoteServiceAdapter adapter(RemoteServiceAdapterOptions{
      .remote_endpoint_available = false,
      .timeout_on_invoke = false,
      .adapter_id = "remote-unreachable",
      .invoke_remote = {},
  });

  const auto result = adapter.invoke(make_request());

  assert_equal(static_cast<int>(AdapterTransportOutcome::unreachable),
               static_cast<int>(result.transport_outcome),
               "unreachable remote endpoint should surface as unreachable");
  assert_equal(std::string("adapter_unavailable"),
               result.provider_status_code,
               "unreachable endpoint should report adapter_unavailable");
}

void test_remote_service_adapter_delegates_to_injected_remote_handler() {
  using dasall::tests::support::assert_equal;

  const RemoteServiceAdapter adapter(RemoteServiceAdapterOptions{
      .remote_endpoint_available = true,
      .timeout_on_invoke = false,
      .adapter_id = "remote-loopback",
      .invoke_remote = [](const AdapterInvocationRequest& request) {
        return AdapterInvocationResult{
            .transport_outcome = AdapterTransportOutcome::acknowledged,
            .provider_status_code = "202",
            .payload_json = request.payload_json,
            .latency_ms = 14U,
            .side_effects = {},
            .evidence_refs = {"remote://loopback/ack-1"},
        };
      },
  });

  const auto result = adapter.invoke(make_request());

  assert_equal(static_cast<int>(AdapterTransportOutcome::acknowledged),
               static_cast<int>(result.transport_outcome),
               "remote adapter should preserve successful invocation results");
  assert_equal(std::string("{\"state\":\"off\"}"),
               result.payload_json,
               "remote loopback handler should receive and return payload_json unchanged");
  assert_equal(14,
               static_cast<int>(result.latency_ms),
               "adapter should preserve latency_ms from the injected remote handler");
}

}  // namespace

int main() {
  try {
    test_remote_service_adapter_reports_fixed_identity();
    test_remote_service_adapter_returns_timeout_result();
    test_remote_service_adapter_reports_unreachable_endpoint();
    test_remote_service_adapter_delegates_to_injected_remote_handler();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}