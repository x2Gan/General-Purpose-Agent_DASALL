#include "conflict/MemoryConflictResolver.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dasall::memory {
namespace {

constexpr std::array<std::string_view, 11> kNoiseTokens = {
    "a", "an", "the", "is", "are", "was", "were", "to", "of", "and", "or",
};

constexpr std::array<std::string_view, 8> kNegationMarkers = {
    "not", "never", "no ", "disabled", "不", "未", "无", "禁止",
};

constexpr std::array<std::pair<std::string_view, std::string_view>, 9>
    kPolarityPairs = {{
        {"enabled", "disabled"},
        {"allow", "deny"},
        {"online", "offline"},
        {"success", "failed"},
        {"true", "false"},
        {"on", "off"},
        {"启用", "禁用"},
        {"开启", "关闭"},
        {"允许", "拒绝"},
    }};

[[nodiscard]] std::string ascii_lower(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const unsigned char ch : text) {
    if (ch < 128U) {
      lowered.push_back(static_cast<char>(std::tolower(ch)));
    } else {
      lowered.push_back(static_cast<char>(ch));
    }
  }
  return lowered;
}

[[nodiscard]] bool is_token_char(unsigned char ch) {
  return ch >= 128U || std::isalnum(ch) != 0 || ch == '_';
}

[[nodiscard]] std::vector<std::string> tokenize(std::string_view text) {
  const std::string lowered = ascii_lower(text);
  std::vector<std::string> tokens;
  std::string current;

  for (const unsigned char ch : lowered) {
    if (is_token_char(ch)) {
      current.push_back(static_cast<char>(ch));
      continue;
    }

    if (!current.empty()) {
      tokens.push_back(current);
      current.clear();
    }
  }

  if (!current.empty()) {
    tokens.push_back(current);
  }

  return tokens;
}

[[nodiscard]] bool is_noise_token(const std::string& token) {
  return std::find(kNoiseTokens.begin(), kNoiseTokens.end(), token) !=
         kNoiseTokens.end();
}

[[nodiscard]] bool contains_substring(const std::string& text,
                                      std::string_view needle) {
  return text.find(needle) != std::string::npos;
}

[[nodiscard]] bool has_negation_marker(const std::string& text) {
  return std::any_of(kNegationMarkers.begin(), kNegationMarkers.end(),
                     [&text](std::string_view marker) {
                       return contains_substring(text, marker);
                     });
}

[[nodiscard]] std::unordered_set<std::string> build_anchor_token_set(
    std::string_view text) {
  std::unordered_set<std::string> anchors;
  for (const auto& token : tokenize(text)) {
    if (is_noise_token(token)) {
      continue;
    }

    const bool polarity_token = std::any_of(
        kPolarityPairs.begin(), kPolarityPairs.end(),
        [&token](const auto& pair) {
          return token == pair.first || token == pair.second;
        });
    if (polarity_token) {
      continue;
    }

    anchors.insert(token);
  }
  return anchors;
}

[[nodiscard]] int shared_anchor_count(std::string_view existing_text,
                                      std::string_view candidate_text) {
  const auto existing_tokens = build_anchor_token_set(existing_text);
  const auto candidate_tokens = build_anchor_token_set(candidate_text);

  int shared_count = 0;
  for (const auto& token : existing_tokens) {
    if (candidate_tokens.contains(token)) {
      ++shared_count;
    }
  }
  return shared_count;
}

[[nodiscard]] std::optional<long long> extract_single_number(
    std::string_view text) {
  std::optional<long long> value;
  std::string digits;

  const auto flush_digits = [&value, &digits]() -> bool {
    if (digits.empty()) {
      return true;
    }

    if (value.has_value()) {
      return false;
    }

    value = std::stoll(digits);
    digits.clear();
    return true;
  };

  for (const unsigned char ch : text) {
    if (std::isdigit(ch) != 0) {
      digits.push_back(static_cast<char>(ch));
      continue;
    }

    if (!flush_digits()) {
      return std::nullopt;
    }
  }

  if (!flush_digits()) {
    return std::nullopt;
  }

  return value;
}

[[nodiscard]] bool has_polarity_conflict(const std::string& existing_text,
                                         const std::string& candidate_text) {
  return std::any_of(kPolarityPairs.begin(), kPolarityPairs.end(),
                     [&existing_text, &candidate_text](const auto& pair) {
                       const bool existing_first =
                           contains_substring(existing_text, pair.first);
                       const bool existing_second =
                           contains_substring(existing_text, pair.second);
                       const bool candidate_first =
                           contains_substring(candidate_text, pair.first);
                       const bool candidate_second =
                           contains_substring(candidate_text, pair.second);
                       return (existing_first && candidate_second) ||
                              (existing_second && candidate_first);
                     });
}

[[nodiscard]] int confidence_or_zero(const contracts::MemoryFact& fact) {
  return fact.confidence_score.has_value()
             ? static_cast<int>(*fact.confidence_score)
             : 0;
}

