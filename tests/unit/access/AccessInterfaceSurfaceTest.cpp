#include <exception>
#include <iostream>
#include <type_traits>

#include "AccessErrors.h"
#include "AccessTypes.h"
#include "IAccessGateway.h"
#include "IAccessRuntimeBridge.h"
#include "IAdmissionController.h"
#include "IProtocolAdapter.h"
#include "support/TestAssertions.h"

namespace {

void access_public_surface_is_discoverable() {
  static_assert(std::is_enum_v<dasall::access::AccessDisposition>);
  static_assert(std::is_enum_v<dasall::access::AccessGatewayState>);
  static_assert(std::is_abstract_v<dasall::access::IAccessGateway>);
  static_assert(std::is_abstract_v<dasall::access::IAccessRuntimeBridge>);
  static_assert(std::is_abstract_v<dasall::access::IProtocolAdapter>);

  const dasall::access::InboundPacket packet{
      .packet_id = "pkt-001",
      .entry_type = "cli",
      .protocol_kind = "uds",
      .peer_ref = "peer://local/operator",
      .payload = "{\"op\":\"ping\"}",
      .async_preferred = false,
      .stream_requested = false,
  };

  const dasall::access::RuntimeDispatchRequest request{
      .packet = packet,
      .async_allowed = true,
      .stream_requested = false,
  };

  dasall::tests::support::assert_equal(
      "pkt-001",
      request.packet.packet_id,
      "access interface surface test should see access supporting types");
  dasall::tests::support::assert_true(
      request.async_allowed,
      "access interface surface test should preserve dispatch request flags");
}

void access_gateway_state_enumeration_is_defined() {
  // 验证 AccessGatewayState 枚举值按约定定义
  dasall::tests::support::assert_equal(
      0, static_cast<int>(dasall::access::AccessGatewayState::Uninitialized),
      "AccessGatewayState::Uninitialized should be 0");
  dasall::tests::support::assert_equal(
      1, static_cast<int>(dasall::access::AccessGatewayState::Initializing),
      "AccessGatewayState::Initializing should be 1");
  dasall::tests::support::assert_equal(
      2, static_cast<int>(dasall::access::AccessGatewayState::Ready),
      "AccessGatewayState::Ready should be 2");
  dasall::tests::support::assert_equal(
      3, static_cast<int>(dasall::access::AccessGatewayState::Draining),
      "AccessGatewayState::Draining should be 3");
  dasall::tests::support::assert_equal(
      4, static_cast<int>(dasall::access::AccessGatewayState::ShutDown),
      "AccessGatewayState::ShutDown should be 4");
}

void iaccess_gateway_lifecycle_methods_exist() {
  // 验证 IAccessGateway 包含新的生命周期方法
  // 使用 static_assert 和 type traits 验证接口完整性
  
  // 检查 state() 方法存在且为 const 成员函数
  constexpr bool state_method_exists = 
      std::is_invocable_v<decltype(&dasall::access::IAccessGateway::state), 
                          const dasall::access::IAccessGateway*>;
  dasall::tests::support::assert_true(
      state_method_exists,
      "IAccessGateway::state() const method should be defined");

  // 检查 is_ready() 方法存在且为 const 成员函数
  constexpr bool is_ready_method_exists = 
      std::is_invocable_v<decltype(&dasall::access::IAccessGateway::is_ready),
                          const dasall::access::IAccessGateway*>;
  dasall::tests::support::assert_true(
      is_ready_method_exists,
      "IAccessGateway::is_ready() const method should be defined");

  // 检查 shutdown() 方法存在
  constexpr bool shutdown_method_exists =
      std::is_invocable_v<decltype(&dasall::access::IAccessGateway::shutdown),
                          dasall::access::IAccessGateway*,
                          std::chrono::milliseconds>;
  dasall::tests::support::assert_true(
      shutdown_method_exists,
      "IAccessGateway::shutdown(milliseconds) method should be defined");
}

}  // namespace

int main() {
  try {
    access_public_surface_is_discoverable();
    access_gateway_state_enumeration_is_defined();
    iaccess_gateway_lifecycle_methods_exist();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
