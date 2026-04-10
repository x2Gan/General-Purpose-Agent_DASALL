#include "PolicyDecisionProjector.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace dasall::infra::policy {
namespace {

constexpr std::string_view kDefaultRuleAction = "evaluate_default";
constexpr std::string_view kDefaultRuleTarget = "policy:default_effect";
constexpr std::string_view kDecisionEvidencePrefix = "audit://policy/decision/";
constexpr std::string_view kSyntheticInvalidQueryRuleId = "policy-query-invalid";
constexpr std::string_view kSyntheticInvalidSnapshotRuleId = "policy-snapshot-invalid";
constexpr std::string_view kSyntheticDefaultDenyRuleId = "policy-default-deny";

struct MatchedRule {
  const PolicyRuleDescriptor* rule = nullptr;
  int target_specificity = -1;
};

[[nodiscard]] std::string lowercase_copy(std::string_view value) {
  std::string normalized(value);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return normalized;
}

[[nodiscard]] std::string normalize_snapshot_id(const PolicySnapshot& snapshot) {
  return snapshot.snapshot_id.empty() ? std::string("snapshot-unavailable") : snapshot.snapshot_id;
}

[[nodiscard]] std::uint64_t normalize_generation(const PolicySnapshot& snapshot) {
  return snapshot.generation == 0 ? 1U : snapshot.generation;
}

[[nodiscard]] std::string build_evidence_ref(std::string_view snapshot_id,
                                             std::uint64_t generation,
                                             std::string_view rule_id) {
  return std::string(kDecisionEvidencePrefix) + std::string(snapshot_id) + "/" +
         std::to_string(generation) + "/" + std::string(rule_id);
}

[[nodiscard]] PolicyDomain map_query_domain(const PolicyQueryContext& query) {
  const std::string module = lowercase_copy(query.module);
  const std::string target_type = lowercase_copy(query.target_type);
  const std::string operation = lowercase_copy(query.operation);

  if (module == "secret" || target_type == "secret") {
    return PolicyDomain::SecretAccess;
  }

  if (module == "plugin" || target_type == "plugin" || target_type == "manifest") {
    return PolicyDomain::PluginLoad;
  }

  if (module == "diagnostics" || module == "diag" || target_type == "command") {
    return PolicyDomain::DiagnosticsCommand;
  }

  if (module == "ota") {
    return operation == "rollback" ? PolicyDomain::OTARollback : PolicyDomain::OTAApply;
  }

  if (module == "policy") {
    return PolicyDomain::PolicyAdmin;
  }

  return PolicyDomain::Unspecified;
}

[[nodiscard]] bool action_matches(const std::string_view& rule_action,
                                  const std::string_view& query_operation) {
  const std::string normalized_action = lowercase_copy(rule_action);
  if (normalized_action == "*" || normalized_action == "any") {
    return true;
  }

  return normalized_action == lowercase_copy(query_operation);
}

[[nodiscard]] bool subject_matches(const std::string_view& rule_subject,
                                   const std::string_view& actor_ref) {
  const std::string normalized_subject = lowercase_copy(rule_subject);
  if (normalized_subject == "*" || normalized_subject == "any") {
    return true;
  }

  return normalized_subject == lowercase_copy(actor_ref);
}

[[nodiscard]] int target_selector_specificity(const std::string_view& selector,
                                              const PolicyQueryContext& query) {
  const std::string normalized_selector = lowercase_copy(selector);
  const std::string normalized_target_type = lowercase_copy(query.target_type);
  const std::string normalized_target_ref = lowercase_copy(query.target_ref);
  const std::string exact_selector = normalized_target_type + ":" + normalized_target_ref;
  const std::string wildcard_selector = normalized_target_type + ":*";
  const std::string any_selector = normalized_target_type + ":any";

  if (normalized_selector == exact_selector) {
    return 3;
  }

  if (normalized_selector == normalized_target_ref) {
    return 2;
  }

  if (normalized_selector == wildcard_selector || normalized_selector == any_selector) {
    return 1;
  }

  if (normalized_selector == "*" || normalized_selector == "any") {
    return 0;
  }

  return -1;
}

[[nodiscard]] bool is_default_rule(const PolicyRuleDescriptor& rule) {
  return rule.action == kDefaultRuleAction && rule.target_selector == kDefaultRuleTarget;
}

[[nodiscard]] bool prefer_default_rule(const PolicyRuleDescriptor& candidate,
                                       const PolicyRuleDescriptor& current) {
  return std::tie(candidate.priority, candidate.effect, candidate.rule_id) <
         std::tie(current.priority, current.effect, current.rule_id);
}

[[nodiscard]] PolicyDecision map_effect_to_decision(PolicyEffect effect,
                                                    std::vector<std::string>& warnings) {
  switch (effect) {
    case PolicyEffect::Allow:
      return PolicyDecision::Allow;
    case PolicyEffect::Deny:
      return PolicyDecision::Deny;
    case PolicyEffect::RequireConfirmation:
      return PolicyDecision::RequireConfirmation;
    case PolicyEffect::Observe:
      warnings.push_back("observe_effect_projected_as_allow");
      return PolicyDecision::Allow;
    case PolicyEffect::Unspecified:
      break;
  }

  warnings.push_back("invalid_effect_projected_as_deny");
  return PolicyDecision::Deny;
}

[[nodiscard]] PolicyDecisionRef build_decision_from_rule(
    const PolicyRuleDescriptor& rule,
    const PolicySnapshot& snapshot,
    std::vector<std::string> matched_rule_ids,
    std::vector<std::string> warnings = {}) {
  const std::string snapshot_id = normalize_snapshot_id(snapshot);
  const std::uint64_t generation = normalize_generation(snapshot);
  const PolicyDecision decision = map_effect_to_decision(rule.effect, warnings);

  return PolicyDecisionRef{
      .decision = decision,
      .reason_code = rule.reason_code,
      .matched_rule_ids = std::move(matched_rule_ids),
      .snapshot_id = snapshot_id,
      .generation = generation,
      .evidence_ref = build_evidence_ref(snapshot_id, generation, rule.rule_id),
      .warnings = std::move(warnings),
  };
}

[[nodiscard]] PolicyDecisionRef make_synthetic_deny(const PolicySnapshot& snapshot,
                                                    std::string reason_code,
                                                    std::string synthetic_rule_id,
                                                    std::vector<std::string> warnings = {}) {
  const std::string snapshot_id = normalize_snapshot_id(snapshot);
  const std::uint64_t generation = normalize_generation(snapshot);

  return PolicyDecisionRef{
      .decision = PolicyDecision::Deny,
      .reason_code = std::move(reason_code),
      .matched_rule_ids = {synthetic_rule_id},
      .snapshot_id = snapshot_id,
      .generation = generation,
      .evidence_ref = build_evidence_ref(snapshot_id, generation, synthetic_rule_id),
      .warnings = std::move(warnings),
  };
}

}  // namespace