[[nodiscard]] std::string build_reason(ConflictAction action,
                                       int confidence_delta,
                                       bool semantic_conflict) {
  switch (action) {
    case ConflictAction::Supersede:
      return semantic_conflict
                 ? "higher-confidence conflicting fact supersedes existing fact"
                 : "higher-confidence related fact replaces an older entry";
    case ConflictAction::Reject:
      return confidence_delta <= 0
                 ? "existing conflicting fact has equal or higher confidence"
                 : "candidate fact rejected by conflict policy";
    case ConflictAction::Coexist:
      return "related fact is compatible or conflict is inconclusive";
    case ConflictAction::Accept:
    default:
      return "no related conflicting fact detected";
  }
}

}  // namespace

MemoryConflictResolver::MemoryConflictResolver(IFactStore& store) : store_(store) {}

ConflictResolutionPlan MemoryConflictResolver::resolve(
    const FactCandidate& candidate,
    const std::string& session_id) {
  ConflictResolutionPlan plan;

  if (session_id.empty() || !candidate.fact.fact_text.has_value() ||
      candidate.fact.fact_text->empty()) {
    return plan;
  }

  std::vector<contracts::MemoryFact> related_facts;
  try {
    related_facts = find_related_facts(
        session_id, candidate.fact.fact_type, *candidate.fact.fact_text);
  } catch (...) {
    plan.warnings.push_back("conflict_check_skipped");
    plan.degraded = true;
    return plan;
  }

  if (related_facts.empty()) {
    return plan;
  }

  bool saw_reject = false;
  bool saw_supersede = false;
  bool saw_coexist = false;
  int highest_superseded_confidence = -1;

  for (const auto& existing : related_facts) {
    const bool semantic_conflict = is_semantically_conflicting(existing, candidate);
    const auto action = semantic_conflict ? determine_action(existing, candidate)
                                          : ConflictAction::Coexist;
    const int confidence_delta =
        confidence_or_zero(candidate.fact) - confidence_or_zero(existing);

    plan.conflict_records.push_back(ConflictRecord{
        .new_fact_id = candidate.fact.fact_id.value_or(""),
        .existing_fact_id = existing.fact_id.value_or(""),
        .action = action,
        .reason = build_reason(action, confidence_delta, semantic_conflict),
        .confidence_delta = confidence_delta,
    });

    if (action == ConflictAction::Reject) {
      saw_reject = true;
      continue;
    }

    if (action == ConflictAction::Supersede) {
      saw_supersede = true;
      const int existing_confidence = confidence_or_zero(existing);
      if (existing_confidence > highest_superseded_confidence) {
        highest_superseded_confidence = existing_confidence;
        plan.supersede_target_id = existing.fact_id;
      }
      continue;
    }

    saw_coexist = true;
  }

  if (saw_reject) {
    plan.action = ConflictAction::Reject;
    return plan;
  }

  if (saw_supersede) {
    plan.action = ConflictAction::Supersede;
    return plan;
  }

  if (saw_coexist) {
    plan.action = ConflictAction::Coexist;
  }

  return plan;
}

std::vector<contracts::MemoryFact> MemoryConflictResolver::find_related_facts(
    const std::string& session_id,
    const std::optional<std::string>& fact_type,
    const std::string& fact_text) {
  FactQuery query;
  query.session_id = session_id;
  query.min_confidence = 0;
  query.exclude_superseded = true;
  query.limit = 16;

  const auto query_result = store_.query_facts(query);
  std::vector<contracts::MemoryFact> related_facts;
  related_facts.reserve(query_result.facts.size());

  for (const auto& fact : query_result.facts) {
    if (!fact.fact_text.has_value() || fact.fact_text->empty()) {
      continue;
    }

    const bool same_type = fact_type.has_value() && fact.fact_type == fact_type;
    const bool overlapping_text = shared_anchor_count(*fact.fact_text, fact_text) > 0;
    const bool contains_relation = fact.fact_text->find(fact_text) != std::string::npos ||
                                   fact_text.find(*fact.fact_text) != std::string::npos;
    if (!same_type && !overlapping_text && !contains_relation) {
      continue;
    }

    related_facts.push_back(fact);
  }

  return related_facts;
}

bool MemoryConflictResolver::is_semantically_conflicting(
    const contracts::MemoryFact& existing,
    const FactCandidate& candidate) const {
  if (!existing.fact_text.has_value() || !candidate.fact.fact_text.has_value()) {
    return false;
  }

  if (existing.fact_type.has_value() && candidate.fact.fact_type.has_value() &&
      existing.fact_type != candidate.fact.fact_type) {
    return false;
  }

  const std::string existing_text = ascii_lower(*existing.fact_text);
  const std::string candidate_text = ascii_lower(*candidate.fact.fact_text);
  const int anchor_overlap = shared_anchor_count(existing_text, candidate_text);
  if (anchor_overlap == 0) {
    return false;
  }

  if (has_polarity_conflict(existing_text, candidate_text)) {
    return true;
  }

  if (has_negation_marker(existing_text) != has_negation_marker(candidate_text)) {
    return true;
  }

  const auto existing_number = extract_single_number(existing_text);
  const auto candidate_number = extract_single_number(candidate_text);
  if (existing_number.has_value() && candidate_number.has_value() &&
      existing_number != candidate_number) {
    return true;
  }

  return false;
}

ConflictAction MemoryConflictResolver::determine_action(
    const contracts::MemoryFact& existing,
    const FactCandidate& candidate) const {
  return confidence_or_zero(candidate.fact) > confidence_or_zero(existing)
             ? ConflictAction::Supersede
             : ConflictAction::Reject;
}

}  // namespace dasall::memory