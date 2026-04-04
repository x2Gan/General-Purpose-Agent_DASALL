#include "SecretRotationCoordinator.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <utility>

#include "secret/SecretErrors.h"

namespace dasall::infra::secret {
namespace {

constexpr std::string_view kSecretRotationCoordinatorSourceRef =
    "SecretRotationCoordinator";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] SecretQuery make_rotation_query(std::string_view secret_name) {
  return SecretQuery{
      .secret_name = std::string(secret_name),
      .version_hint = {},
      .purpose = std::string("rotate"),
      .access_mode = SecretAccessMode::Rotate,
  };
}

[[nodiscard]] std::string extract_error_message(
    const std::optional<contracts::ErrorInfo>& error_info,
    std::string fallback_message) {
  if (!error_info.has_value()) {
    return fallback_message;
  }

  if (!error_info->details.message.empty()) {
    return error_info->details.message;
  }

  return fallback_message;
}

[[nodiscard]] bool is_numeric_version(std::string_view version) {
  if (version.size() < 2U || version.front() != 'v') {
    return false;
  }

  return std::all_of(version.begin() + 1, version.end(), [](char value) {
    return std::isdigit(static_cast<unsigned char>(value)) != 0;
  });
}

[[nodiscard]] std::string derive_candidate_version(std::string_view current_version) {
  if (is_numeric_version(current_version)) {
    return "v" + std::to_string(std::stoull(std::string(current_version.substr(1))) + 1ULL);
  }

  return std::string(current_version) + ".candidate";
}

[[nodiscard]] RotationResult make_rotation_failure(
    std::string secret_name,
    std::string previous_version,
    std::string candidate_version,
    SecretErrorCode error_code,
    std::string message,
    std::string stage,
    std::string evidence_ref,
    RotationValidationState validation_state = RotationValidationState::Failed,
    bool rollback_ready = false) {
  const SecretErrorMapping mapping = map_secret_error_code(error_code);
  RotationResult failure = RotationResult::failure(
      std::move(secret_name),
      std::move(previous_version),
      std::move(candidate_version),
      std::move(evidence_ref),
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kSecretRotationCoordinatorSourceRef),
      rollback_ready);
  failure.validation_state = validation_state;
  return failure;
}

}  // namespace

SecretRotationCoordinator::SecretRotationCoordinator(
    SecretRotationCoordinatorOptions options,
    std::shared_ptr<ISecretRotationValidator> validator)
    : options_(std::move(options)),
      validator_(validator != nullptr
                     ? std::move(validator)
                     : std::make_shared<AllowAllSecretRotationValidator>()),
      last_detail_ref_(options_.detail_ref_prefix + "idle") {}

void SecretRotationCoordinator::set_validator(
    std::shared_ptr<ISecretRotationValidator> validator) {
  validator_ = validator != nullptr ? std::move(validator)
                                    : std::make_shared<AllowAllSecretRotationValidator>();
}

