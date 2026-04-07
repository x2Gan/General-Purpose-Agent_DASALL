#include "ota/InstallExecutor.h"

#include <string>
#include <utility>

namespace dasall::infra::ota {
namespace {

constexpr char kInstallExecutorSourceRef[] = "InstallExecutor";

[[nodiscard]] std::string append_detail(std::string message, std::string detail) {
  if (detail.empty()) {
    return message;
  }

  message.append(": ");
  message.append(detail);
  return message;
}

[[nodiscard]] StageArtifactResult make_stage_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return StageArtifactResult::failure(result_code,
                                      std::move(message),
                                      std::move(stage),
                                      kInstallExecutorSourceRef);
}

[[nodiscard]] BootSwitchResult make_activate_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return BootSwitchResult::failure(result_code,
                                   std::move(message),
                                   std::move(stage),
                                   kInstallExecutorSourceRef);
}

[[nodiscard]] RollbackResult make_revert_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return RollbackResult::failure(result_code,
                                 std::move(message),
                                 std::move(stage),
                                 kInstallExecutorSourceRef);
}

}  // namespace

InstallExecutor::InstallExecutor(Dependencies dependencies)
    : dependencies_(dependencies) {}

StageArtifactResult InstallExecutor::stage_artifact(
    const ArtifactDescriptor& artifact_descriptor,
    std::string_view target) {
  if (!artifact_descriptor.is_valid() || target.empty()) {
    return make_stage_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "artifact descriptor and target must stay explicit before ota.install.stage",
        "ota.install.stage");
  }

  if (dependencies_.artifact_writer == nullptr ||
      dependencies_.cleanup_handler == nullptr) {
    return make_stage_failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "install executor requires artifact writer and cleanup handler dependencies",
        "ota.install.stage");
  }

  ArtifactWriteResult write_result;
  switch (artifact_descriptor.artifact_class) {
    case ArtifactClass::RepoBound:
      write_result = dependencies_.artifact_writer->write_repo_bound(
          artifact_descriptor,
          target);
      break;
    case ArtifactClass::SlotBound:
      write_result = dependencies_.artifact_writer->write_slot_bound(
          artifact_descriptor,
          target);
      break;
    case ArtifactClass::Unspecified:
      return make_stage_failure(
          contracts::ResultCode::ValidationFieldMissing,
          "artifact_class must stay inside the frozen repo_bound or slot_bound set",
          "ota.install.stage");
  }

  if (write_result.is_valid()) {
    return StageArtifactResult::success(InstallEvidence{
        .artifact_id = artifact_descriptor.artifact_id,
        .written_target = write_result.written_target,
        .checksum = write_result.checksum,
        .install_ts = write_result.install_ts,
        .installer_version = write_result.installer_version,
    });
  }

  const auto cleanup_result = dependencies_.cleanup_handler->cleanup_failed_stage(
      artifact_descriptor,
      target);
  if (cleanup_result.cleaned) {
    return make_stage_failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        append_detail(
            "artifact materialization failed and cleanup removed partial install state",
            write_result.detail.empty() ? cleanup_result.detail : write_result.detail),
        "ota.install.stage");
  }

  return make_stage_failure(
      contracts::ResultCode::RuntimeRetryExhausted,
      append_detail(
          "artifact materialization failed and cleanup could not remove partial install state",
          write_result.detail.empty() ? cleanup_result.detail : write_result.detail),
      "ota.install.stage");
}

BootSwitchResult InstallExecutor::activate_plan(const SlotPlan& slot_plan) {
  if (!slot_plan.is_valid()) {
    return make_activate_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "slot plan must target an inactive slot before ota.install.activate",
        "ota.install.activate");
  }

  if (dependencies_.activation_adapter == nullptr) {
    return make_activate_failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "install executor requires an activation adapter dependency",
        "ota.install.activate");
  }

  const auto result = dependencies_.activation_adapter->activate(slot_plan);
  if (result.references_only_contract_error_types()) {
    return result;
  }

  return make_activate_failure(
      contracts::ResultCode::RuntimeRetryExhausted,
      "activation adapter returned a non-contract-shaped boot switch result",
      "ota.install.activate");
}

RollbackResult InstallExecutor::revert_install(
    const RollbackToken& rollback_token) {
  if (!rollback_token.is_valid()) {
    return make_revert_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "rollback token must stay attributable and non-empty before ota.install.revert",
        "ota.install.revert");
  }

  if (dependencies_.revert_adapter == nullptr) {
    return make_revert_failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "install executor requires a revert adapter dependency",
        "ota.install.revert");
  }

  const auto result = dependencies_.revert_adapter->revert(rollback_token);
  if (result.references_only_contract_error_types()) {
    return result;
  }

  return make_revert_failure(
      contracts::ResultCode::RuntimeRetryExhausted,
      "revert adapter returned a non-contract-shaped rollback result",
      "ota.install.revert");
}

}  // namespace dasall::infra::ota