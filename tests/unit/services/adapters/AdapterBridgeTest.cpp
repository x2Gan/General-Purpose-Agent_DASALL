#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "adapters/AdapterBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::services::internal::AdapterAvailabilityState;
using dasall::services::internal::AdapterBridge;
using dasall::services::internal::AdapterBridgeDependencies;
using dasall::services::internal::AdapterInvocationRequest;
using dasall::services::internal::AdapterInvocationResult;
using dasall::services::internal::AdapterReceipt;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterRouteRequestKind;
using dasall::services::internal::AdapterSelection;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::AdapterTrustClass;
using dasall::services::internal::IAdapterInvoker;

class FakeAdapterInvoker final : public IAdapterInvoker {
 public:
  FakeAdapterInvoker(std::string adapter_id,
                     AdapterRouteKind route_kind,
                     AdapterInvocationResult result)
      : adapter_id_(std::move(adapter_id)),
        route_kind_(route_kind),
        result_(std::move(result)) {}

  [[nodiscard]] std::string_view adapter_id() const override {
    return adapter_id_;
  }

  [[nodiscard]] AdapterRouteKind route_kind() const override {
    return route_kind_;
  }

  [[nodiscard]] AdapterInvocationResult invoke(const AdapterInvocationRequest&) const override {
    called_ = true;
    if (throw_message_.has_value()) {
      throw std::runtime_error(*throw_message_);
    }

    return result_;
  }

  void set_throw_message(std::string message) {
    throw_message_ = std::move(message);
  }

  [[nodiscard]] bool called() const {
    return called_;
  }

 private:
  std::string adapter_id_;
  AdapterRouteKind route_kind_;
  AdapterInvocationResult result_;
  mutable bool called_ = false;
  std::optional<std::string> throw_message_;
};

[[nodiscard]] AdapterSelection make_selection(std::string adapter_id,
                                              AdapterRouteKind route_kind,
                                              std::string target_id = "target-036") {
  return AdapterSelection{
      .route_kind = route_kind,
      .adapter_id = std::move(adapter_id),
      .target_id = std::move(target_id),
      .route_equivalence_class = "default-equivalence",
      .fallback_hop = 0U,
      .selected_reason = "preferred_route_selected",
      .trust_class = AdapterTrustClass::trusted_local,
      .availability_state = AdapterAvailabilityState::available,
  };
}

[[nodiscard]] AdapterInvocationRequest make_request() {
  return AdapterInvocationRequest{
      .request_id = "req-036",
      .capability_id = "cap.exec",
      .target_id = "target-036",
      .request_kind = AdapterRouteRequestKind::action,
      .operation_name = "safe_mode.enter",
      .payload_json = "{\"enabled\":true}",
  };
}

void test_adapter_bridge_returns_receipt_from_registered_invoker() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeAdapterInvoker invoker(
      "platform-primary",
      AdapterRouteKind::local_platform,
      AdapterInvocationResult{
          .transport_outcome = AdapterTransportOutcome::acknowledged,
          .provider_status_code = "200",
          .payload_json = "{\"status\":\"ok\"}",
          .latency_ms = 12U,
          .side_effects = {},
          .evidence_refs = {"provider://receipt/200"},
      });

  AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const auto receipt = bridge.invoke(
      make_selection("platform-primary", AdapterRouteKind::local_platform),
      make_request());

  assert_true(invoker.called(), "bridge should invoke the registered adapter exactly once");
  assert_equal(static_cast<int>(AdapterTransportOutcome::acknowledged),
               static_cast<int>(receipt.transport_outcome),
               "bridge should preserve the transport outcome returned by the invoker");
  assert_equal(std::string("200"),
               receipt.provider_status_code,
               "bridge should preserve provider status code");
  assert_equal(std::string("{\"status\":\"ok\"}"),
               receipt.payload_json,
               "bridge should preserve payload_json");
  assert_equal(12,
               static_cast<int>(receipt.latency_ms),
               "bridge should preserve latency_ms");
  assert_true(!receipt.receipt_ref.empty(), "bridge should synthesize a deterministic receipt_ref");
}

