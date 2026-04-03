#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "audit/AuditService.h"
#include "audit/IAuditHealthProbe.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] std::string make_health_detail_ref(std::string_view suffix) {
  return std::string(dasall::infra::kAuditHealthDetailNamespace) + "/" +
         std::string(suffix);
}

dasall::infra::AuditEvent make_event(std::string ref_suffix) {
  const auto event_id = std::string("audit-event-") + ref_suffix;

  return dasall::infra::AuditEvent{
      .event_id = event_id,
      .action = std::string("diagnostics.export"),
      .actor = std::string("runtime"),
      .target = std::string("diag-bundle"),
      .outcome = dasall::infra::AuditOutcome::Succeeded,
      .evidence_ref = {.kind = dasall::infra::AuditEvidenceKind::ToolResult,
                       .ref = std::move(ref_suffix)},
      .side_effects = {"bundle_written"},
      .timestamp = 1712217600000,
  };
}

dasall::infra::AuditContext make_context() {
  return dasall::infra::AuditContext{};
}

class AuditServiceBackedHealthProbe final : public dasall::infra::audit::IAuditHealthProbe {
 public:
  AuditServiceBackedHealthProbe(const dasall::infra::audit::AuditService& service,
                                std::int64_t sampled_at_unix_ms,
                                bool metrics_bridge_degraded = false)
      : service_(service),
        sampled_at_unix_ms_(sampled_at_unix_ms),
        metrics_bridge_degraded_(metrics_bridge_degraded) {}

  [[nodiscard]] dasall::infra::AuditHealthStatus evaluate() const override {
    const auto lifecycle_state = service_.lifecycle_state_name();
    if (lifecycle_state != "started") {
      const bool stopped = lifecycle_state == "stopped";
      return dasall::infra::AuditHealthStatus{
          .state = dasall::infra::AuditHealthState::Unavailable,
          .last_failure_reason =
              std::string(stopped ? "service_stopped" : "service_not_started"),
          .detail_ref = make_health_detail_ref(
              stopped ? "unavailable/service_stopped"
                      : "unavailable/service_not_started"),
          .error_code = dasall::contracts::ResultCode::RuntimeRetryExhausted,
          .sampled_at_unix_ms = sampled_at_unix_ms_,
          .fallback_active = false,
          .metrics_bridge_degraded = metrics_bridge_degraded_,
      };
    }

    if (service_.is_degraded()) {
      return dasall::infra::AuditHealthStatus{
          .state = dasall::infra::AuditHealthState::Degraded,
          .last_failure_reason = std::string("fallback_active"),
          .detail_ref = make_health_detail_ref("degraded/fallback_active"),
          .error_code = std::nullopt,
          .sampled_at_unix_ms = sampled_at_unix_ms_,
          .fallback_active = true,
          .metrics_bridge_degraded = metrics_bridge_degraded_,
      };
    }

    if (metrics_bridge_degraded_) {
      return dasall::infra::AuditHealthStatus{
          .state = dasall::infra::AuditHealthState::Degraded,
          .last_failure_reason = std::string("metrics_bridge_degraded"),
          .detail_ref = make_health_detail_ref("degraded/metrics_bridge"),
          .error_code = std::nullopt,
          .sampled_at_unix_ms = sampled_at_unix_ms_,
          .fallback_active = false,
          .metrics_bridge_degraded = true,
      };
    }

    return dasall::infra::AuditHealthStatus{
        .state = dasall::infra::AuditHealthState::Ready,
        .last_failure_reason = std::string(),
        .detail_ref = make_health_detail_ref("ready"),
        .error_code = std::nullopt,
        .sampled_at_unix_ms = sampled_at_unix_ms_,
        .fallback_active = false,
        .metrics_bridge_degraded = false,
    };
  }

 private:
  const dasall::infra::audit::AuditService& service_;
  std::int64_t sampled_at_unix_ms_ = 0;
  bool metrics_bridge_degraded_ = false;
};

