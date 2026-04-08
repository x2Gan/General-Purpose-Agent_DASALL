#include <exception>
#include <iostream>
#include <type_traits>

#include "IFileSystem.h"
#include "INetwork.h"
#include "IIPC.h"
#include "IQueue.h"
#include "ITimer.h"
#include "IThread.h"
#include "support/TestAssertions.h"

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

void test_socket_endpoint_default_values_and_consistency() {
  using dasall::platform::ConnectOptions;
  using dasall::platform::NetworkTransport;
  using dasall::platform::SocketEndpoint;
  using dasall::tests::support::assert_true;

  const ConnectOptions default_options;

  assert_true(default_options.connect_timeout_ms == 3000,
              "connect options should default to 3000 ms connect timeout");
  assert_true(!default_options.reuse_address,
              "connect options should not reuse address by default");
  assert_true(default_options.has_consistent_values(),
              "default connect options should remain internally consistent");

  const SocketEndpoint empty_host{};
  const SocketEndpoint zero_port{
      .host = "localhost",
      .port = 0,
      .transport = NetworkTransport::Tcp,
  };
  const SocketEndpoint valid_tcp_endpoint{
      .host = "127.0.0.1",
      .port = 8080,
      .transport = NetworkTransport::Tcp,
  };
  const SocketEndpoint valid_udp_endpoint{
      .host = "192.168.1.1",
      .port = 1234,
      .transport = NetworkTransport::Udp,
  };

  assert_true(!empty_host.has_consistent_values(),
              "socket endpoint should reject empty host");
  assert_true(!zero_port.has_consistent_values(),
              "socket endpoint should reject zero port");
  assert_true(valid_tcp_endpoint.has_consistent_values(),
              "socket endpoint should accept valid tcp endpoint");
  assert_true(valid_udp_endpoint.has_consistent_values(),
              "socket endpoint should accept valid udp endpoint");
}

void test_network_surface_rejects_inconsistent_network_inputs() {
  using dasall::platform::ConnectionHandle;
  using dasall::platform::NetworkBuffer;
  using dasall::platform::NetworkReceiveResult;
  using dasall::tests::support::assert_true;

  const ConnectionHandle invalid_handle{};
  const ConnectionHandle valid_handle{
      .native_fd = 5,
  };

  const NetworkReceiveResult invalid_receive_result{
      .data = NetworkBuffer{0x01U, 0x02U},
      .peer_closed = true,
  };
  const NetworkReceiveResult valid_receive_result_with_data{
      .data = NetworkBuffer{0xAAU},
      .peer_closed = false,
  };
  const NetworkReceiveResult valid_receive_result_peer_closed{
      .data = {},
      .peer_closed = true,
  };

  assert_true(!invalid_handle.has_consistent_values(),
              "connection handle should reject zero native fd");
  assert_true(valid_handle.has_consistent_values(),
              "connection handle should accept non-zero native fd");
  assert_true(!invalid_receive_result.has_consistent_values(),
              "network receive result should reject non-empty data when peer_closed is true");
  assert_true(valid_receive_result_with_data.has_consistent_values(),
              "network receive result should accept non-empty data when peer is open");
  assert_true(valid_receive_result_peer_closed.has_consistent_values(),
              "network receive result should accept empty data when peer_closed is true");
}

void test_inetwork_interface_surface_stays_stable() {
  using dasall::platform::ConnectOptions;
  using dasall::platform::ConnectionHandle;
  using dasall::platform::INetwork;
  using dasall::platform::NetworkBuffer;
  using dasall::platform::NetworkReceiveResult;
  using dasall::platform::NetworkSendResult;
  using dasall::platform::PlatformResult;
  using dasall::platform::SocketEndpoint;
  using dasall::tests::support::assert_true;

  using ConnectSignature =
      PlatformResult<ConnectionHandle> (INetwork::*)(const SocketEndpoint&,
                                                     const ConnectOptions&);
  using SendSignature =
      PlatformResult<NetworkSendResult> (INetwork::*)(const ConnectionHandle&,
                                                      const NetworkBuffer&, std::int32_t);
  using ReceiveSignature =
      PlatformResult<NetworkReceiveResult> (INetwork::*)(const ConnectionHandle&,
                                                         std::int32_t);
  using ShutdownSignature = PlatformResult<bool> (INetwork::*)(const ConnectionHandle&);

  static_assert(std::is_same_v<decltype(&INetwork::connect), ConnectSignature>,
                "INetwork::connect signature should remain stable");
  static_assert(std::is_same_v<decltype(&INetwork::send), SendSignature>,
                "INetwork::send signature should remain stable");
  static_assert(std::is_same_v<decltype(&INetwork::receive), ReceiveSignature>,
                "INetwork::receive signature should remain stable");
  static_assert(std::is_same_v<decltype(&INetwork::shutdown), ShutdownSignature>,
                "INetwork::shutdown signature should remain stable");

  assert_true(std::is_abstract_v<INetwork>, "INetwork should remain an abstract interface");
}