void test_adapter_bridge_reports_unregistered_adapter_as_unreachable() {
  using dasall::tests::support::assert_equal;

  AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {}});
  const auto receipt = bridge.invoke(
      make_selection("missing-adapter", AdapterRouteKind::local_service),
      make_request());

  assert_equal(static_cast<int>(AdapterTransportOutcome::unreachable),
               static_cast<int>(receipt.transport_outcome),
               "bridge should surface unregistered adapters as unreachable");
  assert_equal(std::string("adapter_not_registered"),
               receipt.provider_status_code,
               "bridge should expose adapter_not_registered in provider_status_code");
}

void test_adapter_bridge_rejects_route_kind_mismatch() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeAdapterInvoker invoker(
      "route-mismatch",
      AdapterRouteKind::remote_service,
      AdapterInvocationResult{});

  AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const auto receipt = bridge.invoke(
      make_selection("route-mismatch", AdapterRouteKind::local_service),
      make_request());

  assert_true(!invoker.called(), "bridge should not invoke adapters whose route_kind mismatches the selection");
  assert_equal(static_cast<int>(AdapterTransportOutcome::rejected),
               static_cast<int>(receipt.transport_outcome),
               "bridge should reject route kind mismatches");
  assert_equal(std::string("route_kind_mismatch"),
               receipt.provider_status_code,
               "bridge should preserve route_kind mismatch reason");
}

void test_adapter_bridge_preserves_partial_side_effect_facts() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeAdapterInvoker invoker(
      "remote-partial",
      AdapterRouteKind::remote_service,
      AdapterInvocationResult{
          .transport_outcome = AdapterTransportOutcome::partial,
          .provider_status_code = "202-partial",
          .payload_json = "{\"status\":\"partial\"}",
          .latency_ms = 87U,
          .side_effects = {"device.toggled"},
          .evidence_refs = {"audit://receipt/partial-1"},
      });

  AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const auto receipt = bridge.invoke(
      make_selection("remote-partial", AdapterRouteKind::remote_service),
      make_request());

  assert_equal(static_cast<int>(AdapterTransportOutcome::partial),
               static_cast<int>(receipt.transport_outcome),
               "bridge should preserve partial transport outcomes");
  assert_equal(1,
               static_cast<int>(receipt.side_effects.size()),
               "bridge should preserve returned side_effects facts");
  assert_equal(1,
               static_cast<int>(receipt.evidence_refs.size()),
               "bridge should preserve returned evidence_refs facts");
  assert_true(receipt.side_effects.front() == "device.toggled",
              "bridge should keep the original side effect description");
}

void test_adapter_bridge_surfaces_adapter_exception_without_swallowing_it() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeAdapterInvoker invoker(
      "exploding-adapter",
      AdapterRouteKind::local_service,
      AdapterInvocationResult{});
  invoker.set_throw_message("provider crashed");

  AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const auto receipt = bridge.invoke(
      make_selection("exploding-adapter", AdapterRouteKind::local_service),
      make_request());

  assert_true(invoker.called(), "bridge should attempt the invocation before surfacing bridge_exception");
  assert_equal(static_cast<int>(AdapterTransportOutcome::rejected),
               static_cast<int>(receipt.transport_outcome),
               "bridge should convert thrown adapter failures into rejected receipts");
  assert_equal(std::string("bridge_exception"),
               receipt.provider_status_code,
               "bridge should record bridge_exception for thrown adapter failures");
  assert_true(receipt.payload_json.find("provider crashed") != std::string::npos,
              "bridge should preserve the thrown message in payload_json for diagnostics");
}

}  // namespace

int main() {
  try {
    test_adapter_bridge_returns_receipt_from_registered_invoker();
    test_adapter_bridge_reports_unregistered_adapter_as_unreachable();
    test_adapter_bridge_rejects_route_kind_mismatch();
    test_adapter_bridge_preserves_partial_side_effect_facts();
    test_adapter_bridge_surfaces_adapter_exception_without_swallowing_it();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}