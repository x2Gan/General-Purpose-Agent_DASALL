/// tests/unit/access/DaemonProtocolAdapterTest.cpp
///
/// ACC-TODO-029 基础单元测试：DaemonProtocolAdapter
///
/// 覆盖：
///   - can_handle() 匹配 ("daemon", "ipc_uds") 并拒绝其他组合
///   - decode() 将 JSON payload 解析为 InboundPacket
///   - decode() 空 payload 返回空 packet
///   - encode() 将 PublishEnvelope 序列化并发送成功

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "daemon/DaemonProtocolAdapter.h"
#include "PlatformResult.h"
#include "daemon/DaemonProtocolTypes.h"
#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

class CapturingIpc final : public dasall::platform::IIPC {
 public:
  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle>::success(
        dasall::platform::IpcListenerHandle{.native_fd = 11U});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 12U});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 13U});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload& payload) override {
    sent_payload = payload;
    return dasall::platform::PlatformResult<dasall::platform::IpcSendResult>::success(
        dasall::platform::IpcSendResult{.bytes_sent = payload.size()});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult>::success(
        dasall::platform::IpcReceiveResult{});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>
  describe_peer(const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>::success(
        dasall::platform::PeerIdentitySnapshot{
            .peer_uid = 1000U,
            .peer_gid = 1000U,
            .peer_pid = 4242U,
            .is_local_socket_peer = true,
        });
  }

  [[nodiscard]] dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<bool>::success(true);
  }

  dasall::platform::IpcPayload sent_payload;
};

/// -----------------------------------------------------------------------
/// 测试 can_handle() 匹配逻辑
/// -----------------------------------------------------------------------

void test_daemon_adapter_can_handle_daemon_ipc_uds() {
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  DaemonProtocolAdapter adapter(ipc);

  assert_true(adapter.can_handle("daemon", "ipc_uds"),
              "adapter should handle (daemon, ipc_uds)");
}

void test_daemon_adapter_cannot_handle_other_combinations() {
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  DaemonProtocolAdapter adapter(ipc);

  assert_true(!adapter.can_handle("gateway", "http"),
              "adapter should not handle (gateway, http)");
  assert_true(!adapter.can_handle("daemon", "http"),
              "adapter should not handle (daemon, http)");
  assert_true(!adapter.can_handle("", ""),
              "adapter should not handle empty entry/protocol");
}

/// -----------------------------------------------------------------------
/// 测试 decode() 解析逻辑
/// -----------------------------------------------------------------------

void test_daemon_adapter_decode_empty_payload_returns_empty_packet() {
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::IpcChannelHandle;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  DaemonProtocolAdapter adapter(ipc);

  IpcChannelHandle channel;
  channel.native_fd = 1;

  // 注入空 payload
  adapter.set_active_channel(channel, {});
  const auto packet = adapter.decode();

  assert_true(packet.entry_type.empty(),
              "decode with empty payload should return packet with empty entry_type");
}

void test_daemon_adapter_decode_ping_sets_packet_id() {
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::IpcChannelHandle;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  DaemonProtocolAdapter adapter(ipc);

  IpcChannelHandle channel;
  channel.native_fd = 1;

  const std::string ping_json = R"({"schema_version":"1","command":"ping"})";
  std::vector<std::uint8_t> payload(ping_json.begin(), ping_json.end());
  adapter.set_active_channel(channel, payload);

  const auto packet = adapter.decode();

  assert_equal(std::string("daemon"), packet.entry_type,
               "decoded packet should have entry_type=daemon");
  assert_equal(std::string("ipc_uds"), packet.protocol_kind,
               "decoded packet should have protocol_kind=ipc_uds");
  assert_equal(std::string("ping"), packet.packet_id,
               "ping op should set packet_id=ping");
}

void test_daemon_adapter_decode_submit_extracts_fields() {
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::IpcChannelHandle;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  DaemonProtocolAdapter adapter(ipc);

  IpcChannelHandle channel;
  channel.native_fd = 1;

  const std::string submit_json =
      R"({"schema_version":"1","request_id":"pkt-029","trace_id":"trace-029",)"
      R"("session_hint":"session-029","idempotency_key":"idem-029",)"
      R"("command":"submit","payload":"hello world",)"
      R"("args":{"peer_ref":"cli-user","route":"safe"},"async_preference":true})";
  std::vector<std::uint8_t> payload(submit_json.begin(), submit_json.end());
  adapter.set_active_channel(channel, payload);

  const auto packet = adapter.decode();

  assert_equal(std::string("pkt-029"), packet.packet_id,
               "decode should extract packet_id");
  assert_equal(std::string("hello world"), packet.payload,
               "decode should extract payload");
  assert_equal(std::string("cli-user"), packet.peer_ref,
               "decode should extract peer_ref");
  assert_true(packet.async_preferred,
              "async_preference=true should decode as async_preferred=true");
}

void test_daemon_adapter_decode_malformed_frame_returns_empty_packet() {
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::IpcChannelHandle;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  DaemonProtocolAdapter adapter(ipc);

  IpcChannelHandle channel;
  channel.native_fd = 1;

  const std::string malformed_json =
      R"({"schema_version":"1","command":"run","payload":"unterminated")";
  std::vector<std::uint8_t> payload(malformed_json.begin(), malformed_json.end());
  adapter.set_active_channel(channel, payload);

  const auto packet = adapter.decode();

  assert_true(packet.entry_type.empty(),
              "malformed frame should not decode into a valid inbound packet");
}

void test_daemon_adapter_encode_accepted_async_response_frame() {
  using dasall::access::PublishEnvelope;
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::IpcChannelHandle;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<CapturingIpc>();
  DaemonProtocolAdapter adapter(ipc);

  IpcChannelHandle channel;
  channel.native_fd = 7U;
  adapter.set_active_channel(channel, {});

  PublishEnvelope envelope;
  envelope.request_id = "req-011";
  envelope.trace_id = "trace-011";
  envelope.protocol_status_hint = "202";
  envelope.result_id = "receipt-011";

  const bool encoded = adapter.encode(envelope);
  assert_true(encoded, "encode should succeed when ipc send succeeds");

  const std::string payload(ipc->sent_payload.begin(), ipc->sent_payload.end());
  assert_true(payload.find("\"schema_version\":\"1\"") != std::string::npos,
              "encoded payload should include schema_version");
  assert_true(payload.find("\"request_id\":\"req-011\"") != std::string::npos,
              "encoded payload should include request_id");
  assert_true(payload.find("\"trace_id\":\"trace-011\"") != std::string::npos,
              "encoded payload should include trace_id");
  assert_true(payload.find("\"disposition\":\"accepted_async\"") != std::string::npos,
              "encoded payload should include accepted_async disposition");
  assert_true(payload.find("\"receipt_ref\":\"receipt-011\"") != std::string::npos,
              "accepted_async response should include receipt_ref");
}

}  // namespace

int main() {
  try {
    test_daemon_adapter_can_handle_daemon_ipc_uds();
    test_daemon_adapter_cannot_handle_other_combinations();
    test_daemon_adapter_decode_empty_payload_returns_empty_packet();
    test_daemon_adapter_decode_ping_sets_packet_id();
    test_daemon_adapter_decode_submit_extracts_fields();
    test_daemon_adapter_decode_malformed_frame_returns_empty_packet();
    test_daemon_adapter_encode_accepted_async_response_frame();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonProtocolAdapterTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
