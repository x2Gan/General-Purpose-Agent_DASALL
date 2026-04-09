#include <functional>
#include <exception>
#include <iostream>

#include "execution/ExecutionDiagnoseService.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ResultCodeCategory;
using dasall::services::ExecutionDiagnoseRequest;
using dasall::services::ServiceCallContext;
using dasall::services::internal::AdapterAvailabilityState;
using dasall::services::internal::AdapterBridge;
using dasall::services::internal::AdapterBridgeDependencies;
using dasall::services::internal::AdapterCandidateView;
using dasall::services::internal::AdapterInvocationRequest;
using dasall::services::internal::AdapterInvocationResult;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterRouteRequestKind;
using dasall::services::internal::AdapterRouter;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::AdapterTrustClass;
using dasall::services::internal::CapabilitySnapshotView;
using dasall::services::internal::ExecutionDiagnoseService;
using dasall::services::internal::ExecutionDiagnoseServiceDependencies;
using dasall::services::internal::FallbackEnvelope;
using dasall::services::internal::IAdapterInvoker;
using dasall::services::internal::ResultMapper;
using dasall::services::internal::ServicePolicyView;

class DiagnoseInvoker final : public IAdapterInvoker {
 public:
  explicit DiagnoseInvoker(
      std::function<AdapterInvocationResult(const AdapterInvocationRequest& request)> invoke)
      : invoke_(std::move(invoke)) {}

  [[nodiscard]] std::string_view adapter_id() const override { return "diagnose-primary"; }
  [[nodiscard]] AdapterRouteKind route_kind() const override {
    return AdapterRouteKind::local_service;
  }
  [[nodiscard]] AdapterInvocationResult invoke(
      const AdapterInvocationRequest& request) const override {
    return invoke_(request);
  }

 private:
  std::function<AdapterInvocationResult(const AdapterInvocationRequest& request)> invoke_;
};

[[nodiscard]] ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 3000;

  return ServiceCallContext{
      .request_id = "req-019",
      .session_id = "session-019",
      .trace_id = "trace-019",
      .tool_call_id = "tool-019",
      .goal_id = "goal-019",
      .budget_guard = budget,
      .deadline_ms = 9000,
  };
}

[[nodiscard]] ExecutionDiagnoseRequest make_request(bool include_last_error = false) {
  return ExecutionDiagnoseRequest{
      .context = make_context(),
      .target = {
          .capability_id = "cap.exec",
          .target_id = "target-019",
      },
      .include_last_error = include_last_error,
  };
}

[[nodiscard]] CapabilitySnapshotView make_snapshot() {
  return CapabilitySnapshotView{
      .capability_id = "cap.exec",
      .capability_version = "v1",
      .supported_actions = {},
      .supported_queries = {"diagnose"},
      .route_classes = {AdapterRouteKind::local_service},
      .preferred_locality = AdapterRouteKind::local_service,
  };
}

[[nodiscard]] ServicePolicyView make_policy_view() {
  return ServicePolicyView{
      .local_platform_route_enabled = false,
      .adapter_preference_order = {AdapterRouteKind::local_service},
  };
}

[[nodiscard]] FallbackEnvelope make_fallback_envelope() {
  return FallbackEnvelope{
      .requested_action_class = "query.diagnose",
      .ordered_candidates = {AdapterRouteKind::local_service},
      .route_equivalence_class = "query.diagnose",
      .allow_degrade = true,
      .deny_reason_on_exhaustion = "fallback_blocked",
  };
}

[[nodiscard]] AdapterCandidateView make_candidate() {
  return AdapterCandidateView{
      .adapter_id = "diagnose-primary",
      .route_kind = AdapterRouteKind::local_service,
      .route_equivalence_class = "query.diagnose",
      .trust_class = AdapterTrustClass::caller_verified,
      .availability_state = AdapterAvailabilityState::available,
      .supported_capabilities = {"cap.exec"},
  };
}

void test_execution_diagnose_service_returns_read_only_report() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  bool saw_diagnose_operation = false;
  bool saw_include_last_error = false;
  const DiagnoseInvoker invoker([&](const AdapterInvocationRequest& request) {
    saw_diagnose_operation = request.request_kind == AdapterRouteRequestKind::query &&
                             request.operation_name == "diagnose";
    saw_include_last_error = request.payload_json == "{\"include_last_error\":true}";
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = "{\"reachable\":true,\"last_error\":{\"code\":\"none\"}}",
        .latency_ms = 4U,
        .side_effects = {},
        .evidence_refs = {"diagnose://target-019/latest"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  const ExecutionDiagnoseService service(ExecutionDiagnoseServiceDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
  });

  const auto result = service.diagnose(make_context(), make_request(true));

  assert_true(saw_diagnose_operation, "diagnose should use the query-style diagnose operation");
  assert_true(saw_include_last_error, "include_last_error should be forwarded in diagnose payload");
  assert_true(!result.error.has_value(), "successful diagnose should not surface an error");
  assert_true(result.target_reachable, "successful diagnose should mark target_reachable=true");
  assert_equal(std::string("{\"reachable\":true,\"last_error\":{\"code\":\"none\"}}"),
               result.report_json,
               "diagnose should return the adapter report payload");
}

void test_execution_diagnose_service_rejects_invalid_request() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const DiagnoseInvoker invoker([](const AdapterInvocationRequest&) { return AdapterInvocationResult{}; });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  const ExecutionDiagnoseService service(ExecutionDiagnoseServiceDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
  });

  auto invalid_request = make_request();
  invalid_request.target.capability_id.clear();
  const auto result = service.diagnose(make_context(), invalid_request);

  assert_true(result.error.has_value(), "invalid diagnose request should surface an error");
  assert_true(result.error->failure_type.has_value(),
              "invalid diagnose request should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(*result.error->failure_type),
               "invalid diagnose request should map to validation failure type");
}

void test_execution_diagnose_service_surfaces_adapter_unavailable_errors() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const DiagnoseInvoker invoker([](const AdapterInvocationRequest&) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::timeout,
        .provider_status_code = "adapter_unavailable",
        .payload_json = "{\"error\":\"timeout\"}",
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  const ExecutionDiagnoseService service(ExecutionDiagnoseServiceDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
  });

  const auto result = service.diagnose(make_context(), make_request());

  assert_true(result.error.has_value(), "adapter timeout should surface structured error info");
  assert_true(!result.target_reachable, "adapter timeout should mark target_reachable=false");
  assert_true(result.error->failure_type.has_value(),
              "adapter timeout should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Provider),
               static_cast<int>(*result.error->failure_type),
               "adapter timeout should map to provider failure type");
}

void test_execution_diagnose_service_rejects_receipts_with_side_effects() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const DiagnoseInvoker invoker([](const AdapterInvocationRequest&) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = "{\"reachable\":true}",
        .latency_ms = 2U,
        .side_effects = {"mutated"},
        .evidence_refs = {"audit://unexpected-side-effect"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  const ExecutionDiagnoseService service(ExecutionDiagnoseServiceDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
  });

  const auto result = service.diagnose(make_context(), make_request());

  assert_true(result.error.has_value(),
              "diagnose receipts with side_effects should fail closed");
  assert_true(result.error->failure_type.has_value(),
              "side_effect violation should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(*result.error->failure_type),
               "diagnose side_effect violation should map to validation failure type");
}

}  // namespace

int main() {
  try {
    test_execution_diagnose_service_returns_read_only_report();
    test_execution_diagnose_service_rejects_invalid_request();
    test_execution_diagnose_service_surfaces_adapter_unavailable_errors();
    test_execution_diagnose_service_rejects_receipts_with_side_effects();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}