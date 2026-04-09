#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "mapping/ResultMapper.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ResultCodeCategory;
using dasall::services::internal::AdapterReceipt;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::ResultMapper;

struct MappingCase {
  std::string provider_status_code;
  AdapterTransportOutcome transport_outcome;
  std::vector<std::string> side_effects;
  std::vector<std::string> evidence_refs;
  std::vector<std::string> compensation_hints;
  ResultCodeCategory expected_failure_type;
};

[[nodiscard]] AdapterReceipt make_receipt(std::string provider_status_code,
                                          AdapterTransportOutcome transport_outcome) {
  return AdapterReceipt{
      .receipt_ref = "receipt-040",
      .adapter_id = "adapter-040",
      .route_kind = AdapterRouteKind::local_service,
      .target_id = "target-040",
      .transport_outcome = transport_outcome,
      .provider_status_code = std::move(provider_status_code),
      .payload_json = "{\"status\":\"ok\"}",
      .latency_ms = 4U,
      .side_effects = {},
      .evidence_refs = {},
  };
}

void test_result_mapper_maps_all_service_error_classes_to_expected_failure_type() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const std::vector<MappingCase> cases = {
      {"invalid_request",
       AdapterTransportOutcome::rejected,
       {},
       {},
       {},
       ResultCodeCategory::Validation},
      {"capability_unsupported",
       AdapterTransportOutcome::rejected,
       {},
       {"capability://snapshot/040"},
       {},
       ResultCodeCategory::Validation},
      {"policy_denied",
       AdapterTransportOutcome::rejected,
       {},
       {"policy://decision/040"},
       {},
       ResultCodeCategory::Policy},
      {"route_unavailable",
       AdapterTransportOutcome::rejected,
       {},
       {},
       {},
       ResultCodeCategory::Runtime},
      {"adapter_unavailable",
       AdapterTransportOutcome::timeout,
       {},
       {},
       {},
       ResultCodeCategory::Provider},
      {"target_busy",
       AdapterTransportOutcome::rejected,
       {},
       {},
       {},
       ResultCodeCategory::Provider},
      {"partial_side_effect",
       AdapterTransportOutcome::partial,
       {"device.toggled"},
       {"audit://receipt/partial-040"},
       {"device.untoggle"},
       ResultCodeCategory::Provider},
      {"data_stale",
       AdapterTransportOutcome::rejected,
       {},
       {"cache://snapshot/040"},
       {},
       ResultCodeCategory::Runtime},
      {"subscription_overflow",
       AdapterTransportOutcome::rejected,
       {},
       {"subscription://cursor/040"},
       {},
       ResultCodeCategory::Runtime},
  };

  ResultMapper mapper;
  for (const auto& test_case : cases) {
    auto receipt = make_receipt(test_case.provider_status_code, test_case.transport_outcome);
    receipt.side_effects = test_case.side_effects;
    receipt.evidence_refs = test_case.evidence_refs;

    const auto mapped = mapper.map_result(receipt, test_case.compensation_hints);
    assert_true(mapped.error.has_value(),
                "each ServiceErrorClass should yield a structured ErrorInfo");
    assert_true(mapped.error->failure_type.has_value(),
                "mapped ErrorInfo should always populate failure_type");
    assert_equal(static_cast<int>(test_case.expected_failure_type),
                 static_cast<int>(*mapped.error->failure_type),
                 "ServiceErrorClass should map to the expected ErrorInfo.failure_type");
  }
}

void test_result_mapper_preserves_partial_side_effects_and_compensation_hints() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ResultMapper mapper;
  auto receipt = make_receipt("partial_side_effect", AdapterTransportOutcome::partial);
  receipt.side_effects = {"device.toggled"};
  receipt.evidence_refs = {"audit://receipt/partial-040"};

  const auto result = mapper.to_execution_command_result(
      receipt,
      {"device.untoggle"},
      "exec-040");

  assert_true(result.error.has_value(),
              "partial side effect should surface a structured error");
  assert_equal(1,
               static_cast<int>(result.side_effects.size()),
               "partial side effect should preserve side_effects");
  assert_equal(1,
               static_cast<int>(result.compensation_hints.size()),
               "partial side effect should preserve compensation_hints");
  assert_equal(std::string("audit://receipt/partial-040"),
               result.error->source_ref.ref_id,
               "partial side effect should prioritize evidence ref as source_ref");
}

