#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "audit/AuditService.h"
#include "plugin/PluginAuditAdapter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

dasall::infra::plugin::PluginAuditRecord make_record(std::string reason_code,
                                                     bool succeeded = true) {
  return dasall::infra::plugin::PluginAuditRecord{
      .actor_ref = std::string("runtime"),
      .plugin_id = std::string("plugin.echo"),
      .succeeded = succeeded,
      .evidence_ref = std::string("evidence://plugin.echo"),
      .reason_code = std::move(reason_code),
      .result_code = std::nullopt,
      .request_id = std::string("req-plugin-int-001"),
      .trace_id = std::string("trace-plugin-int-001"),
      .task_id = std::string("task-plugin-int-001"),
  };
}

dasall::infra::plugin::PluginAuditRecord make_policy_deny_record() {
  auto record = make_record(std::string("plugin_policy_denied"), false);
  record.result_code = dasall::contracts::ResultCode::PolicyDenied;
  return record;
}

dasall::infra::ExportQuery make_export_query(std::string action = {}) {
  return dasall::infra::ExportQuery{
      .start_ts = 1,
      .end_ts = 4102444800000,
      .actor = std::string(),
      .action = std::move(action),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  };
}

void test_plugin_audit_trace_integration_persists_and_exports_high_risk_plugin_events() {
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::infra::plugin::PluginAuditAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto audit_service = std::make_shared<AuditService>();
  assert_true(audit_service->init(
                  AuditServiceConfig{.primary_capacity = 4, .fallback_capacity = 2})
                  .ok,
              "plugin audit integration should initialize AuditService before writing plugin audit events");
  assert_true(audit_service->start().ok,
              "plugin audit integration should start AuditService before writing plugin audit events");

  PluginAuditAdapter adapter(audit_service);

  const auto load_result = adapter.write_load_audit(
      make_record(std::string("plugin_load_succeeded"), true));
  const auto deny_result = adapter.write_policy_deny_audit(make_policy_deny_record());

  assert_true(load_result.emitted && deny_result.emitted,
              "plugin audit integration should emit both load and policy deny events through AuditService");
  assert_equal(2, static_cast<int>(audit_service->primary_record_count()),
               "plugin audit integration should retain emitted plugin audit events on the primary audit path");

  const auto export_all = audit_service->export_audit(make_export_query());
  assert_true(export_all.records.size() == 2 && export_all.has_checksum() &&
                  export_all.is_complete_page(),
              "plugin audit integration should export retained plugin audit events as a complete audit page");

  const auto export_policy_deny =
      audit_service->export_audit(make_export_query(std::string("plugin.policy_deny")));
  assert_equal(1, static_cast<int>(export_policy_deny.records.size()),
               "plugin audit integration should filter exported audit events by the frozen plugin.policy_deny action");

  const auto& deny_event = export_policy_deny.records.front();
  assert_equal(std::string("plugin.policy_deny"), deny_event.action,
               "plugin audit integration should preserve the frozen policy deny action during export");
  assert_equal(std::string("plugin:plugin.echo"), deny_event.target,
               "plugin audit integration should preserve the frozen plugin audit target namespace during export");
  assert_true(deny_event.evidence_ref.ref == "evidence://plugin.echo" &&
                  has_side_effect(deny_event, "reason_code:plugin_policy_denied") &&
                  has_side_effect(deny_event, "result_code:PolicyDenied"),
              "plugin audit integration should export reason_code, result_code, and evidence_ref as traceable audit facts");
}

}  // namespace

int main() {
  try {
    test_plugin_audit_trace_integration_persists_and_exports_high_risk_plugin_events();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}