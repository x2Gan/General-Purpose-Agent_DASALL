#include "SecurityPolicyManager.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "policy/PolicyErrors.h"

namespace dasall::infra::policy {
namespace {

constexpr std::string_view kPatchGateAction = "apply_patch";
constexpr std::string_view kPatchGateTarget = "policy:runtime_patch";
constexpr std::string_view kRuleSourcePrefix = "runtime_patch:";

[[nodiscard]] std::uint32_t normalize_safe_mode_threshold(std::uint32_t threshold) {
  return threshold == 0 ? 1U : threshold;
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) {
  return value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string lowercase_copy(std::string_view value) {
  std::string normalized(value);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return normalized;
}

[[nodiscard]] bool is_unsigned_integer(std::string_view value) {
  return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
           return std::isdigit(ch) != 0;
         });
}

[[nodiscard]] std::optional<std::string> find_rule_condition_value(
    const std::vector<PolicyRuleDescriptor>& rules,
    std::string_view action,
    std::string_view target_selector,
    std::string_view key) {
  const std::string prefix = std::string(key) + "=";

  for (const auto& rule : rules) {
    if (rule.action != action || rule.target_selector != target_selector) {
      continue;
    }

    for (const auto& condition : rule.conditions) {
      if (starts_with(condition, prefix)) {
        return condition.substr(prefix.size());
      }
    }
  }

  return std::nullopt;
}

[[nodiscard]] PolicyMode snapshot_mode_from_rules(const std::vector<PolicyRuleDescriptor>& rules,
                                                  PolicyMode fallback) {
  return rules.empty() ? fallback : rules.front().mode;
}

[[nodiscard]] std::string next_snapshot_id(std::uint64_t generation) {
  return std::string("policy-snapshot-") + std::to_string(generation);
}

[[nodiscard]] std::string rollback_snapshot_id(std::string_view target_snapshot_id,
                                               std::uint64_t generation) {
  return std::string("policy-rollback-") + std::to_string(generation) + "-from-" +
         std::string(target_snapshot_id);
}

[[nodiscard]] std::string bundle_generated_at(const PolicyBundle& bundle,
                                              std::string_view fallback) {
  return bundle.generated_at.empty() ? std::string(fallback) : bundle.generated_at;
}

[[nodiscard]] std::vector<std::string> merge_source_chain(const PolicySnapshot& previous_snapshot,
                                                          std::string source_ref) {
  std::vector<std::string> source_chain = previous_snapshot.source_chain;
  if (source_chain.empty()) {
    source_chain.push_back(std::move(source_ref));
    return source_chain;
  }

  source_chain.push_back(std::move(source_ref));
  return source_chain;
}

[[nodiscard]] PolicyBundle apply_patch_to_snapshot(const PolicySnapshot& current_snapshot,
                                                   const PolicyPatch& patch) {
  std::vector<PolicyRuleDescriptor> rules = current_snapshot.effective_rules;
  PolicyMode effective_mode = current_snapshot.mode;

  for (const auto& operation : patch.operations) {
    switch (operation.operation) {
      case PolicyPatchOperationType::AddRule: {
        PolicyRuleDescriptor rule = *operation.rule;
        rule.mode = effective_mode;
        rules.push_back(std::move(rule));
        break;
      }
      case PolicyPatchOperationType::ReplaceRule: {
        PolicyRuleDescriptor rule = *operation.rule;
        const std::string rule_id = operation.rule_id.empty() ? rule.rule_id : operation.rule_id;
        rule.rule_id = rule_id;
        rule.mode = effective_mode;

        const auto existing_rule = std::find_if(rules.begin(), rules.end(), [&](const auto& candidate) {
          return candidate.rule_id == rule_id;
        });
        if (existing_rule != rules.end()) {
          *existing_rule = std::move(rule);
        } else {
          rules.push_back(std::move(rule));
        }
        break;
      }
      case PolicyPatchOperationType::RemoveRule:
        rules.erase(std::remove_if(rules.begin(), rules.end(), [&](const auto& candidate) {
                      return candidate.rule_id == operation.rule_id;
                    }),
                    rules.end());
        break;
      case PolicyPatchOperationType::UpdateMode:
        effective_mode = operation.mode;
        for (auto& rule : rules) {
          rule.mode = effective_mode;
        }
        break;
      case PolicyPatchOperationType::Unspecified:
        break;
    }
  }

  return PolicyBundle{
      .bundle_id = std::string("policy-bundle-patch-") + patch.patch_id,
      .schema_version = std::string("1"),
      .source = std::string(kRuleSourcePrefix) + patch.patch_id,
      .checksum = std::string("patch-checksum:") + patch.patch_id,
      .rules = std::move(rules),
      .generated_at = current_snapshot.created_at.empty() ? std::string("runtime-patch")
                                                          : current_snapshot.created_at,
  };
}

[[nodiscard]] ValidationReport conflict_report_from_result(const ConflictResolutionResult& result) {
  return ValidationReport{
      .blocking_errors = result.resolved ? std::vector<std::string>{}
                                         : std::vector<std::string>{result.reason_code},
      .warnings = result.warnings,
      .invalid_rule_ids = result.conflict_rule_ids,
      .field_paths = result.resolved ? std::vector<std::string>{}
                                     : std::vector<std::string>{std::string("rules")},
  };
}

}  // namespace

