#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <string>

#include "agent/AgentRequest.h"
#include "agent/AgentRequestGuards.h"
#include "boundary/CompatibilityGuards.h"
#include "dasall/tests/support/TestAssertions.h"
#include "event/EventEnvelope.h"
#include "event/EventEnvelopeGuards.h"

namespace {

using WireMap = std::map<std::string, std::string>;

using dasall::contracts::AgentRequest;
using dasall::contracts::EventEnvelope;
using dasall::contracts::RequestChannel;
using dasall::contracts::TimeoutFieldSet;
using dasall::contracts::fallback_unknown_enum_value;
using dasall::contracts::normalize_timeout_fields;
using dasall::contracts::validate_agent_request_field_rules;
using dasall::contracts::validate_event_envelope;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Helper to read a string field from wire map while preserving the
// "missing field" and "empty field" distinction used by guard validation.
std::optional<std::string> find_wire_value(const WireMap& wire,
                                           const std::string& key) {
  const auto it = wire.find(key);
  if (it == wire.end()) {
    return std::nullopt;
  }
  return it->second;
}

// Parses a signed 64-bit integer from wire text format. Invalid numeric payloads
// are treated as absent so guards can produce stable validation failures.
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

// Parses an unsigned 32-bit integer from wire text format with explicit overflow
// protection. Overflow or malformed text is treated as absent.
std::optional<std::uint32_t> parse_wire_uint32(const WireMap& wire,
                                               const std::string& key) {
  const auto text = find_wire_value(wire, key);
  if (!text.has_value()) {
    return std::nullopt;
  }

  try {
    const auto parsed = std::stoull(*text);
    if (parsed > static_cast<unsigned long long>(std::numeric_limits<std::uint32_t>::max())) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// Builds a minimal valid AgentRequest sample used by multiple compatibility
// scenarios to keep expectations stable across round-trip assertions.
AgentRequest make_valid_agent_request_sample() {
  AgentRequest request;
  request.request_id = "req-compat-001";
  request.session_id = "sess-compat-001";
  request.trace_id = "trace-compat-001";
  request.user_input = "请生成今日巡检摘要";
  request.request_channel = RequestChannel::Gateway;
  request.created_at = 1710000000000;
  request.timeout_ms = 5000U;
  return request;
}

// Serializes the AgentRequest subset required by WP05-T013 matrix. The helper
// intentionally emits only stable compatibility fields, leaving room for
// future additive fields without changing baseline assertions.
WireMap serialize_agent_request_v1(const AgentRequest& request) {
  WireMap wire;

  if (request.request_id.has_value()) {
    wire["request_id"] = *request.request_id;
  }
  if (request.session_id.has_value()) {
    wire["session_id"] = *request.session_id;
  }
  if (request.trace_id.has_value()) {
    wire["trace_id"] = *request.trace_id;
  }
  if (request.user_input.has_value()) {
    wire["user_input"] = *request.user_input;
  }
  if (request.request_channel.has_value()) {
    wire["request_channel"] = std::to_string(static_cast<int>(*request.request_channel));
  }
  if (request.created_at.has_value()) {
    wire["created_at"] = std::to_string(*request.created_at);
  }
  if (request.timeout_ms.has_value()) {
    wire["timeout_ms"] = std::to_string(*request.timeout_ms);
  }
  if (request.deadline_at.has_value()) {
    wire["deadline_at"] = std::to_string(*request.deadline_at);
  }

  return wire;
}

// Deserializes AgentRequest from a generic wire map and applies compatibility
// migration for legacy timeout_seconds according to CompatibilityGuards.
AgentRequest deserialize_agent_request_wire(const WireMap& wire) {
  AgentRequest request;

  request.request_id = find_wire_value(wire, "request_id");
  request.session_id = find_wire_value(wire, "session_id");
  request.trace_id = find_wire_value(wire, "trace_id");
  request.user_input = find_wire_value(wire, "user_input");
  request.created_at = parse_wire_int64(wire, "created_at");

  // Unknown request_channel values are downgraded to Unspecified so required
  // field guards can reject them in a deterministic way.
  constexpr std::array<int, 5> kKnownChannels{
      static_cast<int>(RequestChannel::Unspecified),
      static_cast<int>(RequestChannel::Cli),
      static_cast<int>(RequestChannel::Gateway),
      static_cast<int>(RequestChannel::Daemon),
      static_cast<int>(RequestChannel::Simulator),
  };
  const auto raw_channel = parse_wire_int64(wire, "request_channel");
  if (raw_channel.has_value()) {
    request.request_channel = fallback_unknown_enum_value<RequestChannel>(
        static_cast<int>(*raw_channel),
        kKnownChannels.data(),
        kKnownChannels.size(),
        RequestChannel::Unspecified);
  }

  // timeout_ms is canonical; timeout_seconds is read-only legacy input.
  const TimeoutFieldSet timeout_fields{
      .created_at_ms = request.created_at,
      .deadline_at_ms = parse_wire_int64(wire, "deadline_at"),
      .timeout_ms = parse_wire_uint32(wire, "timeout_ms"),
      .timeout_seconds = parse_wire_uint32(wire, "timeout_seconds"),
  };

  const auto normalized = normalize_timeout_fields(timeout_fields);
  if (normalized.ok) {
    request.timeout_ms = normalized.normalized_timeout_ms;
    request.deadline_at = normalized.normalized_deadline_at_ms;
  } else {
    // Keep raw parsed values when normalization fails, so field/boundary guards
    // can expose failure reason in the standard validation path.
    request.timeout_ms = timeout_fields.timeout_ms;
    request.deadline_at = timeout_fields.deadline_at_ms;
  }

  return request;
}

// Builds a valid EventEnvelope sample used by both positive and negative
// serialization scenarios.
EventEnvelope make_valid_event_envelope_sample() {
  EventEnvelope envelope;
  envelope.header.event_id = "evt-compat-001";
  envelope.header.event_type = "agent.result";
  envelope.header.event_version = 1U;
  envelope.header.occurred_at_ms = 1710000000100;
  envelope.header.request_id = "req-compat-001";
  envelope.header.trace_id = "trace-compat-001";
  envelope.header.header_keys = {
      "event_id",
      "event_type",
      "event_version",
      "occurred_at_ms",
      "request_id",
      "trace_id",
  };
  envelope.payload_type = "AgentResult";
  envelope.payload_json = "{\"status\":\"ok\"}";
  return envelope;
}

// Serializes EventEnvelope into a flat map while preserving the explicit header
// namespace so whitelist checks remain deterministic after round-trip parsing.
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
// from observed header.* entries, enabling whitelist validation after parsing.
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

// Positive coverage: V1 AgentRequest fields remain stable after a full
// serialize->deserialize round-trip.
void test_agent_request_round_trip_keeps_required_and_timeout_fields() {
  const auto source = make_valid_agent_request_sample();
  const auto wire = serialize_agent_request_v1(source);
  const auto restored = deserialize_agent_request_wire(wire);

  const auto guard = validate_agent_request_field_rules(restored);
  assert_true(guard.ok, "round-trip AgentRequest should remain guard-valid");
  assert_equal(*source.request_id,
               restored.request_id.value_or(""),
               "request_id should remain stable after round-trip");
  assert_equal(static_cast<int>(source.timeout_ms.value_or(0U)),
               static_cast<int>(restored.timeout_ms.value_or(0U)),
               "timeout_ms should remain stable after round-trip");
}

// Positive coverage: unknown additive fields from a newer producer should not
// break old parser behavior for already-known required fields.
void test_agent_request_forward_compatibility_ignores_unknown_fields() {
  auto wire = serialize_agent_request_v1(make_valid_agent_request_sample());
  wire["future_priority_policy"] = "adaptive";
  wire["future_debug_hint"] = "opaque";

  const auto restored = deserialize_agent_request_wire(wire);
  const auto guard = validate_agent_request_field_rules(restored);
  assert_true(guard.ok,
              "unknown additive fields should be ignored for compatibility");
}

// Positive coverage: legacy timeout_seconds payload must migrate to canonical
// timeout_ms/deadline_at representation.
void test_agent_request_legacy_timeout_seconds_is_migrated() {
  auto wire = serialize_agent_request_v1(make_valid_agent_request_sample());
  wire.erase("timeout_ms");
  wire.erase("deadline_at");
  wire["timeout_seconds"] = "5";

  const auto restored = deserialize_agent_request_wire(wire);
  const auto guard = validate_agent_request_field_rules(restored);

  assert_true(guard.ok,
              "legacy timeout_seconds should migrate into a valid request");
  assert_equal(5000,
               static_cast<int>(restored.timeout_ms.value_or(0U)),
               "timeout_seconds should migrate to timeout_ms");
  assert_equal(std::string("1710000005000"),
               std::to_string(restored.deadline_at.value_or(0)),
               "deadline_at should be derived from created_at plus timeout_ms");
}

// Negative coverage: required field loss during deserialization must be
// detected by guard validation.
void test_agent_request_missing_required_field_is_rejected() {
  auto wire = serialize_agent_request_v1(make_valid_agent_request_sample());
  wire.erase("request_id");

  const auto restored = deserialize_agent_request_wire(wire);
  const auto guard = validate_agent_request_field_rules(restored);
  assert_true(!guard.ok,
              "missing request_id should fail AgentRequest validation");
}

// Positive coverage: EventEnvelope should preserve whitelist-compliant header
// fields through round-trip serialization.
void test_event_envelope_round_trip_keeps_whitelisted_header() {
  const auto source = make_valid_event_envelope_sample();
  const auto wire = serialize_event_envelope_v1(source);
  const auto restored = deserialize_event_envelope_wire(wire);

  const auto guard = validate_event_envelope(restored);
  assert_true(guard.ok,
              "whitelisted EventEnvelope header should pass validation");
  assert_equal(*source.header.event_id,
               restored.header.event_id.value_or(""),
               "event_id should remain stable after round-trip");
}

// Negative coverage: header private fields must not be accepted as serialized
// common header keys.
void test_event_envelope_non_whitelisted_header_key_is_rejected() {
  auto wire = serialize_event_envelope_v1(make_valid_event_envelope_sample());
  wire["header.internal_debug"] = "true";

  const auto restored = deserialize_event_envelope_wire(wire);
  const auto guard = validate_event_envelope(restored);
  assert_true(!guard.ok,
              "non-whitelisted header key should be rejected");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  // Shared runner keeps output format consistent with existing contract tests.
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

  // Banner text ties raw ctest output back to WP05-T013.
  std::cout << "SerializationCompatibilityContractTest - WP05-T013-B\n";

  run_test("test_agent_request_round_trip_keeps_required_and_timeout_fields",
           test_agent_request_round_trip_keeps_required_and_timeout_fields);
  run_test("test_agent_request_forward_compatibility_ignores_unknown_fields",
           test_agent_request_forward_compatibility_ignores_unknown_fields);
  run_test("test_agent_request_legacy_timeout_seconds_is_migrated",
           test_agent_request_legacy_timeout_seconds_is_migrated);
  run_test("test_agent_request_missing_required_field_is_rejected",
           test_agent_request_missing_required_field_is_rejected);
  run_test("test_event_envelope_round_trip_keeps_whitelisted_header",
           test_event_envelope_round_trip_keeps_whitelisted_header);
  run_test("test_event_envelope_non_whitelisted_header_key_is_rejected",
           test_event_envelope_non_whitelisted_header_key_is_rejected);

  // Summary output follows repository contract-test convention.
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}
