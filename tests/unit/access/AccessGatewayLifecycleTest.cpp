#include <chrono>
#include <exception>
#include <iostream>
#include <memory>

#include "AccessTypes.h"
#include "IAccessGateway.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessGatewayState;
using dasall::access::IAccessGateway;
using dasall::access::InboundPacket;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchResult;
using dasall::access::AccessDisposition;

// 简单的模拟实现用于验证接口与生命周期语义
class MockAccessGateway : public IAccessGateway {
 public:
  MockAccessGateway() : current_state_(AccessGatewayState::Uninitialized) {}

  bool init() override {
    current_state_ = AccessGatewayState::Initializing;
    current_state_ = AccessGatewayState::Ready;
    return true;
  }

  RuntimeDispatchResult submit(const InboundPacket& /*packet*/) override {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Rejected;
    return result;
  }

  bool publish_result(const PublishEnvelope& /*envelope*/) override {
    return true;
  }

  AccessGatewayState state() const override { return current_state_; }

  bool is_ready() const override {
    return current_state_ == AccessGatewayState::Ready;
  }

  void shutdown(std::chrono::milliseconds /*drain_timeout*/) override {
    current_state_ = AccessGatewayState::Draining;
    current_state_ = AccessGatewayState::ShutDown;
  }

 private:
  mutable AccessGatewayState current_state_;
};

void gateway_lifecycle_state_transitions_work() {
  auto gateway = std::make_unique<MockAccessGateway>();

  // 初始状态应该是 Uninitialized
  dasall::tests::support::assert_equal(
      static_cast<int>(AccessGatewayState::Uninitialized),
      static_cast<int>(gateway->state()),
      "initial state should be Uninitialized");
  dasall::tests::support::assert_true(
      !gateway->is_ready(),
      "is_ready() should be false in Uninitialized state");

  // 调用 init() 后应该进入 Ready 状态
  gateway->init();
  dasall::tests::support::assert_equal(
      static_cast<int>(AccessGatewayState::Ready),
      static_cast<int>(gateway->state()),
      "state should be Ready after init()");
  dasall::tests::support::assert_true(
      gateway->is_ready(),
      "is_ready() should be true in Ready state");

  // 调用 shutdown() 应该过渡到 Draining 再到 ShutDown
  gateway->shutdown(std::chrono::milliseconds(1000));
  dasall::tests::support::assert_equal(
      static_cast<int>(AccessGatewayState::ShutDown),
      static_cast<int>(gateway->state()),
      "state should be ShutDown after shutdown()");
  dasall::tests::support::assert_true(
      !gateway->is_ready(),
      "is_ready() should be false in ShutDown state");
}

void gateway_is_ready_is_binary_judgment() {
  auto gateway = std::make_unique<MockAccessGateway>();

  // is_ready() 只在 Ready 状态为 true，其他所有状态为 false
  dasall::tests::support::assert_true(
      !gateway->is_ready(),
      "is_ready() should be false for Uninitialized");

  gateway->init();
  dasall::tests::support::assert_true(
      gateway->is_ready(),
      "is_ready() should be true for Ready");

  gateway->shutdown(std::chrono::milliseconds(1000));
  dasall::tests::support::assert_true(
      !gateway->is_ready(),
      "is_ready() should be false for ShutDown");
}

void gateway_state_const_method_is_safe() {
  auto gateway = std::make_unique<MockAccessGateway>();

  // state() 和 is_ready() 都是 const 方法，可以在 const 上下文中调用
  const IAccessGateway& const_gateway = *gateway;

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
  auto gateway = std::make_unique<MockAccessGateway>();
  gateway->init();

  // shutdown() 接受 chrono::milliseconds 参数
  std::chrono::milliseconds timeout(5000);
  gateway->shutdown(timeout);

  dasall::tests::support::assert_equal(
      static_cast<int>(AccessGatewayState::ShutDown),
      static_cast<int>(gateway->state()),
      "shutdown with timeout parameter should work");
}

}  // namespace

int main() {
  try {
    gateway_lifecycle_state_transitions_work();
    gateway_is_ready_is_binary_judgment();
    gateway_state_const_method_is_safe();
    gateway_shutdown_timeout_parameter_accepted();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
