#include <exception>
#include <iostream>
#include <type_traits>

#include "IFileSystem.h"
#include "IQueue.h"
#include "ITimer.h"
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

void test_timer_spec_default_values_match_linux_timer_baseline() {
  using dasall::platform::TimerClockKind;
  using dasall::platform::TimerMode;
  using dasall::platform::TimerSpec;
  using dasall::tests::support::assert_true;

  const TimerSpec spec;

  assert_true(spec.mode == TimerMode::OneShot,
              "timer spec should default to one-shot mode");
  assert_true(spec.interval_ms == 0U,
              "one-shot timer spec should default to zero repeat interval");
  assert_true(spec.initial_delay_ms == 0U,
              "timer spec should allow immediate first fire by default");
  assert_true(spec.clock_kind == TimerClockKind::Monotonic,
              "timer spec should default to monotonic clock");
  assert_true(spec.has_consistent_values(),
              "default timer spec should remain internally consistent");
}

void test_timer_surface_rejects_inconsistent_timer_inputs() {
  using dasall::platform::TimerCancelResult;
  using dasall::platform::TimerDriftStats;
  using dasall::platform::TimerHandle;
  using dasall::platform::TimerMode;
  using dasall::platform::TimerSpec;
  using dasall::tests::support::assert_true;

  TimerSpec invalid_periodic_spec;
  invalid_periodic_spec.mode = TimerMode::Periodic;

  TimerSpec valid_periodic_spec;
  valid_periodic_spec.mode = TimerMode::Periodic;
  valid_periodic_spec.interval_ms = 100U;

  const TimerHandle invalid_handle{};
  const TimerHandle valid_handle{
      .native_id = 7,
  };

  const TimerDriftStats invalid_drift_stats{
      .expiration_count = 0,
      .last_drift_ms = 1,
  };
  const TimerDriftStats valid_drift_stats{
      .expiration_count = 3,
      .last_drift_ms = 4,
      .max_drift_ms = 9,
  };
  const TimerCancelResult cancel_result{
      .cancelled = true,
      .drift_stats = valid_drift_stats,
  };

  assert_true(!invalid_periodic_spec.has_consistent_values(),
              "periodic timer spec should reject zero repeat interval");
  assert_true(valid_periodic_spec.has_consistent_values(),
              "periodic timer spec should accept positive repeat interval");
  assert_true(!invalid_handle.has_consistent_values(),
              "timer handle should reject zero native timer id");
  assert_true(valid_handle.has_consistent_values(),
              "timer handle should accept non-zero native timer id");
  assert_true(!invalid_drift_stats.has_consistent_values(),
              "timer drift stats should reject drift without expirations");
  assert_true(valid_drift_stats.has_consistent_values(),
              "timer drift stats should accept monotonic drift snapshots");
  assert_true(cancel_result.has_consistent_values(),
              "timer cancel result should remain consistent when drift stats are valid");
}

void test_itimer_interface_surface_stays_stable() {
  using dasall::platform::ITimer;
  using dasall::platform::PlatformResult;
  using dasall::platform::TimerCallback;
  using dasall::platform::TimerCancelResult;
  using dasall::platform::TimerHandle;
  using dasall::platform::TimerSpec;
  using dasall::tests::support::assert_true;

  using StartOnceSignature =
      PlatformResult<TimerHandle> (ITimer::*)(const TimerSpec&, TimerCallback);
  using StartPeriodicSignature =
      PlatformResult<TimerHandle> (ITimer::*)(const TimerSpec&, TimerCallback);
  using CancelSignature = PlatformResult<TimerCancelResult> (ITimer::*)(const TimerHandle&);

  static_assert(std::is_same_v<decltype(&ITimer::start_once), StartOnceSignature>,
                "ITimer::start_once signature should remain stable");
  static_assert(std::is_same_v<decltype(&ITimer::start_periodic), StartPeriodicSignature>,
                "ITimer::start_periodic signature should remain stable");
  static_assert(std::is_same_v<decltype(&ITimer::cancel), CancelSignature>,
                "ITimer::cancel signature should remain stable");

  assert_true(std::is_abstract_v<ITimer>, "ITimer should remain an abstract interface");
}

