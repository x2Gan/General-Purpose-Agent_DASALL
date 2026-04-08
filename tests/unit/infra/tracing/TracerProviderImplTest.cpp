#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "audit/IAuditLogger.h"
#include "tracing/TraceErrors.h"
#include "tracing/TracerProviderImpl.h"
#include "support/TestAssertions.h"

namespace {

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
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

  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

[[nodiscard]] dasall::infra::InfraContext make_infra_context() {
  return dasall::infra::InfraContext{
      .request_id = std::string("req-tracer-provider-bridge"),
      .session_id = std::string("sess-tracer-provider-bridge"),
      .trace_id = std::string("trace-tracer-provider-bridge"),
      .task_id = std::string("task-tracer-provider-bridge"),
      .parent_task_id = std::string("parent-tracer-provider-bridge"),
      .lease_id = std::string("lease-tracer-provider-bridge"),
  };
}

[[nodiscard]] dasall::infra::tracing::TracerScope make_scope() {
  return dasall::infra::tracing::TracerScope{
      .name = std::string("infra.tracing"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };
}

[[nodiscard]] dasall::infra::tracing::TraceConfig make_config() {
  return dasall::infra::tracing::TraceConfig{
      .enabled = true,
      .provider_type = std::string("internal"),
      .force_flush_on_stop = true,
  };
}

void test_tracer_provider_impl_rejects_uninitialized_usage() {
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;

  const auto tracer = provider.get_tracer(make_scope());
  assert_true(!tracer,
              "TracerProviderImpl should reject get_tracer() before init() succeeds");

  const auto flush_result = provider.force_flush(100);
  assert_true(!flush_result.ok && flush_result.references_only_contract_error_types() &&
                  flush_result.result_code ==
                      map_trace_error_code(TraceErrorCode::ProviderNotReady).result_code,
              "TracerProviderImpl should expose provider-not-ready failures before init()");
  assert_true(provider.lifecycle_state_name() == "created",
              "TracerProviderImpl should remain in created state before init()");
}

void test_tracer_provider_impl_initializes_and_caches_tracer_scope() {
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;
  const auto init_result = provider.init(make_config());
  assert_true(init_result.ok && provider.lifecycle_state_name() == "initialized" &&
                  provider.last_config().has_value(),
              "TracerProviderImpl should accept the minimal internal TraceConfig and enter initialized state");

  const auto first_tracer = provider.get_tracer(make_scope());
  const auto second_tracer = provider.get_tracer(make_scope());
  assert_true(first_tracer && second_tracer && first_tracer == second_tracer &&
                  provider.tracer_count() == 1U && provider.last_scope().has_value(),
              "TracerProviderImpl should cache a single tracer instance per frozen TracerScope during the initialized lifecycle");

  const auto flush_result = provider.force_flush(250);
  assert_true(flush_result.ok,
              "TracerProviderImpl should expose a working force_flush() outlet after init()");
}

void test_tracer_provider_impl_surfaces_shutdown_timeout() {
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;
  assert_true(provider.init(make_config()).ok,
              "TracerProviderImpl should initialize before the shutdown timeout path is exercised");

  const auto timeout_result = provider.shutdown(0);
  assert_true(!timeout_result.ok && timeout_result.references_only_contract_error_types() &&
                  timeout_result.result_code ==
                      map_trace_error_code(TraceErrorCode::ShutdownTimeout).result_code &&
                  provider.lifecycle_state_name() == "initialized",
              "TracerProviderImpl should surface shutdown timeout observably without dropping initialized state");

  const auto shutdown_result = provider.shutdown(250);
  assert_true(shutdown_result.ok && provider.lifecycle_state_name() == "stopped" &&
                  provider.tracer_count() == 0U,
              "TracerProviderImpl should transition to stopped after a successful shutdown() and clear cached tracers");
}

void test_tracer_provider_impl_emits_sampler_and_shutdown_audit_events() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<RecordingAuditLogger>();
  TracerProviderImpl provider;
  provider.set_audit_logger(logger, make_infra_context());

  const auto init_result = provider.init(make_config());
  const auto shutdown_timeout = provider.shutdown(0);

  assert_true(init_result.ok && !shutdown_timeout.ok && logger->events.size() == 2U,
              "TracerProviderImpl should emit one sampler-change audit event at init and one shutdown fallback audit event when shutdown times out");

  const auto& sampler_event = logger->events.front();
  const auto& sampler_context = logger->contexts.front();
  assert_equal(std::string("tracing.sampler_changed"),
               sampler_event.action,
               "TracerProviderImpl should audit sampler configuration changes through TraceAuditBridge during init()");
  assert_true(sampler_event.outcome == AuditOutcome::Succeeded &&
                  has_side_effect(sampler_event,
                                  "current_sampler_type:parent_based_always_on") &&
                  has_side_effect(sampler_event,
                                  "previous_sampler_type:uninitialized") &&
                  sampler_context.request_id == "req-tracer-provider-bridge",
              "TracerProviderImpl sampler audit should preserve the frozen sampler facts and the configured provider correlation context");

  const auto& shutdown_event = logger->events.back();
  const auto& shutdown_context = logger->contexts.back();
  assert_equal(std::string("tracing.shutdown_force_fallback"),
               shutdown_event.action,
               "TracerProviderImpl should audit shutdown fallback paths through TraceAuditBridge");
  assert_true(shutdown_event.outcome == AuditOutcome::Failed &&
                  has_side_effect(shutdown_event, "error_code:TRC_E_SHUTDOWN_TIMEOUT") &&
                  shutdown_context.session_id == "sess-tracer-provider-bridge",
              "TracerProviderImpl shutdown fallback audit should keep the shutdown error token and reuse the configured provider correlation context");
}

}  // namespace

int main() {
  try {
    test_tracer_provider_impl_rejects_uninitialized_usage();
    test_tracer_provider_impl_initializes_and_caches_tracer_scope();
    test_tracer_provider_impl_surfaces_shutdown_timeout();
    test_tracer_provider_impl_emits_sampler_and_shutdown_audit_events();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}