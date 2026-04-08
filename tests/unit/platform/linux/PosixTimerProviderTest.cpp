#include <exception>
#include <iostream>

#include "support/TestAssertions.h"
#include "linux/PosixTimerProvider.h"

namespace {

void test_posix_timer_provider_supports_periodic_start_and_cancel_with_drift_stats() {
  using dasall::platform::TimerMode;
  using dasall::platform::TimerSpec;
  using dasall::platform::linux::PosixTimerProvider;
  using dasall::tests::support::assert_true;

  PosixTimerProvider provider;
  TimerSpec periodic_spec;
  periodic_spec.mode = TimerMode::Periodic;
  periodic_spec.interval_ms = 10;

  const auto started = provider.start_periodic(periodic_spec, [](const auto&) {});
  assert_true(started.ok(), "start_periodic should succeed for valid periodic spec");

  const auto cancelled = provider.cancel(*started.value);
  assert_true(cancelled.ok(), "cancel should succeed for known timer handle");
  assert_true(cancelled.value->cancelled, "first cancel should report cancelled=true");
  assert_true(cancelled.value->drift_stats.has_consistent_values(),
              "cancel should report internally consistent drift stats");
  assert_true(cancelled.value->drift_stats.expiration_count == 1,
              "periodic skeleton should report one synthetic expiration on cancel");
}

void test_posix_timer_provider_reports_invalid_and_repeated_cancel_paths() {
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::TimerMode;
  using dasall::platform::TimerSpec;
  using dasall::platform::linux::PosixTimerProvider;
  using dasall::tests::support::assert_true;

  PosixTimerProvider provider;

  TimerSpec invalid_periodic_spec;
  invalid_periodic_spec.mode = TimerMode::Periodic;
  invalid_periodic_spec.interval_ms = 0;
  const auto invalid_started = provider.start_periodic(invalid_periodic_spec, [](const auto&) {});
  assert_true(!invalid_started.ok(),
              "start_periodic should fail when periodic interval is zero");
  assert_true(invalid_started.error->code == PlatformErrorCode::InvalidArgument,
              "invalid periodic spec should map to InvalidArgument");

  TimerSpec oneshot_spec;
  oneshot_spec.mode = TimerMode::OneShot;
  oneshot_spec.initial_delay_ms = 5;
  const auto one_started = provider.start_once(oneshot_spec, [](const auto&) {});
  assert_true(one_started.ok(), "start_once should succeed for valid one-shot spec");

  const auto first_cancel = provider.cancel(*one_started.value);
  const auto second_cancel = provider.cancel(*one_started.value);
  assert_true(first_cancel.ok(), "first cancel should succeed");
  assert_true(second_cancel.ok(), "second cancel should still return success payload");
  assert_true(!second_cancel.value->cancelled,
              "second cancel should report cancelled=false for idempotent behavior");
}

}  // namespace

int main() {
  try {
    test_posix_timer_provider_supports_periodic_start_and_cancel_with_drift_stats();
    test_posix_timer_provider_reports_invalid_and_repeated_cancel_paths();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}