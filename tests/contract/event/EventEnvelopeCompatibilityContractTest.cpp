#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <string>

#include "support/TestAssertions.h"
#include "event/EventEnvelope.h"
#include "event/EventEnvelopeGuards.h"

namespace {

using WireMap = std::map<std::string, std::string>;

using dasall::contracts::EventEnvelope;
using dasall::contracts::validate_event_envelope;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Reads a string value from the wire map and keeps missing/present semantics,
// which is needed by optional-field based guard validation.
std::optional<std::string> find_wire_value(const WireMap& wire,
                                           const std::string& key) {
  const auto it = wire.find(key);
  if (it == wire.end()) {
    return std::nullopt;
  }
  return it->second;
}

// Parses uint32 values from wire text fields. Parse errors are treated as
// missing values so guard checks can return stable failure reasons.
std::optional<std::uint32_t> parse_wire_uint32(const WireMap& wire,
                                               const std::string& key) {
  const auto text = find_wire_value(wire, key);
  if (!text.has_value()) {
    return std::nullopt;
  }

  try {
    const auto parsed = std::stoul(*text);
    return static_cast<std::uint32_t>(parsed);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// Parses int64 values from wire text fields. Parse errors are mapped to missing
// values to keep behavior deterministic for compatibility tests.
std::optional<std::int64_t> parse_wire_int64(const WireMap& wire,
                                             const std::string& key) {
  const auto text = find_wire_value(wire, key);
  if (!text.has_value()) {
    return std::nullopt;
  }

  try {
    return std::stoll(*text);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// Builds a stable baseline EventEnvelope sample for round-trip and compatibility
// scenarios in WP05-T015.
EventEnvelope make_valid_event_envelope_sample() {
  EventEnvelope envelope;
  envelope.header.event_id = "evt-env-compat-001";
  envelope.header.event_type = "agent.result.generated";
  envelope.header.event_version = 1U;
  envelope.header.occurred_at_ms = 1710000000200;
  envelope.header.request_id = "req-env-compat-001";
  envelope.header.trace_id = "trace-env-compat-001";
  envelope.header.header_keys = {
      "event_id",
      "event_type",
      "event_version",
      "occurred_at_ms",
      "request_id",
      "trace_id",
  };
  envelope.payload_type = "AgentResult";
  envelope.payload_json = "{\"status\":\"ok\",\"score\":0.91}";
  return envelope;
}

// Serializes EventEnvelope into flat key-value transport representation.
// The serializer intentionally exports only frozen core header keys.
WireMap serialize_event_envelope_v1(const EventEnvelope& envelope) {
  WireMap wire;

  if (envelope.header.event_id.has_value()) {
    wire["header.event_id"] = *envelope.header.event_id;
  }
  if (envelope.header.event_type.has_value()) {
    wire["header.event_type"] = *envelope.header.event_type;
  }
  if (envelope.header.event_version.has_value()) {
    wire["header.event_version"] = std::to_string(*envelope.header.event_version);
  }
  if (envelope.header.occurred_at_ms.has_value()) {
    wire["header.occurred_at_ms"] = std::to_string(*envelope.header.occurred_at_ms);
  }
  if (envelope.header.request_id.has_value()) {
    wire["header.request_id"] = *envelope.header.request_id;
  }
  if (envelope.header.trace_id.has_value()) {
    wire["header.trace_id"] = *envelope.header.trace_id;
  }
  if (envelope.payload_type.has_value()) {
    wire["payload_type"] = *envelope.payload_type;
  }
  if (envelope.payload_json.has_value()) {
    wire["payload_json"] = *envelope.payload_json;
  }

  return wire;
}

// Deserializes EventEnvelope from flat wire map and reconstructs header_keys
// from header.* entries so whitelist guard can evaluate compatibility behavior.
EventEnvelope deserialize_event_envelope_wire(const WireMap& wire) {
  EventEnvelope envelope;

  envelope.header.event_id = find_wire_value(wire, "header.event_id");
  envelope.header.event_type = find_wire_value(wire, "header.event_type");
  envelope.header.event_version = parse_wire_uint32(wire, "header.event_version");
  envelope.header.occurred_at_ms = parse_wire_int64(wire, "header.occurred_at_ms");
  envelope.header.request_id = find_wire_value(wire, "header.request_id");
  envelope.header.trace_id = find_wire_value(wire, "header.trace_id");
  envelope.payload_type = find_wire_value(wire, "payload_type");
  envelope.payload_json = find_wire_value(wire, "payload_json");

  for (const auto& pair : wire) {
    const std::string& key = pair.first;
    const std::string kPrefix = "header.";
    if (key.rfind(kPrefix, 0) == 0) {
      envelope.header.header_keys.push_back(key.substr(kPrefix.size()));
    }
  }

  return envelope;
}

// Positive coverage: core EventEnvelope fields should stay valid after
// serialize->deserialize round-trip.
void test_event_envelope_round_trip_keeps_core_header_stable() {
  const auto source = make_valid_event_envelope_sample();
  const auto wire = serialize_event_envelope_v1(source);
  const auto restored = deserialize_event_envelope_wire(wire);

  const auto guard = validate_event_envelope(restored);
  assert_true(guard.ok,
              "round-trip EventEnvelope should remain guard-valid");
  assert_equal(*source.header.event_id,
               restored.header.event_id.value_or(""),
               "event_id should remain stable after round-trip");
  assert_equal(*source.header.event_type,
               restored.header.event_type.value_or(""),
               "event_type should remain stable after round-trip");
}

// Positive compatibility coverage: unknown future transport metadata outside
// header namespace should be ignored by legacy parser.
void test_unknown_non_header_wire_fields_are_ignored() {
  auto wire = serialize_event_envelope_v1(make_valid_event_envelope_sample());
  wire["future_transport_hop"] = "edge-gateway-3";
  wire["future_retry_hint"] = "soft";

  const auto restored = deserialize_event_envelope_wire(wire);
  const auto guard = validate_event_envelope(restored);
  assert_true(guard.ok,
              "unknown non-header fields should not break compatibility");
}

// Positive compatibility coverage: payload_json extensions should be allowed,
// because payload evolution is carried inside payload body rather than header.
void test_payload_json_extension_is_allowed() {
  auto wire = serialize_event_envelope_v1(make_valid_event_envelope_sample());
  wire["payload_json"] =
      "{\"status\":\"ok\",\"score\":0.91,\"future_explanation\":\"ranked_by_policy_v2\"}";

  const auto restored = deserialize_event_envelope_wire(wire);
  const auto guard = validate_event_envelope(restored);
  assert_true(guard.ok,
              "payload_json extension should remain compatible");
}

// Negative coverage: removing required core header fields must fail envelope
// validation and block incompatible producers.
void test_missing_required_event_type_is_rejected() {
  auto wire = serialize_event_envelope_v1(make_valid_event_envelope_sample());
  wire.erase("header.event_type");

  const auto restored = deserialize_event_envelope_wire(wire);
  const auto guard = validate_event_envelope(restored);
  assert_true(!guard.ok,
              "missing header.event_type should be rejected");
}

// Negative coverage: event_version must remain a positive number.
void test_event_version_zero_is_rejected() {
  auto wire = serialize_event_envelope_v1(make_valid_event_envelope_sample());
  wire["header.event_version"] = "0";

  const auto restored = deserialize_event_envelope_wire(wire);
  const auto guard = validate_event_envelope(restored);
  assert_true(!guard.ok,
              "event_version zero should be rejected");
}

// Negative coverage: placing private extension metadata under header namespace
// violates whitelist policy and must be blocked.
void test_non_whitelisted_header_key_is_rejected() {
  auto wire = serialize_event_envelope_v1(make_valid_event_envelope_sample());
  wire["header.internal_policy_trace"] = "deny:agent_scope";

  const auto restored = deserialize_event_envelope_wire(wire);
  const auto guard = validate_event_envelope(restored);
  assert_true(!guard.ok,
              "non-whitelisted header key should be rejected");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  // Shared runner keeps contract-test output format consistent across WP05.
  auto run_test = [&](const char* name, void (*fn)()) {
    try {
      fn();
      ++passed;
      std::cout << "  PASS: " << name << "\n";
    } catch (const std::exception& ex) {
      ++failed;
      std::cerr << "  FAIL: " << name << " - " << ex.what() << "\n";
    }
  };

  // Banner text helps map ctest logs back to WP05-T015-B.
  std::cout << "EventEnvelopeCompatibilityContractTest - WP05-T015-B\n";

  run_test("test_event_envelope_round_trip_keeps_core_header_stable",
           test_event_envelope_round_trip_keeps_core_header_stable);
  run_test("test_unknown_non_header_wire_fields_are_ignored",
           test_unknown_non_header_wire_fields_are_ignored);
  run_test("test_payload_json_extension_is_allowed",
           test_payload_json_extension_is_allowed);
  run_test("test_missing_required_event_type_is_rejected",
           test_missing_required_event_type_is_rejected);
  run_test("test_event_version_zero_is_rejected",
           test_event_version_zero_is_rejected);
  run_test("test_non_whitelisted_header_key_is_rejected",
           test_non_whitelisted_header_key_is_rejected);

  // Summary follows repository convention for quick scan in CI logs.
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}