RotationResult SecretRotationCoordinator::rotate(ISecretBackend& backend,
                                                 const RotationRequest& request) {
  if (!request.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, "invalid_request");
    return RotationResult::failure(
        request.secret_name,
        {},
        {},
        options_.evidence_ref_prefix + "invalid_request",
        contracts::ResultCode::ValidationFieldMissing,
        "secret rotation coordinator requires a valid rotation request",
        "secret.rotate",
        std::string(kSecretRotationCoordinatorSourceRef),
        false);
  }

  if (request.strategy == RotationStrategy::DualSlot && !options_.dual_slot_enabled) {
    record_failure(map_secret_error_code(SecretErrorCode::RotationValidationFailed).result_code,
                   "dual_slot_disabled");
    return make_rotation_failure(
        request.secret_name,
        {},
        {},
        SecretErrorCode::RotationValidationFailed,
        "dual-slot rotation is disabled by the current secret rotation options",
        "secret.rotate.validate",
        options_.evidence_ref_prefix + "validation/dual_slot_disabled");
  }

  const auto fetched = backend.fetch_record(make_rotation_query(request.secret_name));
  if (!fetched.ok) {
    const contracts::ResultCode result_code =
        fetched.result_code.value_or(contracts::ResultCode::ProviderTimeout);
    record_failure(result_code, "fetch_failed");
    return RotationResult::failure(
        request.secret_name,
        {},
        {},
        options_.evidence_ref_prefix + "fetch_failed/" + request.secret_name,
        result_code,
        extract_error_message(fetched.error_info,
                              "secret backend fetch failed during rotate"),
        "secret.rotate.fetch",
        std::string(kSecretRotationCoordinatorSourceRef),
        false);
  }

  const std::string candidate_version = derive_candidate_version(fetched.record.version);
  const SecretRotationValidationContext validation_context =
      make_validation_context(request, fetched.record.version, candidate_version);
  if (!validation_context.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing,
                   "invalid_validation_context");
    return RotationResult::failure(
        request.secret_name,
        fetched.record.version,
        candidate_version,
        options_.evidence_ref_prefix + "validation/invalid_context/" +
            request.secret_name,
        contracts::ResultCode::ValidationFieldMissing,
        "secret rotation validation context is incomplete",
        "secret.rotate.validate",
        std::string(kSecretRotationCoordinatorSourceRef),
        false);
  }

  SecretRotationValidationDecision validation =
      options_.validation_required
          ? validator_->validate_candidate(validation_context)
          : SecretRotationValidationDecision::success(
                "validation_skipped",
                options_.evidence_ref_prefix + "validation/skipped/" +
                    request.secret_name,
                request.strategy == RotationStrategy::DualSlot);
  if (!validation.is_valid()) {
    record_failure(map_secret_error_code(SecretErrorCode::RotationValidationFailed).result_code,
                   "validator_contract_invalid");
    return make_rotation_failure(
        request.secret_name,
        fetched.record.version,
        candidate_version,
        SecretErrorCode::RotationValidationFailed,
        "rotation validator returned an invalid decision payload",
        "secret.rotate.validate",
        options_.evidence_ref_prefix + "validation/invalid_payload/" +
            request.secret_name);
  }

  if (!validation.accepted) {
    rotation_states_[request.secret_name] = RotationState{
        .previous_version = fetched.record.version,
        .current_version = fetched.record.version,
        .rotation_epoch = next_rotation_epoch_for(request.secret_name),
        .validation_state = RotationValidationState::Failed,
        .revoke_pending = false,
        .rollback_ready = validation.rollback_ready,
        .grace_deadline_ms = 0,
    };
    record_failure(map_secret_error_code(SecretErrorCode::RotationValidationFailed).result_code,
                   "validation_failed");
    return make_rotation_failure(
        request.secret_name,
        fetched.record.version,
        candidate_version,
        SecretErrorCode::RotationValidationFailed,
        validation.reason_code + ": " +
            extract_error_message(validation.error_info,
                                  "rotation validator rejected the candidate version"),
        "secret.rotate.validate",
        validation.evidence_ref,
        RotationValidationState::Failed,
        validation.rollback_ready);
  }

  if (request.validate_only) {
    record_success("validate_only");
    return RotationResult::success(request.secret_name,
                                   fetched.record.version,
                                   candidate_version,
                                   validation.evidence_ref,
                                   validation.rollback_ready);
  }

  const std::uint64_t rotation_epoch = next_rotation_epoch_for(request.secret_name);
  const SecretVersionPromotionRequest promote_request{
      .secret_name = request.secret_name,
      .previous_version = fetched.record.version,
      .candidate_version = candidate_version,
      .rotation_epoch = rotation_epoch,
      .validate_only = false,
  };
  const RotationResult promoted = backend.promote_version(promote_request);
  if (!promoted.rotated) {
    const contracts::ResultCode result_code =
        promoted.result_code.value_or(contracts::ResultCode::ToolExecutionFailed);
    record_failure(result_code, "promote_failed");
    return promoted;
  }

  const bool defer_revoke = request.strategy == RotationStrategy::DualSlot &&
                            options_.grace_period_sec > 0;
  if (defer_revoke) {
    rotation_states_[request.secret_name] = RotationState{
        .previous_version = fetched.record.version,
        .current_version = candidate_version,
        .rotation_epoch = rotation_epoch,
        .validation_state = RotationValidationState::Succeeded,
        .revoke_pending = true,
        .rollback_ready = true,
        .grace_deadline_ms = current_time_unix_ms() +
                            (options_.grace_period_sec * 1000),
    };
    record_success("grace_backlog");
    return RotationResult::success(request.secret_name,
                                   fetched.record.version,
                                   candidate_version,
                                   promoted.evidence_ref,
                                   true);
  }

  const auto revoked = backend.revoke_version(request.secret_name, fetched.record.version);
  if (!revoked.ok) {
    return rollback(backend,
                    request.secret_name,
                    fetched.record.version,
                    candidate_version,
                    rotation_epoch,
                    extract_error_message(revoked.error_info,
                                          "secret backend revoke failed after promote"));
  }

  rotation_states_[request.secret_name] = RotationState{
      .previous_version = fetched.record.version,
      .current_version = candidate_version,
      .rotation_epoch = rotation_epoch,
      .validation_state = RotationValidationState::Succeeded,
      .revoke_pending = false,
      .rollback_ready = false,
      .grace_deadline_ms = 0,
  };
  record_success("ready");
  return RotationResult::success(request.secret_name,
                                 fetched.record.version,
                                 candidate_version,
                                 promoted.evidence_ref,
                                 false);
}

