#include <exception>
#include <iostream>
#include <stdexcept>

#include "adapters/LocalPlatformAdapter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::services::internal::AdapterInvocationRequest;
using dasall::services::internal::AdapterInvocationResult;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterRouteRequestKind;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::LocalPlatformAdapter;
using dasall::services::internal::LocalPlatformAdapterOptions;

[[nodiscard]] AdapterInvocationRequest make_request() {
  return AdapterInvocationRequest{
      .request_id = "req-037",
      .capability_id = "cap.exec",
      .target_id = "target-037",
      .request_kind = AdapterRouteRequestKind::action,
      .operation_name = "safe_mode.enter",
      .payload_json = "{\"enabled\":true}",
  };
}

void test_local_platform_adapter_reports_fixed_identity() {
  using dasall::tests::support::assert_equal;

  const LocalPlatformAdapter adapter(LocalPlatformAdapterOptions{
      .platform_hal_enabled = true,
      .adapter_id = "platform-main",
      .invoke_platform = {},
  });

  assert_equal(std::string("platform-main"),
               std::string(adapter.adapter_id()),
               "adapter should expose the configured adapter_id");
  assert_equal(static_cast<int>(AdapterRouteKind::local_platform),
               static_cast<int>(adapter.route_kind()),
               "adapter should always report local_platform route kind");
}

void test_local_platform_adapter_returns_route_unavailable_when_profile_disables_hal() {
  using dasall::tests::support::assert_equal;

  bool handler_called = false;
  const LocalPlatformAdapter adapter(LocalPlatformAdapterOptions{
      .platform_hal_enabled = false,
      .adapter_id = "platform-disabled",
      .invoke_platform = [&](const AdapterInvocationRequest&) {
        handler_called = true;
        return AdapterInvocationResult{};
      },
  });

  const auto result = adapter.invoke(make_request());

  assert_equal(0,
               handler_called ? 1 : 0,
               "platform handler should not be called when platform_hal is disabled");
  assert_equal(static_cast<int>(AdapterTransportOutcome::rejected),
               static_cast<int>(result.transport_outcome),
               "disabled platform HAL should reject the route");
  assert_equal(std::string("route_unavailable"),
               result.provider_status_code,
               "disabled platform HAL should report route_unavailable");
}

void test_local_platform_adapter_delegates_to_injected_platform_handler() {
  using dasall::tests::support::assert_equal;

  const LocalPlatformAdapter adapter(LocalPlatformAdapterOptions{
      .platform_hal_enabled = true,
      .adapter_id = "platform-loopback",
      .invoke_platform = [](const AdapterInvocationRequest& request) {
        return AdapterInvocationResult{
            .transport_outcome = AdapterTransportOutcome::acknowledged,
            .provider_status_code = "200",
            .payload_json = request.payload_json,
            .latency_ms = 7U,
            .side_effects = {"platform.safe_mode_changed"},
            .evidence_refs = {"platform://loopback/ack-1"},
        };
      },
  });

  const auto result = adapter.invoke(make_request());

  assert_equal(static_cast<int>(AdapterTransportOutcome::acknowledged),
               static_cast<int>(result.transport_outcome),
               "enabled platform HAL should preserve successful invocation results");
  assert_equal(std::string("{\"enabled\":true}"),
               result.payload_json,
               "loopback handler should receive and return payload_json unchanged");
  assert_equal(7,
               static_cast<int>(result.latency_ms),
               "adapter should preserve latency_ms from the injected platform handler");
}

void test_local_platform_adapter_surfaces_platform_exceptions_as_unreachable() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const LocalPlatformAdapter adapter(LocalPlatformAdapterOptions{
      .platform_hal_enabled = true,
      .adapter_id = "platform-exploding",
      .invoke_platform = [](const AdapterInvocationRequest&) -> AdapterInvocationResult {
        throw std::runtime_error("platform hal crashed");
      },
  });

  const auto result = adapter.invoke(make_request());

  assert_equal(static_cast<int>(AdapterTransportOutcome::unreachable),
               static_cast<int>(result.transport_outcome),
               "adapter should convert platform HAL exceptions into unreachable results");
  assert_equal(std::string("platform_hal_exception"),
               result.provider_status_code,
               "adapter should record platform_hal_exception for thrown failures");
  assert_true(result.payload_json.find("platform hal crashed") != std::string::npos,
              "adapter should preserve exception text in payload_json for diagnostics");
}

}  // namespace

int main() {
  try {
    test_local_platform_adapter_reports_fixed_identity();
    test_local_platform_adapter_returns_route_unavailable_when_profile_disables_hal();
    test_local_platform_adapter_delegates_to_injected_platform_handler();
    test_local_platform_adapter_surfaces_platform_exceptions_as_unreachable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}