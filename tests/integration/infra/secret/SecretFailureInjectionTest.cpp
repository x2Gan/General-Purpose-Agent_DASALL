#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "audit/IAuditLogger.h"
#include "secret/SecretAuditBridge.h"
#include "secret/SecretManagerFacade.h"
#include "secret/backends/MockSecretBackend.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class FailingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    if (!event.has_required_fields() || !event.side_effects_are_serializable() ||
        !context.has_non_empty_fields()) {
      return dasall::infra::AuditWriteOutcome{
          .accepted = false,
          .persisted = false,
          .fallback_used = false,
          .error_code = dasall::contracts::ResultCode::ValidationFieldMissing,
      };
    }

    return dasall::infra::AuditWriteOutcome{
        .accepted = false,
        .persisted = false,
        .fallback_used = false,
        .error_code = dasall::contracts::ResultCode::RuntimeRetryExhausted,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery& query) override {
    static_cast<void>(query);
    return dasall::infra::ExportResult{};
  }
};

[[nodiscard]] dasall::infra::secret::MockSecretRecord make_secret_record() {
  using dasall::infra::secret::MockSecretRecord;
  using dasall::infra::secret::SecretBackendType;
  using dasall::infra::secret::SecretClassification;

  return MockSecretRecord{
      .record = {
          .descriptor = {
              .secret_name = std::string("db/root"),
              .backend_type = SecretBackendType::Mock,
              .classification = SecretClassification::Credential,
              .rotation_policy_ref = std::string("rotation/default"),
              .owner_ref = std::string("ops"),
          },
          .backend_ref = std::string("mock.primary"),
          .version = std::string("v3"),
          .cipher_ref = std::string("cipher://mock/db/root/v3"),
          .encrypted_at_rest = true,
      },
      .materialized_text = std::string("root-password"),
      .allowed_permission_domains = {std::string("secret.read")},
  };
}

[[nodiscard]] dasall::infra::secret::SecretQuery make_query() {
  using dasall::infra::secret::SecretAccessMode;
  using dasall::infra::secret::SecretQuery;

  return SecretQuery{
      .secret_name = std::string("db/root"),
      .version_hint = std::string("v3"),
      .purpose = std::string("runtime_bootstrap"),
      .access_mode = SecretAccessMode::Materialize,
  };
}

[[nodiscard]] dasall::infra::secret::SecretAccessContext make_access_context() {
  using dasall::infra::secret::SecretAccessContext;

  return SecretAccessContext{
      .request_id = std::string("req-failure-001"),
      .session_id = std::nullopt,
      .task_id = std::string("task-failure-001"),
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::string("secret.read"),
  };
}

[[nodiscard]] dasall::infra::secret::SecretAuditEvent make_audit_event() {
  using dasall::infra::secret::SecretAuditAction;
  using dasall::infra::secret::SecretAuditEvent;

  return SecretAuditEvent{
      .actor = std::string("runtime"),
      .action = SecretAuditAction::Rotated,
      .target_secret = std::string("db/root"),
      .consumer_module = std::string("runtime"),
      .outcome = true,
      .reason_code = std::string("scheduled_rotation"),
      .version = std::string("v4"),
      .evidence_ref = std::string("tool-call-rotation-001"),
      .request_id = std::string("req-audit-001"),
      .task_id = std::string("task-audit-001"),
  };
}

void test_secret_failure_injection_surfaces_backend_unavailable_through_manager() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::MockSecretBackend;
  using dasall::infra::secret::SecretManagerFacade;
  using dasall::tests::support::assert_true;

  auto backend = std::make_shared<MockSecretBackend>();
  backend->upsert_secret(make_secret_record());
  backend->set_available(false);

  SecretManagerFacade manager(backend);
  const auto handle_result = manager.get_secret(make_query(), make_access_context());

  assert_true(!handle_result.ok && handle_result.is_valid() &&
                  handle_result.result_code == ResultCode::ProviderTimeout,
              "SecretFailureInjectionTest should surface backend unavailable as a provider-category failure through SecretManagerFacade.get_secret");
}

void test_secret_failure_injection_surfaces_audit_sink_failures_through_bridge() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::SecretAuditBridge;
  using dasall::tests::support::assert_true;

  SecretAuditBridge bridge(std::make_shared<FailingAuditLogger>());
  const auto result = bridge.emit_event(make_audit_event());

  assert_true(!result.emitted && result.is_valid() &&
                  result.result_code == ResultCode::ToolExecutionFailed,
              "SecretFailureInjectionTest should map failing audit sink outcomes to the frozen secret audit failure domain instead of silently succeeding");
  assert_true(result.error_info.has_value() &&
                  result.error_info->details.message.find("INF_E_SECRET_AUDIT_WRITE_FAIL") != std::string::npos,
              "SecretFailureInjectionTest should preserve the secret audit failure code inside the bridge error payload");
}

}  // namespace

int main() {
  try {
    test_secret_failure_injection_surfaces_backend_unavailable_through_manager();
    test_secret_failure_injection_surfaces_audit_sink_failures_through_bridge();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}