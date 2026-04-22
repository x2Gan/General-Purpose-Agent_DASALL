#include <exception>
#include <iostream>
#include <vector>

#include "support/TestAssertions.h"
#include "telemetry/RuntimeEventBus.h"

namespace {

using dasall::runtime::RuntimeEventBus;
using dasall::runtime::RuntimeEventBusOptions;
using dasall::runtime::RuntimeEventCategory;
using dasall::runtime::RuntimeEventContext;
using dasall::runtime::RuntimeEventEnvelope;
using dasall::runtime::RuntimeEventSeverity;

[[nodiscard]] RuntimeEventEnvelope make_event(
    const std::string& name,
    const bool audit,
    const std::int64_t timestamp_ms,
    const std::string& request_id = "req-023") {
  return RuntimeEventEnvelope{
      .sequence = 0U,
      .category = audit ? RuntimeEventCategory::Audit : RuntimeEventCategory::Transition,
      .severity = RuntimeEventSeverity::Info,
      .event_name = name,
      .detail = name + " detail",
      .context = RuntimeEventContext{
          .request_id = request_id,
          .session_id = std::string("session-023"),
          .trace_id = std::string("trace-023"),
          .turn_id = std::string("turn-023"),
          .checkpoint_id = std::string("chk-023"),
      },
      .error_code = std::nullopt,
      .attributes = {},
      .audit = audit,
      .timestamp_ms = timestamp_ms,
  };
}

void test_event_bus_dispatches_subscribed_events_with_correlation_ids() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RuntimeEventBus bus(RuntimeEventBusOptions{
      .max_non_audit_queue_depth = 4U,
      .now_ms = []() { return 1700000000000LL; },
  });

  std::vector<RuntimeEventEnvelope> delivered;
  const auto subscription =
      bus.subscribe("runtime.transition", [&delivered](const RuntimeEventEnvelope& event) {
        delivered.push_back(event);
      });
  assert_true(subscription.is_valid(),
              "subscribe should return a valid RuntimeEventSubscription handle");

  const auto result = bus.publish(make_event("runtime.transition", false, 1700000000001LL));
  assert_true(result.accepted, "publish should accept a valid transition event");
  assert_equal(1,
               static_cast<int>(bus.dispatch_pending()),
               "dispatch_pending should deliver the queued transition event");
  assert_equal(1,
               static_cast<int>(delivered.size()),
               "subscriber should receive exactly one matching event");
  assert_equal(std::string("req-023"),
               delivered.front().context.request_id.value_or(std::string()),
               "event bus should preserve request_id correlation fields");
}

void test_event_bus_drops_oldest_non_audit_event_when_capacity_is_exceeded() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RuntimeEventBus bus(RuntimeEventBusOptions{
      .max_non_audit_queue_depth = 2U,
      .now_ms = []() { return 1700000000100LL; },
  });

  (void)bus.publish(make_event("runtime.transition.1", false, 1700000000101LL));
  (void)bus.publish(make_event("runtime.transition.2", false, 1700000000102LL));
  const auto overflow_result =
      bus.publish(make_event("runtime.transition.3", false, 1700000000103LL));

  assert_true(overflow_result.dropped_oldest,
              "overflow should drop the oldest non-audit event");
  assert_equal(1,
               static_cast<int>(bus.drop_count()),
               "overflow should increment event bus drop count");

  const auto pending = bus.pending_snapshot();
  assert_equal(2,
               static_cast<int>(pending.size()),
               "pending queue should retain the bounded non-audit depth");
  assert_equal(std::string("runtime.transition.2"),
               pending.front().event_name,
               "overflow should evict the oldest non-audit event first");
  assert_equal(std::string("runtime.transition.3"),
               pending.back().event_name,
               "overflow should keep the latest non-audit event");
}

void test_event_bus_preserves_audit_events_when_non_audit_queue_overflows() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RuntimeEventBus bus(RuntimeEventBusOptions{
      .max_non_audit_queue_depth = 1U,
      .now_ms = []() { return 1700000000200LL; },
  });

  (void)bus.publish(make_event("runtime.audit.safe_mode", true, 1700000000201LL));
  (void)bus.publish(make_event("runtime.transition.old", false, 1700000000202LL));
  const auto overflow_result =
      bus.publish(make_event("runtime.transition.new", false, 1700000000203LL));

  assert_true(overflow_result.accepted,
              "audit-preserving overflow should still accept the new non-audit event");
  const auto pending = bus.pending_snapshot();
  assert_equal(2,
               static_cast<int>(pending.size()),
               "audit event should be preserved alongside the newest non-audit event");
  assert_true(pending.front().audit,
              "audit event should remain queued when non-audit overflow occurs");
  assert_equal(std::string("runtime.transition.new"),
               pending.back().event_name,
               "newest non-audit event should replace the older non-audit event");
}

}  // namespace

int main() {
  try {
    test_event_bus_dispatches_subscribed_events_with_correlation_ids();
    test_event_bus_drops_oldest_non_audit_event_when_capacity_is_exceeded();
    test_event_bus_preserves_audit_events_when_non_audit_queue_overflows();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}