void test_result_mapper_rejects_partial_side_effect_without_evidence() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ResultMapper mapper;
  auto receipt = make_receipt("partial_side_effect", AdapterTransportOutcome::partial);
  receipt.side_effects = {"device.toggled"};

  const auto result = mapper.to_execution_command_result(receipt, {}, "exec-040");

  assert_true(result.error.has_value(),
              "partial side effect without evidence should fail closed");
  assert_true(result.error->failure_type.has_value(),
              "invalid partial mapping should still surface a failure type");
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(*result.error->failure_type),
               "partial side effect without evidence should degrade to validation failure");
  assert_equal(0,
               static_cast<int>(result.side_effects.size()),
               "invalid partial mapping should not leak unverified side_effects");
  assert_equal(0,
               static_cast<int>(result.compensation_hints.size()),
               "invalid partial mapping should not emit compensation_hints");
}

void test_result_mapper_marks_subscription_overflow_as_resync_required() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ResultMapper mapper;
  auto receipt = make_receipt("subscription_overflow", AdapterTransportOutcome::rejected);
  receipt.evidence_refs = {"subscription://cursor/040"};

  const auto result = mapper.to_execution_subscription_result(
      receipt,
      std::nullopt,
      7U);

  assert_true(result.error.has_value(),
              "subscription overflow should surface structured error details");
  assert_true(result.resync_required,
              "subscription overflow should set resync_required");
  assert_equal(7,
               static_cast<int>(result.dropped_count),
               "subscription overflow should preserve dropped_count fact");
}

void test_result_mapper_preserves_from_cache_fact_for_data_stale() {
  using dasall::tests::support::assert_true;

  ResultMapper mapper;
  auto receipt = make_receipt("data_stale", AdapterTransportOutcome::rejected);
  receipt.evidence_refs = {"cache://snapshot/040"};

  const auto result = mapper.to_data_query_result(receipt, true);
  assert_true(result.error.has_value(),
              "data stale should surface structured error details");
  assert_true(result.from_cache,
              "data stale should preserve from_cache fact for query lanes");
}

void test_result_mapper_preserves_acknowledged_success_without_error() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ResultMapper mapper;
  auto receipt = make_receipt("ok", AdapterTransportOutcome::acknowledged);
  receipt.payload_json = "{\"status\":\"ack\"}";
  receipt.side_effects = {"device.toggled"};
  receipt.evidence_refs = {"audit://receipt/success-040"};

  const auto result = mapper.to_execution_command_result(
      receipt,
      {},
      "exec-040");

  assert_true(!result.error.has_value(),
              "acknowledged receipt should remain a success result");
  assert_equal(std::string("exec-040"),
               result.execution_id,
               "success result should preserve execution_id");
  assert_equal(1,
               static_cast<int>(result.side_effects.size()),
               "success result should preserve side_effect facts for command lanes");
  assert_equal(std::string("{\"status\":\"ack\"}"),
               result.payload_json,
               "success result should preserve payload_json");
}

}  // namespace

int main() {
  try {
    test_result_mapper_maps_all_service_error_classes_to_expected_failure_type();
    test_result_mapper_preserves_partial_side_effects_and_compensation_hints();
    test_result_mapper_rejects_partial_side_effect_without_evidence();
    test_result_mapper_marks_subscription_overflow_as_resync_required();
    test_result_mapper_preserves_from_cache_fact_for_data_stale();
    test_result_mapper_preserves_acknowledged_success_without_error();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}