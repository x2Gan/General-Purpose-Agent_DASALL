#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AccessErrors.h"
#include "AccessGatewayFactory.h"
#include "secret/SecretManagerFacade.h"
#include "secret/backends/MockSecretBackend.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::InboundPacket;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] RuntimeDispatchResult make_async_result() {
  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::AcceptedAsync;
  result.receipt_ref = "task:046-secret";
  return result;
}

[[nodiscard]] InboundPacket make_packet() {
  InboundPacket packet;
  packet.packet_id = "req-046-secret";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.async_preferred = true;
  packet.payload = "ping";
  return packet;
}

[[nodiscard]] DaemonAccessPipelineOptions make_base_options() {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.entry_type = "daemon";
  options.bootstrap_config.listen_ref = "unix:///tmp/dasall-046-secret.sock";
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.bootstrap_config.trusted_local_subjects = {"local://uid/1000"};
  options.runtime_dispatch_backend = [](const RuntimeDispatchRequest&) {
    return make_async_result();
  };
  return options;
}

[[nodiscard]] dasall::infra::secret::MockSecretRecord make_secret_record() {
  using dasall::infra::secret::MockSecretRecord;
  using dasall::infra::secret::SecretBackendType;
  using dasall::infra::secret::SecretClassification;

  return MockSecretRecord{
      .record = {
          .descriptor = {
              .secret_name = std::string("access/receipt-hmac"),
              .backend_type = SecretBackendType::Mock,
              .classification = SecretClassification::SensitiveConfig,
              .rotation_policy_ref = std::string("rotation/access"),
              .owner_ref = std::string("access"),
          },
          .backend_ref = std::string("mock.primary"),
          .version = std::string("v2"),
          .cipher_ref = std::string("cipher://mock/access/receipt-hmac/v2"),
          .encrypted_at_rest = true,
      },
      .materialized_text = std::string("formal-hmac-secret-046"),
      .allowed_permission_domains = {std::string("secret.read")},
  };
}

void accepted_async_is_rejected_when_secret_is_missing() {
  auto options = make_base_options();
  options.bootstrap_config.ownership_token_hmac_secret_ref =
      std::string("secret://access/receipt-hmac");

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr, "missing secret test should still create a gateway");
  assert_true(gateway->init(), "missing secret test should initialize the daemon gateway");

  const auto result = gateway->submit(make_packet());
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "accepted async should fail closed when the ownership secret cannot be materialized");
  assert_true(result.error_ref.has_value() && *result.error_ref == "ownership_secret_unavailable",
              "missing secret should surface an explicit ownership_secret_unavailable error");
  assert_equal(static_cast<int>(dasall::access::AccessErrorCode::InternalError),
               std::stoi(result.response_context.at("error_code")),
               "missing secret fail-closed should map to InternalError");
}

void accepted_async_registers_receipt_when_secret_manager_is_available() {
  using dasall::infra::secret::MockSecretBackend;
  using dasall::infra::secret::SecretManagerFacade;

  auto backend = std::make_shared<MockSecretBackend>();
  backend->upsert_secret(make_secret_record());

  auto options = make_base_options();
  options.bootstrap_config.ownership_token_hmac_secret_ref =
      std::string("secret://access/receipt-hmac");
  options.ownership_secret_manager = std::make_shared<SecretManagerFacade>(backend);

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "secret manager test should create a daemon gateway");
  assert_true(gateway->init(),
              "secret manager test should initialize a daemon gateway");

  const auto result = gateway->submit(make_packet());
  assert_equal(static_cast<int>(AccessDisposition::AcceptedAsync),
               static_cast<int>(result.disposition),
               "materialized ownership secret should preserve accepted async disposition");
  assert_true(result.publish_envelope.has_value() &&
                  result.publish_envelope->receipt.has_value(),
              "materialized ownership secret should produce a receipt payload");
  assert_true(!result.publish_envelope->receipt->ownership_token.empty(),
              "materialized ownership secret should generate a non-empty ownership token");
  assert_equal(std::string("task:046-secret"),
               result.publish_envelope->receipt->receipt_id,
               "receipt id should preserve runtime receipt_ref as the stable query anchor");
}

}  // namespace

int main() {
  try {
    accepted_async_is_rejected_when_secret_is_missing();
    accepted_async_registers_receipt_when_secret_manager_is_available();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}