SecretRotationCoordinatorStatus SecretRotationCoordinator::get_status() const {
  const std::uint64_t backlog = static_cast<std::uint64_t>(std::count_if(
      rotation_states_.begin(),
      rotation_states_.end(),
      [](const auto& entry) { return entry.second.revoke_pending; }));

  return SecretRotationCoordinatorStatus{
      .rotation_backlog = backlog,
      .rollback_failures = rollback_failures_,
      .degraded = backlog > 0 || rollback_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

std::uint64_t SecretRotationCoordinator::next_rotation_epoch_for(
    std::string_view secret_name) const {
  const auto state_it = rotation_states_.find(std::string(secret_name));
  if (state_it == rotation_states_.end() ||
      state_it->second.rotation_epoch < options_.initial_rotation_epoch) {
    return options_.initial_rotation_epoch + 1;
  }

  return state_it->second.rotation_epoch + 1;
}

SecretRotationValidationContext SecretRotationCoordinator::make_validation_context(
    const RotationRequest& request,
    std::string previous_version,
    std::string candidate_version) const {
  return SecretRotationValidationContext{
      .secret_name = request.secret_name,
      .previous_version = std::move(previous_version),
      .candidate_version = std::move(candidate_version),
      .strategy = request.strategy,
      .requested_by = request.requested_by,
      .reason_code = request.reason_code,
      .validation_required = options_.validation_required,
      .dual_slot_enabled = options_.dual_slot_enabled,
      .next_rotation_epoch = next_rotation_epoch_for(request.secret_name),
      .grace_period_sec = options_.grace_period_sec,
  };
}

RotationResult SecretRotationCoordinator::rollback(ISecretBackend& backend,
                                                   std::string_view secret_name,
                                                   std::string_view previous_version,
                                                   std::string_view candidate_version,
                                                   std::uint64_t rotation_epoch,
                                                   std::string trigger_message) {
  const SecretVersionPromotionRequest rollback_request{
      .secret_name = std::string(secret_name),
      .previous_version = std::string(candidate_version),
      .candidate_version = std::string(previous_version),
      .rotation_epoch = std::max(options_.initial_rotation_epoch + 1, rotation_epoch + 1),
      .validate_only = false,
  };
  const RotationResult rolled_back = backend.promote_version(rollback_request);
  if (!rolled_back.rotated) {
    ++rollback_failures_;
    rotation_states_[std::string(secret_name)] = RotationState{
        .previous_version = std::string(previous_version),
        .current_version = std::string(candidate_version),
        .rotation_epoch = rotation_epoch,
        .validation_state = RotationValidationState::Failed,
        .revoke_pending = false,
        .rollback_ready = false,
        .grace_deadline_ms = 0,
    };
    record_failure(map_secret_error_code(SecretErrorCode::RotationRollbackFailed).result_code,
                   "rollback_failed");
    return make_rotation_failure(
        std::string(secret_name),
        std::string(previous_version),
        std::string(candidate_version),
        SecretErrorCode::RotationRollbackFailed,
        std::move(trigger_message),
        "secret.rotate.rollback",
        options_.evidence_ref_prefix + "rollback_failed/" + std::string(secret_name));
  }

  rotation_states_[std::string(secret_name)] = RotationState{
      .previous_version = std::string(candidate_version),
      .current_version = std::string(previous_version),
      .rotation_epoch = rollback_request.rotation_epoch,
      .validation_state = RotationValidationState::RolledBack,
      .revoke_pending = false,
      .rollback_ready = false,
      .grace_deadline_ms = 0,
  };
  record_failure(map_secret_error_code(SecretErrorCode::RotationValidationFailed).result_code,
                 "rolled_back");
  return make_rotation_failure(
      std::string(secret_name),
      std::string(previous_version),
      std::string(candidate_version),
      SecretErrorCode::RotationValidationFailed,
      std::move(trigger_message),
      "secret.rotate.rollback",
      options_.evidence_ref_prefix + "rolled_back/" + std::string(secret_name),
      RotationValidationState::RolledBack,
      false);
}

void SecretRotationCoordinator::record_success(std::string detail_suffix) {
  last_error_code_.reset();
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

void SecretRotationCoordinator::record_failure(contracts::ResultCode result_code,
                                               std::string detail_suffix) {
  last_error_code_ = result_code;
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

}  // namespace dasall::infra::secret