void test_ipc_endpoint_and_listen_options_consistency() {
  using dasall::platform::IpcEndpoint;
  using dasall::platform::ListenOptions;
  using dasall::tests::support::assert_true;

  const IpcEndpoint empty_path{};
  const IpcEndpoint valid_socket_path{
      .socket_path = "/tmp/dasall/agent.sock",
      .use_abstract_namespace = false,
  };
  const IpcEndpoint valid_abstract_path{
      .socket_path = "dasall-agent",
      .use_abstract_namespace = true,
  };

  assert_true(!empty_path.has_consistent_values(),
              "ipc endpoint should reject empty socket path");
  assert_true(valid_socket_path.has_consistent_values(),
              "ipc endpoint should accept valid filesystem socket path");
  assert_true(valid_abstract_path.has_consistent_values(),
              "ipc endpoint should accept valid abstract namespace path");

  const ListenOptions default_options;

  assert_true(default_options.backlog == 5U,
              "listen options should default to backlog of 5");
  assert_true(default_options.max_payload_bytes == 1048576U,
              "listen options should default to 1 MiB max payload");
  assert_true(default_options.has_consistent_values(),
              "default listen options should remain internally consistent");

  ListenOptions invalid_backlog;
  invalid_backlog.backlog = 0;

  ListenOptions invalid_payload;
  invalid_payload.max_payload_bytes = 0;

  assert_true(!invalid_backlog.has_consistent_values(),
              "listen options should reject zero backlog");
  assert_true(!invalid_payload.has_consistent_values(),
              "listen options should reject zero max payload bytes");
}

void test_ipc_surface_rejects_inconsistent_ipc_inputs() {
  using dasall::platform::IpcChannelHandle;
  using dasall::platform::IpcListenerHandle;
  using dasall::platform::IpcPayload;
  using dasall::platform::IpcReceiveResult;
  using dasall::tests::support::assert_true;

  const IpcListenerHandle invalid_listener{};
  const IpcListenerHandle valid_listener{
      .native_fd = 10,
  };
  const IpcChannelHandle invalid_channel{};
  const IpcChannelHandle valid_channel{
      .native_fd = 11,
  };

  const IpcReceiveResult invalid_receive_peer_closed_with_data{
      .data = IpcPayload{0xBBU},
      .peer_closed = true,
  };
  const IpcReceiveResult valid_receive_with_data{
      .data = IpcPayload{0x01U, 0x02U},
      .peer_closed = false,
  };
  const IpcReceiveResult valid_receive_peer_closed{
      .data = {},
      .peer_closed = true,
  };

  assert_true(!invalid_listener.has_consistent_values(),
              "ipc listener handle should reject zero native fd");
  assert_true(valid_listener.has_consistent_values(),
              "ipc listener handle should accept non-zero native fd");
  assert_true(!invalid_channel.has_consistent_values(),
              "ipc channel handle should reject zero native fd");
  assert_true(valid_channel.has_consistent_values(),
              "ipc channel handle should accept non-zero native fd");
  assert_true(!invalid_receive_peer_closed_with_data.has_consistent_values(),
              "ipc receive result should reject non-empty data when peer_closed is true");
  assert_true(valid_receive_with_data.has_consistent_values(),
              "ipc receive result should accept data when peer is open");
  assert_true(valid_receive_peer_closed.has_consistent_values(),
              "ipc receive result should accept empty data when peer_closed is true");
}

void test_iipc_interface_surface_stays_stable() {
  using dasall::platform::IIPC;
  using dasall::platform::IpcChannelHandle;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::IpcListenerHandle;
  using dasall::platform::IpcPayload;
  using dasall::platform::IpcReceiveResult;
  using dasall::platform::IpcSendResult;
  using dasall::platform::ListenOptions;
  using dasall::platform::PlatformResult;
  using dasall::tests::support::assert_true;

  using ListenSignature =
      PlatformResult<IpcListenerHandle> (IIPC::*)(const IpcEndpoint&, const ListenOptions&);
  using AcceptSignature =
      PlatformResult<IpcChannelHandle> (IIPC::*)(const IpcListenerHandle&, std::int32_t);
  using ConnectSignature =
      PlatformResult<IpcChannelHandle> (IIPC::*)(const IpcEndpoint&, std::int32_t);
  using SendSignature =
      PlatformResult<IpcSendResult> (IIPC::*)(const IpcChannelHandle&, const IpcPayload&);
  using ReceiveSignature =
      PlatformResult<IpcReceiveResult> (IIPC::*)(const IpcChannelHandle&, std::int32_t);
  using CloseSignature = PlatformResult<bool> (IIPC::*)(const IpcChannelHandle&);

  static_assert(std::is_same_v<decltype(&IIPC::listen), ListenSignature>,
                "IIPC::listen signature should remain stable");
  static_assert(std::is_same_v<decltype(&IIPC::accept), AcceptSignature>,
                "IIPC::accept signature should remain stable");
  static_assert(std::is_same_v<decltype(&IIPC::connect), ConnectSignature>,
                "IIPC::connect signature should remain stable");
  static_assert(std::is_same_v<decltype(&IIPC::send), SendSignature>,
                "IIPC::send signature should remain stable");
  static_assert(std::is_same_v<decltype(&IIPC::receive), ReceiveSignature>,
                "IIPC::receive signature should remain stable");
  static_assert(std::is_same_v<decltype(&IIPC::close), CloseSignature>,
                "IIPC::close signature should remain stable");

  assert_true(std::is_abstract_v<IIPC>, "IIPC should remain an abstract interface");
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
    test_socket_endpoint_default_values_and_consistency();
    test_network_surface_rejects_inconsistent_network_inputs();
    test_inetwork_interface_surface_stays_stable();
        test_ipc_endpoint_and_listen_options_consistency();
        test_ipc_surface_rejects_inconsistent_ipc_inputs();
        test_iipc_interface_surface_stays_stable();
    } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}