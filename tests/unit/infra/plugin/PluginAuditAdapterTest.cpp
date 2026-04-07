#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

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
      .request_id = std::string("req-plugin-001"),
      .trace_id = std::string("trace-plugin-001"),
      .task_id = std::string("task-plugin-001"),
  };
}

dasall::infra::plugin::PluginAuditRecord make_policy_deny_record() {
  auto record = make_record(std::string("plugin_policy_denied"), false);
  record.result_code = dasall::contracts::ResultCode::PolicyDenied;
  return record;
}

dasall::infra::plugin::PluginAuditRecord make_validation_failure_record(
    std::string reason_code) {
  auto record = make_record(std::move(reason_code), false);
  record.result_code = dasall::contracts::ResultCode::ValidationFieldMissing;
  return record;
}

class ScriptedAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);

    if (!scripted_outcomes.empty()) {
      const auto outcome = scripted_outcomes.front();
      scripted_outcomes.pop_front();
      return outcome;
    }

    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  std::deque<dasall::infra::AuditWriteOutcome> scripted_outcomes;
  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

void test_plugin_audit_adapter_emits_load_unload_policy_and_validation_failure_events() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::plugin::PluginAuditAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  PluginAuditAdapter adapter(logger);

  const auto load_result = adapter.write_load_audit(
      make_record(std::string("plugin_load_succeeded"), true));
  const auto unload_result = adapter.write_unload_audit(
      make_record(std::string("plugin_unload_succeeded"), true));
  const auto deny_result = adapter.write_policy_deny_audit(make_policy_deny_record());
    const auto signature_fail_result = adapter.write_signature_fail_audit(
      make_validation_failure_record(std::string("plugin_signature_failed")));
    const auto compatibility_fail_result = adapter.write_compatibility_fail_audit(
      make_validation_failure_record(std::string("plugin_abi_incompatible")));
  const auto status = adapter.get_status();

  assert_true(load_result.emitted && load_result.is_valid(),
              "PluginAuditAdapter should emit a valid audit payload for plugin.load");
  assert_true(unload_result.emitted && unload_result.is_valid(),
              "PluginAuditAdapter should emit a valid audit payload for plugin.unload");
  assert_true(deny_result.emitted && deny_result.is_valid(),
              "PluginAuditAdapter should emit a valid audit payload for plugin.policy_deny");
  assert_true(signature_fail_result.emitted && signature_fail_result.is_valid(),
              "PluginAuditAdapter should emit a valid audit payload for plugin.signature_fail");
  assert_true(compatibility_fail_result.emitted && compatibility_fail_result.is_valid(),
              "PluginAuditAdapter should emit a valid audit payload for plugin.compatibility_fail");
  assert_true(status.is_valid() && status.emitted_total == 5 && !status.degraded,
              "PluginAuditAdapter should keep a healthy status after successful high-risk plugin emissions");
  assert_equal(5, static_cast<int>(logger->events.size()),
               "PluginAuditAdapter should dispatch one AuditEvent per high-risk plugin action");

  const auto& load_event = logger->events[0];
  const auto& unload_event = logger->events[1];
  const auto& deny_event = logger->events[2];
  const auto& signature_fail_event = logger->events[3];
  const auto& compatibility_fail_event = logger->events[4];
  const auto& compatibility_fail_context = logger->contexts[4];

  assert_equal(std::string("plugin.load"), load_event.action,
               "PluginAuditAdapter should map load emissions to the frozen plugin.load action");
  assert_equal(std::string("plugin.unload"), unload_event.action,
               "PluginAuditAdapter should map unload emissions to the frozen plugin.unload action");
  assert_equal(std::string("plugin.policy_deny"), deny_event.action,
               "PluginAuditAdapter should map policy denials to the frozen plugin.policy_deny action");
    assert_equal(std::string("plugin.signature_fail"), signature_fail_event.action,
           "PluginAuditAdapter should map signature failures to the frozen plugin.signature_fail action");
    assert_equal(std::string("plugin.compatibility_fail"), compatibility_fail_event.action,
           "PluginAuditAdapter should map compatibility failures to the frozen plugin.compatibility_fail action");
  assert_equal(std::string("plugin:plugin.echo"), deny_event.target,
               "PluginAuditAdapter should encode plugin ids inside the frozen plugin: audit target namespace");
  assert_true(load_event.outcome == AuditOutcome::Succeeded &&
                  unload_event.outcome == AuditOutcome::Succeeded &&
            deny_event.outcome == AuditOutcome::Rejected &&
            signature_fail_event.outcome == AuditOutcome::Rejected &&
            compatibility_fail_event.outcome == AuditOutcome::Rejected,
          "PluginAuditAdapter should map high-risk plugin actions to stable audit outcomes");
  assert_true(has_side_effect(load_event, "reason_code:plugin_load_succeeded") &&
                  has_side_effect(unload_event, "reason_code:plugin_unload_succeeded") &&
                  has_side_effect(deny_event, "reason_code:plugin_policy_denied") &&
            has_side_effect(deny_event, "result_code:PolicyDenied") &&
            has_side_effect(signature_fail_event,
                    "reason_code:plugin_signature_failed") &&
            has_side_effect(signature_fail_event,
                    "result_code:ValidationFieldMissing") &&
            has_side_effect(compatibility_fail_event,
                    "reason_code:plugin_abi_incompatible") &&
            has_side_effect(compatibility_fail_event,
                    "result_code:ValidationFieldMissing"),
              "PluginAuditAdapter should serialize the frozen plugin audit reason_code and optional result_code facts");
    assert_true(compatibility_fail_context.request_id == "req-plugin-001" &&
            compatibility_fail_context.trace_id == "trace-plugin-001" &&
            compatibility_fail_context.task_id == "task-plugin-001" &&
            compatibility_fail_context.worker_type == "plugin",
              "PluginAuditAdapter should project request, trace, task, and worker_type into AuditContext");
}

