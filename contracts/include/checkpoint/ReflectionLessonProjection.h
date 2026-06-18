#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::contracts {

inline constexpr std::string_view kReflectionLessonExperienceKind =
    "self_reflection";

// ReflectionLessonProjection is the additive, suggestion-only projection that
// allows cognition reflection to surface one reusable lesson to runtime/memory
// without promoting execution control back into ReflectionDecision.
struct ReflectionLessonProjection {
  std::optional<std::string> lesson_summary;
  std::optional<std::string> trigger_condition;
  std::optional<std::string> recommended_action;
  std::optional<std::uint32_t> effectiveness_score;
  std::optional<std::vector<std::string>> applicable_domains;
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts
