#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/SummaryMemory.h"

namespace dasall::memory {

enum class HierarchicalSummaryLevel {
  Dialog = 0,
  Topic = 1,
  Profile = 2,
};

inline constexpr std::string_view to_string_view(
    HierarchicalSummaryLevel level) {
  switch (level) {
    case HierarchicalSummaryLevel::Dialog:
      return "dialog";
    case HierarchicalSummaryLevel::Topic:
      return "topic";
    case HierarchicalSummaryLevel::Profile:
      return "profile";
  }
  return "dialog";
}

inline constexpr std::string_view summary_level_tag(
    HierarchicalSummaryLevel level) {
  switch (level) {
    case HierarchicalSummaryLevel::Dialog:
      return "summary_level:dialog";
    case HierarchicalSummaryLevel::Topic:
      return "summary_level:topic";
    case HierarchicalSummaryLevel::Profile:
      return "summary_level:profile";
  }
  return "summary_level:dialog";
}

inline constexpr std::optional<HierarchicalSummaryLevel>
next_hierarchical_summary_level(HierarchicalSummaryLevel level) {
  switch (level) {
    case HierarchicalSummaryLevel::Dialog:
      return HierarchicalSummaryLevel::Topic;
    case HierarchicalSummaryLevel::Topic:
      return HierarchicalSummaryLevel::Profile;
    case HierarchicalSummaryLevel::Profile:
      return std::nullopt;
  }
  return std::nullopt;
}

inline std::optional<HierarchicalSummaryLevel> extract_summary_level(
    const contracts::SummaryMemory& summary) {
  if (!summary.tags.has_value()) {
    return std::nullopt;
  }

  for (const auto& tag : *summary.tags) {
    if (tag == summary_level_tag(HierarchicalSummaryLevel::Dialog)) {
      return HierarchicalSummaryLevel::Dialog;
    }
    if (tag == summary_level_tag(HierarchicalSummaryLevel::Topic)) {
      return HierarchicalSummaryLevel::Topic;
    }
    if (tag == summary_level_tag(HierarchicalSummaryLevel::Profile)) {
      return HierarchicalSummaryLevel::Profile;
    }
  }

  return std::nullopt;
}

inline bool summary_matches_level(const contracts::SummaryMemory& summary,
                                  HierarchicalSummaryLevel level) {
  const auto actual_level = extract_summary_level(summary);
  if (!actual_level.has_value()) {
    return level == HierarchicalSummaryLevel::Dialog;
  }
  return *actual_level == level;
}

inline void ensure_summary_level_tag(contracts::SummaryMemory& summary,
                                     HierarchicalSummaryLevel level) {
  if (!summary.tags.has_value()) {
    summary.tags = std::vector<std::string>{};
  }

  const std::string desired_tag{summary_level_tag(level)};
  for (auto& tag : *summary.tags) {
    if (tag.rfind("summary_level:", 0) == 0) {
      tag = desired_tag;
      return;
    }
  }

  summary.tags->push_back(desired_tag);
}

struct HierarchicalSummaryRequest {
  std::string session_id;
  HierarchicalSummaryLevel source_level = HierarchicalSummaryLevel::Dialog;
  HierarchicalSummaryLevel target_level = HierarchicalSummaryLevel::Topic;
  std::vector<contracts::SummaryMemory> source_summaries;
  std::optional<std::string> user_id;
  int target_token_budget = 0;
};

}  // namespace dasall::memory