void test_plugin_audit_adapter_rejects_invalid_records_before_emit() {
  using dasall::contracts::ResultCode;
  using dasall::infra::plugin::PluginAuditAdapter;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  PluginAuditAdapter adapter(logger);

  auto invalid_record = make_record(std::string("plugin_load_succeeded"), true);
  invalid_record.plugin_id.clear();

  const auto result = adapter.write_load_audit(std::move(invalid_record));
  const auto status = adapter.get_status();

  assert_true(!result.emitted && result.is_valid() &&
                  result.result_code == ResultCode::ValidationFieldMissing,
              "PluginAuditAdapter should reject invalid records before they reach the audit sink");
  assert_true(status.is_valid() && status.degraded && status.emit_failures == 1,
              "PluginAuditAdapter should expose degraded status after invalid input is rejected");
}

void test_plugin_audit_adapter_requires_audit_logger_for_high_risk_actions() {
  using dasall::contracts::ResultCode;
  using dasall::infra::plugin::PluginAuditAdapter;
  using dasall::tests::support::assert_true;

  PluginAuditAdapter adapter;

  const auto result = adapter.write_policy_deny_audit(make_policy_deny_record());
  const auto status = adapter.get_status();

  assert_true(!result.emitted && result.is_valid() &&
                  result.result_code == ResultCode::RuntimeRetryExhausted,
              "PluginAuditAdapter should fail closed when the required audit sink is unavailable");
  assert_true(result.error_info.has_value() &&
                  result.error_info->details.message.find("audit::IAuditLogger") != std::string::npos,
              "PluginAuditAdapter should surface the missing audit sink in the returned error payload");
  assert_true(status.is_valid() && status.degraded && status.emit_failures == 1,
              "PluginAuditAdapter should record missing audit logger as an explicit emit failure");
}

}  // namespace

int main() {
  try {
    test_plugin_audit_adapter_emits_load_unload_policy_and_validation_failure_events();
    test_plugin_audit_adapter_rejects_invalid_records_before_emit();
    test_plugin_audit_adapter_requires_audit_logger_for_high_risk_actions();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}