SecurityPolicyManager::SecurityPolicyManager(const IPolicySchemaValidator& validator,
                                             IPolicySnapshotStore& snapshot_store,
                                             SecurityPolicyManagerOptions options)
    : validator_(validator),
      snapshot_store_(snapshot_store),
      options_{.dry_run_required = options.dry_run_required,
               .safe_mode_threshold = normalize_safe_mode_threshold(options.safe_mode_threshold)} {}

PolicyOpResult SecurityPolicyManager::load_policy(const PolicyBundle& bundle) {
  const ValidationReport report = validator_.validate_bundle(bundle);
  if (report.has_blocking_errors()) {
    return make_failure(classify_bundle_validation_error(report),
                        report.blocking_errors.front(),
                        "policy.load",
                        bundle.bundle_id.empty() ? std::string("policy-bundle") : bundle.bundle_id);
  }

  const ConflictResolutionResult resolved = conflict_resolver_.resolve(bundle);
  if (!resolved.resolved) {
    return make_failure(PolicyErrorCode::ConflictUnresolved,
                        resolved.reason_code,
                        "policy.load",
                        bundle.bundle_id.empty() ? std::string("policy-bundle") : bundle.bundle_id);
  }

  const PolicySnapshot previous_snapshot = snapshot_store_.current();
  const std::uint64_t generation = previous_snapshot.is_valid() ? previous_snapshot.generation + 1U : 1U;
  const PolicySnapshot next_snapshot = build_snapshot(bundle,
                                                      resolved.effective_rules,
                                                      previous_snapshot,
                                                      next_snapshot_id(generation));
  const PolicyOpResult commit_result = snapshot_store_.commit(next_snapshot);
  if (!commit_result.applied) {
    return commit_result;
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  sync_options_from_rules_locked(next_snapshot.effective_rules);
  reset_patch_failures_locked();
  clear_dry_run_locked();
  return commit_result;
}

ValidationReport SecurityPolicyManager::dry_run_patch(const PolicyPatch& patch) {
  const PolicySnapshot current_snapshot = snapshot_store_.current();
  const ValidationReport patch_report = validator_.validate_patch(current_snapshot, patch);
  if (patch_report.has_blocking_errors()) {
    return patch_report;
  }

  const PolicyBundle candidate_bundle = build_candidate_bundle(current_snapshot, patch);
  const ValidationReport bundle_report = validator_.validate_bundle(candidate_bundle);
  if (bundle_report.has_blocking_errors()) {
    return bundle_report;
  }

  const ConflictResolutionResult resolved = conflict_resolver_.resolve(candidate_bundle);
  if (!resolved.resolved) {
    return conflict_report_from_result(resolved);
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  remember_dry_run_locked(patch);
  return ValidationReport{
      .blocking_errors = {},
      .warnings = resolved.warnings,
      .invalid_rule_ids = {},
      .field_paths = {},
  };
}

PolicyOpResult SecurityPolicyManager::apply_patch(const PolicyPatch& patch) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (safe_mode_) {
      return make_failure(PolicyErrorCode::DryRunRejected,
                          "policy_safe_mode_active",
                          "policy.apply_patch",
                          patch.patch_id.empty() ? std::string("policy-patch") : patch.patch_id);
    }

    if (options_.dry_run_required && !dry_run_matches_locked(patch)) {
      note_patch_failure_locked();
      return make_failure(PolicyErrorCode::DryRunRejected,
                          "policy_patch_dry_run_required",
                          "policy.apply_patch",
                          patch.patch_id.empty() ? std::string("policy-patch") : patch.patch_id);
    }
  }

  const ValidationReport dry_run_report = dry_run_patch(patch);
  if (dry_run_report.has_blocking_errors()) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    note_patch_failure_locked();
    return make_failure(classify_patch_validation_error(dry_run_report),
                        dry_run_report.blocking_errors.front(),
                        "policy.apply_patch",
                        patch.patch_id.empty() ? std::string("policy-patch") : patch.patch_id);
  }

  const PolicySnapshot current_snapshot = snapshot_store_.current();
  const PolicyBundle candidate_bundle = build_candidate_bundle(current_snapshot, patch);
  const ConflictResolutionResult resolved = conflict_resolver_.resolve(candidate_bundle);
  if (!resolved.resolved) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    note_patch_failure_locked();
    return make_failure(PolicyErrorCode::ConflictUnresolved,
                        resolved.reason_code,
                        "policy.apply_patch",
                        patch.patch_id.empty() ? std::string("policy-patch") : patch.patch_id);
  }

  const std::uint64_t generation = current_snapshot.is_valid() ? current_snapshot.generation + 1U : 1U;
  const PolicySnapshot next_snapshot = build_snapshot(candidate_bundle,
                                                      resolved.effective_rules,
                                                      current_snapshot,
                                                      next_snapshot_id(generation));
  const PolicyOpResult commit_result = snapshot_store_.commit(next_snapshot);
  if (!commit_result.applied) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    note_patch_failure_locked();
    return commit_result;
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  sync_options_from_rules_locked(next_snapshot.effective_rules);
  reset_patch_failures_locked();
  clear_dry_run_locked();
  return commit_result;
}