void test_queue_options_default_values_match_linux_queue_baseline() {
  using dasall::platform::QueueOptions;
  using dasall::platform::QueueOverflowPolicy;
  using dasall::platform::QueueShutdownPolicy;
  using dasall::tests::support::assert_true;

  const QueueOptions options;

  assert_true(options.capacity == 1024U,
              "queue options should keep default capacity from linux baseline");
  assert_true(options.overflow_policy == QueueOverflowPolicy::Reject,
              "queue options should default to reject overflow policy");
  assert_true(options.shutdown_policy == QueueShutdownPolicy::Drain,
              "queue options should default to drain shutdown policy");
  assert_true(options.has_consistent_values(),
              "default queue options should remain internally consistent");
}

void test_queue_surface_rejects_inconsistent_queue_inputs() {
  using dasall::platform::QueueHandle;
  using dasall::platform::QueueItem;
  using dasall::platform::QueueOptions;
  using dasall::platform::QueuePopResult;
  using dasall::tests::support::assert_true;

  QueueOptions invalid_options;
  invalid_options.capacity = 0;

  const QueueHandle invalid_handle{};
  const QueueHandle valid_handle{
      .native_id = 9,
  };

  const QueuePopResult invalid_pop_result{
      .has_item = false,
      .item = QueueItem{1U, 2U, 3U},
      .queue_depth = 0,
  };
  const QueuePopResult valid_pop_result{
      .has_item = true,
      .item = QueueItem{7U},
      .queue_depth = 1,
  };

  assert_true(!invalid_options.has_consistent_values(),
              "queue options should reject zero capacity");
  assert_true(!invalid_handle.has_consistent_values(),
              "queue handle should reject zero native queue id");
  assert_true(valid_handle.has_consistent_values(),
              "queue handle should accept non-zero native queue id");
  assert_true(!invalid_pop_result.has_consistent_values(),
              "queue pop result should reject payload when has_item is false");
  assert_true(valid_pop_result.has_consistent_values(),
              "queue pop result should accept payload when has_item is true");
}

void test_iqueue_interface_surface_stays_stable() {
  using dasall::platform::IQueue;
  using dasall::platform::PlatformResult;
  using dasall::platform::QueueCloseResult;
  using dasall::platform::QueueHandle;
  using dasall::platform::QueueItem;
  using dasall::platform::QueueOptions;
  using dasall::platform::QueuePopResult;
  using dasall::platform::QueuePushResult;
  using dasall::tests::support::assert_true;

  using CreateQueueSignature = PlatformResult<QueueHandle> (IQueue::*)(const QueueOptions&);
  using PushSignature =
      PlatformResult<QueuePushResult> (IQueue::*)(const QueueHandle&, const QueueItem&,
                                                  std::int32_t);
  using PopSignature =
      PlatformResult<QueuePopResult> (IQueue::*)(const QueueHandle&, std::int32_t);
  using CloseSignature = PlatformResult<QueueCloseResult> (IQueue::*)(const QueueHandle&);

  static_assert(std::is_same_v<decltype(&IQueue::create_queue), CreateQueueSignature>,
                "IQueue::create_queue signature should remain stable");
  static_assert(std::is_same_v<decltype(&IQueue::push), PushSignature>,
                "IQueue::push signature should remain stable");
  static_assert(std::is_same_v<decltype(&IQueue::pop), PopSignature>,
                "IQueue::pop signature should remain stable");
  static_assert(std::is_same_v<decltype(&IQueue::close), CloseSignature>,
                "IQueue::close signature should remain stable");

  assert_true(std::is_abstract_v<IQueue>, "IQueue should remain an abstract interface");
}

void test_file_write_options_default_values_match_linux_filesystem_baseline() {
  using dasall::platform::FileWriteMode;
  using dasall::platform::FileWriteOptions;
  using dasall::tests::support::assert_true;

  const FileWriteOptions options;

  assert_true(options.mode == FileWriteMode::Overwrite,
              "file write options should default to overwrite mode");
  assert_true(!options.sync_on_write,
              "file write options should not force sync by default");
  assert_true(options.tmp_suffix == ".tmp",
              "file write options should use .tmp suffix for atomic write by default");
  assert_true(options.has_consistent_values(),
              "default file write options should remain internally consistent");
}