PolicyDecisionRef PolicyDecisionProjector::project(const PolicyQueryContext& query,
                                                   const PolicySnapshot& snapshot) const {
  if (!snapshot.is_valid()) {
    return make_synthetic_deny(snapshot,
                               "policy_snapshot_invalid",
                               std::string(kSyntheticInvalidSnapshotRuleId));
  }

  if (!query.has_required_fields()) {
    return make_synthetic_deny(snapshot,
                               "policy_query_invalid",
                               std::string(kSyntheticInvalidQueryRuleId));
  }

  const PolicyDomain query_domain = map_query_domain(query);
  const PolicyRuleDescriptor* default_rule = nullptr;
  std::vector<MatchedRule> matched_rules;

  for (const auto& rule : snapshot.effective_rules) {
    if (is_default_rule(rule)) {
      if (default_rule == nullptr || prefer_default_rule(rule, *default_rule)) {
        default_rule = &rule;
      }
      continue;
    }

    if (query_domain == PolicyDomain::Unspecified || rule.domain != query_domain) {
      continue;
    }

    if (!action_matches(rule.action, query.operation) ||
        !subject_matches(rule.subject, query.actor_ref)) {
      continue;
    }

    const int specificity = target_selector_specificity(rule.target_selector, query);
    if (specificity < 0) {
      continue;
    }

    matched_rules.push_back(MatchedRule{.rule = &rule, .target_specificity = specificity});
  }

  if (!matched_rules.empty()) {
    std::sort(matched_rules.begin(), matched_rules.end(), [](const MatchedRule& left,
                                                             const MatchedRule& right) {
      if (left.target_specificity != right.target_specificity) {
        return left.target_specificity > right.target_specificity;
      }
      if (left.rule->priority != right.rule->priority) {
        return left.rule->priority < right.rule->priority;
      }
      if (left.rule->effect != right.rule->effect) {
        return policy_effect_precedence(left.rule->effect) <
               policy_effect_precedence(right.rule->effect);
      }
      return left.rule->rule_id < right.rule->rule_id;
    });

    std::vector<std::string> matched_rule_ids(matched_rules.size());
    std::transform(matched_rules.begin(),
                   matched_rules.end(),
                   matched_rule_ids.begin(),
                   [](const MatchedRule& matched_rule) {
                     return matched_rule.rule->rule_id;
                   });

    return build_decision_from_rule(*matched_rules.front().rule,
                                    snapshot,
                                    std::move(matched_rule_ids));
  }

  if (default_rule != nullptr) {
    return build_decision_from_rule(*default_rule,
                                    snapshot,
                                    {default_rule->rule_id},
                                    {std::string("default_effect_applied")});
  }

  std::vector<std::string> warnings;
  if (query_domain == PolicyDomain::Unspecified) {
    warnings.push_back("query_domain_unknown");
  }
  warnings.push_back("default_effect_rule_missing");

  return make_synthetic_deny(snapshot,
                             "policy_query_no_match",
                             std::string(kSyntheticDefaultDenyRuleId),
                             std::move(warnings));
}

}  // namespace dasall::infra::policy