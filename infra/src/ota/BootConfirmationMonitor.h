#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "health/IHealthMonitor.h"
#include "ota/IBootControlAdapter.h"

namespace dasall::infra::ota {

struct BootSuccessSignal {
  bool signal_received = false;
  bool self_check_ok = false;
  std::string detail_ref;
  std::int64_t observed_ts = 0;

  [[nodiscard]] bool is_valid() const {
    if (detail_ref.empty()) {
      return false;
    }

    if (self_check_ok && !signal_received) {
      return false;
    }

    if (signal_received && observed_ts <= 0) {
      return false;
    }

    return true;
  }
};

struct HeartbeatFreshnessReport {
  bool all_fresh = false;
  bool watchdog_reset_detected = false;
  std::vector<std::string> stale_entities;
  std::string detail_ref;

  [[nodiscard]] bool is_valid() const {
    if (detail_ref.empty()) {
      return false;
    }

    if (all_fresh) {
      return !watchdog_reset_detected && stale_entities.empty();
    }

    return watchdog_reset_detected || has_unique_non_empty_values(stale_entities);
  }
};

struct VersionReportSnapshot {
  std::string package_id;
  std::vector<std::string> slot_bound_versions;
  std::string detail_ref;
  std::uint64_t version = 0;
  std::int64_t observed_ts = 0;

  [[nodiscard]] bool is_valid() const {
    return !package_id.empty() && !slot_bound_versions.empty() &&
           has_unique_non_empty_values(slot_bound_versions) && !detail_ref.empty() &&
           version > 0 && observed_ts > 0;
  }
};

struct BootConfirmationPolicySnapshot {
  std::vector<std::string> health_blocking_components;
  std::vector<std::string> required_heartbeat_entities;
  std::vector<std::string> expected_slot_versions;
  bool rollback_token_armed = true;
  bool auto_rollback_on_failure = true;

  [[nodiscard]] bool is_valid() const {
    return !health_blocking_components.empty() &&
           has_unique_non_empty_values(health_blocking_components) &&
           !required_heartbeat_entities.empty() &&
           has_unique_non_empty_values(required_heartbeat_entities) &&
           !expected_slot_versions.empty() &&
           has_unique_non_empty_values(expected_slot_versions);
  }
};

struct BootConfirmationRequest {
  std::string plan_id;
  std::string package_id;
  SlotPlan slot_plan;
  RollbackToken rollback_token;
  BootConfirmationPolicySnapshot policy;

  [[nodiscard]] bool is_valid() const {
    return !plan_id.empty() && !package_id.empty() && slot_plan.is_valid() &&
           rollback_token.is_valid() &&
           rollback_token.previous_boot_target == slot_plan.active_slot &&
           policy.is_valid();
  }
};

enum class BootConfirmationState {
  Pending = 0,
  Confirmed = 1,
  Failed = 2,
  TimedOut = 3,
};

struct BootConfirmationSelfCheck {
  bool success_signal_present = false;
  bool self_check_ok = false;
  bool terminal_failure = false;
  BootSuccessSignal signal;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  std::string detail_ref;

  [[nodiscard]] static BootConfirmationSelfCheck pending(BootSuccessSignal signal,
                                                         std::string detail_ref) {
    return BootConfirmationSelfCheck{
        .success_signal_present = false,
        .self_check_ok = false,
        .terminal_failure = false,
        .signal = std::move(signal),
        .result_code = std::nullopt,
        .error = std::nullopt,
        .detail_ref = std::move(detail_ref),
    };
  }

  [[nodiscard]] static BootConfirmationSelfCheck success(BootSuccessSignal signal,
                                                         std::string detail_ref) {
    return BootConfirmationSelfCheck{
        .success_signal_present = true,
        .self_check_ok = true,
        .terminal_failure = false,
        .signal = std::move(signal),
        .result_code = std::nullopt,
        .error = std::nullopt,
        .detail_ref = std::move(detail_ref),
    };
  }

  [[nodiscard]] static BootConfirmationSelfCheck failure(BootSuccessSignal signal,
                                                         contracts::ResultCode result_code,
                                                         std::string message,
                                                         std::string stage,
                                                         std::string source_ref,
                                                         std::string detail_ref) {
    return BootConfirmationSelfCheck{
        .success_signal_present = signal.signal_received,
        .self_check_ok = false,
        .terminal_failure = true,
        .signal = std::move(signal),
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.ota",
                .ref_id = std::move(source_ref),
            },
        },
        .detail_ref = std::move(detail_ref),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return !result_code.has_value() && signal.is_valid() && !detail_ref.empty();
    }

    return result_code.has_value() && signal.is_valid() && !detail_ref.empty() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (terminal_failure) {
      return signal.is_valid() && result_code.has_value() && error.has_value() &&
             references_only_contract_error_types();
    }

    if (success_signal_present) {
      return self_check_ok && !result_code.has_value() && !error.has_value() &&
             signal.is_valid() && signal.signal_received && signal.self_check_ok &&
             !detail_ref.empty();
    }

    return !self_check_ok && !terminal_failure && !result_code.has_value() &&
           !error.has_value() && signal.is_valid() && !signal.signal_received &&
           !detail_ref.empty();
  }
};

