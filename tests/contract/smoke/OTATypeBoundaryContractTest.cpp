#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "ota/OTATypes.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "support/TestAssertions.h"

namespace {

void test_ota_types_only_reuse_contract_result_code_and_error_info_semantics() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::ota::OTAStatusSnapshot;
  using dasall::infra::ota::PrecheckReport;
  using dasall::infra::ota::UpgradeOutcome;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PrecheckReport{}.blocking_reasons),
                               std::vector<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(UpgradeOutcome{}.result_code),
                               std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(OTAStatusSnapshot{}.last_failure_code),
                               std::optional<ResultCode>>);

  const PrecheckReport precheck_report{
      .health_ok = false,
      .resource_ok = true,
      .compatibility_ok = true,
      .policy_ok = true,
      .blocking_reasons = {ErrorInfo{
          .failure_type = dasall::contracts::ResultCodeCategory::Validation,
          .retryable = false,
          .safe_to_replan = false,
          .details = dasall::contracts::ErrorDetails{
              .code = 1001,
              .message = std::string("disk budget unavailable"),
              .stage = std::string("ota.precheck"),
          },
          .source_ref = dasall::contracts::ErrorSourceRefMinimal{
              .ref_type = std::string("infra.ota"),
              .ref_id = std::string("OTAPrecheckService"),
          },
      }},
  };
  assert_true(precheck_report.uses_contract_error_types_only(),
              "OTA precheck should reuse only frozen contract ErrorInfo semantics for blocking reasons");

  const UpgradeOutcome failed_outcome{
      .phase = std::string("verify_failed"),
      .result_code = ResultCode::ValidationFieldMissing,
      .rollback_applied = false,
      .final_version_set = {},
      .evidence_ref = std::string("audit://ota/apply/fail-001"),
  };
  assert_true(failed_outcome.is_failure() &&
                  failed_outcome.references_only_contract_result_code(),
              "upgrade outcome should surface failure only through the frozen ResultCode contract segment");

  const OTAStatusSnapshot snapshot{
      .last_plan_id = std::string("ota-plan-001"),
      .state = std::string("degraded"),
      .active_slot = std::string("rootfs_a"),
      .pending_confirm = false,
      .last_failure_code = ResultCode::ProviderTimeout,
      .backlog_count = 2,
  };
  assert_true(snapshot.references_only_contract_result_code(),
              "status snapshot should keep last failure summary in the frozen ResultCode space only");
}

void test_ota_private_objects_keep_identifier_and_manifest_fields_in_infra_domain() {
  using dasall::infra::ota::ArtifactDescriptor;
  using dasall::infra::ota::InstallEvidence;
  using dasall::infra::ota::PackageDescriptor;
  using dasall::infra::ota::RollbackToken;
  using dasall::infra::ota::UpgradePlan;
  using dasall::infra::ota::UpgradeRequester;
  using dasall::infra::ota::VerifiedPackageManifest;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(UpgradePlan{}.plan_id), std::string>);
  static_assert(std::is_same_v<decltype(UpgradeRequester{}.actor_ref), std::string>);
  static_assert(std::is_same_v<decltype(PackageDescriptor{}.package_id), std::string>);
  static_assert(std::is_same_v<decltype(ArtifactDescriptor{}.artifact_id), std::string>);
  static_assert(std::is_same_v<decltype(VerifiedPackageManifest{}.hash_set),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(RollbackToken{}.rollback_id), std::string>);
  static_assert(std::is_same_v<decltype(InstallEvidence{}.artifact_id), std::string>);

  const UpgradePlan plan{
      .plan_id = std::string("ota-plan-001"),
      .requested_by = UpgradeRequester{
          .actor_ref = std::string("ops-user"),
          .request_id = std::string("req-ota-001"),
      },
      .target_scope = std::string("device.local"),
      .artifact_refs = {std::string("artifact-rootfs-a")},
      .strategy = std::string("safe_switch"),
      .validate_only = true,
  };
  assert_true(plan.is_valid(),
              "OTA plan identifiers should remain infra-private strings while reusing only request/actor semantics");
}

}  // namespace

int main() {
  try {
    test_ota_types_only_reuse_contract_result_code_and_error_info_semantics();
    test_ota_private_objects_keep_identifier_and_manifest_fields_in_infra_domain();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}