#include "ota/OTAPrecheckService.h"

#include <string>
#include <utility>

#include "InfraErrorCode.h"

namespace dasall::infra::ota {
namespace {

constexpr char kOTAPrecheckSourceRef[] = "OTAPrecheckService";

[[nodiscard]] contracts::ErrorInfo make_error_info(contracts::ResultCode result_code,
                                                   std::string message,
                                                   std::string stage,
                                                   bool retryable,
                                                   bool safe_to_replan = false) {
  return contracts::ErrorInfo{
      .failure_type = contracts::classify_result_code(result_code),
      .retryable = retryable,
      .safe_to_replan = safe_to_replan,
      .details = contracts::ErrorDetails{
          .code = static_cast<int>(result_code),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "infra.ota",
          .ref_id = kOTAPrecheckSourceRef,
      },
  };
}

void append_error(PrecheckReport& report,
                  contracts::ResultCode result_code,
                  std::string message,
                  std::string stage,
                  bool retryable,
                  bool safe_to_replan = false) {
  report.blocking_reasons.push_back(make_error_info(result_code,
                                                    std::move(message),
                                                    std::move(stage),
                                                    retryable,
                                                    safe_to_replan));
}

}  // namespace

OTAPrecheckService::OTAPrecheckService(Dependencies dependencies)
    : dependencies_(dependencies) {}

PrecheckReport OTAPrecheckService::precheck(const UpgradePlan& plan) const {
  PrecheckReport report{
      .health_ok = true,
      .resource_ok = true,
      .compatibility_ok = true,
      .policy_ok = true,
      .blocking_reasons = {},
  };

  if (!plan.is_valid()) {
    report.compatibility_ok = false;
    append_error(report,
                 contracts::ResultCode::ValidationFieldMissing,
                 "upgrade plan must stay fully specified before ota.precheck",
                 "ota.precheck.compatibility",
                 false);
  }

  bool policy_ready = false;
  OTAPrecheckPolicy policy{};
  if (dependencies_.policy_provider == nullptr) {
    report.policy_ok = false;
    const auto mapping = map_infra_error_code(InfraErrorCode::ConfigInvalid);
    append_error(report,
                 mapping.result_code,
                 "ota precheck requires a policy provider for threshold evaluation",
                 "ota.precheck.policy",
                 false);
  } else {
    policy = dependencies_.policy_provider->current_policy();
    if (!policy.is_valid()) {
      report.policy_ok = false;
      const auto mapping = map_infra_error_code(InfraErrorCode::ConfigInvalid);
      append_error(report,
                   mapping.result_code,
                   "ota precheck policy must keep cpu threshold within 0-100 range",
                   "ota.precheck.policy",
                   false);
    } else {
      policy_ready = true;
      if (!policy.enabled) {
        report.policy_ok = false;
        append_error(report,
                     contracts::ResultCode::PolicyDenied,
                     "ota apply is disabled for the current profile",
                     "ota.precheck.policy",
                     false);
      }

      if (!plan.validate_only && policy.mode != OTAMode::ApplyEnabled) {
        report.policy_ok = false;
        append_error(report,
                     contracts::ResultCode::PolicyDenied,
                     "ota apply requires infra.ota.mode=apply_enabled unless validate_only is set",
                     "ota.precheck.policy",
                     false);
      }

      if (policy.freeze_after_failures > 0 &&
          policy.consecutive_failures >= policy.freeze_after_failures) {
        report.policy_ok = false;
        append_error(report,
                     contracts::ResultCode::PolicyDenied,
                     "ota apply is frozen after repeated failures; only precheck and query_status remain available",
                     "ota.precheck.policy",
                     true);
      }
    }
  }

  if (dependencies_.health_provider == nullptr) {
    report.health_ok = false;
    const auto mapping = map_infra_error_code(InfraErrorCode::HealthProbeTimeout);
    append_error(report,
                 mapping.result_code,
                 "ota precheck requires health readiness input",
                 "ota.precheck.health",
                 true);
  } else {
    const auto health = dependencies_.health_provider->current_health();
    if (!health.is_valid()) {
      report.health_ok = false;
      const auto mapping = map_infra_error_code(InfraErrorCode::HealthProbeTimeout);
      append_error(report,
                   mapping.result_code,
                   "ota precheck health snapshot must keep failed component ids unique and non-empty",
                   "ota.precheck.health",
                   true);
    } else if (policy_ready && policy.require_health_ready && !health.ready) {
      report.health_ok = false;
      append_error(report,
                   contracts::ResultCode::PolicyDenied,
                   "health readiness gate failed before ota.apply",
                   "ota.precheck.health",
                   true);
    }
  }

  if (dependencies_.resource_probe == nullptr) {
    report.resource_ok = false;
    const auto mapping = map_infra_error_code(InfraErrorCode::HealthProbeTimeout);
    append_error(report,
                 mapping.result_code,
                 "ota precheck requires resource snapshot input",
                 "ota.precheck.resource",
                 true);
  } else {
    const auto resources = dependencies_.resource_probe->current_resources();
    if (!resources.is_valid()) {
      report.resource_ok = false;
      const auto mapping = map_infra_error_code(InfraErrorCode::HealthProbeTimeout);
      append_error(report,
                   mapping.result_code,
                   "ota precheck resource snapshot must keep cpu load within 0-100 range",
                   "ota.precheck.resource",
                   true);
    } else if (policy_ready) {
      if (resources.free_space_mb < policy.min_free_space_mb) {
        report.resource_ok = false;
        append_error(report,
                     contracts::ResultCode::PolicyDenied,
                     "free space fell below infra.ota.precheck.min_free_space_mb",
                     "ota.precheck.resource",
                     true);
      }

      if (resources.cpu_load_pct > policy.max_cpu_load_pct) {
        report.resource_ok = false;
        append_error(report,
                     contracts::ResultCode::PolicyDenied,
                     "cpu load exceeded infra.ota.precheck.max_cpu_load_pct",
                     "ota.precheck.resource",
                     true);
      }
    }
  }

  return report;
}

}  // namespace dasall::infra::ota