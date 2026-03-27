#include <exception>
#include <iostream>
#include <type_traits>

#include "IThread.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_thread_options_default_values_match_linux_thread_baseline() {
  using dasall::platform::ThreadDetachPolicy;
  using dasall::platform::ThreadOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const ThreadOptions options;

  assert_equal("platform-worker", options.name,
               "thread options should expose stable default thread name");
  assert_true(options.stack_size_kb == 512U,
              "thread options should keep default stack size from linux baseline");
  assert_true(options.detach_policy == ThreadDetachPolicy::Joinable,
              "thread options should default to joinable detach policy");
  assert_true(!options.affinity_hint.has_value(),
              "thread options should keep affinity hint optional by default");
  assert_true(options.has_consistent_values(),
              "default thread options should remain internally consistent");
}

void test_thread_surface_rejects_inconsistent_thread_inputs() {
  using dasall::platform::ThreadHandle;
  using dasall::platform::ThreadOptions;
  using dasall::tests::support::assert_true;

  ThreadOptions missing_name;
  missing_name.name.clear();

  ThreadOptions zero_stack_size;
  zero_stack_size.stack_size_kb = 0;

  const ThreadHandle invalid_handle{};
  const ThreadHandle valid_handle{
      .native_id = 42,
  };

  assert_true(!missing_name.has_consistent_values(),
              "thread options should reject empty thread name");
  assert_true(!zero_stack_size.has_consistent_values(),
              "thread options should reject zero stack size");
  assert_true(!invalid_handle.has_consistent_values(),
              "thread handle should reject zero native thread id");
  assert_true(valid_handle.has_consistent_values(),
              "thread handle should accept non-zero native thread id");
}

void test_ithread_interface_surface_stays_stable() {
  using dasall::platform::IThread;
  using dasall::platform::PlatformResult;
  using dasall::platform::ThreadEntry;
  using dasall::platform::ThreadHandle;
  using dasall::platform::ThreadJoinResult;
  using dasall::platform::ThreadOptions;
  using dasall::tests::support::assert_true;

  using CreateThreadSignature =
      PlatformResult<ThreadHandle> (IThread::*)(const ThreadOptions&, ThreadEntry);
  using JoinThreadSignature =
      PlatformResult<ThreadJoinResult> (IThread::*)(const ThreadHandle&, std::int32_t);
  using RequestStopSignature = PlatformResult<bool> (IThread::*)(const ThreadHandle&);

  static_assert(std::is_same_v<decltype(&IThread::create_thread), CreateThreadSignature>,
                "IThread::create_thread signature should remain stable");
  static_assert(std::is_same_v<decltype(&IThread::join_thread), JoinThreadSignature>,
                "IThread::join_thread signature should remain stable");
  static_assert(std::is_same_v<decltype(&IThread::request_stop), RequestStopSignature>,
                "IThread::request_stop signature should remain stable");

  assert_true(std::is_abstract_v<IThread>, "IThread should remain an abstract interface");
}

}  // namespace

int main() {
  try {
    test_thread_options_default_values_match_linux_thread_baseline();
    test_thread_surface_rejects_inconsistent_thread_inputs();
    test_ithread_interface_surface_stays_stable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}