void test_audit_health_integration_reports_ready_after_successful_write() {
  using dasall::infra::AuditHealthState;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  AuditService service;
  assert_true(service.init(AuditServiceConfig{.primary_capacity = 2, .fallback_capacity = 1}).ok,
              "audit health integration should initialize the service before evaluating health");
  assert_true(service.start().ok,
              "audit health integration should start the service before evaluating health");

  const auto write_result = service.write_audit(make_event("health-ready-001"), make_context());
  assert_true(write_result.is_success(),
              "audit health integration should keep the baseline write on the primary path");

  AuditServiceBackedHealthProbe probe(service, 1712217601000);
  const auto snapshot = probe.evaluate();
  assert_true(snapshot.has_consistent_state() && snapshot.state == AuditHealthState::Ready,
              "audit health integration should expose Ready after a successful primary-path write");
}

void test_audit_health_integration_reports_degraded_when_fallback_is_active() {
  using dasall::infra::AuditHealthState;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  AuditService service;
  assert_true(service.init(AuditServiceConfig{.primary_capacity = 1, .fallback_capacity = 1}).ok,
              "audit health integration should initialize before degraded-path validation");
  assert_true(service.start().ok,
              "audit health integration should start before degraded-path validation");

  assert_true(service.write_audit(make_event("health-degraded-001"), make_context()).is_success(),
              "audit health integration should fill the primary slot before fallback is needed");
  const auto degraded_write = service.write_audit(make_event("health-degraded-002"), make_context());
  assert_true(degraded_write.is_degraded_success(),
              "audit health integration should route the second event through fallback when primary capacity is exhausted");

  AuditServiceBackedHealthProbe probe(service, 1712217602000);
  const auto snapshot = probe.evaluate();
  assert_true(snapshot.has_consistent_state() && snapshot.state == AuditHealthState::Degraded,
              "audit health integration should expose Degraded when fallback stays active");
  assert_true(snapshot.fallback_active && snapshot.last_failure_reason == "fallback_active",
              "audit health integration should surface fallback_active as the degraded reason");
}

void test_audit_health_integration_reports_metrics_bridge_degraded_without_promoting_unavailable() {
  using dasall::infra::AuditHealthState;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  AuditService service;
  assert_true(service.init(AuditServiceConfig{.primary_capacity = 1, .fallback_capacity = 1}).ok,
              "audit health integration should initialize before metrics degraded validation");
  assert_true(service.start().ok,
              "audit health integration should start before metrics degraded validation");

  AuditServiceBackedHealthProbe probe(service, 1712217603000, true);
  const auto snapshot = probe.evaluate();
  assert_true(snapshot.has_consistent_state() && snapshot.state == AuditHealthState::Degraded,
              "audit health integration should keep metrics bridge degradation at the Degraded state");
  assert_true(snapshot.metrics_bridge_degraded && !snapshot.error_code.has_value(),
              "audit health integration should expose metrics bridge degradation without forcing an unavailable error code");
}

void test_audit_health_integration_reports_unavailable_when_service_is_stopped() {
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditHealthState;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  AuditService service;
  assert_true(service.init(AuditServiceConfig{.primary_capacity = 1, .fallback_capacity = 1}).ok,
              "audit health integration should initialize before stopped-state validation");
  assert_true(service.start().ok,
              "audit health integration should start before stopped-state validation");
  assert_true(service.stop().ok,
              "audit health integration should stop cleanly before unavailable-state validation");

  AuditServiceBackedHealthProbe probe(service, 1712217604000);
  const auto snapshot = probe.evaluate();
  assert_true(snapshot.has_consistent_state() && snapshot.state == AuditHealthState::Unavailable,
              "audit health integration should expose Unavailable after the service has stopped");
  assert_true(snapshot.error_code == ResultCode::RuntimeRetryExhausted &&
                  snapshot.last_failure_reason == "service_stopped",
              "audit health integration should keep stopped-state failures observable through the frozen unavailable semantics");
}

}  // namespace

int main() {
  try {
    test_audit_health_integration_reports_ready_after_successful_write();
    test_audit_health_integration_reports_degraded_when_fallback_is_active();
    test_audit_health_integration_reports_metrics_bridge_degraded_without_promoting_unavailable();
    test_audit_health_integration_reports_unavailable_when_service_is_stopped();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
