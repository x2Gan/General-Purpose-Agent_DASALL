#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"
#include "event/EventPayloadGuards.h"

namespace {

using dasall::contracts::EventDomain;
using dasall::contracts::EventPayload;
using dasall::contracts::EventPayloadBoundaryDecision;
using dasall::contracts::EventStabilityTier;
using dasall::contracts::EventType;
using dasall::contracts::evaluate_event_payload_forbidden_alias;
using dasall::contracts::validate_event_payload_field_rules;
using dasall::contracts::validate_event_payload_required_fields;
using dasall::contracts::validate_event_type_field_rules;
using dasall::contracts::validate_event_type_required_fields;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Builds a valid EventType sample used by positive-path assertions.
EventType make_valid_event_type() {
  EventType event_type;
  event_type.type_key = "checkpoint.snapshot.created";
  event_type.domain = EventDomain::Checkpoint;
  event_type.major_version = 1U;
  event_type.schema_revision = 3U;
  event_type.stability_tier = EventStabilityTier::Stable;
  return event_type;
}

// Builds a valid EventPayload sample used by positive-path assertions.
EventPayload make_valid_event_payload() {
  EventPayload payload;
  payload.payload_type = "checkpoint_budget_snapshot";
  payload.payload_json = "{\"remaining\":100,\"spent\":20}";
  payload.schema_ref = "contracts/event/checkpoint_budget_snapshot/v1";
  payload.field_aliases = std::vector<std::string>{"remaining", "spent"};
  payload.producer_module = "runtime.checkpoint";
  payload.payload_version = 1U;
  return payload;
}

void test_valid_event_type_passes_required_and_field_guards() {
  const auto event_type = make_valid_event_type();

  const auto required_result = validate_event_type_required_fields(event_type);
  assert_true(required_result.ok,
              "valid EventType should pass required-field guard");

  const auto field_result = validate_event_type_field_rules(event_type);
  assert_true(field_result.ok,
              "valid EventType should pass field-rules guard");
}

void test_event_type_missing_type_key_fails_required_guard() {
  auto event_type = make_valid_event_type();
  event_type.type_key = std::nullopt;

  const auto result = validate_event_type_required_fields(event_type);
  assert_true(!result.ok, "missing type_key must fail required guard");
  assert_equal("type_key is required and must be non-empty",
               std::string(result.reason),
               "missing type_key should return canonical reason");
}

void test_valid_event_payload_passes_required_and_field_guards() {
  const auto payload = make_valid_event_payload();

  const auto required_result = validate_event_payload_required_fields(payload);
  assert_true(required_result.ok,
              "valid EventPayload should pass required-field guard");

  const auto field_result = validate_event_payload_field_rules(payload);
  assert_true(field_result.ok,
              "valid EventPayload should pass field-rules guard");
}

void test_event_payload_rejects_header_alias_leakage() {
  auto payload = make_valid_event_payload();
  payload.field_aliases = std::vector<std::string>{"remaining", "event_id"};

  const auto result = validate_event_payload_field_rules(payload);
  assert_true(!result.ok,
              "payload alias list containing event_id must be rejected");
  assert_equal(
      static_cast<int>(EventPayloadBoundaryDecision::RejectEnvelopeHeaderAlias),
      static_cast<int>(result.decision),
      "header alias leakage must keep normalized decision");
  assert_equal("event payload must not carry EventEnvelope header aliases",
               std::string(result.reason),
               "header alias leakage should return canonical reason");
}

void test_event_payload_rejects_duplicate_aliases() {
  auto payload = make_valid_event_payload();
  payload.field_aliases = std::vector<std::string>{"remaining", "remaining"};

  const auto result = validate_event_payload_field_rules(payload);
  assert_true(!result.ok, "duplicate payload aliases must be rejected");
  assert_equal("field_aliases must not contain duplicate items",
               std::string(result.reason),
               "duplicate aliases should return canonical reason");
}

void test_direct_alias_evaluation_keeps_stable_decision() {
  const auto result = evaluate_event_payload_forbidden_alias("header_keys");
  assert_true(!result.ok, "header_keys alias must be rejected");
  assert_equal(
      static_cast<int>(EventPayloadBoundaryDecision::RejectEnvelopeCarrierAlias),
      static_cast<int>(result.decision),
      "carrier alias rejection must keep normalized decision");
}

}  // namespace

int main() {
  try {
    test_valid_event_type_passes_required_and_field_guards();
    test_event_type_missing_type_key_fails_required_guard();
    test_valid_event_payload_passes_required_and_field_guards();
    test_event_payload_rejects_header_alias_leakage();
    test_event_payload_rejects_duplicate_aliases();
    test_direct_alias_evaluation_keeps_stable_decision();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
