#include "PolicyConflictResolver.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace dasall::infra::policy {
namespace {

struct RuleRank {
  std::uint32_t primary = 0;
  std::uint32_t secondary = 0;

  [[nodiscard]] auto tie() const {
    return std::tie(primary, secondary);
  }
};

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) {
  return value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string extract_priority_order(const PolicyBundle& bundle) {
  for (const auto& rule : bundle.rules) {
    for (const auto& condition : rule.conditions) {
      if (starts_with(condition, "priority_order=")) {
        const std::string order = condition.substr(std::string_view("priority_order=").size());
        return order == "explicit-priority" ? "explicit-priority" : "deny-first";
      }
    }
  }

  return "deny-first";
}

[[nodiscard]] std::string make_scope_key(const PolicyRuleDescriptor& rule) {
  return std::string(to_string(rule.domain)) + '|' + rule.subject + '|' + rule.action + '|' +
         rule.target_selector;
}

[[nodiscard]] RuleRank make_rank(const PolicyRuleDescriptor& rule,
                                 std::string_view priority_order) {
  if (priority_order == "explicit-priority") {
    return RuleRank{
        .primary = rule.priority,
        .secondary = policy_effect_precedence(rule.effect),
    };
  }

  return RuleRank{
      .primary = policy_effect_precedence(rule.effect),
      .secondary = rule.priority,
  };
}

[[nodiscard]] bool is_compat_group(const std::vector<PolicyRuleDescriptor>& rules) {
  return std::all_of(rules.begin(), rules.end(), [](const PolicyRuleDescriptor& rule) {
    return rule.mode == PolicyMode::Compatibility;
  });
}

[[nodiscard]] bool same_rank(const PolicyRuleDescriptor& lhs,
                             const PolicyRuleDescriptor& rhs,
                             std::string_view priority_order) {
  return make_rank(lhs, priority_order).tie() == make_rank(rhs, priority_order).tie();
}

}  // namespace

ConflictResolutionResult PolicyConflictResolver::resolve(const PolicyBundle& bundle) const {
  ConflictResolutionResult result;
  result.priority_order = extract_priority_order(bundle);

  if (!bundle.is_valid()) {
    result.reason_code = "policy_bundle_invalid";
    return result;
  }

  std::map<std::string, std::vector<PolicyRuleDescriptor>> grouped_rules;
  for (const auto& rule : bundle.rules) {
    grouped_rules[make_scope_key(rule)].push_back(rule);
  }

  for (auto& [scope_key, scoped_rules] : grouped_rules) {
    std::sort(scoped_rules.begin(), scoped_rules.end(), [&](const PolicyRuleDescriptor& lhs,
                                                            const PolicyRuleDescriptor& rhs) {
      const RuleRank lhs_rank = make_rank(lhs, result.priority_order);
      const RuleRank rhs_rank = make_rank(rhs, result.priority_order);
      if (lhs_rank.tie() != rhs_rank.tie()) {
        return lhs_rank.tie() < rhs_rank.tie();
      }

      return lhs.rule_id < rhs.rule_id;
    });

    const PolicyRuleDescriptor& winner = scoped_rules.front();
    std::vector<std::string> tied_rule_ids{winner.rule_id};
    for (std::size_t index = 1; index < scoped_rules.size(); ++index) {
      if (!same_rank(winner, scoped_rules[index], result.priority_order)) {
        break;
      }
      tied_rule_ids.push_back(scoped_rules[index].rule_id);
    }

    if (tied_rule_ids.size() > 1U) {
      if (!is_compat_group(scoped_rules)) {
        result.conflict_rule_ids.insert(result.conflict_rule_ids.end(),
                                        tied_rule_ids.begin(),
                                        tied_rule_ids.end());
        result.reason_code = "policy_conflict_unresolved";
        result.effective_rules.clear();
        result.resolved = false;
        return result;
      }

      result.warnings.push_back("compat_conflict_downgraded:" + scope_key);
    }

    result.effective_rules.push_back(winner);
  }

  result.resolved = true;
  result.error_code = PolicyErrorCode::ConflictUnresolved;
  result.reason_code = result.warnings.empty() ? "policy_conflicts_resolved"
                                               : "policy_conflicts_resolved_with_compat_downgrade";
  return result;
}

}  // namespace dasall::infra::policy