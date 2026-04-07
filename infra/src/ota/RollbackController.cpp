#include "ota/RollbackController.h"

#include <string>
#include <utility>

#include "InfraErrorCode.h"

namespace dasall::infra::ota {
namespace {

constexpr char kRollbackControllerSourceRef[] = "RollbackController";

[[nodiscard]] RollbackResult make_rollback_failure(std::string message,
                                                   std::string stage) {
  const auto mapping = map_infra_error_code(InfraErrorCode::OTARollbackFail);
  return RollbackResult::failure(mapping.result_code,
                                 std::move(message),
                                 std::move(stage),
                                 kRollbackControllerSourceRef);
}

[[nodiscard]] RollbackResult make_validation_failure(std::string message,
                                                     std::string stage) {
  return RollbackResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                 std::move(message),
                                 std::move(stage),
                                 kRollbackControllerSourceRef);
}

[[nodiscard]] RepoPointerRecoveryResult make_repo_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return RepoPointerRecoveryResult::failure(result_code,
                                            std::move(message),
                                            std::move(stage),
                                            kRollbackControllerSourceRef);
}

[[nodiscard]] bool install_evidence_matches_token(
    const RollbackToken& rollback_token,
    const std::vector<InstallEvidence>& install_evidence) {
  if (install_evidence.empty()) {
    return false;
  }

  std::vector<std::string> artifact_ids;
  artifact_ids.reserve(install_evidence.size());
  for (const auto& evidence : install_evidence) {
    if (!evidence.is_valid()) {
      return false;
    }

    artifact_ids.push_back(evidence.artifact_id);
  }

  if (!has_unique_non_empty_values(artifact_ids)) {
    return false;
  }

  for (const auto& artifact_id : rollback_token.staged_artifacts) {
    if (std::find(artifact_ids.begin(), artifact_ids.end(), artifact_id) ==
        artifact_ids.end()) {
      return false;
    }
  }

  return true;
}

}  // namespace

RollbackController::RollbackController(Dependencies dependencies)
    : dependencies_(dependencies) {}

RollbackResult RollbackController::rollback(
    const RollbackToken& rollback_token,
    const std::vector<InstallEvidence>& install_evidence) {
  if (!rollback_token.is_valid() || rollback_token.expires_at <= rollback_token.created_at) {
    return make_validation_failure(
        "rollback token must stay non-empty and preserve created_at < expires_at before ota.rollback",
        "ota.rollback");
  }

  if (!install_evidence_matches_token(rollback_token, install_evidence)) {
    return make_validation_failure(
        "rollback token staged_artifacts must match a valid install evidence set before ota.rollback",
        "ota.rollback");
  }

  if (dependencies_.time_provider == nullptr ||
      dependencies_.evidence_writer == nullptr) {
    return make_rollback_failure(
        "rollback controller requires time provider and evidence writer dependencies",
        "ota.rollback");
  }

  const auto now = dependencies_.time_provider->now_utc();
  if (now.empty()) {
    return make_rollback_failure(
        "time provider must return a non-empty UTC timestamp before ota.rollback",
        "ota.rollback");
  }

  if (now >= rollback_token.expires_at) {
    return make_rollback_failure(
        "rollback token expired before rollback execution could begin",
        "ota.rollback");
  }

  const auto boot_restore = restore_boot_target(rollback_token.previous_boot_target);
  if (!boot_restore.applied || !boot_restore.references_only_contract_error_types()) {
    return RollbackResult::failure(
        boot_restore.result_code,
        boot_restore.error.has_value() ? boot_restore.error->details.message
                                       : "failed to restore previous boot target",
        "ota.rollback.restore_boot_target",
        kRollbackControllerSourceRef);
  }

  const auto repo_restore = recover_repo_pointer(rollback_token, install_evidence);
  if (!repo_restore.recovered || !repo_restore.references_only_contract_error_types()) {
    return RollbackResult::failure(
        repo_restore.result_code,
        repo_restore.error.has_value() ? repo_restore.error->details.message
                                       : "failed to recover repo pointer state during rollback",
        "ota.rollback.recover_repo_pointer",
        kRollbackControllerSourceRef);
  }

  const auto evidence_result = dependencies_.evidence_writer->write(
      rollback_token,
      rollback_token.previous_boot_target,
      repo_restore.restored_refs);
  if (!evidence_result.recorded ||
      !evidence_result.references_only_contract_error_types()) {
    return make_rollback_failure(
        evidence_result.error.has_value()
            ? evidence_result.error->details.message
            : "rollback evidence writer failed to produce a durable evidence ref",
        "ota.rollback.record_evidence");
  }

  return RollbackResult::success(rollback_token.previous_boot_target,
                                 evidence_result.evidence_ref);
}

BootMutationResult RollbackController::restore_boot_target(
    std::string_view previous_boot_target) {
  if (previous_boot_target.empty()) {
    return BootMutationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "previous boot target must stay explicit before ota.rollback.restore_boot_target",
        "ota.rollback.restore_boot_target",
        kRollbackControllerSourceRef);
  }

  if (dependencies_.boot_control_adapter == nullptr) {
    return BootMutationResult::failure(
        map_infra_error_code(InfraErrorCode::OTARollbackFail).result_code,
        "rollback controller requires a boot control adapter dependency",
        "ota.rollback.restore_boot_target",
        kRollbackControllerSourceRef);
  }

  const auto result = dependencies_.boot_control_adapter->set_next_boot(
      previous_boot_target);
  if (result.references_only_contract_error_types()) {
    return result;
  }

  return BootMutationResult::failure(
      map_infra_error_code(InfraErrorCode::OTARollbackFail).result_code,
      "boot control adapter returned a non-contract-shaped restore result",
      "ota.rollback.restore_boot_target",
      kRollbackControllerSourceRef);
}

RepoPointerRecoveryResult RollbackController::recover_repo_pointer(
    const RollbackToken& rollback_token,
    const std::vector<InstallEvidence>& install_evidence) const {
  if (!rollback_token.is_valid() ||
      !install_evidence_matches_token(rollback_token, install_evidence)) {
    return make_repo_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "repo pointer recovery requires a valid rollback token and matching install evidence",
        "ota.rollback.recover_repo_pointer");
  }

  if (dependencies_.repo_pointer_recovery_adapter == nullptr) {
    return make_repo_failure(
        map_infra_error_code(InfraErrorCode::OTARollbackFail).result_code,
        "rollback controller requires a repo pointer recovery dependency",
        "ota.rollback.recover_repo_pointer");
  }

  const auto result = dependencies_.repo_pointer_recovery_adapter->recover(
      rollback_token,
      install_evidence);
  if (result.references_only_contract_error_types()) {
    return result;
  }

  return make_repo_failure(
      map_infra_error_code(InfraErrorCode::OTARollbackFail).result_code,
      "repo pointer recovery adapter returned a non-contract-shaped result",
      "ota.rollback.recover_repo_pointer");
}

}  // namespace dasall::infra::ota