struct BootConfirmationResult {
  BootConfirmationState state = BootConfirmationState::Pending;
  bool pending_confirm = false;
  bool rollback_requested = false;
  std::string detail_ref;
  std::optional<BootMutationResult> boot_mutation;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static BootConfirmationResult pending(std::string detail_ref) {
    return BootConfirmationResult{
        .state = BootConfirmationState::Pending,
        .pending_confirm = true,
        .rollback_requested = false,
        .detail_ref = std::move(detail_ref),
        .boot_mutation = std::nullopt,
        .result_code = std::nullopt,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static BootConfirmationResult success(BootMutationResult boot_mutation,
                                                      std::string detail_ref) {
    return BootConfirmationResult{
        .state = BootConfirmationState::Confirmed,
        .pending_confirm = false,
        .rollback_requested = false,
        .detail_ref = std::move(detail_ref),
        .boot_mutation = std::move(boot_mutation),
        .result_code = std::nullopt,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static BootConfirmationResult failure(BootConfirmationState state,
                                                      contracts::ResultCode result_code,
                                                      std::string message,
                                                      std::string stage,
                                                      std::string source_ref,
                                                      std::string detail_ref,
                                                      bool rollback_requested,
                                                      std::optional<BootMutationResult> boot_mutation = std::nullopt) {
    return BootConfirmationResult{
        .state = state,
        .pending_confirm = false,
        .rollback_requested = rollback_requested,
        .detail_ref = std::move(detail_ref),
        .boot_mutation = std::move(boot_mutation),
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.ota",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool terminal() const {
    return state == BootConfirmationState::Confirmed ||
           state == BootConfirmationState::Failed ||
           state == BootConfirmationState::TimedOut;
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return !result_code.has_value() && !detail_ref.empty();
    }

    return result_code.has_value() && !detail_ref.empty() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (state == BootConfirmationState::Pending) {
      return pending_confirm && !rollback_requested && !boot_mutation.has_value() &&
             !result_code.has_value() && !error.has_value() && !detail_ref.empty();
    }

    if (state == BootConfirmationState::Confirmed) {
      return terminal() && !pending_confirm && !rollback_requested &&
             !result_code.has_value() && !error.has_value() &&
             boot_mutation.has_value() && boot_mutation->applied &&
             boot_mutation->references_only_contract_error_types() &&
             boot_mutation->operation == "mark_boot_success" && !detail_ref.empty();
    }

    const bool mutation_is_contract_shaped = !boot_mutation.has_value() ||
                                             boot_mutation->references_only_contract_error_types();
    return terminal() && !pending_confirm && result_code.has_value() && error.has_value() &&
           references_only_contract_error_types() && mutation_is_contract_shaped;
  }
};

struct BootConfirmationMonitorStatus {
  std::uint64_t confirmed_total = 0;
  std::uint64_t pending_total = 0;
  std::uint64_t failed_total = 0;
  std::uint64_t timed_out_total = 0;
  bool pending_confirm = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref;

  [[nodiscard]] bool is_valid() const {
    if (detail_ref.empty()) {
      return false;
    }

    if (last_error_code.has_value() &&
        contracts::classify_result_code(*last_error_code) ==
            contracts::ResultCodeCategory::Unknown) {
      return false;
    }

    return true;
  }
};

struct BootConfirmationMonitorOptions {
  std::string detail_ref_prefix = "status://ota/confirm/";
};

class IBootSuccessSignalProvider {
 public:
  virtual ~IBootSuccessSignalProvider() = default;

  [[nodiscard]] virtual BootSuccessSignal read_success_signal(
      std::string_view plan_id,
      std::string_view target_slot) const = 0;
};

class IHeartbeatFreshnessSource {
 public:
  virtual ~IHeartbeatFreshnessSource() = default;

  [[nodiscard]] virtual HeartbeatFreshnessReport evaluate_freshness(
      const std::vector<std::string>& required_entities) const = 0;
};

class IVersionReportSource {
 public:
  virtual ~IVersionReportSource() = default;

  [[nodiscard]] virtual VersionReportSnapshot read_version_report(
      std::string_view target_slot) const = 0;
};

class BootConfirmationMonitor {
 public:
  struct Dependencies {
    IBootControlAdapter* boot_control_adapter = nullptr;
    const IHealthMonitor* health_monitor = nullptr;
    const IBootSuccessSignalProvider* success_signal_provider = nullptr;
    const IHeartbeatFreshnessSource* heartbeat_freshness_source = nullptr;
    const IVersionReportSource* version_report_source = nullptr;
  };

  explicit BootConfirmationMonitor(Dependencies dependencies,
                                   BootConfirmationMonitorOptions options = {});

  [[nodiscard]] BootConfirmationSelfCheck evaluate_self_check(
      const BootConfirmationRequest& request) const;
  [[nodiscard]] BootConfirmationResult await_confirm(
      const BootConfirmationRequest& request);
  [[nodiscard]] BootConfirmationResult handle_timeout(
      const BootConfirmationRequest& request);
  [[nodiscard]] BootConfirmationMonitorStatus get_status() const;

 private:
  [[nodiscard]] BootConfirmationResult fail_without_mutation(
      BootConfirmationState state,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string detail_ref,
      bool rollback_requested);

  [[nodiscard]] BootConfirmationResult mark_boot_failed_and_fail(
      const BootConfirmationRequest& request,
      BootConfirmationState state,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string detail_ref);

  void record_pending(std::string detail_ref);
  void record_success(std::string detail_ref);
  void record_failure(contracts::ResultCode result_code, std::string detail_ref);
  void record_timeout(contracts::ResultCode result_code, std::string detail_ref);

  Dependencies dependencies_;
  BootConfirmationMonitorOptions options_;
  std::uint64_t confirmed_total_ = 0;
  std::uint64_t pending_total_ = 0;
  std::uint64_t failed_total_ = 0;
  std::uint64_t timed_out_total_ = 0;
  bool pending_confirm_ = false;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_;
};

}  // namespace dasall::infra::ota