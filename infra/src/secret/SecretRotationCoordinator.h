#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "secret/ISecretBackend.h"

#include "SecretRotationValidator.h"

namespace dasall::infra::secret {

struct SecretRotationCoordinatorOptions {
  bool dual_slot_enabled = true;
  bool validation_required = true;
  std::int64_t grace_period_sec = 600;
  std::uint64_t initial_rotation_epoch = 1;
  std::string detail_ref_prefix = "status://secret/rotation/";
  std::string evidence_ref_prefix = "audit://secret/rotation/";
};

struct SecretRotationCoordinatorStatus {
  std::uint64_t rotation_backlog = 0;
  std::uint64_t rollback_failures = 0;
  bool degraded = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref;

  [[nodiscard]] bool has_rotation_backlog() const {
    return rotation_backlog > 0;
  }

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

class SecretRotationCoordinator {
 public:
  explicit SecretRotationCoordinator(
      SecretRotationCoordinatorOptions options = {},
      std::shared_ptr<ISecretRotationValidator> validator = nullptr);

  void set_validator(std::shared_ptr<ISecretRotationValidator> validator);

  [[nodiscard]] RotationResult rotate(ISecretBackend& backend,
                                      const RotationRequest& request);

  [[nodiscard]] SecretRotationCoordinatorStatus get_status() const;

 private:
  struct RotationState {
    std::string previous_version;
    std::string current_version;
    std::uint64_t rotation_epoch = 0;
    RotationValidationState validation_state = RotationValidationState::Unspecified;
    bool revoke_pending = false;
    bool rollback_ready = false;
    std::int64_t grace_deadline_ms = 0;
  };

  using RotationStateMap = std::map<std::string, RotationState>;

  [[nodiscard]] std::uint64_t next_rotation_epoch_for(
      std::string_view secret_name) const;

  [[nodiscard]] SecretRotationValidationContext make_validation_context(
      const RotationRequest& request,
      std::string previous_version,
      std::string candidate_version) const;

  [[nodiscard]] RotationResult rollback(ISecretBackend& backend,
                                        std::string_view secret_name,
                                        std::string_view previous_version,
                                        std::string_view candidate_version,
                                        std::uint64_t rotation_epoch,
                                        std::string trigger_message);

  void record_success(std::string detail_suffix);
  void record_failure(contracts::ResultCode result_code, std::string detail_suffix);

  SecretRotationCoordinatorOptions options_;
  std::shared_ptr<ISecretRotationValidator> validator_;
  RotationStateMap rotation_states_;
  std::uint64_t rollback_failures_ = 0;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_;
};

}  // namespace dasall::infra::secret