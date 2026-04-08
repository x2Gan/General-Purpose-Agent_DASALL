#include <exception>
#include <iostream>

#include "support/TestAssertions.h"
#include "linux/PosixThreadProvider.h"

namespace {

void test_posix_thread_provider_create_and_join_success_after_stop_request() {
  using dasall::platform::ThreadOptions;
  using dasall::platform::linux::PosixThreadProvider;
  using dasall::tests::support::assert_true;

  PosixThreadProvider provider;
  ThreadOptions options;

  const auto create_result = provider.create_thread(options, [] {});
  assert_true(create_result.ok(), "create_thread should succeed on baseline options");

  const auto stop_result = provider.request_stop(*create_result.value);
  assert_true(stop_result.ok(), "request_stop should succeed for known thread");
  assert_true(*stop_result.value, "request_stop should return true on success");

  const auto join_result = provider.join_thread(*create_result.value, 1);
  assert_true(join_result.ok(), "join_thread should succeed after stop is requested");
  assert_true(join_result.value->joined, "joined flag should be true on success");
}

void test_posix_thread_provider_reports_timeout_and_resource_exhausted_paths() {
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::ThreadOptions;
  using dasall::platform::linux::PosixThreadProvider;
  using dasall::tests::support::assert_true;

  PosixThreadProvider provider;
  ThreadOptions default_options;

  const auto created = provider.create_thread(default_options, [] {});
  assert_true(created.ok(), "create_thread should succeed for timeout path setup");

  const auto timeout_result = provider.join_thread(*created.value, 0);
  assert_true(!timeout_result.ok(), "join_thread should fail with timeout on zero timeout");
  assert_true(timeout_result.error->code == PlatformErrorCode::Timeout,
              "join timeout should map to PlatformErrorCode::Timeout");

  ThreadOptions huge_stack_options;
  huge_stack_options.stack_size_kb = 8192;
  const auto exhausted_result = provider.create_thread(huge_stack_options, [] {});
  assert_true(!exhausted_result.ok(),
              "create_thread should fail when stack exceeds skeleton resource limit");
  assert_true(exhausted_result.error->code == PlatformErrorCode::ResourceExhausted,
              "oversized stack should map to PlatformErrorCode::ResourceExhausted");
}

}  // namespace

int main() {
  try {
    test_posix_thread_provider_create_and_join_success_after_stop_request();
    test_posix_thread_provider_reports_timeout_and_resource_exhausted_paths();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}