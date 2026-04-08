#include <exception>
#include <iostream>
#include <string>

#include "watchdog/HeartbeatSample.h"
#include "support/TestAssertions.h"

namespace {

void test_heartbeat_sample_accepts_complete_monotonic_shape() {
  using dasall::infra::watchdog::HeartbeatSample;
  using dasall::tests::support::assert_true;

  const HeartbeatSample sample{
      .entity_id = std::string("runtime.main_loop"),
      .heartbeat_ts = 1711958400000,
      .deadline_ts = 1711958415000,
      .seq_no = 7,
  };

  assert_true(sample.has_required_fields(),
              "HeartbeatSample should require entity_id, monotonic timestamps, and a positive seq_no");
}

void test_heartbeat_sample_detects_out_of_order_and_stale_samples() {
  using dasall::infra::watchdog::HeartbeatSample;
  using dasall::tests::support::assert_true;

  const HeartbeatSample latest{
      .entity_id = std::string("runtime.main_loop"),
      .heartbeat_ts = 1711958405000,
      .deadline_ts = 1711958420000,
      .seq_no = 8,
  };
  const HeartbeatSample out_of_order{
      .entity_id = std::string("runtime.main_loop"),
      .heartbeat_ts = 1711958404000,
      .deadline_ts = 1711958419000,
      .seq_no = 7,
  };
  const HeartbeatSample duplicate{
      .entity_id = std::string("runtime.main_loop"),
      .heartbeat_ts = 1711958405000,
      .deadline_ts = 1711958420000,
      .seq_no = 8,
  };
  const HeartbeatSample invalid{
      .entity_id = std::string("runtime.main_loop"),
      .heartbeat_ts = 1711958405000,
      .deadline_ts = 1711958404000,
      .seq_no = 0,
  };

  assert_true(out_of_order.is_older_than(latest),
              "HeartbeatSample should treat lower seq_no samples as older even before ingestor logic exists");
  assert_true(out_of_order.is_stale_against(latest),
              "HeartbeatSample should flag out-of-order samples as stale against the latest accepted sample");
  assert_true(duplicate.is_stale_against(latest),
              "HeartbeatSample should reject exact duplicates as stale to support idempotent ingest guards");
  assert_true(!invalid.has_required_fields(),
              "HeartbeatSample should reject invalid deadline and zero seq_no placeholders");
}

}  // namespace

int main() {
  try {
    test_heartbeat_sample_accepts_complete_monotonic_shape();
    test_heartbeat_sample_detects_out_of_order_and_stale_samples();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}