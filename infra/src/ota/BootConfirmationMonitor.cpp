#include "ota/BootConfirmationMonitor.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "InfraErrorCode.h"

namespace dasall::infra::ota {
namespace {

constexpr std::string_view kBootConfirmationMonitorSourceRef =
    "BootConfirmationMonitor";

[[nodiscard]] std::string normalized_or(std::string value, std::string_view fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] BootSuccessSignal placeholder_signal(std::string detail_ref) {
  return BootSuccessSignal{
      .signal_received = false,
      .self_check_ok = false,
      .detail_ref = std::move(detail_ref),
      .observed_ts = 0,
  };
}

[[nodiscard]] std::string status_detail_ref(const BootConfirmationMonitorOptions& options,
                                            std::string_view suffix) {
  return normalized_or(options.detail_ref_prefix, "status://ota/confirm/") +
         std::string(suffix);
}

[[nodiscard]] bool contains_any_component(
    const HealthSnapshot::ComponentList& current_components,
    const std::vector<std::string>& required_components) {
  for (const auto& component : required_components) {
    if (std::find(current_components.begin(), current_components.end(), component) !=
        current_components.end()) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool same_version_set(std::vector<std::string> lhs,
                                    std::vector<std::string> rhs) {
  if (!has_unique_non_empty_values(lhs) || !has_unique_non_empty_values(rhs) ||
      lhs.size() != rhs.size()) {
    return false;
  }

  std::sort(lhs.begin(), lhs.end());
  std::sort(rhs.begin(), rhs.end());
  return lhs == rhs;
}

[[nodiscard]] bool health_gate_satisfied(const HealthSnapshot& snapshot,
                                         const BootConfirmationPolicySnapshot& policy) {
  if (!snapshot.has_consistent_state() || !snapshot.has_version_metadata()) {
    return false;
  }

  if (!snapshot.liveness || !snapshot.readiness) {
    return false;
  }

  if (snapshot.degraded &&
      contains_any_component(snapshot.failed_components,
                             policy.health_blocking_components)) {
    return false;
  }

  return true;
}

}  // namespace

BootConfirmationMonitor::BootConfirmationMonitor(
    Dependencies dependencies,
    BootConfirmationMonitorOptions options)
    : dependencies_(dependencies),
      options_(std::move(options)),
      last_detail_ref_(status_detail_ref(options_, "idle")) {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "status://ota/confirm/");
}

BootConfirmationSelfCheck BootConfirmationMonitor::evaluate_self_check(
    const BootConfirmationRequest& request) const {
  const std::string fallback_detail_ref =
      status_detail_ref(options_, "self_check/pending");

  if (!request.is_valid()) {
    return BootConfirmationSelfCheck::failure(
        placeholder_signal(fallback_detail_ref),
        contracts::ResultCode::ValidationFieldMissing,
        "boot confirmation requires a valid request with plan, package, slot plan, rollback token, and frozen policy",
        "ota.boot.confirm.evaluate_self_check",
        std::string(kBootConfirmationMonitorSourceRef),
        fallback_detail_ref);
  }

  if (dependencies_.success_signal_provider == nullptr) {
    const std::string detail_ref =
        status_detail_ref(options_, "self_check/dependency_missing");
    return BootConfirmationSelfCheck::failure(
        placeholder_signal(detail_ref),
        contracts::ResultCode::RuntimeRetryExhausted,
        "boot confirmation monitor requires a success signal provider dependency",
        "ota.boot.confirm.evaluate_self_check",
        std::string(kBootConfirmationMonitorSourceRef),
        detail_ref);
  }

  const auto signal = dependencies_.success_signal_provider->read_success_signal(
      request.plan_id,
      request.slot_plan.target_slot);
  if (!signal.is_valid()) {
    const std::string detail_ref =
        status_detail_ref(options_, "self_check/invalid_signal");
    return BootConfirmationSelfCheck::failure(
        placeholder_signal(detail_ref),
        contracts::ResultCode::ValidationFieldMissing,
        "boot success signal provider must emit a valid explicit self-check payload",
        "ota.boot.confirm.evaluate_self_check",
        std::string(kBootConfirmationMonitorSourceRef),
        detail_ref);
  }

  if (!signal.signal_received) {
    return BootConfirmationSelfCheck::pending(signal, signal.detail_ref);
  }

  if (!signal.self_check_ok) {
    return BootConfirmationSelfCheck::failure(
        signal,
        contracts::ResultCode::PolicyDenied,
        "explicit self-check failure must block mark_boot_success and enter confirm failure path",
        "ota.boot.confirm.evaluate_self_check",
        std::string(kBootConfirmationMonitorSourceRef),
        signal.detail_ref);
  }

  return BootConfirmationSelfCheck::success(signal, signal.detail_ref);
}

BootConfirmationResult BootConfirmationMonitor::await_confirm(
    const BootConfirmationRequest& request) {
  if (!request.is_valid()) {
    return fail_without_mutation(BootConfirmationState::Failed,
                                 contracts::ResultCode::ValidationFieldMissing,
                                 "boot confirmation requires a valid request before awaiting success criteria",
                                 "ota.boot.confirm.await",
                                 status_detail_ref(options_, "invalid_request"),
                                 false);
  }

  if (dependencies_.boot_control_adapter == nullptr ||
      dependencies_.health_monitor == nullptr ||
      dependencies_.heartbeat_freshness_source == nullptr ||
      dependencies_.version_report_source == nullptr) {
    return fail_without_mutation(BootConfirmationState::Failed,
                                 contracts::ResultCode::RuntimeRetryExhausted,
                                 "boot confirmation monitor requires boot, health, heartbeat, and version-report dependencies",
                                 "ota.boot.confirm.await",
                                 status_detail_ref(options_, "dependency_missing"),
                                 false);
  }

  const auto active_target = dependencies_.boot_control_adapter->get_active_target();
  if (!active_target.resolved || !active_target.references_only_contract_error_types()) {
    return fail_without_mutation(
        BootConfirmationState::Failed,
        active_target.result_code,
        active_target.error.has_value()
            ? active_target.error->details.message
            : "boot control adapter could not resolve the current active target during confirm",
        "ota.boot.confirm.await",
        status_detail_ref(options_, "active_target_query_failed"),
        false);
  }

  if (active_target.active_target != request.slot_plan.target_slot) {
    return mark_boot_failed_and_fail(
        request,
        BootConfirmationState::Failed,
        contracts::ResultCode::PolicyDenied,
        "boot confirm requires the rebooted active target to match SlotPlan.target_slot",
        "ota.boot.confirm.await",
        status_detail_ref(options_, "active_target_mismatch"));
  }

  if (!request.policy.rollback_token_armed) {
    return mark_boot_failed_and_fail(
        request,
        BootConfirmationState::Failed,
        contracts::ResultCode::PolicyDenied,
        "boot confirm requires an armed rollback token before mark_boot_success can run",
        "ota.boot.confirm.await",
        status_detail_ref(options_, "rollback_token_not_armed"));
  }

  const auto self_check = evaluate_self_check(request);
  if (!self_check.is_valid()) {
    return fail_without_mutation(BootConfirmationState::Failed,
                                 contracts::ResultCode::RuntimeRetryExhausted,
                                 "boot confirmation self-check evaluation produced an invalid internal result",
                                 "ota.boot.confirm.await",
                                 status_detail_ref(options_, "invalid_self_check_result"),
                                 false);
  }

  if (self_check.terminal_failure) {
    return mark_boot_failed_and_fail(request,
                                     BootConfirmationState::Failed,
                                     *self_check.result_code,
                                     self_check.error->details.message,
                                     "ota.boot.confirm.await",
                                     self_check.detail_ref);
  }

  if (!self_check.success_signal_present) {
    record_pending(self_check.detail_ref);
    return BootConfirmationResult::pending(self_check.detail_ref);
  }

  const auto health_result = dependencies_.health_monitor->get_snapshot();
  if (!health_result.ok || !health_result.references_only_contract_error_types()) {
    return fail_without_mutation(
        BootConfirmationState::Failed,
        health_result.result_code.value_or(contracts::ResultCode::RuntimeRetryExhausted),
        health_result.error.has_value()
            ? health_result.error->details.message
            : "health monitor could not provide a contract-shaped snapshot for boot confirmation",
        "ota.boot.confirm.await",
        status_detail_ref(options_, "health_snapshot_query_failed"),
        false);
  }

  if (!health_gate_satisfied(health_result.snapshot, request.policy)) {
    const std::string detail_ref =
        status_detail_ref(options_, "pending/await_health");
    record_pending(detail_ref);
    return BootConfirmationResult::pending(detail_ref);
  }

  const auto heartbeat_report = dependencies_.heartbeat_freshness_source->evaluate_freshness(
      request.policy.required_heartbeat_entities);
  if (!heartbeat_report.is_valid()) {
    return fail_without_mutation(BootConfirmationState::Failed,
                                 contracts::ResultCode::ValidationFieldMissing,
                                 "heartbeat freshness source must emit a valid confirm-critical report",
                                 "ota.boot.confirm.await",
                                 status_detail_ref(options_, "heartbeat_report_invalid"),
                                 false);
  }

  if (heartbeat_report.watchdog_reset_detected || !heartbeat_report.all_fresh) {
    return mark_boot_failed_and_fail(
        request,
        BootConfirmationState::Failed,
        contracts::ResultCode::PolicyDenied,
        heartbeat_report.watchdog_reset_detected
            ? "watchdog reset must immediately fail boot confirmation"
            : "required heartbeat freshness must immediately fail boot confirmation when any confirm-critical entity is stale",
        "ota.boot.confirm.await",
        heartbeat_report.detail_ref);
  }

  const auto version_report = dependencies_.version_report_source->read_version_report(
      request.slot_plan.target_slot);
  if (!version_report.is_valid()) {
    return fail_without_mutation(BootConfirmationState::Failed,
                                 contracts::ResultCode::ValidationFieldMissing,
                                 "version report source must emit a valid slot-bound version snapshot",
                                 "ota.boot.confirm.await",
                                 status_detail_ref(options_, "version_report_invalid"),
                                 false);
  }

  if (version_report.package_id != request.package_id ||
      !same_version_set(version_report.slot_bound_versions,
                        request.policy.expected_slot_versions)) {
    return mark_boot_failed_and_fail(
        request,
        BootConfirmationState::Failed,
        contracts::ResultCode::PolicyDenied,
        "slot-bound version report must match the current package and frozen expected version set before mark_boot_success",
        "ota.boot.confirm.await",
        version_report.detail_ref);
  }

  const auto mutation = dependencies_.boot_control_adapter->mark_boot_success(
      request.slot_plan.target_slot);
  if (!mutation.applied || !mutation.references_only_contract_error_types()) {
    return fail_without_mutation(
        BootConfirmationState::Failed,
        mutation.result_code,
        mutation.error.has_value()
            ? mutation.error->details.message
            : "boot control adapter rejected mark_boot_success during confirm completion",
        "ota.boot.confirm.await",
        status_detail_ref(options_, "mark_boot_success_failed"),
        false);
  }

  const std::string success_detail_ref = status_detail_ref(options_, "success");
  record_success(success_detail_ref);
  return BootConfirmationResult::success(mutation, success_detail_ref);
}

BootConfirmationResult BootConfirmationMonitor::handle_timeout(
    const BootConfirmationRequest& request) {
  if (!request.is_valid()) {
    return fail_without_mutation(BootConfirmationState::TimedOut,
                                 contracts::ResultCode::ValidationFieldMissing,
                                 "boot confirmation timeout handling requires a valid request",
                                 "ota.boot.confirm.handle_timeout",
                                 status_detail_ref(options_, "timeout/invalid_request"),
                                 false);
  }

  if (dependencies_.boot_control_adapter == nullptr) {
    return fail_without_mutation(BootConfirmationState::TimedOut,
                                 contracts::ResultCode::RuntimeRetryExhausted,
                                 "boot confirmation timeout handling requires a boot control adapter dependency",
                                 "ota.boot.confirm.handle_timeout",
                                 status_detail_ref(options_, "timeout/dependency_missing"),
                                 false);
  }

  const auto timeout_mapping =
      map_infra_error_code(InfraErrorCode::OTABootConfirmTimeout);
  const auto mutation = dependencies_.boot_control_adapter->mark_boot_failed(
      request.slot_plan.target_slot);
  if (!mutation.applied || !mutation.references_only_contract_error_types()) {
    return fail_without_mutation(BootConfirmationState::TimedOut,
                                 mutation.result_code,
                                 mutation.error.has_value()
                                     ? mutation.error->details.message
                                     : "boot control adapter rejected mark_boot_failed during confirm timeout handling",
                                 "ota.boot.confirm.handle_timeout",
                                 status_detail_ref(options_, "timeout/mark_boot_failed_failed"),
                                 false);
  }

  const std::string detail_ref = status_detail_ref(options_, "timeout");
  record_timeout(timeout_mapping.result_code, detail_ref);
  return BootConfirmationResult::failure(
      BootConfirmationState::TimedOut,
      timeout_mapping.result_code,
      std::string(infra_error_code_name(InfraErrorCode::OTABootConfirmTimeout)) +
          ": confirm window expired before explicit success criteria were satisfied",
      "ota.boot.confirm.handle_timeout",
      std::string(kBootConfirmationMonitorSourceRef),
      detail_ref,
      request.policy.auto_rollback_on_failure,
      mutation);
}

BootConfirmationMonitorStatus BootConfirmationMonitor::get_status() const {
  return BootConfirmationMonitorStatus{
      .confirmed_total = confirmed_total_,
      .pending_total = pending_total_,
      .failed_total = failed_total_,
      .timed_out_total = timed_out_total_,
      .pending_confirm = pending_confirm_,
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? status_detail_ref(options_, "idle")
                                             : last_detail_ref_,
  };
}

BootConfirmationResult BootConfirmationMonitor::fail_without_mutation(
    BootConfirmationState state,
    contracts::ResultCode result_code,
    std::string message,
    std::string stage,
    std::string detail_ref,
    bool rollback_requested) {
  if (state == BootConfirmationState::TimedOut) {
    record_timeout(result_code, detail_ref);
  } else {
    record_failure(result_code, detail_ref);
  }

  return BootConfirmationResult::failure(state,
                                         result_code,
                                         std::move(message),
                                         std::move(stage),
                                         std::string(kBootConfirmationMonitorSourceRef),
                                         std::move(detail_ref),
                                         rollback_requested);
}

BootConfirmationResult BootConfirmationMonitor::mark_boot_failed_and_fail(
    const BootConfirmationRequest& request,
    BootConfirmationState state,
    contracts::ResultCode result_code,
    std::string message,
    std::string stage,
    std::string detail_ref) {
  const auto mutation = dependencies_.boot_control_adapter->mark_boot_failed(
      request.slot_plan.target_slot);
  if (!mutation.applied || !mutation.references_only_contract_error_types()) {
    return fail_without_mutation(state,
                                 mutation.result_code,
                                 mutation.error.has_value()
                                     ? mutation.error->details.message
                                     : "boot control adapter rejected mark_boot_failed during confirm failure handling",
                                 std::move(stage),
                                 status_detail_ref(options_, "mark_boot_failed_failed"),
                                 false);
  }

  record_failure(result_code, detail_ref);
  return BootConfirmationResult::failure(state,
                                         result_code,
                                         std::move(message),
                                         std::move(stage),
                                         std::string(kBootConfirmationMonitorSourceRef),
                                         std::move(detail_ref),
                                         request.policy.auto_rollback_on_failure,
                                         mutation);
}

void BootConfirmationMonitor::record_pending(std::string detail_ref) {
  ++pending_total_;
  pending_confirm_ = true;
  last_error_code_.reset();
  last_detail_ref_ = std::move(detail_ref);
}

void BootConfirmationMonitor::record_success(std::string detail_ref) {
  ++confirmed_total_;
  pending_confirm_ = false;
  last_error_code_.reset();
  last_detail_ref_ = std::move(detail_ref);
}

void BootConfirmationMonitor::record_failure(contracts::ResultCode result_code,
                                             std::string detail_ref) {
  ++failed_total_;
  pending_confirm_ = false;
  last_error_code_ = result_code;
  last_detail_ref_ = std::move(detail_ref);
}

void BootConfirmationMonitor::record_timeout(contracts::ResultCode result_code,
                                             std::string detail_ref) {
  ++timed_out_total_;
  pending_confirm_ = false;
  last_error_code_ = result_code;
  last_detail_ref_ = std::move(detail_ref);
}

}  // namespace dasall::infra::ota