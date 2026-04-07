#include "ota/SlotSwitchCoordinator.h"

#include <string>
#include <utility>

namespace dasall::infra::ota {
namespace {

constexpr char kSlotSwitchCoordinatorSourceRef[] = "SlotSwitchCoordinator";

[[nodiscard]] SlotSelectionResult make_selection_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return SlotSelectionResult::failure(result_code,
                                      std::move(message),
                                      std::move(stage),
                                      kSlotSwitchCoordinatorSourceRef);
}

[[nodiscard]] SlotSwitchPreparationResult make_preparation_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return SlotSwitchPreparationResult::failure(result_code,
                                              std::move(message),
                                              std::move(stage),
                                              kSlotSwitchCoordinatorSourceRef);
}

[[nodiscard]] BootSwitchResult make_switch_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return BootSwitchResult::failure(result_code,
                                   std::move(message),
                                   std::move(stage),
                                   kSlotSwitchCoordinatorSourceRef);
}

[[nodiscard]] bool staged_evidence_is_valid(
    const std::vector<InstallEvidence>& staged_evidence) {
  if (staged_evidence.empty()) {
    return false;
  }

  std::vector<std::string> artifact_ids;
  artifact_ids.reserve(staged_evidence.size());
  for (const auto& evidence : staged_evidence) {
    if (!evidence.is_valid()) {
      return false;
    }

    artifact_ids.push_back(evidence.artifact_id);
  }

  return has_unique_non_empty_values(artifact_ids);
}

}  // namespace

SlotSwitchCoordinator::SlotSwitchCoordinator(Dependencies dependencies)
    : dependencies_(dependencies) {}

SlotSelectionResult SlotSwitchCoordinator::select_inactive_slot(
    std::string_view slot_group) const {
  if (slot_group.empty()) {
    return make_selection_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "slot group must remain explicit before ota.slot_switch.select",
        "ota.slot_switch.select");
  }

  if (dependencies_.slot_inventory_provider == nullptr) {
    return make_selection_failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "slot switch coordinator requires a slot inventory provider dependency",
        "ota.slot_switch.select");
  }

  const auto inventory = dependencies_.slot_inventory_provider->describe_slot_group(
      slot_group);
  if (!inventory.is_valid()) {
    return make_selection_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "slot inventory must declare an active slot and at least one distinct inactive candidate",
        "ota.slot_switch.select");
  }

  const auto target_it = std::find_if(
      inventory.candidate_slots.begin(),
      inventory.candidate_slots.end(),
      [&](const std::string& slot_name) {
        return slot_name != inventory.active_slot;
      });
  if (target_it == inventory.candidate_slots.end()) {
    return make_selection_failure(
        contracts::ResultCode::PolicyDenied,
        "slot inventory does not expose an inactive target for the requested slot group",
        "ota.slot_switch.select");
  }

  return SlotSelectionResult::success(inventory.active_slot, *target_it);
}

SlotSwitchPreparationResult SlotSwitchCoordinator::build_slot_plan(
    std::string_view slot_group,
    const std::vector<InstallEvidence>& staged_evidence,
    const SwitchPolicySnapshot& policy) const {
  if (!policy.is_valid() || !staged_evidence_is_valid(staged_evidence)) {
    return make_preparation_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "slot switch preparation requires a valid policy and non-empty staged install evidence",
        "ota.slot_switch.prepare");
  }

  if (dependencies_.rollback_token_factory == nullptr) {
    return make_preparation_failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "slot switch coordinator requires a rollback token factory dependency",
        "ota.slot_switch.prepare");
  }

  const auto slot_selection = select_inactive_slot(slot_group);
  if (!slot_selection.selected) {
    return make_preparation_failure(slot_selection.result_code,
                                    slot_selection.error.has_value()
                                        ? slot_selection.error->details.message
                                        : "slot selection failed",
                                    "ota.slot_switch.prepare");
  }

  SlotPlan slot_plan{
      .active_slot = slot_selection.active_slot,
      .target_slot = slot_selection.target_slot,
      .slot_group = std::string(slot_group),
      .switch_policy = policy.switch_policy,
      .confirm_deadline = policy.confirm_deadline,
  };
  if (!slot_plan.is_valid()) {
    return make_preparation_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "slot plan must preserve inactive target, slot group, switch policy, and confirm deadline",
        "ota.slot_switch.prepare");
  }

  const auto rollback_token = dependencies_.rollback_token_factory->make_token(
      slot_plan,
      staged_evidence);
  if (!rollback_token.is_valid()) {
    return make_preparation_failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "rollback token factory must emit a valid in-memory token before set_next_boot",
        "ota.slot_switch.prepare");
  }

  return SlotSwitchPreparationResult::success(std::move(slot_plan),
                                              rollback_token);
}

BootSwitchResult SlotSwitchCoordinator::set_next_boot(const SlotPlan& slot_plan) {
  if (!slot_plan.is_valid()) {
    return make_switch_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "slot plan must keep target_slot inactive before ota.slot_switch.set_next_boot",
        "ota.slot_switch.set_next_boot");
  }

  if (dependencies_.boot_control_adapter == nullptr) {
    return make_switch_failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "slot switch coordinator requires a boot control adapter dependency",
        "ota.slot_switch.set_next_boot");
  }

  const auto current = dependencies_.boot_control_adapter->get_active_target();
  if (!current.resolved || !current.references_only_contract_error_types()) {
    return make_switch_failure(
        current.result_code,
        current.error.has_value() ? current.error->details.message
                                  : "active boot target query failed before ota.slot_switch.set_next_boot",
        "ota.slot_switch.set_next_boot");
  }

  if (current.active_target == slot_plan.target_slot) {
    return make_switch_failure(
        contracts::ResultCode::PolicyDenied,
        "target slot must remain inactive at switch time",
        "ota.slot_switch.set_next_boot");
  }

  const auto mutation = dependencies_.boot_control_adapter->set_next_boot(
      slot_plan.target_slot);
  if (!mutation.applied || !mutation.references_only_contract_error_types()) {
    return make_switch_failure(
        mutation.result_code,
        mutation.error.has_value() ? mutation.error->details.message
                                   : "boot control adapter rejected the next boot mutation",
        "ota.slot_switch.set_next_boot");
  }

  return BootSwitchResult::success(slot_plan.target_slot, true);
}

}  // namespace dasall::infra::ota