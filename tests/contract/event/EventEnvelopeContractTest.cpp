#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"
#include "event/EventEnvelopeGuards.h"

namespace {

dasall::contracts::EventEnvelope make_valid_event_envelope() {
  return dasall::contracts::EventEnvelope{
      .header = {
          .event_id = std::string("evt-001"),
          .event_type = std::string("checkpoint.snapshot.created"),
          .event_version = 1,
          .occurred_at_ms = 1730000000,
          .request_id = std::string("req-001"),
          .trace_id = std::string("trace-001"),
          .header_keys = {
              "event_id",
              "event_type",
              "event_version",
              "occurred_at_ms",
              "request_id",
              "trace_id",
          },
      },
      .payload_type = std::string("checkpoint_budget_snapshot"),
      .payload_json = std::string("{\"remaining\":100}"),
  };
}

void test_valid_event_envelope_passes_guard() {
  using dasall::contracts::validate_event_envelope;
  using dasall::tests::support::assert_true;

  // Positive case: header carries only common fields and private details stay
  // in payload, so envelope passes whitelist validation.
  const auto envelope = make_valid_event_envelope();
  const auto result = validate_event_envelope(envelope);

  assert_true(result.ok, "valid event envelope should pass validation");
}

void test_private_header_key_is_rejected() {
  using dasall::contracts::validate_event_envelope;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: module-private key promoted into header must be blocked.
  auto envelope = make_valid_event_envelope();
  envelope.header.header_keys.push_back("worker_internal_state");

  const auto result = validate_event_envelope(envelope);

  assert_true(!result.ok, "non-whitelisted header key should be rejected");
  assert_equal("header contains non-whitelisted key",
               std::string(result.reason),
               "guard should report private field promotion to header");
}

}  // namespace

int main() {
  try {
    test_valid_event_envelope_passes_guard();
    test_private_header_key_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