PolicySnapshot SecurityPolicyManager::snapshot() const {
  const PolicySnapshot current_snapshot = snapshot_store_.current();
  return current_snapshot.is_valid() ? current_snapshot : snapshot_store_.last_known_good();
}

PolicyOpResult SecurityPolicyManager::rollback(const std::string& snapshot_id) {
  const PolicySnapshot target_snapshot = snapshot_store_.get_by_id(snapshot_id);
  if (!target_snapshot.is_valid()) {
    return make_failure(PolicyErrorCode::SnapshotNotFound,
                        "policy_snapshot_not_found",
                        "policy.rollback",
                        snapshot_id.empty() ? std::string("policy-snapshot") : snapshot_id);
  }

  const PolicySnapshot current_snapshot = snapshot_store_.current();
  const std::uint64_t generation = current_snapshot.is_valid() ? current_snapshot.generation + 1U
                                                               : target_snapshot.generation + 1U;
  const PolicySnapshot rollback_snapshot{
      .snapshot_id = rollback_snapshot_id(snapshot_id, generation),
      .generation = generation,
      .version = target_snapshot.version,
      .mode = target_snapshot.mode,
      .effective_rules = target_snapshot.effective_rules,
      .created_at = target_snapshot.created_at,
      .source_chain = merge_source_chain(current_snapshot,
                                        std::string("rollback:") + snapshot_id),
      .last_known_good_ref = target_snapshot.snapshot_id,
  };

  const PolicyOpResult commit_result = snapshot_store_.commit(rollback_snapshot);
  if (!commit_result.applied) {
    return make_failure(PolicyErrorCode::RollbackFailed,
                        commit_result.error_info.has_value()
                            ? commit_result.error_info->details.message
                            : std::string("policy_rollback_failed"),
                        "policy.rollback",
                        snapshot_id);
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  sync_options_from_rules_locked(rollback_snapshot.effective_rules);
  reset_patch_failures_locked();
  clear_dry_run_locked();
  return PolicyOpResult::success(rollback_snapshot.snapshot_id,
                                 rollback_snapshot.generation,
                                 true,
                                 false);
}

PolicyDecisionRef SecurityPolicyManager::evaluate(const PolicyQueryContext& query) const {
  PolicySnapshot active_snapshot = snapshot_store_.current();
  if (!active_snapshot.is_valid()) {
    active_snapshot = snapshot_store_.last_known_good();
  }

  return decision_projector_.project(query, active_snapshot);
}

PolicyOpResult SecurityPolicyManager::make_failure(PolicyErrorCode code,
                                                   std::string message,
                                                   std::string stage,
                                                   std::string source_ref) const {
  const PolicyErrorMapping mapping = map_policy_error_code(code);
  return PolicyOpResult::failure(mapping.result_code,
                                 std::string(policy_error_code_name(code)) + ": " + std::move(message),
                                 std::move(stage),
                                 std::move(source_ref));
}

PolicyBundle SecurityPolicyManager::build_candidate_bundle(const PolicySnapshot& current_snapshot,
                                                           const PolicyPatch& patch) const {
  return apply_patch_to_snapshot(current_snapshot, patch);
}

PolicySnapshot SecurityPolicyManager::build_snapshot(
    const PolicyBundle& bundle,
    const std::vector<PolicyRuleDescriptor>& effective_rules,
    const PolicySnapshot& previous_snapshot,
    std::string snapshot_id) const {
  const std::uint64_t generation = previous_snapshot.is_valid() ? previous_snapshot.generation + 1U : 1U;
  return PolicySnapshot{
      .snapshot_id = std::move(snapshot_id),
      .generation = generation,
      .version = bundle.schema_version,
      .mode = snapshot_mode_from_rules(effective_rules,
                                       previous_snapshot.is_valid() ? previous_snapshot.mode
                                                                    : PolicyMode::Enforced),
      .effective_rules = effective_rules,
      .created_at = bundle_generated_at(bundle, "policy-snapshot"),
      .source_chain = merge_source_chain(previous_snapshot, bundle.source),
      .last_known_good_ref = previous_snapshot.is_valid() ? previous_snapshot.snapshot_id : std::string(),
  };
}

PolicyErrorCode SecurityPolicyManager::classify_bundle_validation_error(
    const ValidationReport& report) const {
  return std::any_of(report.blocking_errors.begin(), report.blocking_errors.end(), [](const auto& error) {
           return lowercase_copy(error).find("schema") != std::string::npos;
         })
             ? PolicyErrorCode::SchemaUnsupported
             : PolicyErrorCode::BundleInvalid;
}

PolicyErrorCode SecurityPolicyManager::classify_patch_validation_error(
    const ValidationReport& report) const {
  return std::any_of(report.blocking_errors.begin(), report.blocking_errors.end(), [](const auto& error) {
           return lowercase_copy(error).find("patch_base_mismatch") != std::string::npos;
         })
             ? PolicyErrorCode::PatchBaseMismatch
             : PolicyErrorCode::DryRunRejected;
}

void SecurityPolicyManager::sync_options_from_rules_locked(
    const std::vector<PolicyRuleDescriptor>& rules) {
  if (const auto dry_run_required =
          find_rule_condition_value(rules, kPatchGateAction, kPatchGateTarget, "dry_run_required");
      dry_run_required.has_value()) {
    options_.dry_run_required = lowercase_copy(*dry_run_required) == "true";
  }

  if (const auto safe_mode_threshold =
          find_rule_condition_value(rules, kPatchGateAction, kPatchGateTarget, "safe_mode_threshold");
      safe_mode_threshold.has_value() && is_unsigned_integer(*safe_mode_threshold)) {
    options_.safe_mode_threshold = normalize_safe_mode_threshold(
        static_cast<std::uint32_t>(std::stoul(*safe_mode_threshold)));
  }
}

void SecurityPolicyManager::note_patch_failure_locked() {
  ++consecutive_patch_failures_;
  if (consecutive_patch_failures_ >= options_.safe_mode_threshold) {
    safe_mode_ = true;
  }
}

void SecurityPolicyManager::reset_patch_failures_locked() {
  consecutive_patch_failures_ = 0;
  safe_mode_ = false;
}

bool SecurityPolicyManager::dry_run_matches_locked(const PolicyPatch& patch) const {
  return !last_dry_run_patch_id_.empty() && last_dry_run_patch_id_ == patch.patch_id &&
         last_dry_run_base_generation_ == patch.base_generation;
}

void SecurityPolicyManager::remember_dry_run_locked(const PolicyPatch& patch) {
  last_dry_run_patch_id_ = patch.patch_id;
  last_dry_run_base_generation_ = patch.base_generation;
}

void SecurityPolicyManager::clear_dry_run_locked() {
  last_dry_run_patch_id_.clear();
  last_dry_run_base_generation_ = 0;
}

}  // namespace dasall::infra::policy