#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

#include "AccessGateway.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessGatewayState;
using dasall::access::AccessGateway;
using dasall::access::InboundPacket;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchResult;
using dasall::access::AccessDisposition;

void gateway_lifecycle_state_transitions_work() {
  AccessGateway gateway(
      [](const InboundPacket&) {
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      },
      [](const PublishEnvelope&) {
        return true;
      });

  // 初始状态应该是 Uninitialized
  dasall::tests::support::assert_equal(
      static_cast<int>(AccessGatewayState::Uninitialized),
      static_cast<int>(gateway.state()),
      "initial state should be Uninitialized");
  dasall::tests::support::assert_true(
      !gateway.is_ready(),
      "is_ready() should be false in Uninitialized state");

  // 调用 init() 后应该进入 Ready 状态
  dasall::tests::support::assert_true(
      gateway.init(),
      "init should succeed when gateway is uninitialized");
  dasall::tests::support::assert_equal(
      static_cast<int>(AccessGatewayState::Ready),
      static_cast<int>(gateway.state()),
      "state should be Ready after init()");
  dasall::tests::support::assert_true(
      gateway.is_ready(),
      "is_ready() should be true in Ready state");

  // 调用 shutdown() 应该过渡到 Draining 再到 ShutDown
  gateway.shutdown(std::chrono::milliseconds(1000));
  dasall::tests::support::assert_equal(
      static_cast<int>(AccessGatewayState::ShutDown),
      static_cast<int>(gateway.state()),
      "state should be ShutDown after shutdown()");
  dasall::tests::support::assert_true(
      !gateway.is_ready(),
      "is_ready() should be false in ShutDown state");
}

void gateway_is_ready_is_binary_judgment() {
  AccessGateway gateway(
      [](const InboundPacket&) {
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      },
      {});

  // is_ready() 只在 Ready 状态为 true，其他所有状态为 false
  dasall::tests::support::assert_true(
      !gateway.is_ready(),
      "is_ready() should be false for Uninitialized");

  dasall::tests::support::assert_true(
      gateway.init(),
      "init should succeed before ready-state check");
  dasall::tests::support::assert_true(
      gateway.is_ready(),
      "is_ready() should be true for Ready");

  gateway.shutdown(std::chrono::milliseconds(1000));
  dasall::tests::support::assert_true(
      !gateway.is_ready(),
      "is_ready() should be false for ShutDown");
}

void gateway_state_const_method_is_safe() {
  AccessGateway gateway;

  // state() 和 is_ready() 都是 const 方法，可以在 const 上下文中调用
  const AccessGateway& const_gateway = gateway;

  AccessGatewayState state = const_gateway.state();
  bool is_ready = const_gateway.is_ready();

  dasall::tests::support::assert_equal(
      static_cast<int>(AccessGatewayState::Uninitialized),
      static_cast<int>(state),
      "const state() should work");
  dasall::tests::support::assert_true(
      !is_ready,
      "const is_ready() should work");
}

void gateway_shutdown_timeout_parameter_accepted() {
  std::size_t abandoned_requests = 0;
  AccessGateway gateway(
      [](const InboundPacket&) {
        // 模拟短暂 in-flight 请求，验证 shutdown 会等待/超时后结束。
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      },
      {},
      [&abandoned_requests](std::size_t count) { abandoned_requests = count; });
  dasall::tests::support::assert_true(
      gateway.init(),
      "init should succeed before shutdown timeout check");

  InboundPacket packet;
  packet.packet_id = "pkt-024-lifecycle";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http";

  std::thread inflight([&gateway, packet]() {
    (void)gateway.submit(packet);
  });

  // shutdown() 接受 chrono::milliseconds 参数
  std::chrono::milliseconds timeout(5000);
  gateway.shutdown(timeout);

  if (inflight.joinable()) {
    inflight.join();
  }

  dasall::tests::support::assert_equal(
      static_cast<int>(AccessGatewayState::ShutDown),
      static_cast<int>(gateway.state()),
      "shutdown with timeout parameter should work");
  dasall::tests::support::assert_equal(
      static_cast<std::size_t>(0),
      abandoned_requests,
      "shutdown observer should remain quiet when inflight requests drain within timeout");
}

void gateway_shutdown_reports_abandoned_requests_to_observer() {
  std::size_t abandoned_requests = 0;
  AccessGateway gateway(
      [](const InboundPacket&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      },
      {},
      [&abandoned_requests](std::size_t count) { abandoned_requests = count; });
  dasall::tests::support::assert_true(
      gateway.init(),
      "init should succeed before shutdown observer timeout check");

  InboundPacket packet;
  packet.packet_id = "pkt-022-lifecycle-timeout";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http";

  std::thread inflight([&gateway, packet]() {
    (void)gateway.submit(packet);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  gateway.shutdown(std::chrono::milliseconds(1));

  if (inflight.joinable()) {
    inflight.join();
  }

  dasall::tests::support::assert_equal(
      static_cast<std::size_t>(1),
      abandoned_requests,
      "shutdown observer should receive abandoned inflight count after timeout");
}

}  // namespace

int main() {
  try {
    gateway_lifecycle_state_transitions_work();
    gateway_is_ready_is_binary_judgment();
    gateway_state_const_method_is_safe();
    gateway_shutdown_timeout_parameter_accepted();
    gateway_shutdown_reports_abandoned_requests_to_observer();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
