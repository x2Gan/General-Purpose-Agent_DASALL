#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "PolicyConflictResolver.h"
#include "PolicyDecisionProjector.h"
#include "policy/IPolicySchemaValidator.h"
#include "policy/IPolicySnapshotStore.h"
#include "policy/ISecurityPolicyManager.h"

namespace dasall::infra::policy {

struct SecurityPolicyManagerOptions {
  bool dry_run_required = true;
  std::uint32_t safe_mode_threshold = 3;
};

class SecurityPolicyManager final : public ISecurityPolicyManager {
 public:
  SecurityPolicyManager(const IPolicySchemaValidator& validator,
                        IPolicySnapshotStore& snapshot_store,
                        SecurityPolicyManagerOptions options = {});

  [[nodiscard]] PolicyOpResult load_policy(const PolicyBundle& bundle) override;
  [[nodiscard]] PolicyOpResult apply_patch(const PolicyPatch& patch) override;
  [[nodiscard]] ValidationReport dry_run_patch(const PolicyPatch& patch) override;
  [[nodiscard]] PolicySnapshot snapshot() const override;
  [[nodiscard]] PolicyOpResult rollback(const std::string& snapshot_id) override;
  [[nodiscard]] PolicyDecisionRef evaluate(const PolicyQueryContext& query) const override;

 private:
  [[nodiscard]] PolicyOpResult make_failure(PolicyErrorCode code,
                                            std::string message,
                                            std::string stage,
                                            std::string source_ref) const;
  [[nodiscard]] PolicyBundle build_candidate_bundle(const PolicySnapshot& current_snapshot,
                                                    const PolicyPatch& patch) const;
  [[nodiscard]] PolicySnapshot build_snapshot(const PolicyBundle& bundle,
                                              const std::vector<PolicyRuleDescriptor>& effective_rules,
                                              const PolicySnapshot& previous_snapshot,
                                              std::string snapshot_id) const;
  [[nodiscard]] PolicyErrorCode classify_bundle_validation_error(
      const ValidationReport& report) const;
  [[nodiscard]] PolicyErrorCode classify_patch_validation_error(
      const ValidationReport& report) const;
  void sync_options_from_rules_locked(const std::vector<PolicyRuleDescriptor>& rules);
  void note_patch_failure_locked();
  void reset_patch_failures_locked();
  [[nodiscard]] bool dry_run_matches_locked(const PolicyPatch& patch) const;
  void remember_dry_run_locked(const PolicyPatch& patch);
  void clear_dry_run_locked();

  const IPolicySchemaValidator& validator_;
  IPolicySnapshotStore& snapshot_store_;
  PolicyConflictResolver conflict_resolver_;
  PolicyDecisionProjector decision_projector_;

  mutable std::mutex state_mutex_;
  SecurityPolicyManagerOptions options_;
  std::uint32_t consecutive_patch_failures_ = 0;
  bool safe_mode_ = false;
  std::string last_dry_run_patch_id_;
  std::uint64_t last_dry_run_base_generation_ = 0;
};

}  // namespace dasall::infra::policy