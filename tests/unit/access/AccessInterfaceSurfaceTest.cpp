#include <exception>
#include <iostream>
#include <memory>
#include <string_view>
#include <type_traits>

#include "AccessGatewayFactory.h"
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
    static_assert(std::is_abstract_v<dasall::access::IAdmissionController>);
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
      .subject_identity = {
          .actor_ref = "actor://local/tester",
          .subject_type = "operator",
          .auth_method = "local_trusted",
          .trust_level = "trusted",
          .tenant_ref = "tenant-default",
          .auth_metadata = std::nullopt,
      },
      .decision_proof = {
          .decision = "Allow",
          .policy_decision_ref = "policy://default/rule-1",
          .reason_code = "ALLOW",
          .reason_description = std::nullopt,
          .evaluated_at = std::nullopt,
      },
      .client_capability_view = "unary",
      .async_allowed = true,
      .stream_requested = false,
      .request_context = {},
      .access_deadline = std::nullopt,
  };

  dasall::tests::support::assert_equal(
      "pkt-001",
      request.packet.packet_id,
      "access interface surface test should see access supporting types");
  dasall::tests::support::assert_true(
      request.async_allowed,
      "access interface surface test should preserve dispatch request flags");

  const dasall::access::AccessAdmissionResult admission_result{};
  dasall::tests::support::assert_true(
      !admission_result.admitted,
      "access interface surface test should see AccessAdmissionResult defaults");

  const dasall::access::LocalPeerUidFact local_peer_fact{};
  dasall::tests::support::assert_true(
      !local_peer_fact.eligible_for_local_trusted,
      "access interface surface test should expose LocalPeerUidFact defaults");

  auto gateway = dasall::access::create_access_gateway();
  dasall::tests::support::assert_true(
      static_cast<bool>(gateway),
      "access interface surface test should create gateway through public factory");
  dasall::tests::support::assert_equal(
      static_cast<int>(dasall::access::AccessGatewayState::Uninitialized),
      static_cast<int>(gateway->state()),
      "factory-created gateway should expose public lifecycle state before init");
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

void iaccess_runtime_bridge_methods_exist() {
  constexpr bool dispatch_method_exists =
      std::is_invocable_r_v<
          dasall::access::RuntimeDispatchResult,
          decltype(&dasall::access::IAccessRuntimeBridge::dispatch),
          dasall::access::IAccessRuntimeBridge*,
          const dasall::access::RuntimeDispatchRequest&>;
  dasall::tests::support::assert_true(
      dispatch_method_exists,
      "IAccessRuntimeBridge::dispatch should be defined");

  constexpr bool cancel_method_exists =
      std::is_invocable_r_v<
          bool,
          decltype(&dasall::access::IAccessRuntimeBridge::cancel),
          dasall::access::IAccessRuntimeBridge*,
          std::string_view,
          std::string_view>;
  dasall::tests::support::assert_true(
      cancel_method_exists,
      "IAccessRuntimeBridge::cancel(request_id, actor_ref) should be defined");
}

void access_config_supporting_surface_is_discoverable() {
  dasall::access::AccessBootstrapConfig bootstrap_config;
  bootstrap_config.bootstrap_revision = "bootstrap-rev-001";
  bootstrap_config.entry_type = "gateway";

  dasall::access::SnapshotVersionFingerprint fingerprint;
  fingerprint.bootstrap_revision = bootstrap_config.bootstrap_revision;
  fingerprint.effective_profile_id = "profile-default";
  fingerprint.runtime_policy_generation = 1;

  dasall::access::AccessAuthView auth_view;
  dasall::access::AccessAdmissionView admission_view;
  dasall::access::AccessPublishView publish_view;
  dasall::access::AccessRuntimeGovernanceView governance_view;

  dasall::tests::support::assert_equal(
      std::string("bootstrap-rev-001"),
      fingerprint.bootstrap_revision,
      "Access config supporting surface should expose fingerprint metadata");
  dasall::tests::support::assert_true(
      auth_view.strict_auth_required,
      "AccessAuthView.strict_auth_required should default to true");
  dasall::tests::support::assert_true(
      admission_view.default_deny,
      "AccessAdmissionView.default_deny should default to true");
  dasall::tests::support::assert_equal(
      std::string("deny"),
      governance_view.security_default_effect,
      "AccessRuntimeGovernanceView.security_default_effect should default to deny");
  dasall::tests::support::assert_true(
      publish_view.max_payload_bytes > 0,
      "AccessPublishView should expose payload guard fields");
}

}  // namespace

int main() {
  try {
    access_public_surface_is_discoverable();
    access_gateway_state_enumeration_is_defined();
    iaccess_gateway_lifecycle_methods_exist();
    iaccess_runtime_bridge_methods_exist();
    access_config_supporting_surface_is_discoverable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