void test_file_surface_rejects_inconsistent_filesystem_inputs() {
  using dasall::platform::FileStatResult;
  using dasall::platform::FileWriteOptions;
  using dasall::tests::support::assert_true;

  FileWriteOptions invalid_options;
  invalid_options.tmp_suffix.clear();

  const FileStatResult nonexistent_file{};

  const FileStatResult invalid_stat_nonexistent_but_typed{
      .exists = false,
      .is_regular_file = true,
      .is_directory = false,
      .size_bytes = 0,
      .last_modified_ms = 0,
  };
  const FileStatResult invalid_stat_both_types{
      .exists = true,
      .is_regular_file = true,
      .is_directory = true,
      .size_bytes = 0,
      .last_modified_ms = 0,
  };
  const FileStatResult invalid_stat_nonexistent_with_size{
      .exists = false,
      .is_regular_file = false,
      .is_directory = false,
      .size_bytes = 100,
      .last_modified_ms = 0,
  };
  const FileStatResult valid_stat_regular_file{
      .exists = true,
      .is_regular_file = true,
      .is_directory = false,
      .size_bytes = 4096,
      .last_modified_ms = 1000,
  };
  const FileStatResult valid_stat_directory{
      .exists = true,
      .is_regular_file = false,
      .is_directory = true,
      .size_bytes = 0,
      .last_modified_ms = 500,
  };

  assert_true(!invalid_options.has_consistent_values(),
              "file write options should reject empty tmp suffix");
  assert_true(nonexistent_file.has_consistent_values(),
              "file stat result should accept nonexistent file with all defaults");
  assert_true(!invalid_stat_nonexistent_but_typed.has_consistent_values(),
              "file stat result should reject is_regular_file=true when file does not exist");
  assert_true(!invalid_stat_both_types.has_consistent_values(),
              "file stat result should reject both is_regular_file and is_directory set");
  assert_true(!invalid_stat_nonexistent_with_size.has_consistent_values(),
              "file stat result should reject nonzero size when file does not exist");
  assert_true(valid_stat_regular_file.has_consistent_values(),
              "file stat result should accept valid regular file stat");
  assert_true(valid_stat_directory.has_consistent_values(),
              "file stat result should accept valid directory stat");
}

void test_ifilesystem_interface_surface_stays_stable() {
  using dasall::platform::FileBuffer;
  using dasall::platform::FileStatResult;
  using dasall::platform::FileWriteOptions;
  using dasall::platform::IFileSystem;
  using dasall::platform::PlatformResult;
  using dasall::tests::support::assert_true;

  using ReadFileSignature =
      PlatformResult<FileBuffer> (IFileSystem::*)(const std::string&, std::int32_t);
  using WriteAtomicSignature =
      PlatformResult<bool> (IFileSystem::*)(const std::string&, const FileBuffer&,
                                            const FileWriteOptions&);
  using EnsureDirectorySignature =
      PlatformResult<bool> (IFileSystem::*)(const std::string&);
  using StatSignature =
      PlatformResult<FileStatResult> (IFileSystem::*)(const std::string&);

  static_assert(std::is_same_v<decltype(&IFileSystem::read_file), ReadFileSignature>,
                "IFileSystem::read_file signature should remain stable");
  static_assert(std::is_same_v<decltype(&IFileSystem::write_atomic), WriteAtomicSignature>,
                "IFileSystem::write_atomic signature should remain stable");
  static_assert(
      std::is_same_v<decltype(&IFileSystem::ensure_directory), EnsureDirectorySignature>,
      "IFileSystem::ensure_directory signature should remain stable");
  static_assert(std::is_same_v<decltype(&IFileSystem::stat), StatSignature>,
                "IFileSystem::stat signature should remain stable");

  assert_true(std::is_abstract_v<IFileSystem>, "IFileSystem should remain an abstract interface");
}

}  // namespace

int main() {
  try {
    test_thread_options_default_values_match_linux_thread_baseline();
    test_thread_surface_rejects_inconsistent_thread_inputs();
    test_ithread_interface_surface_stays_stable();
    test_timer_spec_default_values_match_linux_timer_baseline();
    test_timer_surface_rejects_inconsistent_timer_inputs();
    test_itimer_interface_surface_stays_stable();
    test_queue_options_default_values_match_linux_queue_baseline();
    test_queue_surface_rejects_inconsistent_queue_inputs();
    test_iqueue_interface_surface_stays_stable();
    test_file_write_options_default_values_match_linux_filesystem_baseline();
    test_file_surface_rejects_inconsistent_filesystem_inputs();
    test_ifilesystem_interface_surface_stays_stable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}